// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Mikhail Revzin <p3.141592653589793238462643@gmail.com>
// Part of the Iskra-226 project: https://github.com/Ptr314/Iskra-226
// Description: текстовая форма программы → промежуточное представление

#include "core/front_text.h"

#include <cstdio>
#include <cstring>

#include "core/devtable.h"
#include "core/expr.h"

namespace iskra {

unsigned NameTable::index(const std::string & name)
{
    for (unsigned i = 0; i < names_.size(); ++i)
        if (names_[i] == name) return i;

    names_.push_back(name);

    VarInfo v;
    v.known = true;
    const char last = name[name.size() - 1];
    v.is_string = (last == '$');
    v.is_integer = (last == '%');
    if (v.is_string) v.str_len = 16;      // длина по умолчанию
    vars_.push_back(v);

    return static_cast<unsigned>(names_.size() - 1);
}

namespace {

bool is_digit(char c) { return c >= '0' && c <= '9'; }
bool is_letter(char c) { return c >= 'A' && c <= 'Z'; }

// Ключевые слова ищутся раньше имён и от длинных к коротким: в исходном
// тексте пробелов может не быть вовсе — «FOR T=-6TO60STEPS1».
struct Keyword { const char * text; Tok::Type type; bool function; };

const Keyword KEYWORDS[] = {
    // Функции: за именем следует открывающая скобка, она входит в лексему.
    { "ARCTAN", Tok::UNKNOWN,  true  },
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
class TextLexer : public TokenSource
{
public:
    TextLexer(const std::string & s, unsigned pos, unsigned end, NameTable & names)
        : s_(s), p_(pos), end_(end), names_(names) {}

    bool next(Tok & t, bool operand_expected);

    unsigned pos() const { return p_; }
    void set_pos(unsigned p) { p_ = p; }
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
    bool fail(const std::string & m) { if (error_.empty()) error_ = m; return false; }

    const std::string & text() const { return s_; }

private:
    bool match_keyword(unsigned & len, const Keyword *& kw) const;
    bool lex_hex(Tok & t);

    const std::string & s_;
    unsigned p_;
    unsigned end_;
    NameTable & names_;
    std::string error_;
};

bool TextLexer::match_keyword(unsigned & len, const Keyword *& kw) const
{
    for (unsigned k = 0; k < KEYWORD_COUNT; ++k) {
        const unsigned n = static_cast<unsigned>(std::strlen(KEYWORDS[k].text));
        if (p_ + n > end_) continue;
        if (s_.compare(p_, n, KEYWORDS[k].text) != 0) continue;
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

bool TextLexer::take_hex2(unsigned & out)
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
        ++p_;
        t.t = Tok::UNKNOWN;
        t.s = "системная переменная #";
        return true;
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
        t.var = names_.index(name);
        t.s = name;

        // В тексте индексация видна по скобке; скобку съедаем, чтобы разбор
        // списка индексов шёл так же, как в токенах, где её нет вовсе.
        const unsigned save = p_;
        skip_spaces();
        if (p_ < end_ && s_[p_] == '(') {
            ++p_;
            // Пустые скобки — ссылка на массив целиком: A¤().
            const unsigned after = p_;
            skip_spaces();
            if (p_ < end_ && s_[p_] == ')') { ++p_; t.t = Tok::ARRAY; }
            else { p_ = after; t.indexed = true; t.table_array = true; }

            // Обращение с индексом объявляет массив неявно; размерность по
            // умолчанию — десять элементов.
            VarInfo & v = names_.vars()[t.var];
            if (!v.is_array) { v.is_array = true; if (!v.dim1) v.dim1 = 10; }
        } else {
            p_ = save;
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

// ---------------------------------------------------------------------------

class LineParser
{
public:
    LineParser(const std::string & s, unsigned pos, unsigned end, NameTable & names)
        : lex_(s, pos, end, names), names_(names) {}

    bool parse_line(Line & line, std::string & error);

private:
    bool parse_stmt(Stmt & s);
    bool print_items(Stmt & s, ExprParser & ex);
    bool var_list(std::vector<Expr> & out, ExprParser & ex);
    // Приставка дисковых операторов: буква устройства, `/адрес`, `#строка`.
    bool disk_prefix(Stmt & s, bool with_device);
    bool dim_list(Stmt & s, ExprParser & ex);
    bool err(const std::string & m) { if (error_.empty()) error_ = m; return false; }

    TextLexer lex_;
    NameTable & names_;
    std::string error_;
};

bool LineParser::disk_prefix(Stmt & s, bool with_device)
{
    if (with_device) {
        // Буква устройства — одна: имя переменной из двух букв не бывает,
        // так что `TA¤` читается как T и A¤.
        if (lex_.take_word("F"))      { s.disk.has_device = true; s.disk.device = 0; }
        else if (lex_.take_word("R")) { s.disk.has_device = true; s.disk.device = 1; }
        else if (lex_.take_word("T")) { s.disk.has_device = true; s.disk.device = 2; }
    }
    if (lex_.take_char('/')) {
        unsigned a = 0;
        if (!lex_.take_hex2(a)) return err("нет адреса устройства после «/»");
        s.disk.has_addr = true;
        s.disk.addr = a;
        lex_.take_char(',');
    }
    if (lex_.take_char('#')) {
        ExprParser ex(lex_);
        if (!ex.parse(s.disk.row)) return err(ex.error());
        s.disk.has_row = true;
        lex_.take_char(',');
    }
    return true;
}

bool LineParser::print_items(Stmt & s, ExprParser & ex)
{
    s.kind = ST_PRINT;
    s.newline = true;

    if (lex_.at_end() || lex_.at_colon()) return true;

    Tok t;
    for (;;) {
        PrintItem item;
        if (!ex.parse(item.e)) return err(ex.error());

        if (!ex.peek(t, false)) return err(ex.error());
        if (t.t == Tok::SEMI) { item.sep = SEP_TIGHT; ex.consume(); }
        else if (t.t == Tok::COMMA) { item.sep = SEP_ZONE; ex.consume(); }
        else if (t.t == Tok::END) { item.sep = SEP_NONE; }
        else return err("непонятный разделитель в PRINT");

        s.items.push_back(item);
        if (item.sep == SEP_NONE) break;

        if (lex_.at_end() || lex_.at_colon()) { s.newline = false; break; }
    }
    return true;
}

bool LineParser::var_list(std::vector<Expr> & out, ExprParser & ex)
{
    for (;;) {
        Expr target;
        if (!ex.parse_lvalue(target)) return err(ex.error());
        out.push_back(target);

        Tok t;
        if (!ex.peek(t, false)) return err(ex.error());
        if (t.t != Tok::COMMA) break;
        ex.consume();
    }
    return true;
}

// DIM R(6),Q(6),K(12) — в тексте размеры записаны явно. Строковая длина
// пишется без скобок: DIM A¤2.
bool LineParser::dim_list(Stmt & s, ExprParser & ex)
{
    s.kind = ST_DIM;
    for (;;) {
        Tok t;
        if (!ex.take(t, true)) return err(ex.error());
        if (t.t != Tok::VAR) return err("DIM: ожидалась переменная");

        DimEntry d;
        d.var = t.var;
        const bool is_string = !t.s.empty() && t.s[t.s.size() - 1] == '$';

        if (t.indexed) {
            // Скобку лексер уже съел; дальше размерности через запятую.
            for (;;) {
                Tok n;
                if (!ex.take(n, true)) return err(ex.error());
                if (n.t != Tok::NUM) return err("DIM: размерность не число");
                long v = 0;
                n.num.to_int(v);
                if (d.dim1 == 0) d.dim1 = static_cast<unsigned>(v);
                else d.dim2 = static_cast<unsigned>(v);

                if (!ex.take(n, false)) return err(ex.error());
                if (n.t == Tok::RPAR) break;
                if (n.t != Tok::COMMA) return err("DIM: список размерностей не закрыт");
            }
        }

        // За размерностями символьного массива может стоять длина строки.
        if (is_string) {
            Tok n;
            if (!ex.peek(n, true)) return err(ex.error());
            if (n.t == Tok::NUM) {
                ex.consume();
                long v = 0;
                n.num.to_int(v);
                d.str_len = static_cast<unsigned>(v);
            }
        }
        // Разобранное объявление сразу попадает в таблицу переменных:
        // дальше по программе разбор опирается на неё так же, как
        // токенизированный опирается на таблицы файла.
        if (d.var < names_.vars().size()) {
            VarInfo & v = names_.vars()[d.var];
            if (d.dim1) { v.is_array = true; v.dim1 = d.dim1; v.dim2 = d.dim2; }
            if (d.str_len) v.str_len = d.str_len;
        }
        s.dims.push_back(d);

        Tok sep;
        if (!ex.peek(sep, false)) return err(ex.error());
        if (sep.t != Tok::COMMA) break;
        ex.consume();
    }
    return true;
}

bool LineParser::parse_stmt(Stmt & s)
{
    lex_.skip_spaces();

    if (lex_.take_word("REM") || lex_.take_word("%")) {
        // % — краткая запись REM; остаток строки это текст.
        s.kind = ST_REM;
        lex_.set_pos(lex_.end());
        return true;
    }
    if (lex_.take_word("DIM") || lex_.take_word("COM")) {
        // COM объявляет то же самое, только в общей области: для исполнения
        // разница несущественна.
        ExprParser ex(lex_);
        return dim_list(s, ex);
    }
    if (lex_.take_word("PRINT")) {
        ExprParser ex(lex_);
        return print_items(s, ex);
    }
    if (lex_.take_word("CONVERT")) {
        s.kind = ST_CONVERT;
        ExprParser ex(lex_);
        if (!ex.parse(s.e)) return err(ex.error());
        Tok t;
        if (!ex.take(t, false) || t.t != Tok::KW_TO) return err("CONVERT без TO");
        Expr target;
        if (!ex.parse_lvalue(target)) return err(ex.error());
        s.targets.push_back(target);

        if (!ex.peek(t, false)) return err(ex.error());
        if (t.t == Tok::COMMA) {
            ex.consume();
            // Образ записан в скобках: CONVERT A TO A¤,(###)
            if (!lex_.take_image(s.prompt)) return err("CONVERT: непонятный образ");
            s.has_prompt = true;
        }
        return true;
    }
    if (lex_.take_word("MAT")) {
        if (!lex_.take_word("REDIM")) return err("после MAT ожидался REDIM");
        s.kind = ST_REDIM;
        ExprParser ex(lex_);
        for (;;) {
            Tok t;
            if (!ex.take(t, true)) return err(ex.error());
            if (t.t != Tok::VAR && t.t != Tok::ARRAY)
                return err("MAT REDIM: ожидался массив");

            DimEntry d;
            d.var = t.var;
            d.computed = true;
            const bool is_string = !t.s.empty() && t.s[t.s.size() - 1] == '$';

            // Лексер уже съел открывающую скобку, если она была.
            if (!t.indexed) return err("MAT REDIM: нет размерностей");
            for (;;) {
                Expr sz;
                if (!ex.parse(sz)) return err(ex.error());
                d.sizes.push_back(sz);
                if (!ex.peek(t, false)) return err(ex.error());
                if (t.t == Tok::COMMA) { ex.consume(); continue; }
                if (t.t == Tok::RPAR) { ex.consume(); break; }
                return err("MAT REDIM: список размерностей не закрыт");
            }

            if (is_string) {
                if (!ex.peek(t, true)) return err(ex.error());
                if (t.t == Tok::NUM) {
                    long v = 0;
                    t.num.to_int(v);
                    d.str_len = static_cast<unsigned>(v);
                    ex.consume();
                }
            }
            s.dims.push_back(d);

            if (!ex.peek(t, false)) return err(ex.error());
            if (t.t != Tok::COMMA) break;
            ex.consume();
        }
        return true;
    }
    if (lex_.take_word("LINPUT")) {
        s.kind = ST_LINPUT;
        ExprParser ex(lex_);
        Tok t;
        if (!ex.peek(t, true)) return err(ex.error());
        if (t.t == Tok::STR) {
            s.prompt = t.s;
            s.has_prompt = true;
            ex.consume();
            if (!ex.peek(t, false)) return err(ex.error());
            if (t.t == Tok::COMMA) ex.consume();
        }
        if (!ex.peek(t, true)) return err(ex.error());
        if (t.t == Tok::MINUS) ex.consume();

        Expr target;
        if (!ex.parse_lvalue(target)) return err(ex.error());
        s.targets.push_back(target);
        return true;
    }
    if (lex_.take_word("INPUT")) {
        s.kind = ST_INPUT;
        ExprParser ex(lex_);
        Tok t;
        if (!ex.peek(t, true)) return err(ex.error());
        if (t.t == Tok::STR) {
            s.prompt = t.s;
            s.has_prompt = true;
            ex.consume();
            if (!ex.peek(t, false)) return err(ex.error());
            if (t.t == Tok::COMMA) ex.consume();
        }
        return var_list(s.targets, ex);
    }
    // Раньше вычисляемого ON: «ON ERROR» — совсем другой оператор.
    if (lex_.take_word("ON ERROR")) {
        s.kind = ST_ONERR;
        if (lex_.at_end() || lex_.at_colon()) { s.mode = EM_OFF; return true; }

        // Весь оператор читается одним разборщиком: заглядывание сдвигает
        // лексер, и мешать его с чтением слов напрямую нельзя.
        ExprParser ex(lex_);
        Tok t;
        if (!ex.peek(t, true)) return err(ex.error());
        if (t.t == Tok::VAR) {
            Expr a, b;
            if (!ex.parse_lvalue(a)) return err(ex.error());
            if (!ex.take(t, false) || t.t != Tok::COMMA)
                return err("ON ERROR: между приёмниками нужна запятая");
            if (!ex.parse_lvalue(b)) return err(ex.error());
            s.targets.push_back(a);
            s.targets.push_back(b);
        }
        if (!ex.take(t, false)) return err(ex.error());
        long v = 0;
        if (t.t == Tok::KW_THEN) {
            // Номер строки лексер забирает внутрь самой лексемы THEN — так
            // же, как в `IF … THEN <строка>`.
            s.mode = EM_THEN;
            if (!t.num.to_int(v)) return err("ON ERROR: неверный номер строки");
        } else if (t.t == Tok::KW_GOTO || t.t == Tok::KW_GOSUB) {
            s.mode = (t.t == Tok::KW_GOTO) ? EM_GOTO : EM_GOSUB;
            if (!ex.take(t, true) || t.t != Tok::NUM)
                return err("ON ERROR: нет номера строки");
            if (!t.num.to_int(v)) return err("ON ERROR: неверный номер строки");
        } else {
            return err("ON ERROR без GOTO, THEN или GOSUB");
        }
        if (v < 0) return err("ON ERROR: неверный номер строки");
        s.line = static_cast<unsigned>(v);
        return true;
    }

    if (lex_.take_word("ON")) {
        s.kind = ST_ON;
        ExprParser ex(lex_);
        if (!ex.parse(s.e)) return err(ex.error());
        Tok t;
        if (!ex.take(t, false)) return err(ex.error());
        if (t.t == Tok::KW_GOSUB) s.is_gosub = true;
        else if (t.t != Tok::KW_GOTO) return err("ON без GOTO или GOSUB");
        for (;;) {
            if (!ex.take(t, true) || t.t != Tok::NUM) return err("ON: ожидался номер строки");
            long v = 0;
            t.num.to_int(v);
            s.lines.push_back(static_cast<unsigned>(v));
            if (!ex.peek(t, false)) return err(ex.error());
            if (t.t != Tok::COMMA) break;
            ex.consume();
        }
        return true;
    }
    if (lex_.take_word("RETURN")) { s.kind = ST_RETURN; return true; }
    if (lex_.take_word("INIT")) {
        // INIT(<значение>)<список приёмников> (руководство, разд. 13.3).
        if (!lex_.take_word("(")) return err("INIT без открывающей скобки");
        s.kind = ST_INIT;

        unsigned code = 0;
        if (lex_.take_hex_byte(code)) {
            s.e = Expr();
            s.e.kind = EX_NUM;
            s.e.num = Number::from_int(static_cast<long>(code));
            if (!lex_.take_word(")")) return err("INIT( без закрывающей скобки");
        } else {
            ExprParser ex(lex_);
            if (!ex.parse(s.e)) return err(ex.error());
            Tok t;
            if (!ex.take(t, false) || t.t != Tok::RPAR)
                return err("INIT( без закрывающей скобки");
        }

        ExprParser ex(lex_);
        for (;;) {
            Expr target;
            if (!ex.parse_lvalue(target, true)) return err(ex.error());
            s.targets.push_back(target);
            Tok t;
            if (!ex.peek(t, false)) return err(ex.error());
            if (t.t != Tok::COMMA) break;
            ex.consume();
        }
        return true;
    }
    if (lex_.take_word("BIN")) {
        // BIN(<символьная переменная>[,2])=<а.в.> (руководство, разд. 14.2).
        if (!lex_.take_word("(")) return err("BIN без открывающей скобки");
        s.kind = ST_BIN;
        s.bytes = 1;

        ExprParser ex(lex_);
        Expr target;
        if (!ex.parse_lvalue(target, true)) return err(ex.error());
        s.targets.push_back(target);

        Tok t;
        if (!ex.take(t, false)) return err(ex.error());
        if (t.t == Tok::COMMA) {
            long n = 0;
            if (!ex.take(t, true) || t.t != Tok::NUM || !t.num.to_int(n) || n != 2)
                return err("второй аргумент BIN( бывает только 2");
            s.bytes = 2;
            if (!ex.take(t, false)) return err(ex.error());
        }
        if (t.t != Tok::RPAR) return err("BIN( без закрывающей скобки");
        if (!ex.take(t, false) || t.t != Tok::EQ) return err("BIN( без знака равенства");
        if (!ex.parse(s.e)) return err(ex.error());
        return true;
    }
    if (lex_.take_word("DEFFN")) {
        // Апостроф отличает помеченный вход в подпрограмму от DEFFN —
        // определения функции пользователя, которого пока нет.
        if (!lex_.take_word("'")) return err("DEFFN без апострофа ещё не поддержан");
        s.kind = ST_DEFFN;
        if (!lex_.take_uint(s.label)) return err("DEFFN' без имени подпрограммы");
        if (s.label > 255) return err("имя помеченной подпрограммы больше 255");

        ExprParser ex(lex_);
        Tok t;
        if (!ex.peek(t, true)) return err(ex.error());
        // Определение клавиши спецфункции: текст задаётся литералом либо
        // шестнадцатеричной записью — DEFFN '31 HEX(0D) в SCOPE.
        if (t.t == Tok::STR || t.t == Tok::FN_HEX) {
            s.has_prompt = true;
            s.prompt = t.s;
            ex.consume();
            return true;
        }
        if (t.t != Tok::LPAR) return true;  // вход без параметров
        ex.consume();
        for (;;) {
            if (!ex.take(t, true) || t.t != Tok::VAR)
                return err("DEFFN': формальный параметр — это переменная");
            s.params.push_back(t.var);
            if (!ex.take(t, false)) return err(ex.error());
            if (t.t == Tok::RPAR) break;
            if (t.t != Tok::COMMA) return err("DEFFN': непонятный список параметров");
        }
        return true;
    }
    {
        const bool gosub = lex_.take_word("GOSUB");
        if (gosub && lex_.take_word("'")) {
            s.kind = ST_GOSUBQ;
            if (!lex_.take_uint(s.label)) return err("GOSUB' без имени подпрограммы");
            if (s.label > 255) return err("имя помеченной подпрограммы больше 255");

            ExprParser ex(lex_);
            Tok t;
            if (!ex.peek(t, true)) return err(ex.error());
            if (t.t != Tok::LPAR) return true;      // вызов без параметров
            ex.consume();
            for (;;) {
                Expr a;
                if (!ex.parse(a)) return err(ex.error());
                s.args.push_back(a);
                if (!ex.take(t, false)) return err(ex.error());
                if (t.t == Tok::RPAR) break;
                if (t.t != Tok::COMMA) return err("GOSUB': непонятный список параметров");
            }
            return true;
        }
        if (gosub || lex_.take_word("GOTO")) {
            s.kind = gosub ? ST_GOSUB : ST_GOTO;
            lex_.skip_spaces();
            ExprParser ex(lex_);
            Tok t;
            if (!ex.take(t, true) || t.t != Tok::NUM)
                return err("переход без номера строки");
            long v = 0;
            t.num.to_int(v);
            s.line = static_cast<unsigned>(v);
            return true;
        }
    }
    if (lex_.take_word("IF")) {
        s.kind = ST_IF;
        ExprParser ex(lex_);
        if (!ex.parse(s.e)) return err(ex.error());
        Tok t;
        if (!ex.take(t, false) || t.t != Tok::KW_THEN) return err("IF без THEN");
        long v = 0;
        t.num.to_int(v);
        s.line = static_cast<unsigned>(v);
        return true;
    }
    if (lex_.take_word("FOR")) {
        s.kind = ST_FOR;
        ExprParser ex(lex_);
        Tok t;
        if (!ex.take(t, true) || t.t != Tok::VAR) return err("FOR без переменной");
        s.var = t.var;
        if (!ex.take(t, false) || t.t != Tok::EQ) return err("FOR без =");
        if (!ex.parse(s.e)) return err(ex.error());
        if (!ex.take(t, false) || t.t != Tok::KW_TO) return err("FOR без TO");
        if (!ex.parse(s.limit)) return err(ex.error());
        if (!ex.peek(t, false)) return err(ex.error());
        if (t.t == Tok::KW_STEP) {
            ex.consume();
            if (!ex.parse(s.step)) return err(ex.error());
            s.has_step = true;
        }
        return true;
    }
    if (lex_.take_word("NEXT")) {
        s.kind = ST_NEXT;
        ExprParser ex(lex_);
        Tok t;
        if (!ex.take(t, true) || t.t != Tok::VAR) return err("NEXT без переменной");
        s.var = t.var;
        return true;
    }
    // Дисковые операторы. Длинные слова раньше коротких: «DATA LOAD DC OPEN»
    // перед «DATA LOAD DC», «SCRATCH DISK» перед «SCRATCH».
    if (lex_.take_word("SCRATCH DISK")) {
        s.kind = ST_SCRATCH_DISK;
        if (!disk_prefix(s, true)) return false;
        if (lex_.take_word("LS")) {
            if (!lex_.take_char('=')) return err("SCRATCH DISK: LS без =");
            ExprParser ex(lex_);
            if (!ex.parse(s.e)) return err(ex.error());
            s.has_prompt = true;
            lex_.take_char(',');
        }
        if (!lex_.take_word("END")) return err("SCRATCH DISK без END");
        if (!lex_.take_char('=')) return err("SCRATCH DISK: END без =");
        ExprParser ex(lex_);
        if (!ex.parse(s.limit)) return err(ex.error());
        return true;
    }
    if (lex_.take_word("SCRATCH")) {
        s.kind = ST_SCRATCH;
        if (!disk_prefix(s, true)) return false;
        // Один разборщик на весь список: после выражения он уже заглянул
        // вперёд, и читать запятую прямо из лексера нельзя.
        ExprParser ex(lex_);
        for (;;) {
            Expr name;
            if (!ex.parse(name)) return err(ex.error());
            s.targets.push_back(name);
            Tok t;
            if (!ex.peek(t, false)) return err(ex.error());
            if (t.t != Tok::COMMA) break;
            ex.consume();
        }
        if (s.targets.empty()) return err("SCRATCH без имени файла");
        return true;
    }

    if (lex_.take_word("DATA LOAD DC OPEN")) {
        s.kind = ST_OPEN;
        if (!disk_prefix(s, true)) return false;
        ExprParser ex(lex_);
        if (!ex.parse(s.e)) return err(ex.error());
        return true;
    }
    if (lex_.take_word("DATA LOAD DC")) {
        s.kind = ST_DLOAD;
        if (!disk_prefix(s, false)) return false;
        ExprParser ex(lex_);
        if (!var_list(s.targets, ex)) return false;
        if (s.targets.empty()) return err("DATA LOAD DC без приёмников");
        return true;
    }
    if (lex_.take_word("DSKIP") || lex_.take_word("DBACKSPACE")) {
        // Какое из двух слов съедено, видно по последнему знаку.
        s.kind = ST_DSKIP;
        s.backwards = (lex_.text()[lex_.pos() - 1] == 'E');
        if (!disk_prefix(s, false)) return false;
        if (lex_.take_word("BEG")) s.mode = SK_BEG;
        else if (lex_.take_word("END")) s.mode = SK_END;
        else {
            s.mode = SK_COUNT;
            ExprParser ex(lex_);
            if (!ex.parse(s.e)) return err(ex.error());
            if (lex_.take_word("S")) s.sectors = true;
        }
        return true;
    }
    if (lex_.take_word("LIMITS")) {
        s.kind = ST_LIMITS;
        if (!disk_prefix(s, true)) return false;
        // Форма 1 начинается с имени файла, форма 2 — сразу с приёмников
        // (руководство, разд. 18.8.3).
        {
            const unsigned save = lex_.pos();
            ExprParser ex(lex_);
            Expr first;
            if (!ex.parse(first)) return err(ex.error());
            if (first.kind == EX_STR ||
                (first.var < names_.vars().size() &&
                 names_.vars()[first.var].is_string &&
                 (first.kind == EX_VAR || first.kind == EX_ELEM ||
                  first.kind == EX_ARRAY))) {
                s.e = first;
                s.has_prompt = true;
                lex_.take_char(',');
            } else {
                lex_.set_pos(save);
            }
        }
        ExprParser ex(lex_);
        if (!var_list(s.targets, ex)) return false;
        if (s.targets.size() < 3 || s.targets.size() > 4)
            return err("LIMITS: приёмников должно быть три или четыре");
        return true;
    }

    if (lex_.take_word("SELECT")) {
        s.kind = ST_SELECT;
        // Порядок проверок важен: длинные слова раньше коротких, иначе
        // DISK разберётся как D, а PRINT и PLOT — как P.
        for (;;) {
            SelectItem it;
            unsigned v = 0;
            if (lex_.take_char('#')) {
                it.code = SC_ROW;
                if (!lex_.take_digit(it.row)) return err("SELECT #: нет номера строки");
                if (it.row > 7) return err("SELECT #: номер строки 0…7");
                if (!lex_.take_hex2(it.addr)) return err("SELECT #: нет адреса устройства");
                it.has_addr = true;
            } else if (lex_.take_word("DISK")) {
                it.code = SC_DISK;
                if (!lex_.take_hex2(it.addr)) return err("SELECT DISK: нет адреса");
                it.has_addr = true;
            } else {
                static const struct { const char * word; unsigned code; } GROUPS[] = {
                    { "PRINT", SC_PRINT }, { "PLOT", SC_PLOT }, { "LIST", SC_LIST },
                    { "TAPE",  SC_TAPE  }, { "CO",   SC_CO   }
                };
                bool named = false;
                for (unsigned k = 0; k < sizeof(GROUPS) / sizeof(GROUPS[0]); ++k)
                    if (lex_.take_word(GROUPS[k].word)) {
                        it.code = GROUPS[k].code;
                        if (!lex_.take_hex2(it.addr))
                            return err("SELECT: нет адреса устройства");
                        it.has_addr = true;
                        named = true;
                        break;
                    }
                if (!named) {
                    if (lex_.take_word("P")) {
                        it.code = SC_PAUSE;
                        if (lex_.take_digit(v)) { it.addr = v; it.has_addr = true; }
                    } else if (lex_.take_word("D") || lex_.take_word("R")
                               || lex_.take_word("G")) {
                        // Единицы измерения углов. Какая именно — по букве,
                        // которую только что съели.
                        it.code = SC_TRIG;
                        const char letter = lex_.text()[lex_.pos() - 1];
                        it.addr = (letter == 'D') ? ANG_DEG
                                : (letter == 'G') ? ANG_GRAD : ANG_RAD;
                        it.has_addr = true;
                    } else {
                        return err("SELECT: неизвестная группа устройств");
                    }
                }
            }
            // Дисковод F или R — только у дисковых форм.
            if (it.code == SC_ROW || it.code == SC_DISK) {
                if (lex_.take_word("F")) { it.removable = false; it.has_drive = true; }
                else if (lex_.take_word("R")) { it.removable = true; it.has_drive = true; }
            }
            // Ширина строки в скобках.
            if (it.code != SC_ROW && it.code != SC_DISK && it.code != SC_TRIG
                && lex_.take_char('(')) {
                if (!lex_.take_uint(it.width)) return err("SELECT: нет ширины строки");
                if (!lex_.take_char(')')) return err("SELECT: не закрыта скобка");
            }
            s.selects.push_back(it);
            if (!lex_.take_char(',')) break;
        }
        return true;
    }

    if (lex_.take_word("STOP")) { s.kind = ST_STOP; return true; }
    if (lex_.take_word("END"))  { s.kind = ST_END;  return true; }

    // Всё остальное — присваивание, с необязательным LET.
    lex_.take_word("LET");
    {
        s.kind = ST_LET;
        ExprParser ex(lex_);
        if (!var_list(s.targets, ex)) return false;
        Tok t;
        if (!ex.take(t, false) || t.t != Tok::EQ) return err("оператор не опознан");
        if (!ex.parse(s.e)) return err(ex.error());
    }
    return true;
}

bool LineParser::parse_line(Line & line, std::string & error)
{
    lex_.skip_spaces();

    unsigned num = 0;
    bool any = false;
    while (!lex_.at_end()) {
        const char c = lex_.text()[lex_.pos()];
        if (!is_digit(c)) break;
        num = num * 10 + (c - '0');
        lex_.set_pos(lex_.pos() + 1);
        any = true;
    }
    if (!any) { error = "строка без номера"; return false; }
    line.number = num;

    for (;;) {
        if (lex_.at_end()) break;
        if (lex_.at_colon()) { lex_.set_pos(lex_.pos() + 1); continue; }

        Stmt s;
        if (!parse_stmt(s)) {
            char b[32];
            std::sprintf(b, "%u", num);
            error = std::string("строка ") + b + ": " + error_;
            return false;
        }
        line.stmts.push_back(s);
    }
    return true;
}

} // namespace

bool parse_text(const std::string & koi8, Program & prog, NameTable & names,
                std::string & error)
{
    unsigned p = 0;
    const unsigned n = static_cast<unsigned>(koi8.size());

    while (p < n) {
        // Разделители: 85 на дискете, перевод строки в файлах корпуса.
        unsigned e = p;
        while (e < n) {
            const unsigned char c = static_cast<unsigned char>(koi8[e]);
            if (c == 0x85 || c == 0x0A || c == 0x0D) break;
            ++e;
        }

        bool blank = true;
        for (unsigned i = p; i < e; ++i)
            if (koi8[i] != ' ') { blank = false; break; }

        if (!blank) {
            Line line;
            LineParser lp(koi8, p, e, names);
            if (!lp.parse_line(line, error)) return false;
            prog.lines.push_back(line);
        }

        p = e;
        while (p < n) {
            const unsigned char c = static_cast<unsigned char>(koi8[p]);
            if (c != 0x85 && c != 0x0A && c != 0x0D) break;
            ++p;
        }
    }

    if (prog.lines.empty()) { error = "в программе нет ни одной строки"; return false; }

    // Типы и размеры переменных нужны интерпретатору так же, как таблицы
    // файла — при разборе оттранслированной формы.
    prog.vars = names.vars();
    return true;
}

} // namespace iskra