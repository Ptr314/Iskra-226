// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Mikhail Revzin <p3.141592653589793238462643@gmail.com>
// Part of the Iskra-226 project: https://github.com/Ptr314/Iskra-226
// Description: исполнение промежуточного представления

#pragma once

#include <map>
#include <string>
#include <vector>

#include "core/host.h"
#include "core/ir.h"

namespace iskra {

// Значение выражения: число либо строка символов в КОИ-8.
struct Value {
    Value() : is_str(false) {}
    bool is_str;
    Number num;
    std::string str;
};

// Исполнитель. Одинаково работает с представлением, построенным из текста,
// и с построенным из токенов, — в этом весь смысл общего IR.
class Interp
{
public:
    Interp(const Program & prog, Host & host);

    // Ограничение на число выполненных операторов: страховка от зацикливания
    // в автотестах. Ноль снимает ограничение.
    void set_max_steps(unsigned long n) { max_steps_ = n; }

    // Исполняет программу до STOP, END или конца текста.
    bool run(std::string & error);

    // Значение переменной после прогона — для проверок.
    bool variable(unsigned index, Number & out) const;

private:
    struct Frame {
        unsigned var;
        Number limit;
        Number step;
        unsigned line;      // куда возвращает NEXT
        unsigned stmt;
    };

    bool exec(const Stmt & s);
    bool eval(const Expr & e, Value & v);
    bool eval_num(const Expr & e, Number & n);
    bool eval_str(const Expr & e, std::string & out);

    bool do_print(const Stmt & s);
    bool do_dim(const Stmt & s);
    // Ячейка переменной или элемента массива — и для чтения, и для записи.
    bool slot(const Expr & e, Number *& out);
    bool array_alloc(unsigned var, unsigned dim1, unsigned dim2);

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
    bool str_loc(const Expr & e, StrLoc & loc);
    bool is_string_expr(const Expr & e) const;
    bool assign_string(const Expr & target, const std::string & value);
    bool do_input(const Stmt & s);
    bool do_for(const Stmt & s);
    bool do_next(const Stmt & s);

    void emit(const std::string & koi8);
    void emit_newline();
    void emit_zone();

    bool jump(unsigned line_number);
    bool fail(const std::string & m);

    const Program & prog_;
    Host & host_;
    struct Array {
        Array() : dim1(0), dim2(0) {}
        unsigned dim1;
        unsigned dim2;              // 0 у одномерного
        std::vector<Number> cells;
    };

    std::map<unsigned, Number> vars_;
    std::map<unsigned, Array> arrays_;
    std::map<unsigned, std::string> strs_;
    std::vector<Frame> loops_;
    std::vector<std::pair<unsigned, unsigned> > calls_;   // GOSUB: куда вернуться

    unsigned li_;           // индекс текущей строки
    unsigned si_;           // индекс текущего оператора
    bool jumped_;
    bool stopped_;
    unsigned long max_steps_;
    std::string error_;
};

} // namespace iskra