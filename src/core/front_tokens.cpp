// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Mikhail Revzin <p3.141592653589793238462643@gmail.com>
// Part of the Iskra-226 project: https://github.com/Ptr314/Iskra-226
// Description: токенизированная форма программы → промежуточное представление

#include "core/front_tokens.h"

#include <cstdio>

#include "core/expr.h"

namespace iskra {

namespace {

const unsigned SECTOR_DATA = 254;      // байт полезных данных в секторе
const uint8_t VAR_MAX = 0xC9;          // индексы переменных 00…C9
const uint8_t REC_SEP = 0xFE;

std::string hex2(unsigned v)
{
    char b[8];
    std::sprintf(b, "%02X", v & 0xFF);
    return b;
}

bool bcd_ok(uint8_t b)
{
    return (b >> 4) <= 9 && (b & 0x0F) <= 9;
}

unsigned bcd2(uint8_t b)
{
    return (b >> 4) * 10 + (b & 0x0F);
}

// Переход к следующему сектору выполняется только если весь остаток
// текущего 254-байтового куска нулевой (docs/format.md, разд. 3).
void skip_padding(const std::vector<uint8_t> & code, unsigned & p)
{
    const unsigned b = ((p / SECTOR_DATA) + 1) * SECTOR_DATA;
    if (b <= p || b > code.size()) return;
    for (unsigned i = p; i < b; ++i)
        if (code[i] != 0) return;
    p = b;
}

bool is_record_start(const std::vector<uint8_t> & code, unsigned p)
{
    skip_padding(code, p);
    if (p + 3 > code.size()) return false;
    if (!bcd_ok(code[p]) || !bcd_ok(code[p + 1])) return false;
    const unsigned len = code[p + 2];
    if (len < 1) return false;
    const unsigned e = p + 2 + len;
    return e < code.size() && code[e] == REC_SEP;
}

// ---------------------------------------------------------------------------
// Источник лексем поверх байтов операндов одного оператора
// ---------------------------------------------------------------------------

class ByteSource : public TokenSource
{
public:
    ByteSource(const uint8_t * p, unsigned len)
        : p_(p), n_(len), i_(0) {}

    bool next(Tok & t, bool operand_expected);

    unsigned pos() const { return i_; }
    bool at_end() const { return i_ >= n_; }

    const std::string & error() const { return error_; }

private:
    bool number_e5(Tok & t, bool with_exponent);
    bool fail(const std::string & m) { if (error_.empty()) error_ = m; return false; }

