// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Mikhail Revzin <p3.141592653589793238462643@gmail.com>
// Part of the Iskra-226 project: https://github.com/Ptr314/Iskra-226
// Description: промежуточное представление программы

#pragma once

#include <string>
#include <vector>

#include "core/number.h"

namespace iskra {

// Оба представления программы — текст и токены — приводятся сюда, и дальше
// исполняется только это. Модель взята с токенизированной формы, потому что
// она беднее: переменные адресуются индексом, имён в ней нет.

enum ExprKind {
    EX_NUM,          // числовая константа
    EX_STR,          // строковый литерал в КОИ-8
    EX_VAR,          // переменная по индексу
    EX_PI,

    EX_NEG,          // унарный минус
    EX_ADD, EX_SUB, EX_MUL, EX_DIV, EX_POW,

    EX_EQ, EX_NE, EX_LT, EX_LE, EX_GT, EX_GE,

    EX_ABS, EX_INT, EX_SGN, EX_SQR, EX_LOG, EX_EXP,

    EX_HEX,          // HEX(...) — байты лежат в str
    EX_AT            // AT(строка, позиция [, сколько стереть])
};

struct Expr {
    Expr() : kind(EX_NUM), var(0) {}

    ExprKind kind;
    Number num;
    std::string str;
    unsigned var;
    std::vector<Expr> a;
};

// Разделитель между элементами PRINT.
enum PrintSep {
    SEP_NONE = 0,    // последний элемент
    SEP_TIGHT,       // ';' — плотный формат
    SEP_ZONE         // ',' — зонный формат, 5 зон по 16 позиций
};

struct PrintItem {
    PrintItem() : sep(SEP_NONE) {}
    Expr e;
    PrintSep sep;
};

enum StmtKind {
    ST_PRINT,
    ST_INPUT,
    ST_LET,
    ST_FOR,
    ST_NEXT,
    ST_IF,
    ST_GOTO,
    ST_STOP,
    ST_END,
    ST_REM
};

struct Stmt {
    Stmt() : kind(ST_REM), var(0), line(0), has_prompt(false), has_step(false),
             newline(true) {}

    StmtKind kind;

    std::vector<PrintItem> items;    // PRINT
    std::vector<unsigned> targets;   // LET — переменные слева; INPUT — приёмники
    Expr e;                          // LET — правая часть; IF — условие; FOR — начало
    Expr limit;                      // FOR — TO
    Expr step;                       // FOR — STEP
    unsigned var;                    // FOR, NEXT
    unsigned line;                   // GOTO, IF … THEN <строка>
    std::string prompt;              // INPUT — подсказка в КОИ-8
    bool has_prompt;
    bool has_step;
    bool newline;                    // PRINT без хвостового разделителя
};

struct Line {
    Line() : number(0) {}
    unsigned number;
    std::vector<Stmt> stmts;
};

struct Program {
    std::vector<Line> lines;

    // Индекс строки с данным номером; false, если такой нет.
    bool find(unsigned number, unsigned & index) const;
};

} // namespace iskra