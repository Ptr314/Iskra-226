// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Mikhail Revzin <p3.141592653589793238462643@gmail.com>
// Part of the Iskra-226 project: https://github.com/Ptr314/Iskra-226
// Description: лексер текстовой формы — общий для разбора и для трансляции

#include "core/text_lexer.h"

#include <cstdio>
#include <cstring>

namespace iskra {



// Ключевые слова ищутся раньше имён и от длинных к коротким: в исходном
// тексте пробелов может не быть вовсе — «FOR T=-6TO60STEPS1».

const Keyword KEYWORDS[] = {
    // Функции: за именем следует открывающая скобка, она входит в лексему.
    // Тригонометрия: имена языка есть (разд. 4.7), а байты токенов не
    // установлены — свободны `F9`–`FC`, и какое имя за каким, корпус не
    // показывает (docs/format.md, разд. 5). Слова всё равно должны быть в
    // таблице: без них `COS(X)` разбирается как переменная `C` и хвост
    // `OS(X)`, то есть молча неправильно. Пусть лучше будет честный отказ.
    { "ARCTAN", Tok::UNKNOWN,  true  },
    { "ARCSIN", Tok::UNKNOWN,  true  },
    { "ARCCOS", Tok::UNKNOWN,  true  },
    { "SIN",    Tok::UNKNOWN,  true  },
    { "COS",    Tok::UNKNOWN,  true  },
    { "TAN",    Tok::UNKNOWN,  true  },
    { "ROUND",  Tok::FN_ROUND, true  },
    { "RND",    Tok::FN_RND,   true  },
    { "XOR",    Tok::XOR,      false },   // связки условий
    { "AND",    Tok::AND,      false },
    { "OR",     Tok::OR,       false },
    { "PRINT",  Tok::UNKNOWN,  false },   // разбирается на уровне оператора
    { "INPUT",  Tok::UNKNOWN,  false },
    { "STEP",   Tok::KW_STEP,  false },
    { "THEN",   Tok::KW_THEN,  false },
    { "NEXT",   Tok::UNKNOWN,  false },
    { "GOSUB",  Tok::KW_GOSUB, false },
    { "RETURN", Tok::UNKNOWN,  false },
    { "GOTO",   Tok::KW_GOTO,  false },
    { "STOP",   Tok::UNKNOWN,  false },
    { "HEX",    Tok::FN_HEX,   true  },
    { "STR",    Tok::FN_STR,   true  },
    { "LEN",    Tok::FN_LEN,   true  },
    { "NUM",    Tok::FN_NUM,   true  },
    { "VAL",    Tok::FN_VAL,   true  },
    { "POS",    Tok::FN_POS,   true  },
    { "SQR",    Tok::FN_SQR,   true  },
    { "ABS",    Tok::FN_ABS,   true  },
    { "INT",    Tok::FN_INT,   true  },
    { "SGN",    Tok::FN_SGN,   true  },
    { "LOG",    Tok::FN_LOG,   true  },
    { "EXP",    Tok::FN_EXP,   true  },
    { "REM",    Tok::UNKNOWN,  false },
    { "DIM",    Tok::UNKNOWN,  false },
    { "COM",    Tok::UNKNOWN,  false },
    { "CONVERT", Tok::UNKNOWN, false },
    { "LINPUT", Tok::UNKNOWN,  false },
    { "REDIM",  Tok::UNKNOWN,  false },
    { "MAT",    Tok::UNKNOWN,  false },
    { "TAB",    Tok::FN_TAB,   true  },
    { "END",    Tok::UNKNOWN,  false },
    { "FOR",    Tok::UNKNOWN,  false },
    { "LET",    Tok::UNKNOWN,  false },
    { "AT",     Tok::FN_AT,    true  },
    { "IF",     Tok::UNKNOWN,  false },
    { "TO",     Tok::KW_TO,    false }
};
const unsigned KEYWORD_COUNT = sizeof(KEYWORDS) / sizeof(KEYWORDS[0]);

// Лексер одного оператора. Останавливается на двоеточии и в конце строки,
// сообщая об этом лексемой END, — двоеточием разделяются операторы.

bool TextLexer::match_keyword(unsigned & len, const Keyword *& kw) const
{
    for (unsigned k = 0; k < KEYWORD_COUNT; ++k) {
        const unsigned n = static_cast<unsigned>(std::strlen(KEYWORDS[k].text));
        if (p_ + n > end_) continue;
        if (s_.compare(p_, n, KEYWORDS[k].text) != 0) continue;

        // За именем функции обязана стоять скобка. Без этой проверки
        // «999-ATHEN6470» читалось бы как функция AT: пробелов в тексте
        // может не быть вовсе, и «A» с «THEN» слипаются.
        if (KEYWORDS[k].function) {
            unsigned q = p_ + n;
            while (q < end_ && s_[q] == ' ') ++q;
            if (q >= end_ || s_[q] != '(') continue;
        }

        len = n;
        kw = &KEYWORDS[k];
        return true;
    }
    return false;
}

bool TextLexer::take_word(const char * w)
{
    skip_spaces();
    const unsigned n = static_cast<unsigned>(std::strlen(w));
    if (p_ + n > end_) return false;
    if (s_.compare(p_, n, w) != 0) return false;
    p_ += n;
    return true;
}

bool TextLexer::take_fn_name(unsigned & out)
{
    skip_spaces();
    if (p_ >= end_) return false;
    const char c = s_[p_];
    if (!is_letter(c) && !is_digit(c)) return false;
    ++p_;
    out = static_cast<unsigned char>(c);
    return true;
}

bool TextLexer::take_uint(unsigned & out)
{
    skip_spaces();
    unsigned v = 0;
    bool any = false;
    while (p_ < end_ && is_digit(s_[p_])) { v = v * 10 + (s_[p_++] - '0'); any = true; }
    if (!any) return false;
    out = v;
    return true;
}

bool TextLexer::take_hex_digit(unsigned & out)
{
    skip_spaces();
    if (p_ >= end_) return false;
    const char c = s_[p_];
    int d;
    if (c >= '0' && c <= '9') d = c - '0';
    else if (c >= 'A' && c <= 'F') d = c - 'A' + 10;
    else return false;
    // Дальше обязана стоять скобка: иначе это имя переменной.
    unsigned q = p_ + 1;
    while (q < end_ && s_[q] == ' ') ++q;
    if (q >= end_ || s_[q] != '(') return false;
    p_ = q;
    out = static_cast<unsigned>(d);
    return true;
}

bool TextLexer::take_hex_byte(unsigned & out)
{
    skip_spaces();
    const unsigned save = p_;
    unsigned v = 0;
    for (unsigned k = 0; k < 2; ++k) {
        if (p_ >= end_) { p_ = save; return false; }
        const char c = s_[p_];
        int d;
        if (c >= '0' && c <= '9') d = c - '0';
        else if (c >= 'A' && c <= 'F') d = c - 'A' + 10;
        else { p_ = save; return false; }
        v = v * 16 + static_cast<unsigned>(d);
        ++p_;
    }
    // Ровно две цифры и сразу скобка: иначе это выражение — INIT(M¤),
    // где «M¤» тоже начинается с шестнадцатеричной цифры.
    skip_spaces();
    if (p_ >= end_ || s_[p_] != ')') { p_ = save; return false; }
    out = v;
    return true;
}

bool TextLexer::take_hex2(unsigned & out, bool strict)
{
    skip_spaces();
    const unsigned save = p_;
    unsigned v = 0;
    for (unsigned k = 0; k < 2; ++k) {
        if (p_ >= end_) { p_ = save; return false; }
        const char c = s_[p_];
        int d;
        if (c >= '0' && c <= '9') d = c - '0';
        else if (c >= 'A' && c <= 'F') d = c - 'A' + 10;
        else { p_ = save; return false; }
        v = v * 16 + static_cast<unsigned>(d);
        ++p_;
    }
    // За двумя цифрами не должно стоять продолжение имени: `/A1¤` — это
    // символьная переменная `A1¤`, а не адрес `A1` (`ROM` 3041). Адреса
    // так и пишут — ровно две цифры и разделитель.
    if (strict && p_ < end_) {
        const char n = s_[p_];
        if (n == '\x24' || n == '%' || is_letter(n) || is_digit(n)) {
            p_ = save;
            return false;
        }
    }
    out = v;
    return true;
}

bool TextLexer::take_digit(unsigned & out)
{
    skip_spaces();
    if (p_ >= end_ || !is_digit(s_[p_])) return false;
    out = static_cast<unsigned>(s_[p_++] - '0');
    return true;
}

bool TextLexer::take_char(char c)
{
    skip_spaces();
    if (p_ >= end_ || s_[p_] != c) return false;
    ++p_;
    return true;
}

bool TextLexer::take_image(std::string & out)
{
    skip_spaces();
    if (p_ >= end_ || s_[p_] != '(') return false;
    ++p_;

    out.clear();
    while (p_ < end_ && s_[p_] != ')') {
        if (s_[p_] != ' ') out += s_[p_];
        ++p_;
    }
    if (p_ >= end_) return false;
    ++p_;
    return true;
}

// HEX(...) содержит не выражение, а шестнадцатеричные пары.
bool TextLexer::lex_hex(Tok & t)
{
    std::string bytes;
    for (;;) {
        skip_spaces();
        if (p_ >= end_) return fail("HEX( без закрывающей скобки");
        if (s_[p_] == ')') { ++p_; break; }

        int hi = -1, lo = -1;
        for (int k = 0; k < 2; ++k) {
            if (p_ >= end_) return fail("HEX( оборвался");
            const char c = s_[p_++];
            int v;
            if (c >= '0' && c <= '9') v = c - '0';
            else if (c >= 'A' && c <= 'F') v = c - 'A' + 10;
            else return fail("в HEX( не шестнадцатеричная цифра");
            if (k == 0) hi = v; else lo = v;
        }
        bytes += static_cast<char>((hi << 4) | lo);
    }
    t.t = Tok::FN_HEX;
    t.s = bytes;
    return true;
}

bool TextLexer::next(Tok & t, bool /*operand_expected*/)
{
    t = Tok();
    skip_spaces();
    if (p_ >= end_ || s_[p_] == ':') { t.t = Tok::END; return true; }

    // #PI — единственная встречающаяся системная константа.
    if (s_[p_] == '#') {
        if (p_ + 3 <= end_ && s_.compare(p_, 3, "#PI") == 0) {
            p_ += 3;
            t.t = Tok::PI;
            return true;
        }
        // Иначе это префикс номера строки таблицы устройств: `PRINT #5`
        // = `4C 03 DB E8 05` (EDITOR 6871).
        ++p_;
        t.t = Tok::HASH;
        return true;
    }

    // FN<имя>( — обращение к функции пользователя. В таблицу ключевых слов
    // это не укладывается: там за именем функции обязана стоять скобка, а
    // здесь между ними имя. Проверяем всю связку сразу — иначе «FNA(8)»
    // разобралось бы как переменные F, N и A подряд.
    if (p_ + 2 <= end_ && s_.compare(p_, 2, "FN") == 0) {
        unsigned q = p_ + 2;
        while (q < end_ && s_[q] == ' ') ++q;
        if (q < end_ && (is_letter(s_[q]) || is_digit(s_[q]))) {
            const char nm = s_[q];
            unsigned r = q + 1;
            while (r < end_ && s_[r] == ' ') ++r;
            if (r < end_ && s_[r] == '(') {
                p_ = r + 1;
                t.t = Tok::FN_USER;
                t.var = static_cast<unsigned char>(nm);
                return true;
            }
        }
    }

    unsigned klen = 0;
    const Keyword * kw = 0;
    if (is_letter(s_[p_]) && match_keyword(klen, kw)) {
        p_ += klen;
        if (kw->function) {
            skip_spaces();
            if (p_ >= end_ || s_[p_] != '(')
                return fail(std::string(kw->text) + " без открывающей скобки");
            ++p_;
            if (kw->type == Tok::FN_HEX) return lex_hex(t);
            if (kw->type == Tok::UNKNOWN) { t.t = Tok::UNKNOWN; t.s = kw->text; return true; }
            t.t = kw->type;
            return true;
        }
        if (kw->type == Tok::UNKNOWN) { t.t = Tok::UNKNOWN; t.s = kw->text; return true; }
        t.t = kw->type;
        if (kw->type == Tok::KW_THEN) {
            // За THEN следует номер строки.
            skip_spaces();
            unsigned v = 0;
            bool any = false;
            while (p_ < end_ && is_digit(s_[p_])) { v = v * 10 + (s_[p_++] - '0'); any = true; }
            if (!any) return fail("THEN без номера строки");
            t.num = Number::from_int(static_cast<long>(v));
        }
        return true;
    }

    // Имя переменной: буква, необязательная цифра, необязательный признак типа.
    if (is_letter(s_[p_])) {
        std::string name;
        name += s_[p_++];
        if (p_ < end_ && is_digit(s_[p_])) name += s_[p_++];
        if (p_ < end_ && (s_[p_] == '\x24' || s_[p_] == '%')) name += s_[p_++];
        t.t = Tok::VAR;
        t.s = name;

        // В тексте индексация видна по скобке; скобку съедаем, чтобы разбор
        // списка индексов шёл так же, как в токенах, где её нет вовсе.
        bool array = false;
        const unsigned save = p_;
        skip_spaces();
        if (p_ < end_ && s_[p_] == '(') {
            ++p_;
            // Пустые скобки — ссылка на массив целиком: A¤().
            const unsigned after = p_;
            skip_spaces();
            if (p_ < end_ && s_[p_] == ')') { ++p_; t.t = Tok::ARRAY; }
            else { p_ = after; t.indexed = true; t.table_array = true; }
            array = true;
        } else {
            p_ = save;
        }

        // Скаляр и массив с одним именем — разные переменные с разными
        // индексами (VICT 36 и 80, docs/format.md, разд. 6).
        if (want_array_) { array = true; want_array_ = false; }
        t.var = names_.index(array ? name + "(" : name);
        if (array) {
            // Обращение с индексом объявляет массив неявно; размерность по
            // умолчанию — десять элементов.
            VarInfo & v = names_.vars()[t.var];
            if (!v.is_array) { v.is_array = true; if (!v.dim1) v.dim1 = 10; }
        }
        return true;
    }

    if (is_digit(s_[p_]) || (s_[p_] == '.' && p_ + 1 < end_ && is_digit(s_[p_ + 1]))) {
        std::string num;
        while (p_ < end_ && is_digit(s_[p_])) num += s_[p_++];
        if (p_ < end_ && s_[p_] == '.') {
            num += s_[p_++];
            while (p_ < end_ && is_digit(s_[p_])) num += s_[p_++];
        }
        if (p_ < end_ && s_[p_] == 'E' &&
            p_ + 1 < end_ && (is_digit(s_[p_ + 1]) || s_[p_ + 1] == '-' || s_[p_ + 1] == '+')) {
            num += s_[p_++];
            if (s_[p_] == '-' || s_[p_] == '+') num += s_[p_++];
            while (p_ < end_ && is_digit(s_[p_])) num += s_[p_++];
        }
        if (!Number::parse(num, t.num)) return fail("не разобралось число " + num);
        t.t = Tok::NUM;
        // Запись с порядком кодируется своим токеном, и по значению её уже
        // не отличить: 1E6 и 1000000 — одно число, но разные байты
        // (STAT08 480). Поэтому исходный текст константы едет с лексемой.
        t.s = num;
        return true;
    }

    if (s_[p_] == '"' || s_[p_] == '\'') {
        const char q = s_[p_++];
        std::string lit;
        while (p_ < end_ && s_[p_] != q) lit += s_[p_++];
        if (p_ >= end_) return fail("незакрытый литерал");
        ++p_;
        t.t = Tok::STR;
        t.s = lit;
        return true;
    }

    const char c = s_[p_++];
    switch (c) {
        case '(': t.t = Tok::LPAR; return true;
        case ')': t.t = Tok::RPAR; return true;
        case ',': t.t = Tok::COMMA; return true;
        case ';': t.t = Tok::SEMI; return true;
        case '+': t.t = Tok::PLUS; return true;
        case '-': t.t = Tok::MINUS; return true;
        case '*': t.t = Tok::STAR; return true;
        case '/': t.t = Tok::SLASH; return true;
        case '^': t.t = Tok::CARET; return true;
        case '=': t.t = Tok::EQ; return true;
        case '>':
            if (p_ < end_ && s_[p_] == '=') { ++p_; t.t = Tok::GE; } else t.t = Tok::GT;
            return true;
        case '<':
            if (p_ < end_ && s_[p_] == '=') { ++p_; t.t = Tok::LE; }
            else if (p_ < end_ && s_[p_] == '>') { ++p_; t.t = Tok::NE; }
            else t.t = Tok::LT;
            return true;
        default: break;
    }

    char b[32];
    std::sprintf(b, "символ %02X", static_cast<unsigned>(static_cast<unsigned char>(c)));
    t.t = Tok::UNKNOWN;
    t.s = b;
    return true;
}

} // namespace iskra
