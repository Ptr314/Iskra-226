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
    explicit VarStore(const std::vector<VarInfo> & vars) : vars_(vars) {}

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
    // Повторное описание символьной переменной очищает её поле.
    void reset_string(unsigned var) { strs_.erase(var); }

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

    void clear()
    {
        nums_.clear();
        arrays_.clear();
        strs_.clear();
    }

private:
    struct Array {
        Array() : dim1(0), dim2(0) {}
        unsigned dim1;
        unsigned dim2;              // 0 у одномерного
        std::vector<Number> cells;
    };

    const std::vector<VarInfo> & vars_;
    std::map<unsigned, Number> nums_;
    std::map<unsigned, Array> arrays_;
    std::map<unsigned, std::string> strs_;
};

} // namespace iskra
