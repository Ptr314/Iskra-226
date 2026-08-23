// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Mikhail Revzin <p3.141592653589793238462643@gmail.com>
// Part of the Iskra-226 project: https://github.com/Ptr314/Iskra-226
// Description: разбор выражений, общий для текста и токенов

#pragma once

#include <string>

#include "core/ir.h"

namespace iskra {

// Лексема выражения. Оба представления сводятся к одному потоку лексем,
// и дальше выражение разбирает общий код: приоритеты операций в тексте и
// в токенах одни и те же.
struct Tok {
    enum Type {
        END,            // конец операндов оператора
        NUM, STR, VAR, PI,
        LPAR, RPAR, COMMA, SEMI,
        PLUS, MINUS, STAR, SLASH, CARET,
        EQ, NE, LT, LE, GT, GE,
        FN_ABS, FN_INT, FN_SGN, FN_SQR, FN_LOG, FN_EXP,
        FN_HEX,         // строка байт уже разобрана в s
        FN_AT,
        KW_TO, KW_STEP, KW_THEN,
        UNKNOWN         // распознано, но не поддержано — текст в s
    };

    Tok() : t(END), var(0) {}

    Type t;
    Number num;
    std::string s;
    unsigned var;
};

// Источник лексем. Токенизированная форма двузначна: один и тот же байт
// значит разное в позиции операнда и в позиции операции, — поэтому источник
// должен знать, чего от него ждут. Текстовому источнику это безразлично.
class TokenSource
{
public:
    virtual ~TokenSource() {}
    virtual bool next(Tok & t, bool operand_expected) = 0;
};

// Разбор выражения по приоритетам: сравнения, затем + -, затем * /,
// затем унарный минус, затем ^.
class ExprParser
{
public:
    explicit ExprParser(TokenSource & src);

    // Полное выражение. При неудаче false, причина — в error().
    bool parse(Expr & out);

    // Заглянуть в следующую лексему, не потребляя её.
    bool peek(Tok & t, bool operand_expected);
    // Потребить лексему, на которую смотрели.
    void consume();
    bool take(Tok & t, bool operand_expected);

    const std::string & error() const { return error_; }
    void fail(const std::string & msg);
    bool failed() const { return !error_.empty(); }

private:
    bool parse_compare(Expr & out);
    bool parse_sum(Expr & out);
    bool parse_product(Expr & out);
    bool parse_unary(Expr & out);
    bool parse_power(Expr & out);
    bool parse_primary(Expr & out);
    bool parse_call(Expr & out, ExprKind kind, unsigned args_min, unsigned args_max);

    TokenSource & src_;
    Tok pending_;
    bool has_pending_;
    bool pending_operand_;
    std::string error_;
};

} // namespace iskra