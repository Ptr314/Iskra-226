// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Mikhail Revzin <p3.141592653589793238462643@gmail.com>
// Part of the Iskra-226 project: https://github.com/Ptr314/Iskra-226
// Description: исполнение промежуточного представления

#include "core/interp.h"

#include <cmath>
#include <cstdio>

namespace iskra {

namespace {
    const unsigned ZONE = 16;      // «каждая строка условно делится на 5 зон»

    std::string num_str(unsigned v)
    {
        char b[16];
        std::sprintf(b, "%u", v);
        return b;
    }
}

bool Program::find(unsigned number, unsigned & index) const
{
    for (unsigned i = 0; i < lines.size(); ++i)
        if (lines[i].number == number) { index = i; return true; }
    return false;
}

Interp::Interp(const Program & prog, Host & host)
    : prog_(prog), host_(host), li_(0), si_(0), jumped_(false), stopped_(false),
      max_steps_(2000000)
{
}

bool Interp::fail(const std::string & m)
{
    if (error_.empty()) {
        error_ = m;
        if (li_ < prog_.lines.size())
            error_ = "строка " + num_str(prog_.lines[li_].number) + ": " + m;
    }
    return false;
}

bool Interp::variable(unsigned index, Number & out) const
{
    std::map<unsigned, Number>::const_iterator it = vars_.find(index);
    if (it == vars_.end()) return false;
    out = it->second;
    return true;
}

void Interp::emit(const std::string & koi8)
{
    if (koi8.empty()) return;
    host_.screen().write(reinterpret_cast<const uint8_t *>(koi8.data()),
                         static_cast<unsigned>(koi8.size()));
}

void Interp::emit_newline()
{
    Screen & s = host_.screen();
    s.put(CC_CR);
    s.put(CC_DOWN);
}

void Interp::emit_zone()
{
    Screen & s = host_.screen();
    const unsigned col = s.col();
    unsigned next = ((col - 1) / ZONE + 1) * ZONE + 1;
    if (next > SCREEN_COLS) {
        // «если предыдущее значение печаталось в последней зоне, следующее
        // будет печататься в первой зоне следующей строки»
        emit_newline();
        return;
    }
    s.at(s.row(), next);
}

// Массивы «Искры» индексируются с единицы: «А(5) означает 5-й элемент
// массива А()», а DIM A(120) отводит 120 элементов (руководство, разд. 7.1).
bool Interp::array_alloc(unsigned var, unsigned dim1, unsigned dim2)
{
    if (dim1 == 0) dim1 = 10;                  // размерность по умолчанию
    const unsigned long total = static_cast<unsigned long>(dim1) *
                                (dim2 ? dim2 : 1);
    if (total > 1000000UL) return fail("слишком большой массив");

    Array & a = arrays_[var];
    a.dim1 = dim1;
    a.dim2 = dim2;
    a.cells.assign(static_cast<std::size_t>(total), Number());
    return true;
}

bool Interp::slot(const Expr & e, Number *& out)
{
    if (e.kind == EX_VAR) {
        out = &vars_[e.var];
        return true;
    }
    if (e.kind != EX_ELEM) return fail("сюда нельзя присвоить значение");

    std::map<unsigned, Array>::iterator it = arrays_.find(e.var);
    if (it == arrays_.end()) {
        // Массив, к которому обратились без DIM: размеры берём из таблиц
        // переменных, если они есть, иначе десять элементов.
        unsigned d1 = 0, d2 = 0;
        if (e.var < prog_.vars.size()) {
            d1 = prog_.vars[e.var].dim1;
            d2 = prog_.vars[e.var].dim2;
        }
        if (!array_alloc(e.var, d1, d2)) return false;
        it = arrays_.find(e.var);
    }
    Array & a = it->second;

    // «Если арифметические выражения состоят из целой и дробной частей,
    // используется только их целая часть» (разд. 7.1).
    unsigned idx[2] = { 0, 0 };
    for (unsigned k = 0; k < e.a.size(); ++k) {
        Number n;
        if (!eval_num(e.a[k], n)) return false;
        long v = 0;
        if (!Number::from_double(std::floor(n.to_double())).to_int(v))
            return fail("индекс массива не целое число");
        if (v < 1) return fail("индекс массива меньше единицы");
        idx[k] = static_cast<unsigned>(v);
    }

    std::size_t off;
    if (a.dim2) {
        if (e.a.size() != 2) return fail("двумерному массиву нужны два индекса");
        if (idx[0] > a.dim1 || idx[1] > a.dim2) return fail("индекс за границей массива");
        off = static_cast<std::size_t>(idx[0] - 1) * a.dim2 + (idx[1] - 1);
    } else {
        if (e.a.size() != 1) return fail("одномерному массиву нужен один индекс");
        if (idx[0] > a.dim1) return fail("индекс за границей массива");
        off = idx[0] - 1;
    }

    out = &a.cells[off];
    return true;
}

bool Interp::do_dim(const Stmt & s)
{
    for (unsigned i = 0; i < s.dims.size(); ++i) {
        const DimEntry & d = s.dims[i];
        if (d.str_len && !d.dim1) continue;         // объявление длины строки
        if (!array_alloc(d.var, d.dim1, d.dim2)) return false;
    }
    return true;
}

bool Interp::eval_num(const Expr & e, Number & n)
{
    Value v;
    if (!eval(e, v)) return false;
    if (v.is_str) return fail("здесь ожидалось число, а не строка");
    n = v.num;
    return true;
}

bool Interp::eval(const Expr & e, Value & v)
{
    v = Value();

    switch (e.kind) {
        case EX_NUM: v.num = e.num; return true;
        case EX_PI:  v.num = Number::pi(); return true;

        case EX_STR:
        case EX_HEX:
            v.is_str = true;
            v.str = e.str;
            return true;

        case EX_VAR: {
            std::map<unsigned, Number>::const_iterator it = vars_.find(e.var);
            // Неинициализированная переменная равна нулю — как на машине.
            if (it != vars_.end()) v.num = it->second;
            return true;
        }

        case EX_ELEM: {
            Number * cell = 0;
            if (!slot(e, cell)) return false;
            v.num = *cell;
            return true;
        }

        case EX_AT:
        case EX_TAB:
            return fail("AT( и TAB( допустимы только в PRINT");

        default: break;
    }

    Number a, b;
    if (!eval_num(e.a[0], a)) return false;

    if (e.kind == EX_NEG)  { v.num = a.negated(); return true; }
    if (e.kind == EX_ABS)  { v.num = a; if (v.num.is_negative()) v.num = v.num.negated(); return true; }
    if (e.kind == EX_SGN)  {
        const int c = a.compare(Number());
        v.num = Number::from_int(c > 0 ? 1 : (c < 0 ? -1 : 0));
        return true;
    }
    if (e.kind == EX_INT) {
        // Отбрасывание дробной части вниз, как INT в Бейсике.
        v.num = Number::from_double(std::floor(a.to_double()));
        return true;
    }
    if (e.kind == EX_SQR) {
        if (!Number::sqrt(a, v.num)) return fail("корень из отрицательного числа");
        return true;
    }
    if (e.kind == EX_LOG) {
        if (a.compare(Number()) <= 0) return fail("логарифм неположительного числа");
        v.num = Number::from_double(std::log(a.to_double()));
        return true;
    }
    if (e.kind == EX_EXP) {
        v.num = Number::from_double(std::exp(a.to_double()));
        return true;
    }

    if (!eval_num(e.a[1], b)) return false;

    bool ok = true;
    switch (e.kind) {
        case EX_ADD: ok = Number::add(a, b, v.num); break;
        case EX_SUB: ok = Number::sub(a, b, v.num); break;
        case EX_MUL: ok = Number::mul(a, b, v.num); break;
        case EX_DIV:
            if (b.is_zero()) return fail("деление на нуль");
            ok = Number::div(a, b, v.num);
            break;
        case EX_POW: ok = Number::pow(a, b, v.num); break;

        case EX_EQ: case EX_NE: case EX_LT:
        case EX_LE: case EX_GT: case EX_GE: {
            const int c = a.compare(b);
            bool r = false;
            switch (e.kind) {
                case EX_EQ: r = (c == 0); break;
                case EX_NE: r = (c != 0); break;
                case EX_LT: r = (c < 0);  break;
                case EX_LE: r = (c <= 0); break;
                case EX_GT: r = (c > 0);  break;
                default:    r = (c >= 0); break;
            }
            v.num = Number::from_int(r ? 1 : 0);
            return true;
        }

        default:
            return fail("не реализованная операция");
    }

    if (!ok) return fail("переполнение порядка");
    return true;
}

bool Interp::do_print(const Stmt & s)
{
    bool last_was_at = false;

    for (unsigned i = 0; i < s.items.size(); ++i) {
        const PrintItem & item = s.items[i];

        if (item.e.kind == EX_TAB) {
            // «позиции строки нумеруются с нуля» (разд. 4.4), и курсор
            // двигается только вправо.
            Number n;
            if (!eval_num(item.e.a[0], n)) return false;
            long v = 0;
            n.to_int(v);
            const unsigned col = static_cast<unsigned>(v < 0 ? 0 : v) + 1;
            if (col > host_.screen().col() && col <= SCREEN_COLS)
                host_.screen().at(host_.screen().row(), col);
            last_was_at = false;
        } else if (item.e.kind == EX_AT) {
            Number r, c;
            if (!eval_num(item.e.a[0], r)) return false;
            if (!eval_num(item.e.a[1], c)) return false;
            long rv = 0, cv = 0;
            r.to_int(rv);
            c.to_int(cv);
            host_.screen().at(static_cast<unsigned>(rv < 1 ? 1 : rv),
                              static_cast<unsigned>(cv < 1 ? 1 : cv));
            if (item.e.a.size() > 2) {
                Number n;
                if (!eval_num(item.e.a[2], n)) return false;
                long nv = 0;
                n.to_int(nv);
                if (nv > 0) host_.screen().erase(static_cast<unsigned>(nv));
            }
            last_was_at = true;
        } else {
            Value v;
            if (!eval(item.e, v)) return false;
            if (v.is_str) {
                emit(v.str);
            } else {
                // «с учетом знака перед числом и пробела после числа»
                emit(v.num.to_display());
                emit(" ");
            }
            last_was_at = false;
        }

        if (item.sep == SEP_ZONE) emit_zone();
    }

    // PRINT AT(...) курсор только ставит и перевода строки не делает:
    // «курсор устанавливается в тридцатую позицию восьмой строки экрана»,
    // а печать следующего оператора идёт с этой позиции (пример 17.5).
    if (s.newline && !last_was_at) emit_newline();
    return true;
}

bool Interp::do_input(const Stmt & s)
{
    if (s.has_prompt) emit(s.prompt);
    emit("?");

    std::string line;
    for (;;) {
        uint8_t code = 0;
        if (!host_.poll_key(code)) return fail("INPUT: нет данных на клавиатуре");
        if (code == 0x0D || code == 0x0A) break;
        if (code == 0x08) {                       // ВШ — забой
            if (!line.empty()) {
                line.resize(line.size() - 1);
                host_.screen().put(CC_LEFT);
                host_.screen().put(0x20);
                host_.screen().put(CC_LEFT);
            }
            continue;
        }
        line += static_cast<char>(code);
        host_.screen().put(code);
    }
    emit_newline();

    unsigned p = 0;
    for (unsigned i = 0; i < s.targets.size(); ++i) {
        std::string field;
        while (p < line.size() && line[p] != ',') field += line[p++];
        if (p < line.size()) ++p;

        Number n;
        if (!Number::parse(field, n)) return fail("INPUT: не число «" + field + "»");
        Number * cell = 0;
        if (!slot(s.targets[i], cell)) return false;
        *cell = n;
    }
    return true;
}

bool Interp::do_for(const Stmt & s)
{
    Number start, limit, step;
    if (!eval_num(s.e, start)) return false;
    if (!eval_num(s.limit, limit)) return false;
    if (s.has_step) {
        if (!eval_num(s.step, step)) return false;
    } else {
        step = Number::from_int(1);
    }
    if (step.is_zero()) return fail("FOR с нулевым шагом");

    vars_[s.var] = start;

    // Уже открытый цикл по той же переменной перезапускается.
    for (unsigned i = 0; i < loops_.size(); ++i) {
        if (loops_[i].var == s.var) { loops_.resize(i); break; }
    }

    Frame f;
    f.var = s.var;
    f.limit = limit;
    f.step = step;
    f.line = li_;
    f.stmt = si_ + 1;
    loops_.push_back(f);
    return true;
}

bool Interp::do_next(const Stmt & s)
{
    while (!loops_.empty() && loops_.back().var != s.var) loops_.pop_back();
    if (loops_.empty()) return fail("NEXT без FOR");

    Frame & f = loops_.back();
    Number v = vars_[f.var];
    if (!Number::add(v, f.step, v)) return fail("переполнение счётчика цикла");
    vars_[f.var] = v;

    const bool up = !f.step.is_negative();
    const bool go_on = up ? (v.compare(f.limit) <= 0) : (v.compare(f.limit) >= 0);

    if (go_on) {
        li_ = f.line;
        si_ = f.stmt;
        jumped_ = true;
    } else {
        loops_.pop_back();
    }
    return true;
}

bool Interp::jump(unsigned line_number)
{
    unsigned idx = 0;
    if (!prog_.find(line_number, idx))
        return fail("нет строки " + num_str(line_number));
    li_ = idx;
    si_ = 0;
    jumped_ = true;
    return true;
}

bool Interp::exec(const Stmt & s)
{
    switch (s.kind) {
        case ST_REM:
            return true;

        case ST_PRINT:
            return do_print(s);

        case ST_INPUT:
            return do_input(s);

        case ST_LET: {
            Number n;
            if (!eval_num(s.e, n)) return false;
            for (unsigned i = 0; i < s.targets.size(); ++i) {
                Number * cell = 0;
                if (!slot(s.targets[i], cell)) return false;
                *cell = n;
            }
            return true;
        }

        case ST_DIM:
            return do_dim(s);

        case ST_GOTO:
            return jump(s.line);

        case ST_GOSUB:
            if (calls_.size() > 1000) return fail("слишком глубокая вложенность GOSUB");
            calls_.push_back(std::make_pair(li_, si_ + 1));
            return jump(s.line);

        case ST_ON: {
            // Значение 1 ведёт на первый номер, 2 — на второй и т. д.
            // Ноль и всё, что за списком, просто передаёт управление дальше.
            Number n;
            if (!eval_num(s.e, n)) return false;
            long v = 0;
            if (!Number::from_double(std::floor(n.to_double())).to_int(v)) return true;
            if (v < 1 || static_cast<unsigned long>(v) > s.lines.size()) return true;

            const unsigned target = s.lines[static_cast<unsigned>(v) - 1];
            if (s.is_gosub) {
                if (calls_.size() > 1000) return fail("слишком глубокая вложенность GOSUB");
                calls_.push_back(std::make_pair(li_, si_ + 1));
            }
            return jump(target);
        }

        case ST_RETURN: {
            if (calls_.empty()) return fail("RETURN без GOSUB");
            li_ = calls_.back().first;
            si_ = calls_.back().second;
            calls_.pop_back();
            jumped_ = true;
            return true;
        }

        case ST_IF: {
            Number c;
            if (!eval_num(s.e, c)) return false;
            if (!c.is_zero()) return jump(s.line);
            return true;                       // иначе — следующий оператор
        }

        case ST_FOR:
            return do_for(s);

        case ST_NEXT:
            return do_next(s);

        case ST_STOP:
        case ST_END:
            stopped_ = true;
            return true;
    }
    return fail("неизвестный оператор");
}

bool Interp::run(std::string & error)
{
    error_.clear();
    li_ = 0;
    si_ = 0;
    stopped_ = false;

    unsigned long steps = 0;

    while (!stopped_) {
        if (li_ >= prog_.lines.size()) break;
        if (si_ >= prog_.lines[li_].stmts.size()) { ++li_; si_ = 0; continue; }

        if (max_steps_ && ++steps > max_steps_) {
            error = "превышено число шагов: похоже на зацикливание";
            return false;
        }

        jumped_ = false;
        if (!exec(prog_.lines[li_].stmts[si_])) { error = error_; return false; }
        if (!jumped_) ++si_;
    }

    host_.present();
    return true;
}

} // namespace iskra