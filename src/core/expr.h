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
        FN_AT, FN_TAB,
        FN_STR,                 // STR( — первая запятая в потоке не кодируется
        FN_LEN, FN_NUM, FN_VAL, FN_POS,   // неявные: закрывающей скобки нет
        ARRAY,                  // ссылка на массив целиком
        HASH,                   // DB — второй аргумент VAL(
        KW_TO, KW_STEP, KW_THEN, KW_GOTO, KW_GOSUB,
        UNKNOWN         // распознано, но не поддержано — текст в s
    };

    Tok() : t(END), var(0), indexed(false), table_array(false) {}

    Type t;
    Number num;
    std::string s;
    unsigned var;

    // У VAR: за именем идёт список индексов. В тексте признак — открывающая
    // скобка, в токенах — то, что переменная объявлена массивом: там скобки
    // у индекса нет вовсе (docs/format.md, разд. 7).
    bool indexed;

    // То же, но строго по таблицам переменных, без заглядывания вперёд.
    // Нужно первому аргументу STR(: там за именем идёт не индекс, а начало
    // подстроки, потому что первая запятая STR( не кодируется, — и
    // заглядывание принимает STR(Z¤,67) за обращение к Z¤(67).
    bool table_array;
};

// Источник лексем. Токенизированная форма двузначна: один и тот же байт
// значит разное в позиции операнда и в позиции операции, — поэтому источник
// должен знать, чего от него ждут. Текстовому источнику это безразлично.
class TokenSource
{
public:
    virtual ~TokenSource() {}
    virtual bool next(Tok & t, bool operand_expected) = 0;

    // Зависит ли разбор лексемы от ожидаемого состояния. У токенов да —
    // и тогда заглядывать вперёд можно только в том состоянии, в каком
    // лексема потом будет прочитана. Текстовому лексеру состояние
    // безразлично, и это ограничение к нему не применяется.
    virtual bool state_sensitive() const { return false; }
};

// Разбор выражения по приоритетам: сравнения, затем + -, затем * /,
// затем унарный минус, затем ^.
class ExprParser
{
public:
    explicit ExprParser(TokenSource & src);

    // Полное выражение. При неудаче false, причина — в error().
    bool parse(Expr & out);

    // Цель присваивания: переменная, элемент массива либо STR(.
    //
    // by_table: решать «скаляр или массив» строго по таблицам переменных,
    // без заглядывания вперёд. Нужно там, где за приёмником сразу идёт
    // значение и заглядывание принимает его за индекс: приёмник BIN(
    // (BIN(A¤)=J% — это 4B 02 22 3B) и первый аргумент STR(.
    bool parse_lvalue(Expr & out, bool by_table = false);

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
    bool parse_indices(Expr & out, const Tok & name);
    bool parse_substr(Expr & out);
    bool parse_implicit(Expr & out, ExprKind kind);

    TokenSource & src_;
    Tok pending_;
    bool has_pending_;
    bool pending_operand_;
    std::string error_;
};

} // namespace iskra