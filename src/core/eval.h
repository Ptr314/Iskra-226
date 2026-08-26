// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Mikhail Revzin <p3.141592653589793238462643@gmail.com>
// Part of the Iskra-226 project: https://github.com/Ptr314/Iskra-226
// Description: вычисление выражений прямо из потока токенов

#pragma once

#include <string>
#include <vector>

#include "core/errors.h"
#include "core/expr.h"
#include "core/value.h"
#include "core/vars.h"

namespace iskra {

// Функция пользователя: `DEFFN A(H)=<а.в.>`, обращение `FN A(<а.в.>)`
// (руководство, разд. 4.8). Тело определения лежит в другом операторе
// программы, и вычислителю до него не дотянуться: он видит только поток
// одного оператора. Кто программу держит — тот функцию и подставляет.
class FnResolver
{
public:
    virtual ~FnResolver() {}
    // name — имя функции кодом символа. false и пустая err значит «такой
    // функции в программе нет».
    virtual bool call_fn(unsigned name, const Value & arg, Value & out,
                         std::string & err) = 0;
};

// Третий обход той же грамматики: `ExprParser` строит дерево, `Encoder` из
// tokenize.cpp выдаёт байты, а этот — значение. Дерева не строит вовсе:
// «интерпретатор осуществляет пошаговый перевод операторов» (руководство,
// разд. 1.2), и выражение перевычисляется при каждом исполнении, как в
// машине.
//
// Старшинство приходится восстанавливать: в потоке лежит плоская
// инфиксная запись со скобками, а не обратная польская.
class Evaluator
{
public:
    Evaluator(TokenSource & src, VarStore & vars)
        : ex_(src), src_(src), vars_(vars), fn_(0), angle_(0), code_(0),
          stop_gt_(false) {}

    // Полное выражение, включая связки условий.
    bool expr(Value & out);
    // Числовое значение; символьное сюда не годится.
    bool number(Number & out);
    // Символьное значение.
    bool text(std::string & out);
    // Один операнд и ничего сверх него — из этого состоит список значений
    // оператора `DATA`: разделителей там нет вовсе, и заглядывать за
    // операцией нельзя. Иначе `DATA 31,334` разберётся как `31 AND …`:
    // `E7` в позиции операции — это `AND` (docs/format.md, разд. 5,
    // «Хвост оператора DATA»).
    bool operand(Value & out);

    // Приёмник: переменная, элемент массива, массив целиком либо STR(.
    // by_table — решать «скаляр или массив» строго по таблицам, без
    // заглядывания вперёд (CLAUDE.md, три ловушки кодировки).
    struct Target {
        Target() : var(0), is_str(false), data(0), off(0), len(0), whole(false),
                   nidx(0) { idx[0] = idx[1] = 0; }
        unsigned var;
        bool is_str;
        // Для символьного приёмника — само место в поле переменной. Держим
        // его в цели, а не в вычислителе: у присваивания целей бывает
        // несколько, и место нужно каждой.
        std::string * data;
        unsigned off;
        unsigned len;
        bool whole;               // приёмник — всё поле целиком
        // Для числового — уже вычисленные индексы.
        long idx[2];
        unsigned nidx;
    };
    bool target(Target & out, bool by_table = false);
    bool store(const Target & t, const Value & v);
    // Прочитать значение из уже разобранного приёмника.
    bool load(const Target & t, Value & v);

    // Кому передавать обращения FN<имя>(. Ноль значит «функций нет» —
    // так вычислитель работает вне программы.
    void set_functions(FnResolver * f) { fn_ = f; }

    // Единица измерения угла для тригонометрии: 0 радианы, 1 градусы,
    // 2 грады (`SELECT D/R/G`, руководство, разд. 4.6). Ставит исполнитель:
    // сама таблица устройств вычислителю не видна.
    void set_angle(unsigned a) { angle_ = a; }

    // Внутри группы `PLOT` байт `D4` — закрывающая скобка группы, а не
    // знак «больше» (docs/format.md, разд. 5, «Подкод 06 00 — это PLOT»).
    // Тот же приём, что у детокенизатора: `Decoder::expr(true)`.
    void set_stop_gt(bool on) { stop_gt_ = on; }

    ExprParser & parser() { return ex_; }
    const ExprParser & parser() const { return ex_; }
    // Причина может лежать в трёх местах: у источника лексем, у разборщика
    // и у самого вычислителя. Берём первую непустую — иначе наружу уходит
    // пустое сообщение.
    const std::string & error() const
    {
        if (!src_.source_error().empty()) return src_.source_error();
        if (!ex_.error().empty()) return ex_.error();
        return error_;
    }
    bool fail(const std::string & m)
    {
        if (error_.empty()) error_ = m;
        return false;
    }
    // Математическая ошибка — это ошибка машины с кодом 03, а не ограничение
    // эмулятора: «если ошибка математическая, то выполняется оператор
    // возврата RETURN» (руководство, пример 11.11). Вычислитель про ON ERROR
    // не знает, поэтому только помечает причину, а превращает её в ошибку
    // машины исполнитель.
    bool math_fail(const std::string & m)
    {
        if (!code_) code_ = err::MATH;
        return fail(m);
    }
    const char * error_code() const { return code_; }

private:
    bool logic(Value & out);
    bool compare(Value & out);
    bool sum(Value & out);
    bool product(Value & out);
    bool unary(Value & out);
    bool power(Value & out);
    bool primary(Value & out);

    bool call(Value & out, Tok::Type fn);
    double to_radians(double v) const;
    double from_radians(double v) const;
    bool user_call(Value & out, unsigned name);
    bool indices(long * idx, unsigned & n);
    bool substr(Value & out, VarStore::StrLoc & loc);
    bool implicit(Value & out, Tok::Type fn);
    bool var_value(const Tok & t, Value & out);

    ExprParser ex_;
    TokenSource & src_;
    VarStore & vars_;
    FnResolver * fn_;
    unsigned angle_;
    const char * code_;    // код ошибки машины, если она машинная
    bool stop_gt_;         // `D4` — конец элемента группы `PLOT`, а не «>»
    std::string lit_;      // литерал под STR("…",…), чтобы было откуда резать
    std::string error_;
};

} // namespace iskra
