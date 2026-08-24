// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Mikhail Revzin <p3.141592653589793238462643@gmail.com>
// Part of the Iskra-226 project: https://github.com/Ptr314/Iskra-226
// Description: промежуточное представление программы

#pragma once

#include <string>
#include <vector>

#include "core/errors.h"
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

    // Связки условий. Равноправны и вычисляются слева направо
    // (руководство, разд. 4.5).
    EX_AND, EX_OR, EX_XOR,

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

// Одна запись оператора SELECT: группа устройств и её параметры. Записи
// в операторе перечисляются через запятую (руководство, разд. 6.1).
struct SelectItem {
    SelectItem() : code(0), row(0), addr(0), width(0),
                   removable(false), has_addr(false), has_drive(false) {}

    unsigned code;       // код группы, см. SelectCode в core/devtable.h
    unsigned row;        // номер строки таблицы у `#n`
    unsigned addr;       // ФАУ устройства; у `P` — цифра паузы
    unsigned width;      // ширина строки; 0 — не задана
    bool removable;      // диск R, а не F
    bool has_addr;       // у `P` — была ли цифра
    bool has_drive;      // был ли указан F или R
};

// Приставка дисковых операторов: буква устройства и номер строки таблицы.
// `DATA LOAD DC OPEN T#2,"АНКЕТА"` — устройство T, строка 2. У операторов,
// работающих с уже открытым файлом, буквы нет вовсе, только `#n`.
struct DiskRef {
    DiskRef() : has_device(false), device(0), has_row(false),
                has_addr(false), addr(0) {}

    bool has_device;
    unsigned device;     // 0 = F, 1 = R, 2 = T (устройство из таблицы)
    bool has_row;
    Expr row;            // номер строки таблицы устройств
    // Явный адрес устройства: `LOAD DC F/1C,"М2"`.
    bool has_addr;
    unsigned addr;
};

// Куда двигать текущий сектор: на столько-то записей (или секторов),
// к началу файла, к концевой записи.
enum SkipMode { SK_COUNT, SK_BEG, SK_END };

enum StmtKind {
    ST_PRINT,
    ST_SELECT,       // выбор устройств и режимов
    ST_OPEN,         // DATA LOAD DC OPEN — открыть существующий файл
    ST_DLOAD,        // DATA LOAD DC — прочитать одну запись
    ST_DSKIP,        // DSKIP и DBACKSPACE — сдвиг текущего сектора
    ST_LIMITS,       // LIMITS — адресные параметры файла в переменные
    ST_ONERR,        // ON ERROR — куда уходить при ошибке
    ST_SCRATCH,      // SCRATCH — вычеркнуть файлы из каталога
    ST_SCRATCH_DISK, // SCRATCH DISK — создать каталог на диске
    ST_DIM,
    ST_REDIM,        // MAT REDIM — размерности вычисляются на ходу
    ST_LINPUT,       // ввод строки целиком, без разбора на поля
    ST_CONVERT,      // CONVERT: символьное представление числа и обратно
    ST_BIN,          // BIN(  — число в один или два двоичных байта
    ST_INIT,         // INIT( — заполнить все байты приёмников одним значением
    ST_INPUT,
    ST_LET,
    ST_FOR,
    ST_NEXT,
    ST_IF,
    ST_GOTO,
    ST_GOSUB,
    ST_DEFFN,        // DEFFN' — помеченный вход в подпрограмму
    ST_GOSUBQ,       // GOSUB' — вызов помеченной подпрограммы
    ST_RETURN,
    ST_ON,
    ST_STOP,
    ST_END,
    ST_REM
};

struct Stmt {
    Stmt() : kind(ST_REM), var(0), line(0), label(0), bytes(1),
             mode(SK_COUNT), sectors(false), backwards(false),
             is_gosub(false), has_prompt(false), has_step(false),
             newline(true) {}

    StmtKind kind;

    std::vector<PrintItem> items;    // PRINT
    std::vector<SelectItem> selects; // SELECT
    std::vector<DimEntry> dims;      // DIM
    std::vector<Expr> targets;       // LET — цели слева; INPUT — приёмники
    Expr e;                          // LET — правая часть; IF — условие; FOR — начало
    Expr limit;                      // FOR — TO
    Expr step;                       // FOR — STEP
    unsigned var;                    // FOR, NEXT
    unsigned line;                   // GOTO, GOSUB, IF … THEN <строка>
    std::vector<unsigned> lines;     // ON … GOTO/GOSUB — список переходов

    // Помеченные подпрограммы: имя — целое 0…255, оно же двоичная метка
    // в оттранслированной форме.
    unsigned label;                  // DEFFN', GOSUB'
    std::vector<unsigned> params;    // DEFFN' — формальные параметры
    std::vector<Expr> args;          // GOSUB' — фактические параметры

    // BIN( — сколько байт занимает результат: 1 или 2. «Параметр 2 определяет
    // число байтов, содержащих преобразованное арифметическое выражение»
    // (руководство, разд. 14.2); других значений там нет.
    unsigned bytes;

    // Дисковые операторы
    DiskRef disk;                    // устройство и строка таблицы
    unsigned mode;                   // DSKIP/DBACKSPACE: SkipMode
    bool sectors;                    // DSKIP/DBACKSPACE: параметр S
    bool backwards;                  // DBACKSPACE, а не DSKIP

    bool is_gosub;                   // ON: переход с возвратом
    // INPUT — подсказка; CONVERT — образ; DEFFN' — текст клавиши спецфункции
    std::string prompt;
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