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
        if (pending_operand_ != operand_expected && src_.state_sensitive()) {
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

// Список индексов массива. Открывающей скобки в токенизированной форме нет,
// в текстовой её уже съел лексер; закрывается в обоих случаях RPAR.
bool ExprParser::parse_indices(Expr & out, const Tok & name)
{
    out = Expr();
    out.kind = EX_ELEM;
    out.var = name.var;

    for (;;) {
        Expr idx;
        if (!parse_compare(idx)) return false;
        out.a.push_back(idx);

        Tok t;
        if (!peek(t, false)) return false;
        if (t.t == Tok::COMMA) { consume(); continue; }
        if (t.t == Tok::RPAR) { consume(); break; }
        fail("список индексов не закрыт");
        return false;
    }
    if (out.a.size() > 2) { fail("массивов больше двух измерений не бывает"); return false; }
    return true;
}

// STR(что, начало [, длина]). Первая запятая в потоке не кодируется, поэтому
// после первого аргумента разбор возвращается в состояние «ожидается
// операнд» (docs/format.md, разд. 5).
bool ExprParser::parse_substr(Expr & out)
{
    out = Expr();
    out.kind = EX_SUBSTR;

    Tok t;
    if (!take(t, true)) return false;
    if (t.t == Tok::ARRAY) {
        Expr whole;
        whole.kind = EX_ARRAY;
        whole.var = t.var;
        out.a.push_back(whole);
    } else if (t.t == Tok::VAR) {
        Expr v;
        if (t.table_array) {
            if (!parse_indices(v, t)) return false;
        } else {
            v.kind = EX_VAR;
            v.var = t.var;
        }
        out.a.push_back(v);
    } else if (t.t == Tok::STR) {
        Expr lit;
        lit.kind = EX_STR;
        lit.str = t.s;
        out.a.push_back(lit);
    } else {
        fail("STR( ждёт символьную переменную");
        return false;
    }

    // Первая запятая STR( в потоке не кодируется, поэтому заглядывать надо
    // в состоянии «ожидается операнд»: следом сразу идёт второй аргумент.
    // В текстовой записи запятая есть и читается здесь же — текстовому
    // лексеру состояние безразлично.
    if (!peek(t, true)) return false;
    if (t.t == Tok::COMMA) consume();

    for (;;) {
        Expr arg;
        if (!parse_compare(arg)) return false;
        out.a.push_back(arg);

        if (!peek(t, false)) return false;
        if (t.t == Tok::COMMA) {
            if (out.a.size() >= 3) { fail("у STR( не больше трёх аргументов"); return false; }
            consume();
            continue;
        }
        if (t.t == Tok::RPAR) { consume(); break; }
        fail("STR( не закрыт");
        return false;
    }
    return true;
}

// LEN(, NUM(, VAL(, POS( — закрывающей скобки в потоке нет: аргументом
// служит один терм, и функция закрывается первой же операцией своего
// уровня. В текстовой записи скобка есть, поэтому съедаем её, если она тут.
bool ExprParser::parse_implicit(Expr & out, ExprKind kind)
{
    out = Expr();
    out.kind = kind;

    Expr arg;
    if (!parse_primary(arg)) return false;
    out.a.push_back(arg);

    Tok t;
    if (!peek(t, false)) return false;

    if (kind == EX_POS) {
        // POS( поглощает сравнение целиком: POS(I¤=A¤).
        ExprKind rel = EX_EQ;
        bool found = true;
        switch (t.t) {
            case Tok::EQ: rel = EX_EQ; break;
            case Tok::NE: rel = EX_NE; break;
            case Tok::LT: rel = EX_LT; break;
            case Tok::LE: rel = EX_LE; break;
            case Tok::GT: rel = EX_GT; break;
            case Tok::GE: rel = EX_GE; break;
            default: found = false; break;
        }
        if (found) {
            consume();
            out.rel = rel;
            Expr rhs;
            if (!parse_primary(rhs)) return false;
            out.a.push_back(rhs);
            if (!peek(t, false)) return false;
        }
    } else if (kind == EX_VAL && t.t == Tok::COMMA) {
        // VAL( допускает второй аргумент: в потоке это пара DE DB.
        consume();
        Tok second;
        if (!take(second, true)) return false;
        Expr n;
        n.kind = EX_NUM;
        n.num = (second.t == Tok::NUM) ? second.num : Number::from_int(2);
        out.a.push_back(n);
        if (!peek(t, false)) return false;
    }

    if (t.t == Tok::RPAR) consume();      // текстовая запись
    return true;
}

bool ExprParser::parse_lvalue(Expr & out)
{
    Tok t;
    if (!peek(t, true)) return false;
    if (t.t == Tok::FN_STR) { consume(); return parse_substr(out); }

    if (!take(t, true)) return false;
    if (t.t != Tok::VAR) {
        fail("слева от знака равенства ожидалась переменная, элемент массива "
             "или STR(");
        return false;
    }

    if (t.indexed) return parse_indices(out, t);

    out = Expr();
    out.kind = EX_VAR;
    out.var = t.var;
    return true;
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
            if (t.indexed) return parse_indices(out, t);
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
        case Tok::FN_TAB: return parse_call(out, EX_TAB, 1, 1);

        case Tok::FN_STR: return parse_substr(out);
        case Tok::FN_LEN: return parse_implicit(out, EX_LEN);
        case Tok::FN_NUM: return parse_implicit(out, EX_NUMF);
        case Tok::FN_VAL: return parse_implicit(out, EX_VAL);
        case Tok::FN_POS: return parse_implicit(out, EX_POS);

        case Tok::ARRAY:
            out.kind = EX_ARRAY;
            out.var = t.var;
            return true;

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