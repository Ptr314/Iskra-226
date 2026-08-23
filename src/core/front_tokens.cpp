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
    ByteSource(const uint8_t * p, unsigned len, const std::vector<VarInfo> * vars)
        : p_(p), n_(len), i_(0), vars_(vars), list_context_(false) {}

    bool next(Tok & t, bool operand_expected);
    bool state_sensitive() const { return true; }

    // В операторах, операнды которых — список приёмников (INPUT, READ …),
    // DE всегда запятая, а не однобайтовый литерал.
    void set_list_context() { list_context_ = true; }

    unsigned pos() const { return i_; }
    bool at_end() const { return i_ >= n_; }

    // Перескочить «шапку» оператора — байты, которые не лексемы выражения:
    // метку GOSUB'/DEFFN', адрес возврата и тому подобное.
    void set_pos(unsigned p) { i_ = (p < n_) ? p : n_; }

    const std::string & error() const { return error_; }

private:
    bool number_e5(Tok & t, bool with_exponent);
    bool fail(const std::string & m) { if (error_.empty()) error_ = m; return false; }

    const uint8_t * p_;
    unsigned n_;
    unsigned i_;
    const std::vector<VarInfo> * vars_;
    bool list_context_;
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

// Может ли байт начинать операнд. Нужно там, где «массив или скаляр»
// по таблицам не разрешается, — у символьных переменных (docs/format.md,
// разд. 7): за именем идёт либо список индексов, либо операция.
bool looks_like_operand(uint8_t b)
{
    if (b <= VAR_MAX) return true;                 // индекс переменной
    switch (b) {
        case 0xE1:                                 // STR(
        case 0xE5: case 0xE6: case 0xE7: case 0xE8:   // константы
        case 0xEB:                                 // (
        case 0xF1:                                 // #PI
        case 0xF2: case 0xF3: case 0xF4: case 0xF5:
        case 0xF6: case 0xF7: case 0xF8:           // функции
            return true;
        default:
            return false;
    }
}

