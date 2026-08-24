// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Mikhail Revzin <p3.141592653589793238462643@gmail.com>
// Part of the Iskra-226 project: https://github.com/Ptr314/Iskra-226
// Description: вычисление выражений прямо из потока токенов

#pragma once

#include <string>
#include <vector>

#include "core/expr.h"
#include "core/value.h"
#include "core/vars.h"

namespace iskra {

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
        : ex_(src), src_(src), vars_(vars) {}

    // Полное выражение, включая связки условий.
    bool expr(Value & out);
    // Числовое значение; символьное сюда не годится.
    bool number(Number & out);
    // Символьное значение.
    bool text(std::string & out);

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

private:
    bool logic(Value & out);
    bool compare(Value & out);
    bool sum(Value & out);
    bool product(Value & out);
    bool unary(Value & out);
    bool power(Value & out);
    bool primary(Value & out);

    bool call(Value & out, Tok::Type fn);
    bool indices(long * idx, unsigned & n);
    bool substr(Value & out, VarStore::StrLoc & loc);
    bool implicit(Value & out, Tok::Type fn);
    bool var_value(const Tok & t, Value & out);

    ExprParser ex_;
    TokenSource & src_;
    VarStore & vars_;
    std::string lit_;      // литерал под STR("…",…), чтобы было откуда резать
    std::string error_;
};

} // namespace iskra
