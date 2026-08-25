// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Mikhail Revzin <p3.141592653589793238462643@gmail.com>
// Part of the Iskra-226 project: https://github.com/Ptr314/Iskra-226
// Description: лексер текстовой формы — общий для разбора и для трансляции

#pragma once

#include <string>

#include "core/expr.h"
#include "core/names.h"

namespace iskra {

inline bool is_digit(char c) { return c >= '0' && c <= '9'; }
inline bool is_letter(char c) { return c >= 'A' && c <= 'Z'; }

// Ключевое слово текстовой формы. Слова ищутся раньше имён и от длинных к
// коротким: в исходном тексте пробелов может не быть вовсе —
// «FOR T=-6TO60STEPS1».
struct Keyword { const char * text; Tok::Type type; bool function; };

class TextLexer : public TokenSource
{
public:
    TextLexer(const std::string & s, unsigned pos, unsigned end, NameTable & names)
        : s_(s), p_(pos), end_(end), names_(names), want_array_(false) {}

    bool next(Tok & t, bool operand_expected);

    unsigned pos() const { return p_; }
    void set_pos(unsigned p) { p_ = p; }
    unsigned tell() const { return p_; }
    void seek(unsigned p) { p_ = p; }
    unsigned end() const { return end_; }

    void skip_spaces() { while (p_ < end_ && s_[p_] == ' ') ++p_; }
    bool at_end() { skip_spaces(); return p_ >= end_; }
    bool at_colon() { skip_spaces(); return p_ < end_ && s_[p_] == ':'; }

    // Ключевое слово в начале оператора; при совпадении съедается.
    bool take_word(const char * w);

    // Целое без знака вне выражения — имя помеченной подпрограммы.
    bool take_uint(unsigned & out);

    // Значение INIT( в виде кода: ровно две шестнадцатеричные цифры и
    // закрывающая скобка. Лексером их не прочитать — «2E» распалось бы на
    // число 2 и имя E, а «FF» стало бы именем переменной.
    bool take_hex_byte(unsigned & out);
    // Одна шестнадцатеричная цифра — код операции оператора BOOL.
    // Читать её лексером тоже нельзя: «9» стало бы числом, «E» именем.
    bool take_hex_digit(unsigned & out);

    // ФАУ устройства в SELECT: ровно две шестнадцатеричные цифры, без
    // скобки за ними. `SELECT PRINT0C`, `SELECT TAPE27`.
    bool take_hex2(unsigned & out);

    // Одна цифра — номер строки таблицы устройств в `SELECT #n`.
    bool take_digit(unsigned & out);

    // Одиночный знак, если он тут стоит.
    bool take_char(char c);

    // Образ CONVERT — не выражение, а набор знаков в скобках: (##.##),
    // (-#.#^^^^). Забираем его сырым текстом.
    bool take_image(std::string & out);

    const std::string & error() const { return error_; }
    const std::string & source_error() const { return error_; }
    bool fail(const std::string & m) { if (error_.empty()) error_ = m; return false; }

    const std::string & text() const { return s_; }
    NameTable & names() { return names_; }

    // `MAT Q=ZER` называет массив без скобок. Признак одноразовый: его
    // забирает ближайшее прочитанное имя.
    void expect_array() { want_array_ = true; }

private:
    bool match_keyword(unsigned & len, const Keyword *& kw) const;
    bool lex_hex(Tok & t);

    const std::string & s_;
    unsigned p_;
    unsigned end_;
    NameTable & names_;
    bool want_array_;
    std::string error_;
};

} // namespace iskra