bool ByteSource::next(Tok & t, bool operand_expected)
{
    t = Tok();
    if (i_ >= n_) { t.t = Tok::END; return true; }

    const uint8_t c = p_[i_++];

    if (c <= VAR_MAX) {
        if (!operand_expected) {
            // Переменная там, где ждали операцию: список индексов у
            // переменной, не помеченной массивом в таблицах. Так выглядят
            // символьные массивы — их отличить от строки-скаляра нечем.
            return fail("похоже на индекс у символьной переменной — они ещё "
                        "не поддержаны");
        }
        t.t = Tok::VAR;
        t.var = c;

        // Скобки у индекса в потоке нет. У числовых переменных массив
        // виден по таблицам: числовой скаляр дескриптора не получает.
        // У символьных так нельзя — отличить строку-скаляр от массива строк
        // можно только по разностям адресов, а те бывают нулевыми. Поэтому
        // здесь работает правило из разд. 7: смотрим, операнд ли дальше.
        if (vars_ && c < vars_->size()) {
            const VarInfo & v = (*vars_)[c];
            t.table_array = v.is_array;
            t.indexed = v.is_string ? (i_ < n_ && looks_like_operand(p_[i_]))
                                    : v.is_array;
        }
        return true;
    }

    // Двузначные токены: значение зависит от того, чего ждёт разбор.
    if (operand_expected) {
        switch (c) {
            case 0xD5: t.t = Tok::FN_AT; return true;
            case 0xDF: t.t = Tok::FN_TAB; return true;
            case 0xE0: {                                // ссылка на массив целиком
                if (i_ >= n_) return fail("ссылка на массив оборвалась");
                const uint8_t v = p_[i_++];
                if (v > VAR_MAX) return fail("после E0 не индекс переменной");
                t.t = Tok::ARRAY;
                t.var = v;
                return true;
            }
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
                if (list_context_) { t.t = Tok::COMMA; return true; }
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
            case 0xCC: t.t = Tok::KW_GOSUB; return true;   // внутри ON
            case 0xCD: t.t = Tok::KW_GOTO;  return true;   // внутри ON
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
        case 0xDB: t.t = Tok::HASH; return true;
        case 0xE1: t.t = Tok::FN_STR; return true;
        case 0xEC: t.t = Tok::FN_POS; return true;
        case 0xED: t.t = Tok::FN_LEN; return true;
        case 0xEE: t.t = Tok::FN_NUM; return true;
        case 0xEF: t.t = Tok::FN_VAL; return true;
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
    StmtParser(const uint8_t * ops, unsigned len, const std::vector<VarInfo> * vars)
        : src_(ops, len, vars), ex_(src_), ops_(ops), len_(len), raw_(0),
          vars_(vars) {}

    // Глагол: один байт либо 0x06NN у двухбайтовых.
    bool parse(unsigned verb, Stmt & s, std::string & error);

private:
    bool print_items(Stmt & s);
    bool expect_end(const char * what);
    bool err(const std::string & m) { error_ = m; return false; }

    ByteSource src_;
    ExprParser ex_;
    const uint8_t * ops_;
    unsigned len_;
    unsigned raw_;                 // позиция при побайтовом чтении
    const std::vector<VarInfo> * vars_;
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

std::string verb_name(unsigned verb)
{
    if (verb < 0x100) return "глагол " + hex2(verb);
    return "двухбайтовый глагол 06 " + hex2(verb & 0xFF);
}

bool StmtParser::parse(unsigned verb, Stmt & s, std::string & error)
{
    error_.clear();
    bool ok = true;

    switch (verb) {
        case 0x4C:                                  // PRINT
            ok = print_items(s);
            break;

        case 0x21:                                  // GOTO
        case 0x22: {                                // GOSUB
            if (len_ != 2 || !bcd_ok(ops_[0]) || !bcd_ok(ops_[1]))
                ok = err("переход без номера строки");
            else {
                s.kind = (verb == 0x21) ? ST_GOTO : ST_GOSUB;
                s.line = bcd2(ops_[0]) * 100 + bcd2(ops_[1]);
            }
            break;
        }

        case 0x5E:                                  // RETURN
            s.kind = ST_RETURN;
            break;

        case 0x23: {                                // GOSUB' — вызов по метке
            // Метка — двоичный байт, не BCD (docs/format.md, разд. 5).
            // Открывающей скобки списка в потоке нет, как у PACK( и BIN(;
            // фактические параметры разделены DE.
            if (len_ < 1) { ok = err("GOSUB' без метки"); break; }
            s.kind = ST_GOSUBQ;
            s.label = ops_[0];
            src_.set_pos(1);
            if (src_.at_end()) break;               // вызов без параметров
            for (;;) {
                Expr a;
                if (!ex_.parse(a)) { ok = err(ex_.error()); break; }
                s.args.push_back(a);
                Tok t;
                if (!ex_.peek(t, false)) { ok = err(ex_.error()); break; }
                if (t.t != Tok::COMMA) break;
                ex_.consume();
            }
            if (ok) ok = expect_end("GOSUB'");
            break;
        }

        case 0x27: {                                // DEFFN' — помеченный вход
            // За меткой четыре байта адреса возврата: интерпретатор машины
            // заполняет их при первом вызове, в свежем файле там нули.
            // Дальше формальные параметры — индексы переменных подряд,
            // без разделителей.
            if (len_ < 5) { ok = err("DEFFN': короткая шапка"); break; }
            s.kind = ST_DEFFN;
            s.label = ops_[0];
            for (raw_ = 5; raw_ < len_; ++raw_) {
                const uint8_t v = ops_[raw_];
                if (v > VAR_MAX) {
                    ok = err("DEFFN': непонятный параметр " + hex2(v));
                    break;
                }
                s.params.push_back(v);
            }
            break;
        }

        case 0x3A: {                                // DEFFN' с текстом
            // Определение клавиши специальных функций: нажатие подставляет
            // текст. Подпрограммой такая метка не является. Шапка та же,
            // что у 27, а дальше ровно один литерал — в кавычках (E3), в
            // апострофах (E4) или шестнадцатеричный (E2, SCOPE 1).
            if (len_ < 5) { ok = err("DEFFN': короткая шапка"); break; }
            s.kind = ST_DEFFN;
            s.label = ops_[0];
            src_.set_pos(5);
            Tok t;
            if (!ex_.take(t, true)) { ok = err(ex_.error()); break; }
            if (t.t != Tok::STR && t.t != Tok::FN_HEX) {
                ok = err("DEFFN': ожидался текст клавиши");
                break;
            }
            s.has_prompt = true;
            s.prompt = t.s;
            ok = expect_end("DEFFN'");
            break;
        }

        case 0x26: {                                // ON <выражение> GOTO/GOSUB
            s.kind = ST_ON;
            if (!ex_.parse(s.e)) { ok = err(ex_.error()); break; }
            Tok t;
            if (!ex_.take(t, false)) { ok = err(ex_.error()); break; }
            if (t.t == Tok::KW_GOSUB) s.is_gosub = true;
            else if (t.t != Tok::KW_GOTO) { ok = err("ON без GOTO или GOSUB"); break; }

            // Дальше номера строк идут сырыми парами BCD, без разделителей.
            unsigned q = len_ - (len_ - src_.pos());
            q = src_.pos();
            while (q + 2 <= len_) {
                if (!bcd_ok(ops_[q]) || !bcd_ok(ops_[q + 1])) {
                    ok = err("ON: номер строки не BCD");
                    break;
                }
                s.lines.push_back(bcd2(ops_[q]) * 100 + bcd2(ops_[q + 1]));
                q += 2;
            }
            if (ok && q != len_) ok = err("ON: лишний байт в списке переходов");
            if (ok && s.lines.empty()) ok = err("ON без номеров строк");
            break;
        }

        case 0x42:                                  // STOP
            s.kind = ST_STOP;
            break;

        case 0x56:                                  // REM — операнды это текст
        case 0x3F:                                  // % — краткий REM
            s.kind = ST_REM;
            break;

        case 0x59:                                  // END
            s.kind = ST_END;
            break;

        case 0x36: {                                // присваивание
            // Цели идут вплотную, без разделителей, до первого '='. Целью
            // может быть и элемент массива: V01(V0A),V02(V0A)=0.
            s.kind = ST_LET;
            for (;;) {
                Tok t;
                if (!ex_.peek(t, true)) { ok = err(ex_.error()); break; }
                if (t.t == Tok::EQ) { ex_.consume(); break; }
                Expr target;
                if (!ex_.parse_lvalue(target)) { ok = err(ex_.error()); break; }
                s.targets.push_back(target);
            }
            if (!ok) break;
            if (s.targets.empty()) { ok = err("присваивание без переменной слева"); break; }
            if (!ex_.parse(s.e)) { ok = err(ex_.error()); break; }
            ok = expect_end("присваивания");
            break;
        }

        case 0x4E:                                  // COM — то же, что DIM,
        case 0x46: {                                // но в общей области
            // В потоке только индексы переменных: размеры лежат в таблицах
            // (docs/format.md, разд. 6) и уже разобраны.
            s.kind = ST_DIM;
            while (raw_ < len_) {
                const uint8_t v = ops_[raw_++];
                if (v == 0xDE) continue;            // запятая, если вдруг есть
                if (v > VAR_MAX) { ok = err("DIM: непонятный операнд " + hex2(v)); break; }
                DimEntry d;
                d.var = v;
                if (vars_ && v < vars_->size()) {
                    d.dim1 = (*vars_)[v].dim1;
                    d.dim2 = (*vars_)[v].dim2;
                    d.str_len = (*vars_)[v].str_len;
                }
                s.dims.push_back(d);
            }
            if (ok && s.dims.empty()) ok = err("DIM без переменных");
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
            // Запятая перед первым приёмником в потоке не кодируется;
            // дальше приёмники разделяются DE. Приёмником может быть
            // элемент массива: INPUT V00(V0B).
            {
                ByteSource rest(ops_ + raw_, len_ - raw_, vars_);
                rest.set_list_context();
                ExprParser p(rest);
                for (;;) {
                    Tok t;
                    if (!p.peek(t, true)) { ok = err(p.error()); break; }
                    if (t.t == Tok::END) break;
                    if (t.t == Tok::COMMA) { p.consume(); continue; }
                    Expr target;
                    if (!p.parse_lvalue(target)) { ok = err(p.error()); break; }
                    s.targets.push_back(target);
                }
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
                ByteSource rest(ops_ + 1, len_ - 1, vars_);
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

        case 0x47: {                                // CONVERT
            // Скобки вокруг образа и запятая перед ним не кодируются
            // (docs/format.md, разд. 7).
            s.kind = ST_CONVERT;
            if (!ex_.parse(s.e)) { ok = err(ex_.error()); break; }
            Tok t;
            if (!ex_.take(t, false) || t.t != Tok::KW_TO) { ok = err("CONVERT без TO"); break; }

            Expr target;
            if (!ex_.parse_lvalue(target)) { ok = err(ex_.error()); break; }
            s.targets.push_back(target);

            if (!ex_.peek(t, true)) { ok = err(ex_.error()); break; }
            if (t.t == Tok::COMMA) { ex_.consume(); if (!ex_.peek(t, true)) { ok = err(ex_.error()); break; } }
            if (t.t == Tok::STR) {
                s.prompt = t.s;                     // образ
                s.has_prompt = true;
                ex_.consume();
            }
            ok = expect_end("CONVERT");
            break;
        }

        case 0x0624: {                              // LINPUT
            s.kind = ST_LINPUT;
            if (raw_ < len_ && (ops_[raw_] == 0xE3 || ops_[raw_] == 0xE4)) {
                ++raw_;
                if (raw_ >= len_) { ok = err("LINPUT: подсказка оборвалась"); break; }
                const unsigned n = ops_[raw_++];
                if (raw_ + n > len_) { ok = err("LINPUT: подсказка оборвалась"); break; }
                s.prompt.assign(reinterpret_cast<const char *>(ops_ + raw_), n);
                s.has_prompt = true;
                raw_ += n;
            }
            {
                ByteSource rest(ops_ + raw_, len_ - raw_, vars_);
                rest.set_list_context();
                ExprParser p(rest);
                Tok t;
                if (!p.peek(t, true)) { ok = err(p.error()); break; }
                // Запятая перед приёмником не кодируется, а минус перед ним
                // назначения пока не имеет — просто пропускаем.
                if (t.t == Tok::COMMA || t.t == Tok::MINUS) p.consume();

                Expr target;
                if (!p.parse_lvalue(target)) { ok = err(p.error()); break; }
                s.targets.push_back(target);
            }
            break;
        }

        case 0x0602: {                              // MAT REDIM
            s.kind = ST_REDIM;
            for (;;) {
                Tok t;
                if (!ex_.peek(t, true)) { ok = err(ex_.error()); break; }
                if (t.t == Tok::END) break;
                if (t.t == Tok::COMMA) { ex_.consume(); continue; }
                if (t.t != Tok::ARRAY && t.t != Tok::VAR) { ok = err("MAT REDIM: ожидался массив"); break; }
                ex_.consume();

                DimEntry d;
                d.var = t.var;
                d.computed = true;

                // Размерности здесь в явных скобках, в отличие от DIM.
                if (!ex_.take(t, true) || t.t != Tok::LPAR) { ok = err("MAT REDIM: нет размерностей"); break; }
                for (;;) {
                    Expr sz;
                    if (!ex_.parse(sz)) { ok = err(ex_.error()); break; }
                    d.sizes.push_back(sz);
                    if (!ex_.peek(t, false)) { ok = err(ex_.error()); break; }
                    if (t.t == Tok::COMMA) { ex_.consume(); continue; }
                    if (t.t == Tok::RPAR) { ex_.consume(); break; }
                    ok = err("MAT REDIM: список размерностей не закрыт");
                    break;
                }
                if (!ok) break;

                // У символьного массива за скобками может стоять длина элемента.
                if (!ex_.peek(t, true)) { ok = err(ex_.error()); break; }
                if (t.t == Tok::NUM) {
                    long v = 0;
                    t.num.to_int(v);
                    d.str_len = static_cast<unsigned>(v);
                    ex_.consume();
                }
                s.dims.push_back(d);
            }
            if (ok && s.dims.empty()) ok = err("MAT REDIM без массивов");
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
            ok = err(verb_name(verb) + " ещё не реализован");
            break;
    }

    if (!ok) error = error_;
    return ok;
}

// ---------------------------------------------------------------------------
// Таблицы переменных (docs/format.md, разд. 6)
// ---------------------------------------------------------------------------

// Таблицы 2 и 3 — один непрерывный массив дескрипторов по 4 байта, идущих
// в порядке убывания индекса переменной. Таблица 1 хранит размеры тех
// переменных, у которых установлен бит 0 флага, и сопоставляется с ними
// порядково.
void build_vars(const std::vector<uint8_t> & code, unsigned L1, unsigned L2,
                unsigned L3, std::vector<VarInfo> & vars)
{
    const unsigned N = L2 / 4 + L3 / 4;
    vars.assign(N, VarInfo());
    if (!N) return;

    const unsigned t1 = 6;
    const unsigned t23 = 6 + L1;
    if (t23 + N * 4 > code.size()) return;

    for (unsigned pos = 0; pos < N; ++pos) {
        const uint8_t flag = code[t23 + pos * 4 + 2];
        VarInfo & v = vars[N - 1 - pos];
        v.known = true;
        v.is_string = (flag & 0x20) != 0;
        v.is_integer = (flag & 0x30) == 0;
    }

    // Порядковое соответствие «запись таблицы 1 → переменная с битом 0».
    unsigned k = 0;
    for (unsigned pos = 0; pos < N; ++pos) {
        if (!(code[t23 + pos * 4 + 2] & 1)) continue;

        const unsigned off = t1 + k * 8;
        if (off + 8 > t23) break;                  // таблица 1 кончилась
        ++k;

        VarInfo & v = vars[N - 1 - pos];
        const unsigned field = code[off + 2] | (code[off + 3] << 8);
        const unsigned nelem = code[off + 4] | (code[off + 5] << 8);
        const unsigned sizecode = code[off + 6] | (code[off + 7] << 8);

        if ((field >> 8) == 0x08) {
            // Одномерный массив либо строка с явной длиной.
            v.dim1 = nelem;
            v.dim2 = 0;
        } else if (nelem == 10 && (field == 295 || field == 1881)) {
            // Неявно объявленный массив: размерность по умолчанию.
            v.dim1 = 10;
            v.dim2 = 0;
        } else {
            v.dim1 = nelem;
            v.dim2 = field;
        }

        if (v.is_string) {
            // Размерный код = 2 x размер элемента, младший бит — «длина
            // задана явно».
            v.str_len = sizecode >> 1;
            if (!v.str_len) v.str_len = 16;
            // Отличить строку-скаляр от массива строк по таблицам нельзя:
            // нужны разности адресов, а они бывают нулевыми. Для исполнения
            // это безразлично — скаляр просто частный случай с одним
            // элементом, — но для разбора важно: у массива за именем идёт
            // список индексов. Считаем массивом всё, где элементов больше
            // одного либо задана вторая размерность.
            v.is_array = (v.dim2 != 0) || (v.dim1 > 1);
        } else {
            // Числовые скаляры дескриптора не получают: раз он есть — массив.
            v.is_array = true;
        }
    }
}

// ---------------------------------------------------------------------------

bool parse_body(const uint8_t * body, unsigned len, Line & line,
                const std::vector<VarInfo> * vars, std::string & error)
{
    unsigned p = 0;
    while (p < len) {
        // Матричные и графические операторы кодируются двумя байтами:
        // 06 <подкод> <len> <операнды> (docs/format.md, разд. 3).
        unsigned verb = body[p];
        unsigned head = 2;
        if (verb == 0x06) {
            if (p + 2 >= len) { error = "двухбайтовый глагол оборвался"; return false; }
            verb = 0x0600 | body[p + 1];
            head = 3;
        }
        if (p + 1 >= len) { error = "оператор оборвался"; return false; }

        const unsigned oplen = body[p + head - 1];
        if (p + head + oplen > len) { error = "длина операндов выходит за строку"; return false; }

        Stmt s;
        StmtParser sp(body + p + head, oplen, vars);
        if (!sp.parse(verb, s, error)) return false;
        line.stmts.push_back(s);

        p += head + oplen;
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

    // Размеры массивов и типы переменных известны только отсюда: в самих
    // операторах DIM в потоке лежат одни индексы.
    build_vars(code, L1, L2, L3, prog.vars);

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
        if (!parse_body(&code[p + 3], len - 1, line, &prog.vars, e)) {
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

namespace {

// Склейка секторов: по 254 байта данных, хвостовые нули включительно —
// правило выравнивания записей опирается на границы кусков.
bool collect_stream(const std::vector<uint8_t> & file, std::vector<uint8_t> & code,
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

    unsigned p = 256;
    while (p + 256 <= file.size()) {
        if (file[p] == 0x1C) break;             // control record
        if (file[p + 1] != 0x80) break;
        code.insert(code.end(), file.begin() + p + 2, file.begin() + p + 256);
        p += 256;
    }
    return true;
}

} // namespace

bool parse_tokenized(const std::vector<uint8_t> & file, Program & prog,
                     std::string & error)
{
    std::vector<uint8_t> code;
    if (!collect_stream(file, code, error)) return false;
    return parse_tokenized_stream(code, prog, error);
}

bool parse_tokenized_vars(const std::vector<uint8_t> & file,
                          std::vector<VarInfo> & vars, std::string & error)
{
    std::vector<uint8_t> code;
    if (!collect_stream(file, code, error)) return false;
    if (code.size() < 8) { error = "поток слишком короток"; return false; }

    build_vars(code, (code[0] << 8) | code[1], (code[2] << 8) | code[3],
               (code[4] << 8) | code[5], vars);
    return true;
}

} // namespace iskra