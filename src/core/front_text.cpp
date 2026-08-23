// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Mikhail Revzin <p3.141592653589793238462643@gmail.com>
// Part of the Iskra-226 project: https://github.com/Ptr314/Iskra-226
// Description: текстовая форма программы → промежуточное представление

#include "core/front_text.h"

#include <cstdio>
#include <cstring>

#include "core/expr.h"

namespace iskra {

unsigned NameTable::index(const std::string & name)
{
    for (unsigned i = 0; i < names_.size(); ++i)
        if (names_[i] == name) return i;
    names_.push_back(name);
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
    { "PRINT",  Tok::UNKNOWN,  false },   // разбирается на уровне оператора
    { "INPUT",  Tok::UNKNOWN,  false },
    { "STEP",   Tok::KW_STEP,  false },
    { "THEN",   Tok::KW_THEN,  false },
    { "NEXT",   Tok::UNKNOWN,  false },
    { "GOTO",   Tok::UNKNOWN,  false },
    { "STOP",   Tok::UNKNOWN,  false },
    { "HEX",    Tok::FN_HEX,   true  },
    { "SQR",    Tok::FN_SQR,   true  },
    { "ABS",    Tok::FN_ABS,   true  },
    { "INT",    Tok::FN_INT,   true  },
    { "SGN",    Tok::FN_SGN,   true  },
    { "LOG",    Tok::FN_LOG,   true  },
    { "EXP",    Tok::FN_EXP,   true  },
    { "REM",    Tok::UNKNOWN,  false },
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
    bool var_list(std::vector<unsigned> & out, ExprParser & ex);
    bool err(const std::string & m) { if (error_.empty()) error_ = m; return false; }

    TextLexer lex_;
    NameTable & names_;
    std::string error_;
};

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

bool LineParser::var_list(std::vector<unsigned> & out, ExprParser & ex)
{
    for (;;) {
        Tok t;
        if (!ex.take(t, true)) return err(ex.error());
        if (t.t != Tok::VAR) return err("ожидалась переменная");
        out.push_back(t.var);

        if (!ex.peek(t, false)) return err(ex.error());
        if (t.t != Tok::COMMA) break;
        ex.consume();
    }
    return true;
}

bool LineParser::parse_stmt(Stmt & s)
{
    lex_.skip_spaces();

    if (lex_.take_word("REM")) {
        s.kind = ST_REM;
        lex_.set_pos(lex_.end());
        return true;
    }
    if (lex_.take_word("PRINT")) {
        ExprParser ex(lex_);
        return print_items(s, ex);
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
    if (lex_.take_word("GOTO")) {
        lex_.skip_spaces();
        ExprParser ex(lex_);
        Tok t;
        if (!ex.take(t, true) || t.t != Tok::NUM) return err("GOTO без номера строки");
        long v = 0;
        t.num.to_int(v);
        s.kind = ST_GOTO;
        s.line = static_cast<unsigned>(v);
        return true;
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
    return true;
}

} // namespace iskra