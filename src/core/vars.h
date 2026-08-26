// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Mikhail Revzin <p3.141592653589793238462643@gmail.com>
// Part of the Iskra-226 project: https://github.com/Ptr314/Iskra-226
// Description: хранилище переменных: числа, числовые массивы, символьные поля

#pragma once

#include <map>
#include <string>
#include <vector>

#include "core/number.h"
#include "core/program.h"

namespace iskra {

// LEN и NUM — чистые функции над содержимым символьной переменной; они
// нужны и исполнению, и вычислителю выражений.
unsigned str_len_value(const std::string & s);
unsigned str_num_value(const std::string & s);

// Переменные адресуются индексом: имён в оттранслированной форме нет вовсе.
// Описания (тип, размерности, длина строки) приходят из таблиц переменных
// образа — docs/format.md, разд. 6.
//
// Индексы элементов здесь уже вычислены и приведены к целому: «если
// арифметические выражения состоят из целой и дробной частей, используется
// только их целая часть» (руководство, разд. 7.1). Само вычисление —
// забота вызывающего, иначе хранилище пришлось бы связывать с разбором.
class VarStore
{
public:
    explicit VarStore(const std::vector<VarInfo> & vars)
        : vars_(vars), boundary_(0), has_boundary_(false) {}

    const std::vector<VarInfo> & vars() const { return vars_; }
    bool is_string(unsigned var) const
    {
        return var < vars_.size() && vars_[var].is_string;
    }
    bool is_array(unsigned var) const
    {
        return var < vars_.size() && vars_[var].is_array;
    }

    // Числовая ячейка — и для чтения, и для записи. n — сколько индексов;
    // ноль значит «скаляр».
    bool slot(unsigned var, const long * idx, unsigned n, Number *& out,
              std::string & error);

    // Выделить числовой массив заново: DIM обнуляет его.
    bool array_alloc(unsigned var, unsigned dim1, unsigned dim2,
                     std::string & error);
    // MAT REDIM меняет размерности, сохраняя содержимое памяти.
    bool array_grow(unsigned var, unsigned dim1, unsigned dim2,
                    std::string & error);
    // Действующие размерности числового массива: `MAT REDIM` меняет их, а
    // в таблицах образа остаются исходные. Массив, к которому ещё не
    // обращались, размещается по таблицам — как это делает slot().
    bool array_dims(unsigned var, unsigned & dim1, unsigned & dim2,
                    std::string & error);
    // Число элементов массива: dim1 * dim2, у одномерного — dim1.
    bool array_count(unsigned var, unsigned & n, std::string & error);

    // Повторное описание символьной переменной очищает её поле и снимает
    // переопределение от `MAT REDIM`: размеры теперь берутся из `DIM`.
    void reset_string(unsigned var) { strs_.erase(var); str_dims_.erase(var); }

    // Символьная переменная — поле байт постоянной длины, заполненное
    // пробелами. Массив строк — одно непрерывное поле: «символьный массив
    // рассматривается как одна непрерывная строка» (руководство, разд. 13.2).
    struct StrLoc {
        StrLoc() : data(0), off(0), len(0) {}
        std::string * data;
        unsigned off;
        unsigned len;
    };

    std::string & str_field(unsigned var);
    // Место элемента символьного массива; n == 0 — вся переменная целиком.
    bool str_element(unsigned var, const long * idx, unsigned n, StrLoc & loc,
                     std::string & error);

    // Длина одного элемента символьной переменной.
    unsigned str_len(unsigned var) const;

    // `MAT REDIM` символьного массива: новые размерности и, если задана,
    // новая длина элемента. Поле пересобирается под них.
    //
    // Переопределение держится **при себе, а не в таблицах образа**: они
    // уходят на дискету через `SAVE DC`, и переписывать их из-за временной
    // перекройки массива нельзя. У числовых массивов так с самого начала —
    // их размерности живут в `arrays_`.
    bool str_redim(unsigned var, unsigned dim1, unsigned dim2, unsigned len,
                   std::string & error);

    // Действующие размерности символьного массива.
    void str_dims(unsigned var, unsigned & dim1, unsigned & dim2) const;

    // Размерности массива, какими они стали после `DIM` и `MAT REDIM`.
    // Если ни того, ни другого не было, d1 и d2 остаются как их задал
    // вызывающий: у него своё умолчание, и менять его тут нечестно.
    // Одномерный массив отдаёт d2 == 1.
    void live_dims(unsigned var, unsigned & d1, unsigned & d2) const;

    void clear()
    {
        nums_.clear();
        arrays_.clear();
        strs_.clear();
        str_dims_.clear();
    }

    // CLEAR N перед загрузкой сегмента: «стирает все переменные, не
    // объявленные с помощью оператора COM, и оставляет общие переменные
    // без изменения» (руководство, разд. 19.1).
    void clear_non_common();

    // `COM CLEAR` двигает границу общих переменных: «эта переменная и все
    // переменные, объявленные вслед за указанной, становятся необщими»
    // (руководство, разд. 19.3). Общие лежат подряд с начала области, так
    // что граница — это один индекс. Без него берётся признак из таблиц
    // образа.
    void set_common_boundary(unsigned first_non_common)
    {
        boundary_ = first_non_common;
        has_boundary_ = true;
    }
    bool is_common(unsigned var) const
    {
        if (has_boundary_) return var < boundary_;
        return var < vars_.size() && vars_[var].is_common;
    }

private:
    struct Array {
        Array() : dim1(0), dim2(0) {}
        unsigned dim1;
        unsigned dim2;              // 0 у одномерного
        std::vector<Number> cells;
    };

    // Что `MAT REDIM` перекроил у символьного массива. Нулевая длина
    // значит «длина элемента прежняя».
    struct StrDim {
        StrDim() : dim1(0), dim2(0), len(0) {}
        unsigned dim1, dim2, len;
    };

    const std::vector<VarInfo> & vars_;
    std::map<unsigned, Number> nums_;
    std::map<unsigned, Array> arrays_;
    std::map<unsigned, std::string> strs_;
    std::map<unsigned, StrDim> str_dims_;
    unsigned boundary_;          // первая необщая переменная после COM CLEAR
    bool has_boundary_;
};

} // namespace iskra