    const uint8_t * p_;
    unsigned n_;
    unsigned i_;
    std::string error_;
};

// E5: описатель + BCD. Старшая тетрада описателя — цифр до запятой,
// младшая — всего цифр. E6 добавляет байт порядка.
bool ByteSource::number_e5(Tok & t, bool with_exponent)
{
    if (i_ >= n_) return fail("константа оборвалась");
    const uint8_t desc = p_[i_++];
    const unsigned ip = desc >> 4;
    const unsigned total = desc & 0x0F;
    const unsigned bytes = (total + 1) / 2;
    if (i_ + bytes > n_) return fail("константа оборвалась");

    std::string digits;
    for (unsigned k = 0; k < bytes; ++k) {
        const uint8_t b = p_[i_ + k];
        digits += static_cast<char>('0' + (b >> 4));
        digits += static_cast<char>('0' + (b & 0x0F));
    }
    i_ += bytes;
    digits.resize(total);

    std::string s;
    if (ip == 0) {
        s = "." + digits;
    } else if (ip >= total) {
        s = digits;
        for (unsigned k = total; k < ip; ++k) s += '0';
    } else {
        s = digits.substr(0, ip) + "." + digits.substr(ip);
    }

    if (with_exponent) {
        if (i_ >= n_) return fail("константа оборвалась");
        const uint8_t e = p_[i_++];
        if (!bcd_ok(e)) return fail("порядок константы не BCD");
        s += "E";
        char b[8];
        std::sprintf(b, "%u", bcd2(e));
        s += b;
    }

    if (!Number::parse(s, t.num)) return fail("не разобралась константа " + s);
    t.t = Tok::NUM;
    return true;
}

bool ByteSource::next(Tok & t, bool operand_expected)
{
    t = Tok();
    if (i_ >= n_) { t.t = Tok::END; return true; }

    const uint8_t c = p_[i_++];

    if (c <= VAR_MAX) {
        if (!operand_expected) {
            // Ссылка на переменную там, где ждали операцию, — это список
            // индексов массива. Массивы ещё не реализованы.
            return fail("массивы ещё не реализованы (индекс после переменной)");
        }
        t.t = Tok::VAR;
        t.var = c;
        return true;
    }

    // Двузначные токены: значение зависит от того, чего ждёт разбор.
    if (operand_expected) {
        switch (c) {
            case 0xD5: t.t = Tok::FN_AT; return true;
            case 0xE9: t.t = Tok::MINUS; return true;   // унарный минус
            case 0xE5: return number_e5(t, false);
            case 0xE6: return number_e5(t, true);
            case 0xE7: {
                if (i_ + 2 > n_) return fail("константа оборвалась");
                const unsigned v = bcd2(p_[i_]) * 100 + bcd2(p_[i_ + 1]);
                i_ += 2;
                t.t = Tok::NUM;
                t.num = Number::from_int(static_cast<long>(v));
                return true;
            }
            case 0xE8: {
                if (i_ >= n_) return fail("константа оборвалась");
                const uint8_t b = p_[i_++];
                if (!bcd_ok(b)) return fail("константа не BCD: " + hex2(b));
                t.t = Tok::NUM;
                t.num = Number::from_int(static_cast<long>(bcd2(b)));
                return true;
            }
            case 0xDE: {
                // Сырой байт: байтовые константы и адреса устройств.
                if (i_ >= n_) return fail("литерал оборвался");
                t.t = Tok::NUM;
                t.num = Number::from_int(static_cast<long>(p_[i_++]));
                return true;
            }
            default: break;
        }
    } else {
        switch (c) {
            case 0xD5: t.t = Tok::NE; return true;
            case 0xD6: t.t = Tok::LE; return true;
            case 0xD7: t.t = Tok::LT; return true;
            case 0xD8: t.t = Tok::GE; return true;
            case 0xDE: t.t = Tok::COMMA; return true;
            case 0xE9: t.t = Tok::MINUS; return true;   // бинарный минус
            case 0xE0: t.t = Tok::CARET; return true;
            case 0xDC: t.t = Tok::SLASH; return true;
            case 0xDF: t.t = Tok::STAR; return true;
            case 0xD3: {
                if (i_ + 2 > n_) return fail("THEN без номера строки");
                const unsigned ln = bcd2(p_[i_]) * 100 + bcd2(p_[i_ + 1]);
                i_ += 2;
                t.t = Tok::KW_THEN;
                t.num = Number::from_int(static_cast<long>(ln));
                return true;
            }
            default: break;
        }
    }

    // Однозначные.
    switch (c) {
        case 0xD0: t.t = Tok::RPAR; return true;
        case 0xD1: t.t = Tok::KW_TO; return true;
        case 0xD2: t.t = Tok::KW_STEP; return true;
        case 0xD4: t.t = Tok::GT; return true;
        case 0xD9: t.t = Tok::EQ; return true;
        case 0xDD: t.t = Tok::SEMI; return true;
        case 0xEA: t.t = Tok::PLUS; return true;
        case 0xEB: t.t = Tok::LPAR; return true;
        case 0xF1: t.t = Tok::PI; return true;
        case 0xF2: t.t = Tok::FN_ABS; return true;
        case 0xF3: t.t = Tok::FN_INT; return true;
        case 0xF5: t.t = Tok::FN_SGN; return true;
        case 0xF6: t.t = Tok::FN_SQR; return true;
        case 0xF7: t.t = Tok::FN_LOG; return true;
        case 0xF8: t.t = Tok::FN_EXP; return true;

        case 0xE2: {                                   // HEX( — длина и данные
            if (i_ >= n_) return fail("HEX( оборвался");
            const unsigned len = p_[i_++];
            if (i_ + len > n_) return fail("HEX( оборвался");
            t.t = Tok::FN_HEX;
            t.s.assign(reinterpret_cast<const char *>(p_ + i_), len);
            i_ += len;
            return true;
        }
        case 0xE3:
        case 0xE4: {                                   // литерал в кавычках / апострофах
            if (i_ >= n_) return fail("литерал оборвался");
            const unsigned len = p_[i_++];
            if (i_ + len > n_) return fail("литерал оборвался");
            t.t = Tok::STR;
            t.s.assign(reinterpret_cast<const char *>(p_ + i_), len);
            i_ += len;
            return true;
        }
        default: break;
    }

    t.t = Tok::UNKNOWN;
    t.s = "токен " + hex2(c) + (operand_expected ? " в позиции операнда"
                                                 : " в позиции операции");
    return true;
}

// ---------------------------------------------------------------------------
// Разбор операторов
// ---------------------------------------------------------------------------

class StmtParser
{
public:
    StmtParser(const uint8_t * ops, unsigned len)
        : src_(ops, len), ex_(src_), ops_(ops), len_(len), raw_(0) {}

    bool parse(uint8_t verb, Stmt & s, std::string & error);

private:
    bool print_items(Stmt & s);
    bool expect_end(const char * what);
    bool err(const std::string & m) { error_ = m; return false; }

