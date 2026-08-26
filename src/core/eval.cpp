// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Mikhail Revzin <p3.141592653589793238462643@gmail.com>
// Part of the Iskra-226 project: https://github.com/Ptr314/Iskra-226
// Description: вычисление выражений прямо из потока токенов

#include "core/eval.h"

#include <cmath>
#include <cstdio>

namespace iskra {

namespace {

bool relation(Tok::Type t, int c, bool & out)
{
    switch (t) {
        case Tok::EQ: out = (c == 0); return true;
        case Tok::NE: out = (c != 0); return true;
        case Tok::LT: out = (c <  0); return true;
        case Tok::LE: out = (c <= 0); return true;
        case Tok::GT: out = (c >  0); return true;
        case Tok::GE: out = (c >= 0); return true;
        default: return false;
    }
}

bool is_relation(Tok::Type t)
{
    return t == Tok::EQ || t == Tok::NE || t == Tok::LT
        || t == Tok::LE || t == Tok::GT || t == Tok::GE;
}

Value from_bool(bool b)
{
    Value v;
    v.num = Number::from_int(b ? 1 : 0);
    return v;
}

} // namespace

bool Evaluator::number(Number & out)
{
    Value v;
    if (!expr(v)) return false;
    if (v.is_str) return fail("здесь нужно число, а не строка");
    out = v.num;
    return true;
}

bool Evaluator::text(std::string & out)
{
    Value v;
    if (!expr(v)) return false;
    if (!v.is_str) return fail("здесь нужна строка, а не число");
    out = v.str;
    return true;
}

bool Evaluator::expr(Value & out) { return logic(out); }

// Связки условий равноправны и вычисляются слева направо (руководство,
// разд. 4.5): `A OR B AND C` — это `(A OR B) AND C`.
bool Evaluator::logic(Value & out)
{
    if (!compare(out)) return false;
    for (;;) {
        Tok t;
        if (!ex_.peek(t, false)) return fail(ex_.error());
        if (t.t != Tok::AND && t.t != Tok::OR && t.t != Tok::XOR) return true;
        ex_.consume();

        Value b;
        if (!compare(b)) return false;
        if (out.is_str || b.is_str) return fail("связка условий над строкой");
        const bool x = !out.num.is_zero(), y = !b.num.is_zero();
        out = from_bool(t.t == Tok::AND ? (x && y)
                      : t.t == Tok::OR  ? (x || y)
                                        : (x != y));
    }
}

bool Evaluator::compare(Value & out)
{
    if (!sum(out)) return false;
    Tok t;
    if (!ex_.peek(t, false)) return fail(ex_.error());
    // `D4` внутри группы `PLOT` — закрывающая скобка группы, а не знак
    // «больше»: `PLOT <X%,Y%,U>` кончается там же, где начинается перо.
    if (!is_relation(t.t) || (stop_gt_ && t.t == Tok::GT)) return true;
    ex_.consume();

    Value b;
    if (!sum(b)) return false;

    int c;
    if (out.is_str || b.is_str) {
        // Сравнение строк побайтовое; более короткая дополняется пробелами.
        if (!out.is_str || !b.is_str) return fail("сравнение строки с числом");
        std::string x = out.str, y = b.str;
        while (x.size() < y.size()) x += ' ';
        while (y.size() < x.size()) y += ' ';
        c = x.compare(y);
    } else {
        c = out.num.compare(b.num);
    }
    bool r = false;
    if (!relation(t.t, c, r)) return fail("непонятный знак сравнения");
    out = from_bool(r);
    return true;
}

bool Evaluator::sum(Value & out)
{
    if (!product(out)) return false;
    for (;;) {
        Tok t;
        if (!ex_.peek(t, false)) return fail(ex_.error());
        if (t.t != Tok::PLUS && t.t != Tok::MINUS) return true;
        ex_.consume();

        Value b;
        if (!product(b)) return false;
        if (out.is_str || b.is_str) return fail("сложение строк не определено");
        Number r;
        const bool ok = (t.t == Tok::PLUS) ? Number::add(out.num, b.num, r)
                                           : Number::sub(out.num, b.num, r);
        if (!ok) return math_fail("переполнение");
        out.num = r;
    }
}

bool Evaluator::product(Value & out)
{
    if (!unary(out)) return false;
    for (;;) {
        Tok t;
        if (!ex_.peek(t, false)) return fail(ex_.error());
        if (t.t != Tok::STAR && t.t != Tok::SLASH) return true;
        ex_.consume();

        Value b;
        if (!unary(b)) return false;
        if (out.is_str || b.is_str) return fail("умножение строк не определено");
        Number r;
        bool ok;
        if (t.t == Tok::STAR) {
            ok = Number::mul(out.num, b.num, r);
        } else {
            if (b.num.is_zero()) return math_fail("деление на ноль");
            ok = Number::div(out.num, b.num, r);
        }
        if (!ok) return math_fail("переполнение");
        out.num = r;
    }
}

// Ровно один операнд: без операций, без степени и без заглядывания вперёд.
// Знак впереди всё же разбираем — как машина кодирует отрицательную
// константу в `DATA`, неизвестно, а прочесть обе записи ничего не стоит.
bool Evaluator::operand(Value & out)
{
    Tok t;
    if (!ex_.peek(t, true)) return fail(ex_.error());
    if (t.t == Tok::MINUS || t.t == Tok::PLUS) {
        const bool neg = (t.t == Tok::MINUS);
        ex_.consume();
        if (!operand(out)) return false;
        if (!neg) return true;
        if (out.is_str) return fail("минус перед строкой");
        out.num = out.num.negated();
        return true;
    }
    return primary(out);          // заглянутую лексему primary() и заберёт
}

bool Evaluator::unary(Value & out)
{
    Tok t;
    if (!ex_.peek(t, true)) return fail(ex_.error());
    if (t.t == Tok::MINUS) {
        ex_.consume();
        if (!unary(out)) return false;
        if (out.is_str) return fail("минус перед строкой");
        out.num = out.num.negated();
        return true;
    }
    if (t.t == Tok::PLUS) { ex_.consume(); return unary(out); }
    return power(out);
}

bool Evaluator::power(Value & out)
{
    if (!primary(out)) return false;
    Tok t;
    if (!ex_.peek(t, false)) return fail(ex_.error());
    if (t.t != Tok::CARET) return true;
    ex_.consume();

    Value b;
    if (!unary(b)) return false;               // возведение правоассоциативно
    if (out.is_str || b.is_str) return fail("степень строки не определена");
    Number r;
    if (!Number::pow(out.num, b.num, r)) return math_fail("ошибка возведения в степень");
    out.num = r;
    return true;
}

// Список индексов до закрывающей скобки; в тексте скобка есть, в потоке
// вместо неё стоит D0.
bool Evaluator::indices(long * idx, unsigned & n)
{
    n = 0;
    for (;;) {
        Number v;
        if (!number(v)) return false;
        // «Если арифметические выражения состоят из целой и дробной частей,
        // используется только их целая часть» (разд. 7.1).
        long k = 0;
        if (!v.floor_to_int(k)) return fail("индекс массива не целое число");
        if (n < 2) idx[n] = k;
        ++n;

        Tok t;
        if (!ex_.take(t, false)) return fail(ex_.error());
        if (t.t == Tok::COMMA) continue;
        if (t.t != Tok::RPAR) return fail("список индексов не закрыт");
        break;
    }
    if (n > 2) return fail("больше двух индексов");
    return true;
}

bool Evaluator::var_value(const Tok & t, Value & out)
{
    long idx[2] = { 0, 0 };
    unsigned n = 0;
    const bool indexed = (t.t == Tok::ARRAY) ? false : t.indexed;
    if (indexed && !indices(idx, n)) return false;

    if (t.t == Tok::ARRAY && !vars_.is_string(t.var))
        return fail("числовой массив целиком в выражении не значение");
    if (vars_.is_string(t.var) || t.t == Tok::ARRAY) {
        VarStore::StrLoc loc;
        // Массив целиком — одна непрерывная строка (разд. 13.2).
        if (!vars_.str_element(t.var, idx, t.t == Tok::ARRAY ? 0 : n, loc, error_))
            return false;
        out.is_str = true;
        out.str = loc.data->substr(loc.off, loc.len);
        return true;
    }
    Number * cell = 0;
    if (!vars_.slot(t.var, idx, n, cell, error_)) return false;
    out.num = *cell;
    return true;
}

// STR(что, начало [, длина]) — вырезка из поля символьной переменной.
// Первая запятая в потоке не кодируется (docs/format.md, разд. 5).
bool Evaluator::substr(Value & out, VarStore::StrLoc & loc)
{
    Tok t;
    if (!ex_.take(t, true)) return fail(ex_.error());

    if (t.t == Tok::STR) {
        // Литерал: вырезаем из него же. Присваивать сюда нельзя — это
        // проверяет target().
        lit_ = t.s;
        loc.data = &lit_;
        loc.off = 0;
        loc.len = static_cast<unsigned>(lit_.size());
    } else if (t.t == Tok::VAR || t.t == Tok::ARRAY) {
        long idx[2] = { 0, 0 };
        unsigned n = 0;
        // Индексация первого аргумента — только по таблицам: за именем тут
        // идёт не индекс, а начало подстроки (CLAUDE.md, ловушка 2).
        if (t.t == Tok::VAR && t.table_array && !indices(idx, n)) return false;
        if (!vars_.str_element(t.var, idx, n, loc, error_)) return false;
    } else {
        return fail("STR( ждёт символьную переменную");
    }

    if (!ex_.peek(t, true)) return fail(ex_.error());
    if (t.t == Tok::COMMA) ex_.consume();      // первая запятая не кодируется

    Number n;
    if (!number(n)) return false;
    long start = 0;
    if (!n.floor_to_int(start)) return fail("STR(: начало не целое число");
    if (start < 1) return fail("STR(: начало меньше единицы");
    if (static_cast<unsigned long>(start) > loc.len)
        return fail("STR(: начало за границей строки");

    const unsigned off = static_cast<unsigned>(start) - 1;
    unsigned len = loc.len - off;              // по умолчанию до конца

    if (!ex_.take(t, false)) return fail(ex_.error());
    if (t.t == Tok::COMMA) {
        if (!number(n)) return false;
        long want = 0;
        if (!n.floor_to_int(want)) return fail("STR(: длина не целое число");
        if (want < 0) return fail("STR(: длина меньше нуля");
        if (static_cast<unsigned long>(want) > len)
            return fail("STR(: длина за границей строки");
        len = static_cast<unsigned>(want);
        if (!ex_.take(t, false)) return fail(ex_.error());
    }
    if (t.t != Tok::RPAR) return fail("STR( не закрыт");

    loc.off += off;
    loc.len = len;
    out.is_str = true;
    out.str = loc.data->substr(loc.off, loc.len);
    return true;
}

// Неявные функции: скобок в потоке нет вовсе, аргумент один.
//
// **Закрывающую скобку трогать нельзя.** В тексте `LEN(A¤)` она есть, а в
// потоке её нет: `B=LEN(A¤)` = `36 04 00 D9 ED 01`, и `D0` там не
// появляется (то же у `NUM`, `VAL` и `POS`). Значит всякий `D0`, который
// виден после аргумента, принадлежит кому-то снаружи — и съесть его значит
// сломать того, кто его ждёт. Ровно так `EDITOR` 5705 и вставал:
// `STR(B9¤,1,LEN B9¤)` — скобку `STR(` съедала `LEN`, а `STR(` потом
// сообщала, что не закрыта.
bool Evaluator::implicit(Value & out, Tok::Type fn)
{
    Value a;
    if (!primary(a)) return false;

    Tok t;
    if (!ex_.peek(t, false)) return fail(ex_.error());

    if (fn == Tok::FN_POS) {
        // «Поиск проводится с начала поисковой переменной, и за одну
        // операцию находится только одна величина» (разд. 15.1).
        if (!a.is_str) return fail("POS( ждёт символьную переменную");
        if (!is_relation(t.t)) return fail("POS( без знака отношения");
        const Tok::Type rel = t.t;
        ex_.consume();

        Value b;
        if (!primary(b)) return false;
        int want;
        if (b.is_str) {
            if (b.str.empty()) return fail("POS( с пустым образцом");
            want = static_cast<unsigned char>(b.str[0]);
        } else {
            long v = 0;
            b.num.to_int(v);
            want = static_cast<int>(v);
        }

        unsigned found = 0;
        for (std::size_t i = 0; i < a.str.size(); ++i) {
            bool hit = false;
            const int c = static_cast<unsigned char>(a.str[i]);
            if (!relation(rel, (c < want) ? -1 : (c > want ? 1 : 0), hit))
                return fail("непонятный знак отношения");
            if (hit) { found = static_cast<unsigned>(i + 1); break; }
        }
        out.num = Number::from_int(static_cast<long>(found));
        return true;
    }

    if (!a.is_str) return fail("функция ждёт символьную переменную");

    if (fn == Tok::FN_LEN) {
        out.num = Number::from_int(static_cast<long>(str_len_value(a.str)));
    } else if (fn == Tok::FN_NUM) {
        out.num = Number::from_int(static_cast<long>(str_num_value(a.str)));
    } else {
        // VAL(: «преобразует двоичное значение содержимого первого байта или
        // первых двух байтов» (разд. 14.2). Старший байт первый — так велит
        // тождество VAL(X¤,2) = VAL(X¤)*256 + VAL(STR(X¤,2)).
        if (a.str.empty()) return fail("VAL( от пустой строки");
        unsigned long r = static_cast<unsigned char>(a.str[0]);
        if (t.t == Tok::COMMA) {
            ex_.consume();
            Tok two;
            if (!ex_.take(two, true)) return fail(ex_.error());
            if (two.t != Tok::HASH) return fail("у VAL( второй аргумент может быть только 2");
            if (a.str.size() < 2) return fail("VAL( с двумя байтами от строки короче двух");
            r = r * 256 + static_cast<unsigned char>(a.str[1]);
        }
        out.num = Number::from_int(static_cast<long>(r));
    }
    return true;
}

// Функция со скобкой: аргументы через запятую, закрывается D0.
// Угол в радианы и обратно. «Углы могут задаваться в градусах, радианах или
// градах» (руководство, разд. 4.6); единицу держит таблица устройств, а
// вычислителю её ставит исполнитель.
double Evaluator::to_radians(double v) const
{
    if (angle_ == 1) return v * 3.14159265358979323846 / 180.0;
    if (angle_ == 2) return v * 3.14159265358979323846 / 200.0;
    return v;
}

double Evaluator::from_radians(double v) const
{
    if (angle_ == 1) return v * 180.0 / 3.14159265358979323846;
    if (angle_ == 2) return v * 200.0 / 3.14159265358979323846;
    return v;
}

bool Evaluator::call(Value & out, Tok::Type fn)
{
    Number args[3];
    unsigned n = 0;
    for (;;) {
        if (n >= 3) return fail("слишком много аргументов у функции");
        if (!number(args[n++])) return false;
        Tok t;
        if (!ex_.take(t, false)) return fail(ex_.error());
        if (t.t == Tok::COMMA) continue;
        if (t.t != Tok::RPAR) return fail("функция не закрыта");
        break;
    }

    if (n != 1) return fail("этой функции нужен один аргумент");
    const Number & a = args[0];
    switch (fn) {
        case Tok::FN_ABS:
            out.num = a.is_negative() ? a.negated() : a;
            return true;
        case Tok::FN_INT:
            // «Ближайшее меньшее целое число» (разд. 4.3).
            out.num = a.floor();
            return true;
        case Tok::FN_SGN: {
            const int c = a.compare(Number());
            out.num = Number::from_int(c > 0 ? 1 : (c < 0 ? -1 : 0));
            return true;
        }
        case Tok::FN_SQR:
            if (!Number::sqrt(a, out.num)) return math_fail("корень из отрицательного числа");
            return true;
        case Tok::FN_LOG:
            if (a.compare(Number()) <= 0) return math_fail("логарифм неположительного числа");
            out.num = Number::from_double(std::log(a.to_double()));
            return true;
        case Tok::FN_EXP:
            out.num = Number::from_double(std::exp(a.to_double()));
            return true;

        // «Углы могут задаваться в градусах, радианах или градах»
        // (разд. 4.6): перед синусом угол приводится к радианам, а обратные
        // функции возвращают его обратно в текущих единицах.
        case Tok::FN_SIN:
            out.num = Number::from_double(std::sin(to_radians(a.to_double())));
            return true;
        case Tok::FN_COS:
            out.num = Number::from_double(std::cos(to_radians(a.to_double())));
            return true;
        case Tok::FN_TAN: {
            const double c = std::cos(to_radians(a.to_double()));
            if (c == 0.0) return math_fail("тангенс прямого угла");
            out.num = Number::from_double(
                std::sin(to_radians(a.to_double())) / c);
            return true;
        }
        case Tok::FN_ASIN: {
            const double x = a.to_double();
            if (x < -1.0 || x > 1.0) return math_fail("ARCSIN вне -1…1");
            out.num = Number::from_double(from_radians(std::asin(x)));
            return true;
        }
        case Tok::FN_ACOS: {
            const double x = a.to_double();
            if (x < -1.0 || x > 1.0) return math_fail("ARCCOS вне -1…1");
            out.num = Number::from_double(from_radians(std::acos(x)));
            return true;
        }
        case Tok::FN_ATAN:
            out.num = Number::from_double(from_radians(std::atan(a.to_double())));
            return true;

        default: break;
    }
    return fail("функция ещё не вычисляется");
}

// FN<имя>(<а.в.>) — «сначала вычисляется значение арифметического
// выражения, затем это значение присваивается формальной переменной и
// осуществляется вычисление значения выражения, заданного в определении
// функции» (руководство, разд. 4.8). Подстановку делает тот, кто держит
// программу: тело определения лежит в другом операторе.
bool Evaluator::user_call(Value & out, unsigned name)
{
    Value arg;
    if (!expr(arg)) return false;
    Tok c;
    if (!ex_.take(c, false) || c.t != Tok::RPAR)
        return fail("FN: скобка не закрыта");

    if (!fn_)
        return fail(std::string("FN ") + static_cast<char>(name) +
                    ": функции пользователя вне программы не бывает");
    std::string err;
    if (!fn_->call_fn(name, arg, out, err))
        return fail(err.empty() ? (std::string("нет функции FN ") +
                                   static_cast<char>(name)) : err);
    return true;
}

bool Evaluator::primary(Value & out)
{
    out = Value();
    Tok t;
    if (!ex_.take(t, true)) return fail(ex_.error());

    switch (t.t) {
        case Tok::NUM: out.num = t.num; return true;
        case Tok::PI:  out.num = Number::pi(); return true;
        case Tok::STR:
        case Tok::FN_HEX:
            out.is_str = true;
            out.str = t.s;
            return true;

        case Tok::VAR:
        case Tok::ARRAY:
            return var_value(t, out);

        case Tok::LPAR: {
            if (!expr(out)) return false;
            Tok c;
            if (!ex_.take(c, false) || c.t != Tok::RPAR)
                return fail("скобка не закрыта");
            return true;
        }

        case Tok::FN_ABS: case Tok::FN_INT: case Tok::FN_SGN:
        case Tok::FN_SQR: case Tok::FN_LOG: case Tok::FN_EXP:
        case Tok::FN_SIN: case Tok::FN_COS: case Tok::FN_TAN:
        case Tok::FN_ASIN: case Tok::FN_ACOS: case Tok::FN_ATAN:
            return call(out, t.t);

        case Tok::FN_STR: {
            VarStore::StrLoc loc;
            return substr(out, loc);
        }
        case Tok::FN_LEN: case Tok::FN_NUM:
        case Tok::FN_VAL: case Tok::FN_POS:
            return implicit(out, t.t);

        case Tok::FN_USER: return user_call(out, t.var);

        default: break;
    }
    // У многих лексем текста нет вовсе, и сообщение выходило пустым.
    // Называем тогда хотя бы номер — так же, как это делает транслятор.
    if (!t.s.empty()) return fail("операнд ещё не вычисляется: " + t.s);
    char b[32];
    std::sprintf(b, "лексема %d", static_cast<int>(t.t));
    return fail(std::string("операнд ещё не вычисляется: ") + b);
}

// --- приёмники --------------------------------------------------------------

bool Evaluator::target(Target & t, bool by_table)
{
    t = Target();
    Tok k;
    if (!ex_.take(k, true)) return fail(ex_.error());

    if (k.t == Tok::FN_STR) {
        Value ignored;
        VarStore::StrLoc loc;
        if (!substr(ignored, loc)) return false;
        if (loc.data == &lit_) return fail("присваивание в литерал");
        // Приёмник — вырезка: запоминаем место, а не значение.
        t.is_str = true;
        t.data = loc.data;
        t.off = loc.off;
        t.len = loc.len;
        t.var = 0;
        t.whole = false;
        return true;
    }
    if (k.t != Tok::VAR && k.t != Tok::ARRAY)
        return fail("приёмником ожидалась переменная");

    t.var = k.var;
    t.nidx = 0;
    const bool indexed = (k.t == Tok::ARRAY) ? false
                       : (by_table ? k.table_array : k.indexed);
    if (indexed && !indices(t.idx, t.nidx)) return false;

    // Числовой массив целиком — приёмник дисковых операторов: значения
    // ложатся в него по элементам, поэтому места в поле у него нет.
    if (k.t == Tok::ARRAY && !vars_.is_string(k.var)) {
        t.whole = true;
        t.nidx = 0;
        return true;
    }

    if (vars_.is_string(k.var) || k.t == Tok::ARRAY) {
        VarStore::StrLoc loc;
        if (!vars_.str_element(k.var, t.idx, k.t == Tok::ARRAY ? 0 : t.nidx,
                               loc, error_))
            return false;
        t.is_str = true;
        t.data = loc.data;
        t.off = loc.off;
        t.len = loc.len;
        t.whole = (k.t == Tok::ARRAY);
    }
    return true;
}

bool Evaluator::load(const Target & t, Value & v)
{
    v = Value();
    if (t.is_str) {
        if (!t.data) return fail("приёмник не разрешён");
        v.is_str = true;
        v.str = t.data->substr(t.off, t.len);
        return true;
    }
    Number * cell = 0;
    if (!vars_.slot(t.var, t.idx, t.nidx, cell, error_)) return false;
    v.num = *cell;
    return true;
}

bool Evaluator::store(const Target & t, const Value & v)
{
    if (t.is_str) {
        if (!v.is_str) return fail("символьной переменной присваивается число");
        if (!t.data) return fail("приёмник не разрешён");
        // Поле постоянной длины: короткое значение дополняется пробелами,
        // длинное обрезается (руководство, разд. 13.1).
        std::string s = v.str;
        if (s.size() > t.len) s.resize(t.len);
        while (s.size() < t.len) s += ' ';
        t.data->replace(t.off, t.len, s);
        return true;
    }
    if (v.is_str) return fail("числовой переменной присваивается строка");
    Number * cell = 0;
    if (!vars_.slot(t.var, t.idx, t.nidx, cell, error_)) return false;
    *cell = v.num;
    return true;
}

} // namespace iskra
