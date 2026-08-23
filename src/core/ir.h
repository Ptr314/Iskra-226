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
    EX_ELEM,         // элемент массива: var — индекс массива, a — индексы
    EX_PI,

    EX_NEG,          // унарный минус
    EX_ADD, EX_SUB, EX_MUL, EX_DIV, EX_POW,

    EX_EQ, EX_NE, EX_LT, EX_LE, EX_GT, EX_GE,

    EX_ABS, EX_INT, EX_SGN, EX_SQR, EX_LOG, EX_EXP,

    EX_HEX,          // HEX(...) — байты лежат в str
    EX_AT,           // AT(строка, позиция [, сколько стереть])
    EX_TAB,          // TAB(позиция), позиции нумеруются с нуля

    // Символьные данные
    EX_ARRAY,        // ссылка на массив целиком: A¤() — одна длинная строка
    EX_SUBSTR,       // STR(что, начало [, длина])
    EX_LEN,          // LEN(  — до последнего непробельного байта, но не 0
    EX_NUMF,         // NUM(  — длина ведущей правильной записи числа
    EX_VAL,          // VAL(  — байт (или два) как число
    EX_POS           // POS(  — поиск байта по отношению; a[0] строка, a[1] образец
};

// Что известно о переменной. В оттранслированной форме — из таблиц
// переменных (docs/format.md, разд. 6), в текстовой — из DIM и из того,
// с индексом ли к ней обращаются.
struct VarInfo {
    VarInfo() : known(false), is_string(false), is_integer(false),
                is_array(false), dim1(0), dim2(0), str_len(0) {}

    bool known;
    bool is_string;
    bool is_integer;
    bool is_array;
    unsigned dim1;       // число элементов; для двумерного — первая размерность
    unsigned dim2;       // 0 у одномерного
    unsigned str_len;
};

struct Expr {
    Expr() : kind(EX_NUM), var(0), rel(EX_EQ) {}

    ExprKind kind;
    Number num;
    std::string str;
    unsigned var;
    ExprKind rel;                // POS(: знак отношения
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

// Одна переменная в операторе DIM. В оттранслированной форме размеры
// берутся из таблиц ещё при разборе, поэтому оба представления дают
// одинаковый оператор.
struct DimEntry {
    DimEntry() : var(0), dim1(0), dim2(0), str_len(0), computed(false) {}
    unsigned var;
    unsigned dim1;
    unsigned dim2;
    unsigned str_len;

    // У MAT REDIM размерности задаются выражениями и вычисляются при
    // исполнении, а не при разборе.
    bool computed;
    std::vector<Expr> sizes;
};

enum StmtKind {
    ST_PRINT,
    ST_DIM,
    ST_REDIM,        // MAT REDIM — размерности вычисляются на ходу
    ST_LINPUT,       // ввод строки целиком, без разбора на поля
    ST_CONVERT,      // CONVERT: символьное представление числа и обратно
    ST_INPUT,
    ST_LET,
    ST_FOR,
    ST_NEXT,
    ST_IF,
    ST_GOTO,
    ST_GOSUB,
    ST_RETURN,
    ST_ON,
    ST_STOP,
    ST_END,
    ST_REM
};

struct Stmt {
    Stmt() : kind(ST_REM), var(0), line(0), is_gosub(false), has_prompt(false),
             has_step(false), newline(true) {}

    StmtKind kind;

    std::vector<PrintItem> items;    // PRINT
    std::vector<DimEntry> dims;      // DIM
    std::vector<Expr> targets;       // LET — цели слева; INPUT — приёмники
    Expr e;                          // LET — правая часть; IF — условие; FOR — начало
    Expr limit;                      // FOR — TO
    Expr step;                       // FOR — STEP
    unsigned var;                    // FOR, NEXT
    unsigned line;                   // GOTO, GOSUB, IF … THEN <строка>
    std::vector<unsigned> lines;     // ON … GOTO/GOSUB — список переходов
    bool is_gosub;                   // ON: переход с возвратом
    std::string prompt;              // INPUT — подсказка; CONVERT — образ
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
    std::vector<VarInfo> vars;

    // Индекс строки с данным номером; false, если такой нет.
    bool find(unsigned number, unsigned & index) const;
};

} // namespace iskra