    ByteSource src_;
    ExprParser ex_;
    const uint8_t * ops_;
    unsigned len_;
    unsigned raw_;                 // позиция при побайтовом чтении
    std::string error_;
};

// Спрашивать конец у источника, а не заглядыванием: заглядывать можно
// только в том состоянии, в каком лексема потом будет прочитана, иначе
// двузначный токен разберётся не так.
bool StmtParser::expect_end(const char * what)
{
    if (!src_.at_end()) return err(std::string("лишние байты в операторе ") + what);
    return true;
}

bool StmtParser::print_items(Stmt & s)
{
    s.kind = ST_PRINT;
    s.newline = true;

    if (src_.at_end()) return true;                // голый PRINT — пустая строка

    Tok t;
    for (;;) {
        PrintItem item;
        if (!ex_.parse(item.e)) return err(ex_.error());

        if (!ex_.peek(t, false)) return err(ex_.error());
        if (t.t == Tok::SEMI) { item.sep = SEP_TIGHT; ex_.consume(); }
        else if (t.t == Tok::COMMA) { item.sep = SEP_ZONE; ex_.consume(); }
        else if (t.t == Tok::END) { item.sep = SEP_NONE; }
        else return err("непонятный разделитель в PRINT");

        s.items.push_back(item);

        if (item.sep == SEP_NONE) break;
        if (src_.at_end()) { s.newline = false; break; }     // хвостовой ';'
    }
    return true;
}

bool StmtParser::parse(uint8_t verb, Stmt & s, std::string & error)
{
    error_.clear();
    bool ok = true;

    switch (verb) {
        case 0x4C:                                  // PRINT
            ok = print_items(s);
            break;

        case 0x21: {                                // GOTO
            if (len_ != 2 || !bcd_ok(ops_[0]) || !bcd_ok(ops_[1]))
                ok = err("GOTO без номера строки");
            else {
                s.kind = ST_GOTO;
                s.line = bcd2(ops_[0]) * 100 + bcd2(ops_[1]);
            }
            break;
        }

        case 0x42:                                  // STOP
            s.kind = ST_STOP;
            break;

        case 0x56:                                  // REM — операнды это текст
            s.kind = ST_REM;
            break;

        case 0x59:                                  // END
            s.kind = ST_END;
            break;

        case 0x36: {                                // присваивание
            // Цели идут вплотную, без разделителей, до первого '='.
            while (raw_ < len_ && ops_[raw_] <= VAR_MAX) s.targets.push_back(ops_[raw_++]);
            if (s.targets.empty()) { ok = err("присваивание без переменной слева"); break; }
            if (raw_ >= len_ || ops_[raw_] != 0xD9) { ok = err("присваивание без ="); break; }
            ++raw_;
            {
                ByteSource rhs(ops_ + raw_, len_ - raw_);
                ExprParser p(rhs);
                s.kind = ST_LET;
                if (!p.parse(s.e)) { ok = err(p.error()); break; }
                Tok t;
                if (!p.peek(t, false) || t.t != Tok::END) { ok = err("лишние байты в присваивании"); break; }
            }
            break;
        }

        case 0x41: {                                // INPUT
            s.kind = ST_INPUT;
            if (raw_ < len_ && (ops_[raw_] == 0xE3 || ops_[raw_] == 0xE4)) {
                ++raw_;
                if (raw_ >= len_) { ok = err("INPUT: подсказка оборвалась"); break; }
                const unsigned n = ops_[raw_++];
                if (raw_ + n > len_) { ok = err("INPUT: подсказка оборвалась"); break; }
                s.prompt.assign(reinterpret_cast<const char *>(ops_ + raw_), n);
                s.has_prompt = true;
                raw_ += n;
            }
            // Запятая перед первым приёмником в потоке не кодируется.
            while (raw_ < len_) {
                if (ops_[raw_] == 0xDE) { ++raw_; continue; }
                if (ops_[raw_] > VAR_MAX) { ok = err("INPUT: непонятный приёмник " + hex2(ops_[raw_])); break; }
                s.targets.push_back(ops_[raw_++]);
            }
            if (ok && s.targets.empty()) ok = err("INPUT без приёмника");
            break;
        }

        case 0x57: {                                // FOR
            // Знак '=' после переменной цикла не кодируется.
            if (len_ < 1 || ops_[0] > VAR_MAX) { ok = err("FOR без переменной"); break; }
            s.kind = ST_FOR;
            s.var = ops_[0];
            {
                ByteSource rest(ops_ + 1, len_ - 1);
                ExprParser p(rest);
                if (!p.parse(s.e)) { ok = err(p.error()); break; }

                Tok t;
                if (!p.take(t, false) || t.t != Tok::KW_TO) { ok = err("FOR без TO"); break; }
                if (!p.parse(s.limit)) { ok = err(p.error()); break; }

                if (!p.peek(t, false)) { ok = err(p.error()); break; }
                if (t.t == Tok::KW_STEP) {
                    p.consume();
                    if (!p.parse(s.step)) { ok = err(p.error()); break; }
                    s.has_step = true;
                    if (!p.peek(t, false)) { ok = err(p.error()); break; }
                }
                if (t.t != Tok::END) { ok = err("лишние байты в FOR"); break; }
            }
            break;
        }

        case 0x52: {                                // NEXT
            if (len_ != 1 || ops_[0] > VAR_MAX) { ok = err("NEXT без переменной"); break; }
            s.kind = ST_NEXT;
            s.var = ops_[0];
            break;
        }

        case 0x24: {                                // IF … THEN <строка>
            s.kind = ST_IF;
            if (!ex_.parse(s.e)) { ok = err(ex_.error()); break; }
            Tok t;
            if (!ex_.take(t, false) || t.t != Tok::KW_THEN) { ok = err("IF без THEN"); break; }
            long ln = 0;
            t.num.to_int(ln);
            s.line = static_cast<unsigned>(ln);
            ok = expect_end("IF");
            break;
        }

        default:
            ok = err("глагол " + hex2(verb) + " ещё не реализован");
            break;
    }

    if (!ok) error = error_;
    return ok;
}

// ---------------------------------------------------------------------------

bool parse_body(const uint8_t * body, unsigned len, Line & line, std::string & error)
{
    unsigned p = 0;
    while (p < len) {
        const uint8_t verb = body[p];
        if (p + 1 >= len) { error = "оператор оборвался"; return false; }
        const unsigned oplen = body[p + 1];
        if (p + 2 + oplen > len) { error = "длина операндов выходит за строку"; return false; }

        Stmt s;
        StmtParser sp(body + p + 2, oplen);
        if (!sp.parse(verb, s, error)) return false;
        line.stmts.push_back(s);

        p += 2 + oplen;
    }
    return true;
}

} // namespace

bool parse_tokenized_stream(const std::vector<uint8_t> & code, Program & prog,
                            std::string & error)
{
    if (code.size() < 8) { error = "поток слишком короток"; return false; }

    const unsigned L1 = (code[0] << 8) | code[1];
    const unsigned L2 = (code[2] << 8) | code[3];
    const unsigned L3 = (code[4] << 8) | code[5];

    unsigned p = 6 + L1 + L2 + L3;
    if (p > code.size()) { error = "таблицы переменных не помещаются в поток"; return false; }

    // У части файлов между таблицами и первой записью лежат четыре байта
    // неизвестного назначения; отличаем по корректности самой записи.
    if (!is_record_start(code, p) && is_record_start(code, p + 4)) p += 4;

    for (;;) {
        skip_padding(code, p);
        if (p + 3 > code.size()) break;
        if (!bcd_ok(code[p]) || !bcd_ok(code[p + 1])) {
            error = "номер строки не BCD по смещению " + hex2(p);
            return false;
        }

        Line line;
        line.number = bcd2(code[p]) * 100 + bcd2(code[p + 1]);
        const unsigned len = code[p + 2];
        if (len < 1) { error = "нулевая длина записи строки"; return false; }
        const unsigned end = p + 2 + len;
        if (end > code.size()) { error = "запись строки выходит за поток"; return false; }

        std::string e;
        if (!parse_body(&code[p + 3], len - 1, line, e)) {
            char b[32];
            std::sprintf(b, "%u", line.number);
            error = std::string("строка ") + b + ": " + e;
            return false;
        }
        prog.lines.push_back(line);

        if (end >= code.size() || code[end] != REC_SEP) break;   // последняя запись
        p = end + 1;
    }

    if (prog.lines.empty()) { error = "в программе нет ни одной строки"; return false; }
    return true;
}

bool parse_tokenized(const std::vector<uint8_t> & file, Program & prog,
                     std::string & error)
{
    if (file.size() < 512) { error = "файл слишком короток"; return false; }
    if (file[0] != 1) { error = "не программа BASIC"; return false; }

    const uint8_t attr = file[9];
    if (!(attr == 0x20 || attr == 0x21 || attr == 0x24 || attr == 0x25)) {
        error = "не программа BASIC (признак " + hex2(attr) + ")";
        return false;
    }
    if ((attr & 1) == 0) {
        error = "программа в текстовом виде, а не оттранслированная";
        return false;
    }

    // Склейка секторов: по 254 байта данных, хвостовые нули включительно —
    // правило выравнивания записей опирается на границы кусков.
    std::vector<uint8_t> code;
    unsigned p = 256;
    while (p + 256 <= file.size()) {
        if (file[p] == 0x1C) break;             // control record
        if (file[p + 1] != 0x80) break;
        code.insert(code.end(), file.begin() + p + 2, file.begin() + p + 256);
        p += 256;
    }

    return parse_tokenized_stream(code, prog, error);
}

} // namespace iskra