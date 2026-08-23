// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Mikhail Revzin <p3.141592653589793238462643@gmail.com>
// Part of the Iskra-226 project: https://github.com/Ptr314/Iskra-226
// Description: разбор выражений, общий для текста и токенов

#include "core/expr.h"

namespace iskra {

ExprParser::ExprParser(TokenSource & src)
    : src_(src), has_pending_(false), pending_operand_(true)
{
}

void ExprParser::fail(const std::string & msg)
{
    if (error_.empty()) error_ = msg;
}

bool ExprParser::peek(Tok & t, bool operand_expected)
{
    if (failed()) return false;
    if (has_pending_) {
        if (pending_operand_ != operand_expected) {
            fail("внутренняя ошибка разбора: смена состояния при заглядывании");
            return false;
        }
        t = pending_;
        return true;
    }
    if (!src_.next(pending_, operand_expected)) {
        fail("не удалось прочитать лексему");
        return false;
    }
    has_pending_ = true;
    pending_operand_ = operand_expected;
    t = pending_;
    return true;
}

void ExprParser::consume()
{
    has_pending_ = false;
}

bool ExprParser::take(Tok & t, bool operand_expected)
{
    if (!peek(t, operand_expected)) return false;
    consume();
    return true;
}

bool ExprParser::parse(Expr & out)
{
    return parse_compare(out);
}

bool ExprParser::parse_compare(Expr & out)
{
    Expr left;
    if (!parse_sum(left)) return false;

    Tok t;
    if (!peek(t, false)) return false;

    ExprKind k;
    switch (t.t) {
        case Tok::EQ: k = EX_EQ; break;
        case Tok::NE: k = EX_NE; break;
        case Tok::LT: k = EX_LT; break;
        case Tok::LE: k = EX_LE; break;
        case Tok::GT: k = EX_GT; break;
        case Tok::GE: k = EX_GE; break;
        default: out = left; return true;
    }
    consume();

    Expr right;
    if (!parse_sum(right)) return false;

    out = Expr();
    out.kind = k;
    out.a.push_back(left);
    out.a.push_back(right);
    return true;
}

bool ExprParser::parse_sum(Expr & out)
{
    Expr left;
    if (!parse_product(left)) return false;

    for (;;) {
        Tok t;
        if (!peek(t, false)) return false;
        if (t.t != Tok::PLUS && t.t != Tok::MINUS) break;
        consume();

        Expr right;
        if (!parse_product(right)) return false;

        Expr node;
        node.kind = (t.t == Tok::PLUS) ? EX_ADD : EX_SUB;
        node.a.push_back(left);
        node.a.push_back(right);
        left = node;
    }
    out = left;
    return true;
}

bool ExprParser::parse_product(Expr & out)
{
    Expr left;
    if (!parse_unary(left)) return false;

    for (;;) {
        Tok t;
        if (!peek(t, false)) return false;
        if (t.t != Tok::STAR && t.t != Tok::SLASH) break;
        consume();

        Expr right;
        if (!parse_unary(right)) return false;

        Expr node;
        node.kind = (t.t == Tok::STAR) ? EX_MUL : EX_DIV;
        node.a.push_back(left);
        node.a.push_back(right);
        left = node;
    }
    out = left;
    return true;
}

bool ExprParser::parse_unary(Expr & out)
{
    Tok t;
    if (!peek(t, true)) return false;

    if (t.t == Tok::MINUS) {
        consume();
        Expr inner;
        if (!parse_unary(inner)) return false;
        out = Expr();
        out.kind = EX_NEG;
        out.a.push_back(inner);
        return true;
    }
    if (t.t == Tok::PLUS) {          // унарный плюс — ничего не меняет
        consume();
        return parse_unary(out);
    }
    return parse_power(out);
}

bool ExprParser::parse_power(Expr & out)
{
    Expr base;
    if (!parse_primary(base)) return false;

    Tok t;
    if (!peek(t, false)) return false;
    if (t.t != Tok::CARET) { out = base; return true; }
    consume();

    // Правая ассоциативность, и показатель может быть со знаком.
    Expr exponent;
    if (!parse_unary(exponent)) return false;

    out = Expr();
    out.kind = EX_POW;
    out.a.push_back(base);
    out.a.push_back(exponent);
    return true;
}

// Аргументы функции; открывающая скобка уже съедена вместе с именем.
bool ExprParser::parse_call(Expr & out, ExprKind kind, unsigned args_min,
                            unsigned args_max)
{
    out = Expr();
    out.kind = kind;

    for (;;) {
        Expr arg;
        if (!parse_compare(arg)) return false;
        out.a.push_back(arg);

        Tok t;
        if (!peek(t, false)) return false;
        if (t.t == Tok::COMMA) {
            if (out.a.size() >= args_max) { fail("слишком много аргументов"); return false; }
            consume();
            continue;
        }
        if (t.t == Tok::RPAR) { consume(); break; }
        // У AT( в токенизированной форме закрывающей скобки может не быть:
        // она закрывается концом оператора.
        if (t.t == Tok::END && kind == EX_AT) break;
        fail("ожидалась запятая или закрывающая скобка");
        return false;
    }

    if (out.a.size() < args_min) { fail("слишком мало аргументов"); return false; }
    return true;
}

bool ExprParser::parse_primary(Expr & out)
{
    Tok t;
    if (!take(t, true)) return false;

    out = Expr();
    switch (t.t) {
        case Tok::NUM:
            out.kind = EX_NUM;
            out.num = t.num;
            return true;

        case Tok::STR:
            out.kind = EX_STR;
            out.str = t.s;
            return true;

        case Tok::PI:
            out.kind = EX_PI;
            return true;

        case Tok::VAR:
            out.kind = EX_VAR;
            out.var = t.var;
            return true;

        case Tok::FN_HEX:
            out.kind = EX_HEX;
            out.str = t.s;
            return true;

        case Tok::LPAR: {
            Expr inner;
            if (!parse_compare(inner)) return false;
            Tok c;
            if (!take(c, false)) return false;
            if (c.t != Tok::RPAR) { fail("ожидалась закрывающая скобка"); return false; }
            out = inner;
            return true;
        }

        case Tok::FN_ABS: return parse_call(out, EX_ABS, 1, 1);
        case Tok::FN_INT: return parse_call(out, EX_INT, 1, 1);
        case Tok::FN_SGN: return parse_call(out, EX_SGN, 1, 1);
        case Tok::FN_SQR: return parse_call(out, EX_SQR, 1, 1);
        case Tok::FN_LOG: return parse_call(out, EX_LOG, 1, 1);
        case Tok::FN_EXP: return parse_call(out, EX_EXP, 1, 1);
        case Tok::FN_AT:  return parse_call(out, EX_AT, 2, 3);

        case Tok::UNKNOWN:
            fail("не поддержано: " + t.s);
            return false;

        case Tok::END:
            fail("оператор оборвался там, где ожидался операнд");
            return false;

        default:
            fail("ожидался операнд");
            return false;
    }
}

} // namespace iskra