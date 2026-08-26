// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Mikhail Revzin <p3.141592653589793238462643@gmail.com>
// Part of the Iskra-226 project: https://github.com/Ptr314/Iskra-226
// Description: трансляция текстовой формы в оттранслированную

#include "core/tokenize.h"

#include <cstdio>

#include "core/text_lexer.h"

namespace iskra {

namespace {

// Разделитель строк на дискете — байт 85 (docs/format.md, разд. 2);
// в файлах корпуса это обычный перевод строки.
bool is_break(char c)
{
    const unsigned char u = static_cast<unsigned char>(c);
    return u == 0x85 || u == 0x0A || u == 0x0D;
}

uint8_t to_bcd(unsigned v) { return static_cast<uint8_t>(((v / 10) % 10) * 16 + v % 10); }

// ---------------------------------------------------------------------------
// Кодировщик выражений
// ---------------------------------------------------------------------------

// Обратная сторона разбора: та же схема рекурсивного спуска, но вместо
// дерева выдаются байты. Позиция (операнд или операция) известна из места
// в спуске — именно она и решает, каким байтом кодировать двузначный токен
// (docs/format.md, разд. 5).
class Encoder
{
public:
    Encoder(TextLexer & lex, std::vector<uint8_t> & out)
        : ex_(lex), lex_(lex), out_(out) {}

    bool expr();                       // связки условий — самый низкий уровень
    bool lvalue();                     // приёмник: переменная или её элемент

    // У лексера сообщение точнее, чем у разборщика выражений: тот про любую
    // беду говорит «не удалось прочитать лексему».
    const std::string & error() const
    {
        return lex_.error().empty() ? error_ : lex_.error();
    }
    bool fail(const std::string & m) { if (error_.empty()) error_ = m; return false; }

    ExprParser & parser() { return ex_; }
    void emit(uint8_t b) { out_.push_back(b); }
    void emit(const std::string & s)
    {
        for (std::size_t i = 0; i < s.size(); ++i)
            out_.push_back(static_cast<uint8_t>(s[i]));
    }

    // Число: E8 — один байт BCD, E7 — два, E5 — описатель и цифры,
    // E6 — то же с порядком. text — исходная запись, если она известна.
    bool number(const Number & n, const std::string & text = std::string());
    // Литерал в кавычках.
    void literal(const std::string & s);

private:
    bool compare();
    bool sum();
    bool product();
    bool unary();
    bool power();
    bool primary();
    bool call(uint8_t token, unsigned args_min, unsigned args_max,
              bool close = true);
    bool indices();
    bool substr();
    // token — байт функции; with_count — у VAL( бывает второй аргумент;
    // with_rel — POS( поглощает сравнение целиком.
    bool implicit(uint8_t token, bool with_count, bool with_rel);

    ExprParser ex_;
    TextLexer & lex_;
    std::vector<uint8_t> & out_;
    std::string error_;
};

bool Encoder::number(const Number & n, const std::string & text)
{
    // Запись с порядком: `1E6` = `E6 11 10 06` (STAT08 480), `1E5` =
    // `E6 11 10 05` (EDITOR 6432). Описатель тот же, что у E5, за цифрами
    // мантиссы — порядок одним байтом BCD.
    const std::size_t e_at = text.find('E');
    if (e_at != std::string::npos && e_at + 1 < text.size()) {
        const std::string mant = text.substr(0, e_at);
        const std::string ord = text.substr(e_at + 1);
        if (ord.empty() || ord[0] == '-')
            return fail("отрицательный порядок ещё не кодируется");
        unsigned exp = 0;
        for (std::size_t i = (ord[0] == '+') ? 1 : 0; i < ord.size(); ++i) {
            if (ord[i] < '0' || ord[i] > '9') return fail("непонятный порядок " + ord);
            exp = exp * 10 + static_cast<unsigned>(ord[i] - '0');
        }
        if (exp > 99) return fail("порядок больше 99");
        std::string digits;
        unsigned before = 0;
        bool point = false;
        for (std::size_t i = 0; i < mant.size(); ++i) {
            if (mant[i] == '.') { point = true; continue; }
            if (mant[i] < '0' || mant[i] > '9') return fail("непонятная мантисса " + mant);
            digits += mant[i];
            if (!point) ++before;
        }
        unsigned total = static_cast<unsigned>(digits.size());
        if (before >= total) total = before;
        if (total > 15) return fail("мантисса длиннее 15 цифр");
        emit(0xE6);
        emit(static_cast<uint8_t>(((before & 0x0F) << 4) | (total & 0x0F)));
        if (digits.size() % 2) digits += '0';
        for (std::size_t i = 0; i + 1 < digits.size() + 1 && i < digits.size(); i += 2)
            emit(static_cast<uint8_t>((digits[i] - '0') * 16 + (digits[i + 1] - '0')));
        emit(to_bcd(exp));
        return true;
    }

    // Целые 0…99 и 0…9999 занимают меньше места отдельными токенами.
    long v = 0;
    if (!n.is_negative() && n.to_int(v) && v >= 0) {
        if (v <= 99) {
            emit(0xE8);
            emit(to_bcd(static_cast<unsigned>(v)));
            return true;
        }
        // Верхняя граница короткой формы — 7999: на 8000 машина переходит
        // к общей записи (VICT 705, `ON E7-8000GOTO…` = `E9 E5 44 80 00`),
        // а во всём корпусе 2053 вхождения E7 лежат ниже неё.
        if (v <= 7999) {
            emit(0xE7);
            emit(to_bcd(static_cast<unsigned>(v) / 100));
            emit(to_bcd(static_cast<unsigned>(v) % 100));
            return true;
        }
    }

    // Общий вид: E5 <описатель> <цифры BCD>, старшая тетрада описателя —
    // цифр до запятой, младшая — всего цифр (docs/format.md, разд. 5).
    std::string s = n.to_display();
    unsigned p = 0;
    while (p < s.size() && (s[p] == ' ' || s[p] == '-')) ++p;
    if (n.is_negative()) return fail("отрицательная константа кодируется знаком");

    std::string digits;
    unsigned before = 0;
    bool seen_point = false;
    for (; p < s.size(); ++p) {
        if (s[p] == '.') { seen_point = true; continue; }
        if (s[p] == 'E') return fail("константа с порядком ещё не кодируется");
        if (s[p] < '0' || s[p] > '9') return fail("непонятная константа " + s);
        digits += s[p];
        if (!seen_point) ++before;
    }
    // Ведущие нули дробной части — значащие: .01 это 02 01, а не 01 10
    // (STAT04 80). Срезать их нельзя.
    unsigned total = static_cast<unsigned>(digits.size());
    if (before >= total) total = before;
    if (total > 15) return fail("константа длиннее 15 цифр");

    emit(0xE5);
    emit(static_cast<uint8_t>(((before & 0x0F) << 4) | (total & 0x0F)));
    std::string body = digits;
    while (body.size() < total) body += '0';
    if (body.size() % 2) body += '0';
    for (std::size_t i = 0; i + 1 < body.size() + 1 && i < body.size(); i += 2)
        emit(static_cast<uint8_t>((body[i] - '0') * 16 + (body[i + 1] - '0')));
    return true;
}

void Encoder::literal(const std::string & s)
{
    emit(0xE3);
    emit(static_cast<uint8_t>(s.size()));
    emit(s);
}

bool Encoder::indices()
{
    // Скобки у индекса в потоке нет, закрывается D0.
    for (;;) {
        if (!expr()) return false;
        Tok t;
        if (!ex_.take(t, false)) return fail(ex_.error());
        if (t.t == Tok::COMMA) { emit(0xDE); continue; }
        if (t.t != Tok::RPAR) return fail("список индексов не закрыт");
        emit(0xD0);
        return true;
    }
}

// STR(что, начало [, длина]). Первая запятая в потоке не кодируется,
// вторая кодируется DE; закрывается D0 (docs/format.md, разд. 5).
bool Encoder::substr()
{
    emit(0xE1);
    Tok t;
    if (!ex_.take(t, true)) return fail(ex_.error());
    if (t.t == Tok::ARRAY) { emit(0xE0); emit(static_cast<uint8_t>(t.var)); }
    else if (t.t == Tok::VAR) {
        emit(static_cast<uint8_t>(t.var));
        if (t.indexed && !indices()) return false;
    } else if (t.t == Tok::STR) {
        literal(t.s);
    } else {
        return fail("STR( ждёт символьную переменную");
    }

    if (!ex_.peek(t, true)) return fail(ex_.error());
    if (t.t == Tok::COMMA) ex_.consume();          // первая запятая не кодируется

    unsigned args = 1;
    for (;;) {
        if (!expr()) return false;
        ++args;
        if (!ex_.take(t, false)) return fail(ex_.error());
        if (t.t == Tok::COMMA) {
            if (args >= 3) return fail("у STR( не больше трёх аргументов");
            emit(0xDE);
            continue;
        }
        if (t.t != Tok::RPAR) return fail("STR( не закрыт");
        break;
    }
    emit(0xD0);
    return true;
}

bool Encoder::implicit(uint8_t token, bool with_count, bool with_rel)
{
    emit(token);
    if (!primary()) return false;

    Tok t;
    if (!ex_.peek(t, false)) return fail(ex_.error());

    if (with_rel) {
        uint8_t rel = 0;
        switch (t.t) {
            case Tok::EQ: rel = 0xD9; break;
            case Tok::NE: rel = 0xD5; break;
            case Tok::LT: rel = 0xD7; break;
            case Tok::LE: rel = 0xD6; break;
            case Tok::GT: rel = 0xD4; break;
            case Tok::GE: rel = 0xD8; break;
            default: break;
        }
        if (rel) {
            ex_.consume();
            emit(rel);
            // Справа обычно стоит код знака: `POS(Q¤=20)` = `EC 0D D9 DE 20`
            // (EDITOR 2630). Отличаем по виду — ровно две шестнадцатеричные
            // цифры и скобка, как у INIT(.
            unsigned code = 0;
            if (lex_.take_hex_byte(code)) {
                emit(0xDE);
                emit(static_cast<uint8_t>(code));
            } else if (!primary()) {
                return false;
            }
            if (!ex_.peek(t, false)) return fail(ex_.error());
        }
    } else if (with_count && t.t == Tok::COMMA) {
        // `VAL(x,2)` = `EF <арг> DE DB`: DB здесь и есть двойка, своих
        // операндов у неё нет (docs/format.md, разд. 5).
        ex_.consume();
        emit(0xDE);
        Tok two;
        if (!ex_.take(two, true)) return fail(ex_.error());
        long v = 0;
        if (two.t != Tok::NUM || !two.num.to_int(v) || v != 2)
            return fail("у VAL( второй аргумент может быть только 2");
        emit(0xDB);
        if (!ex_.peek(t, false)) return fail(ex_.error());
    }

    // Скобка в тексте есть, в потоке её нет — просто съедаем.
    if (t.t == Tok::RPAR) ex_.consume();
    return true;
}

bool Encoder::primary()
{
    Tok t;
    if (!ex_.take(t, true)) return fail(ex_.error());

    switch (t.t) {
        case Tok::NUM: return number(t.num, t.s);
        case Tok::STR: literal(t.s); return true;
        case Tok::PI:  emit(0xF1); return true;

        case Tok::VAR: {
            emit(static_cast<uint8_t>(t.var));
            if (t.indexed) return indices();
            return true;
        }
        case Tok::ARRAY: emit(0xE0); emit(static_cast<uint8_t>(t.var)); return true;

        case Tok::LPAR: {
            emit(0xEB);
            if (!expr()) return false;
            Tok c;
            if (!ex_.take(c, false) || c.t != Tok::RPAR) return fail("скобка не закрыта");
            emit(0xD0);
            return true;
        }

        case Tok::FN_HEX:
            emit(0xE2);
            emit(static_cast<uint8_t>(t.s.size()));
            emit(t.s);
            return true;

        case Tok::FN_ABS: return call(0xF2, 1, 1);
        case Tok::FN_INT: return call(0xF3, 1, 1);
        case Tok::FN_SGN: return call(0xF5, 1, 1);
        case Tok::FN_SQR: return call(0xF6, 1, 1);
        case Tok::FN_LOG: return call(0xF7, 1, 1);
        case Tok::FN_EXP: return call(0xF8, 1, 1);
        case Tok::FN_SIN:  return call(0xF9, 1, 1);
        case Tok::FN_COS:  return call(0xFA, 1, 1);
        case Tok::FN_TAN:  return call(0xFB, 1, 1);
        case Tok::FN_ASIN: return call(0xFC, 1, 1);
        case Tok::FN_ACOS: return call(0xFD, 1, 1);
        case Tok::FN_ATAN: return call(0xFE, 1, 1);
        // ROUND( — два аргумента, вторая запятая кодируется (EDITOR 4132).
        case Tok::FN_ROUND: return call(0xD8, 2, 2);
        case Tok::FN_RND: return call(0xF4, 1, 1);   // EDITOR 4543
        // У AT( закрывающей скобки в потоке нет вовсе: на корпусе
        // 1495 вхождений без неё и ни одного с ней.
        case Tok::FN_AT:  return call(0xD5, 2, 3, false);
        case Tok::FN_TAB: return call(0xDF, 1, 1);

        // FN<имя>( — функция пользователя: `F0`, имя сырым кодом символа,
        // аргумент и `D0` (`GC121` 2310 = `F0 48 31 D0` при `DEFFN H`).
        case Tok::FN_USER: {
            emit(0xF0);
            emit(static_cast<uint8_t>(t.var));
            if (!expr()) return false;
            Tok c;
            if (!ex_.take(c, false) || c.t != Tok::RPAR)
                return fail("FN: скобка не закрыта");
            emit(0xD0);
            return true;
        }

        case Tok::FN_STR: return substr();
        // Неявные функции: скобок в потоке нет вовсе, ни открывающей, ни
        // закрывающей (docs/format.md, разд. 5).
        case Tok::FN_LEN: return implicit(0xED, false, false);
        case Tok::FN_NUM: return implicit(0xEE, false, false);
        case Tok::FN_VAL: return implicit(0xEF, true,  false);
        case Tok::FN_POS: return implicit(0xEC, false, true);

        // `/адрес` в позиции операнда — адрес устройства, а не деление:
        // `PRINT /10` = `4C 03 DC DE 10` (docs/format.md, разд. 5). Адрес
        // шестнадцатеричный, и читать его надо у лексера: числом «10» он
        // разобрался бы как десятка.
        // `#<а.в.>` — номер строки таблицы устройств: `PRINT #5` = `4C 03
        // DB E8 05` (EDITOR 6871).
        case Tok::HASH: emit(0xDB); return primary();

        case Tok::SLASH: {
            unsigned a = 0;
            if (!lex_.take_hex2(a, true)) return fail("после «/» нет адреса устройства");
            emit(0xDC);
            emit(0xDE);
            emit(static_cast<uint8_t>(a));
            return true;
        }

        default: break;
    }
    char b[32];
    std::sprintf(b, "%d", static_cast<int>(t.t));
    return fail("операнд ещё не кодируется, лексема " + std::string(b)
                + (t.s.empty() ? std::string() : ": " + t.s));
}

bool Encoder::call(uint8_t token, unsigned args_min, unsigned args_max,
                   bool close)
{
    // Открывающую скобку лексер уже съел вместе с именем функции; в потоке
    // её нет, закрывается D0 (docs/format.md, разд. 5).
    emit(token);
    unsigned n = 0;
    for (;;) {
        if (!expr()) return false;
        ++n;
        Tok t;
        if (!ex_.take(t, false)) return fail(ex_.error());
        if (t.t == Tok::COMMA) { emit(0xDE); continue; }
        if (t.t != Tok::RPAR) return fail("функция: скобка не закрыта");
        break;
    }
    if (n < args_min || n > args_max) return fail("не столько аргументов у функции");
    if (close) emit(0xD0);
    return true;
}

bool Encoder::power()
{
    if (!primary()) return false;
    Tok t;
    if (!ex_.peek(t, false)) return fail(ex_.error());
    if (t.t != Tok::CARET) return true;
    ex_.consume();
    emit(0xE0);                                 // ^ в позиции операции
    return unary();
}

bool Encoder::unary()
{
    Tok t;
    if (!ex_.peek(t, true)) return fail(ex_.error());
    if (t.t == Tok::MINUS) { ex_.consume(); emit(0xE9); return unary(); }
    if (t.t == Tok::PLUS)  { ex_.consume(); return unary(); }
    return power();
}

bool Encoder::product()
{
    if (!unary()) return false;
    for (;;) {
        Tok t;
        if (!ex_.peek(t, false)) return fail(ex_.error());
        if (t.t == Tok::STAR) { ex_.consume(); emit(0xDF); }
        else if (t.t == Tok::SLASH) { ex_.consume(); emit(0xDC); }
        else return true;
        if (!unary()) return false;
    }
}

bool Encoder::sum()
{
    if (!product()) return false;
    for (;;) {
        Tok t;
        if (!ex_.peek(t, false)) return fail(ex_.error());
        if (t.t == Tok::PLUS) { ex_.consume(); emit(0xEA); }
        else if (t.t == Tok::MINUS) { ex_.consume(); emit(0xE9); }
        else return true;
        if (!product()) return false;
    }
}

bool Encoder::compare()
{
    if (!sum()) return false;
    Tok t;
    if (!ex_.peek(t, false)) return fail(ex_.error());
    uint8_t b;
    switch (t.t) {
        case Tok::EQ: b = 0xD9; break;
        case Tok::NE: b = 0xD5; break;
        case Tok::LT: b = 0xD7; break;
        case Tok::LE: b = 0xD6; break;
        case Tok::GT: b = 0xD4; break;
        case Tok::GE: b = 0xD8; break;
        default: return true;
    }
    ex_.consume();
    emit(b);
    return sum();
}

bool Encoder::expr()
{
    if (!compare()) return false;
    for (;;) {
        Tok t;
        if (!ex_.peek(t, false)) return fail(ex_.error());
        if (t.t == Tok::AND) { ex_.consume(); emit(0xE7); }
        else if (t.t == Tok::OR) { ex_.consume(); emit(0xE6); }
        // `E5` `XOR`, `E6` `OR`, `E7` `AND` идут в таблице ключевых слов
        // интерпретатора подряд (docs/format.md, разд. 4). В корпусе связка
        // `XOR` не встречается ни разу — байт взят из таблицы, не выдуман.
        else if (t.t == Tok::XOR) { ex_.consume(); emit(0xE5); }
        else return true;
        if (!compare()) return false;
    }
}

bool Encoder::lvalue()
{
    Tok t;
    if (!ex_.take(t, true)) return fail(ex_.error());
    // STR( работает и слева от знака равенства (руководство, разд. 13.3).
    if (t.t == Tok::FN_STR) return substr();
    if (t.t == Tok::ARRAY) { emit(0xE0); emit(static_cast<uint8_t>(t.var)); return true; }
    if (t.t != Tok::VAR) return fail("слева от знака равенства ожидалась переменная");
    emit(static_cast<uint8_t>(t.var));
    if (t.indexed) return indices();
    return true;
}

} // namespace

// ---------------------------------------------------------------------------
// Операторы
// ---------------------------------------------------------------------------

namespace {

// Кодирует один оператор; байты операндов складываются в out.
class StmtEncoder
{
public:
    StmtEncoder(TextLexer & lex) : lex_(lex) {}

    // verb — глагол, out — операнды. false и пустой verb значит «строка
    // кончилась».
    bool encode(unsigned & verb, std::vector<uint8_t> & out, bool & done);

    const std::string & error() const { return error_; }

private:
    bool err(const std::string & m) { if (error_.empty()) error_ = m; return false; }
    bool line_number(std::vector<uint8_t> & out, unsigned n);
    // Приставка дисковых операторов: буква устройства, `/адрес`, `#строка`.
    bool disk_prefix(Encoder & enc, std::vector<uint8_t> & out, bool with_device);
    // Первое слово оператора — чтобы в сообщении было видно, чего не хватает.
    std::string leading_word() const;
    // Знак сравнения вне выражения — в MAT SEARCH он стоит отдельной лексемой.
    bool relation(std::vector<uint8_t> & out);
    // Разделитель после разобранного выражения. Спрашивать надо у разборщика:
    // он уже прочитал следующую лексему, и лексер стоит за ней (CLAUDE.md).
    bool take_sep(Encoder & enc, Tok::Type type);
    // Дальше в скобках стоит образ печати, а не выражение?
    bool at_print_image() const;
    // Оператор кончается звёздочкой? Нужно только ASMB.
    bool at_trailing_star() const;
    // Дальше стоит символьное значение? Отличает форму LIMITS с именем файла
    // от формы без него: имя символьное, приёмники числовые.
    bool at_string_name();
    // Определение числовой функции пользователя (руководство, разд. 4.8).
    bool deffn(unsigned & verb, std::vector<uint8_t> & out);
    // Хвост из номеров строк у SAVE DC и LOAD DC: сырые пары BCD через `DE`.
    bool line_tail(Encoder & enc, std::vector<uint8_t> & out);
    // Конец элемента группы `PLOT`: ближайшие `,` или `>` вне скобок и
    // вне литерала.
    unsigned element_end() const;

    TextLexer & lex_;
    std::string error_;
};

// DEFFN <имя функции>(<формальная переменная>) = <а.в.> (разд. 4.8).
// В потоке: `5A <имя> <два байта рабочего поля> <формальная> <выражение>`
// (`LL` 9 = `5A 48 00 00 2A EB 2A EA F2 2A D0 D0 DC E8 02`, то есть
// `DEFFN H(X)=(X+ABS(X))/2`). Знака равенства в потоке нет вовсе.
bool StmtEncoder::deffn(unsigned & verb, std::vector<uint8_t> & out)
{
    verb = 0x5A;
    unsigned nm = 0;
    if (!lex_.take_fn_name(nm))
        return err("DEFFN: имя функции — буква A…Z либо цифра");
    out.push_back(static_cast<uint8_t>(nm));
    // Рабочее поле машина заполняет при исполнении; восстановить его из
    // текста нечем, как и адрес возврата у DEFFN'.
    out.push_back(0x00);
    out.push_back(0x00);

    if (!lex_.take_char('(')) return err("DEFFN: нет скобки после имени функции");
    Encoder enc(lex_, out);
    Tok t;
    if (!enc.parser().take(t, true)) return err(enc.error());
    if (t.t != Tok::VAR) return err("DEFFN: ждали формальную переменную");
    // «Именем формальной переменной может быть имя любой числовой
    // переменной Бейсика» — символьная сюда не годится.
    out.push_back(static_cast<uint8_t>(t.var));
    if (!enc.parser().take(t, false) || t.t != Tok::RPAR)
        return err("DEFFN: скобка не закрыта");
    if (!enc.parser().take(t, false) || t.t != Tok::EQ)
        return err("DEFFN без знака равенства");
    if (!enc.expr()) return err(enc.error());
    return true;
}

unsigned StmtEncoder::element_end() const
{
    unsigned p = lex_.pos();
    const unsigned e = lex_.end();
    unsigned depth = 0;
    bool in_str = false;
    while (p < e) {
        const char c = lex_.text()[p];
        if (c == '"') in_str = !in_str;
        else if (!in_str) {
            if (c == '(') ++depth;
            else if (c == ')') { if (!depth) break; --depth; }
            else if (!depth && (c == ',' || c == '>')) break;
        }
        ++p;
    }
    return p;
}

std::string StmtEncoder::leading_word() const
{
    unsigned p = lex_.pos();
    const unsigned e = lex_.end();
    while (p < e && lex_.text()[p] == ' ') ++p;
    std::string w;
    while (p < e && w.size() < 12) {
        const char c = lex_.text()[p];
        if (c == ' ' || c == ':' || c == '=' || c == ',' || c == ';') break;
        w += c;
        ++p;
    }
    return w.empty() ? std::string("?") : w;
}

bool StmtEncoder::disk_prefix(Encoder & enc, std::vector<uint8_t> & out,
                              bool with_device)
{
    if (with_device) {
        // Буква устройства одна: имён из двух букв не бывает.
        if (lex_.take_word("F")) out.push_back(0x00);
        else if (lex_.take_word("R")) out.push_back(0x01);
        else if (lex_.take_word("T")) out.push_back(0x02);
        else return err("дисковый оператор без устройства");
    }
    // `¤` — контрольное считывание после записи (руководство, разд. 5.2).
    if (lex_.take_char('$')) out.push_back(0xD6);
    if (lex_.take_char('/')) {
        // За `DC` идёт выражение: `/34` это однобайтовый литерал `DE 34`
        // (VICT 2190), а `DC 0B` — переменная (DISSM 7382), программа
        // вычисляет адрес сама. Две шестнадцатеричные цифры пробуем первыми:
        // адреса устройств пишут именно так, и `/0C` иначе разобралось бы
        // как «ноль» и имя.
        out.push_back(0xDC);
        unsigned a = 0;
        if (lex_.take_hex2(a, true)) {
            out.push_back(0xDE);
            out.push_back(static_cast<uint8_t>(a));
        } else if (!enc.expr()) {
            return err("нет адреса устройства после «/»");
        }
        // Запятая за адресом кодируется: `DATA SAVE BT /34,W¤` = 68 05
        // DC DE 34 DE 0F (VICT 2190).
        if (take_sep(enc, Tok::COMMA) || lex_.take_char(',')) out.push_back(0xDE);
    }
    if (lex_.take_char('#')) {
        out.push_back(0xDB);
        if (!enc.expr()) return err(enc.error());
        Tok t;
        if (!enc.parser().peek(t, false)) return err(enc.error());
        if (t.t == Tok::COMMA) { enc.parser().consume(); out.push_back(0xDE); }
        // Разделителя не оказалось — источник надо вернуть: дальше читает
        // сам лексер, а он стоит за заглянутой лексемой (CLAUDE.md).
        else enc.parser().unpeek();
    }
    return true;
}

bool StmtEncoder::relation(std::vector<uint8_t> & out)
{
    // Длинные знаки раньше коротких: «<=» не должно распасться на «<».
    static const struct { const char * text; uint8_t code; } REL[] = {
        { "<=", 0xD6 }, { ">=", 0xD8 }, { "<>", 0xD5 },
        { "=",  0xD9 }, { "<",  0xD7 }, { ">",  0xD4 }
    };
    lex_.skip_spaces();
    const unsigned p = lex_.pos();
    for (unsigned k = 0; k < sizeof(REL) / sizeof(REL[0]); ++k) {
        const std::string s = REL[k].text;
        if (p + s.size() > lex_.end()) continue;
        if (lex_.text().compare(p, s.size(), s) != 0) continue;
        lex_.set_pos(p + static_cast<unsigned>(s.size()));
        out.push_back(REL[k].code);
        return true;
    }
    return err("ждали знак сравнения");
}

bool StmtEncoder::take_sep(Encoder & enc, Tok::Type type)
{
    Tok t;
    if (!enc.parser().peek(t, false)) return false;
    if (t.t != type) return false;
    enc.parser().consume();
    return true;
}

bool StmtEncoder::at_print_image() const
{
    unsigned p = lex_.pos();
    const unsigned e = lex_.end();
    while (p < e && lex_.text()[p] == ' ') ++p;
    if (p >= e || lex_.text()[p] != '(') return false;
    ++p;
    bool any = false;
    for (; p < e; ++p) {
        const char c = lex_.text()[p];
        if (c == ')') return any;
        if (c != '#' && c != '.' && c != '^' && c != '+' && c != '-'
            && c != ' ' && c != ',') return false;
        any = true;
    }
    return false;
}

bool StmtEncoder::at_trailing_star() const
{
    unsigned last = 0;
    bool any = false;
    for (unsigned p = lex_.pos(); p < lex_.end(); ++p) {
        const char c = lex_.text()[p];
        if (c == ':') break;
        if (c == ' ') continue;
        last = static_cast<unsigned char>(c);
        any = true;
    }
    return any && last == '*';
}

bool StmtEncoder::at_string_name()
{
    lex_.skip_spaces();
    unsigned p = lex_.pos();
    const unsigned e = lex_.end();
    if (p >= e) return false;
    if (lex_.text()[p] == '"') return true;
    if (!is_letter(lex_.text()[p])) return false;
    ++p;
    if (p < e && is_digit(lex_.text()[p])) ++p;
    // `¤` в КОИ-8 — тот же байт 24, что и доллар (CLAUDE.md).
    return p < e && lex_.text()[p] == '\x24';
}
bool StmtEncoder::line_number(std::vector<uint8_t> & out, unsigned n)
{
    if (n > 9999) return err("номер строки больше 9999");
    out.push_back(to_bcd(n / 100));
    out.push_back(to_bcd(n % 100));
    return true;
}

// «В операторе SAVE DC после имени программного файла следует указать номер
// начальной и через запятую номер конечной строки записываемого фрагмента»
// (руководство, разд. 5.3). Номера идут сырыми парами BCD через `DE`:
// `SAVE DC F("*ASMBBAS")"*ASMBBAS"9000,9090` = `… 90 00 DE 90 90`
// (`ASMBBAS` 9090). У `LOAD DC` их бывает три (`ROM` 900).
//
// Разделителя между именем и первым номером нет ни в тексте, ни в потоке.
bool StmtEncoder::line_tail(Encoder & enc, std::vector<uint8_t> & out)
{
    for (;;) {
        Tok t;
        if (!enc.parser().peek(t, false)) return err(enc.error());
        if (t.t == Tok::NUM) {
            enc.parser().consume();
            long v = 0;
            if (!t.num.floor_to_int(v) || v < 0)
                return err("номер строки не целый неотрицательный");
            if (!line_number(out, static_cast<unsigned>(v))) return false;
            continue;
        }
        if (t.t == Tok::COMMA) {
            enc.parser().consume();
            out.push_back(0xDE);
            continue;
        }
        enc.parser().unpeek();
        return true;
    }
}

bool StmtEncoder::encode(unsigned & verb, std::vector<uint8_t> & out, bool & done)
{
    done = false;
    out.clear();
    if (lex_.at_end()) { done = true; return true; }

    // REM и % забирают остаток строки как есть, до двоеточия.
    if (lex_.take_word("REM")) {
        verb = 0x56;
        const unsigned e = lex_.end();
        unsigned p = lex_.pos();
        while (p < e && lex_.text()[p] != ':') ++p;
        for (unsigned i = lex_.pos(); i < p; ++i)
            out.push_back(static_cast<uint8_t>(lex_.text()[i]));
        lex_.set_pos(p);
        return true;
    }

    // Краткий REM: тоже забирает остаток строки до двоеточия.
    if (lex_.take_word("%")) {
        verb = 0x3F;
        const unsigned e = lex_.end();
        unsigned p = lex_.pos();
        while (p < e && lex_.text()[p] != ':') ++p;
        for (unsigned i = lex_.pos(); i < p; ++i)
            out.push_back(static_cast<uint8_t>(lex_.text()[i]));
        lex_.set_pos(p);
        return true;
    }

    if (lex_.take_word("INPUT")) {
        // Запятая после подсказки не кодируется, приёмники идут вплотную
        // без разделителей (docs/format.md, разд. 4).
        verb = 0x41;
        Encoder enc(lex_, out);
        Tok t;
        if (!enc.parser().peek(t, true)) return err(enc.error());
        if (t.t == Tok::STR) {
            enc.parser().consume();
            enc.literal(t.s);
            if (!enc.parser().peek(t, false)) return err(enc.error());
            if (t.t == Tok::COMMA) enc.parser().consume();
        }
        // Приёмников может не быть вовсе: `INPUT` без операндов встречается
        // в корпусе (STAT02 100, STAT03 180). Спрашивать надо у разборщика:
        // подсказку он уже прочитал, и лексер стоит за ней.
        if (!enc.parser().peek(t, true)) return err(enc.error());
        if (t.t == Tok::END) return true;
        for (;;) {
            if (!enc.lvalue()) return err(enc.error());
            if (!enc.parser().peek(t, false)) return err(enc.error());
            if (t.t != Tok::COMMA) break;
            enc.parser().consume();
        }
        return true;
    }

    {
        // Пробелов между словами может быть сколько угодно, а может не быть
        // вовсе, поэтому берём их по отдельности и откатываемся, если это
        // обычный `COM`.
        const unsigned save = lex_.pos();
        if (!(lex_.take_word("COM") && lex_.take_word("CLEAR")))
            lex_.set_pos(save);
        else {
        verb = 0x37;
        if (lex_.at_end() || lex_.at_colon()) return true;
        Encoder enc(lex_, out);
        Tok t;
        if (!enc.parser().take(t, true)) return err(enc.error());
        if (t.t == Tok::ARRAY) {
            out.push_back(0xE0);
            out.push_back(static_cast<uint8_t>(t.var));
            return true;
        }
        if (t.t == Tok::VAR && !t.indexed) {
            out.push_back(static_cast<uint8_t>(t.var));
            return true;
        }
        return err("COM CLEAR: ждали переменную либо массив");
        }
    }

    if (lex_.take_word("DIM") || lex_.take_word("COM")) {
        // В потоке только индексы переменных: размеры лежат в таблицах
        // (docs/format.md, разд. 6). Но прочитать их надо — иначе таблицы
        // будет неоткуда взять, и образ, собранный из текста, окажется без
        // размеров массивов.
        const bool common = (lex_.text()[lex_.pos() - 3] == 'C');
        verb = common ? 0x4Eu : 0x46u;
        Encoder enc(lex_, out);
        for (;;) {
            Tok t;
            if (!enc.parser().take(t, true)) return err(enc.error());
            if (t.t != Tok::VAR && t.t != Tok::ARRAY) return err("DIM без переменной");
            out.push_back(static_cast<uint8_t>(t.var));

            unsigned dim1 = 0, dim2 = 0, str_len = 0;
            if (t.indexed) {
                // Открывающую скобку лексер уже съел.
                for (;;) {
                    Tok n;
                    if (!enc.parser().take(n, true)) return err(enc.error());
                    if (n.t != Tok::NUM) return err("DIM: размерность не число");
                    long v = 0;
                    n.num.to_int(v);
                    if (!dim1) dim1 = static_cast<unsigned>(v);
                    else dim2 = static_cast<unsigned>(v);
                    if (!enc.parser().take(n, false)) return err(enc.error());
                    if (n.t == Tok::RPAR) break;
                    if (n.t != Tok::COMMA)
                        return err("DIM: список размерностей не закрыт");
                }
            }
            // За размерностями символьной переменной может стоять длина.
            if (!t.s.empty() && t.s[t.s.size() - 1] == '$') {
                Tok n;
                if (!enc.parser().peek(n, true)) return err(enc.error());
                if (n.t == Tok::NUM) {
                    enc.parser().consume();
                    long v = 0;
                    n.num.to_int(v);
                    str_len = static_cast<unsigned>(v);
                }
            }
            if (t.var < lex_.names().vars().size()) {
                VarInfo & v = lex_.names().vars()[t.var];
                if (dim1) { v.is_array = true; v.dim1 = dim1; v.dim2 = dim2; }
                if (str_len) v.str_len = str_len;
                if (common) v.is_common = true;
            }
            if (!take_sep(enc, Tok::COMMA)) break;
        }
        return true;
    }

    // ON ERROR — раньше вычисляемого ON, это другой оператор.
    if (lex_.take_word("ON ERROR")) {
        verb = 0x34;
        if (lex_.at_end() || lex_.at_colon()) return true;   // отмена обработки
        Encoder enc(lex_, out);
        Tok t;
        if (!enc.parser().peek(t, true)) return err(enc.error());
        if (t.t == Tok::VAR) {
            // Приёмники идут парой, разделителя между ними в потоке нет.
            for (unsigned k = 0; k < 2; ++k) {
                if (!enc.parser().take(t, true) || t.t != Tok::VAR)
                    return err("ON ERROR: ждали символьную переменную");
                out.push_back(static_cast<uint8_t>(t.var));
                if (k == 0 && (!enc.parser().take(t, false) || t.t != Tok::COMMA))
                    return err("ON ERROR: между приёмниками нужна запятая");
            }
        }
        if (!enc.parser().take(t, false)) return err(enc.error());
        long ln = 0;
        if (t.t == Tok::KW_THEN) {
            out.push_back(0xD3);
            if (!t.num.to_int(ln)) return err("ON ERROR: неверный номер строки");
        } else if (t.t == Tok::KW_GOTO || t.t == Tok::KW_GOSUB) {
            out.push_back(t.t == Tok::KW_GOTO ? 0xCDu : 0xCCu);
            if (!enc.parser().take(t, true) || t.t != Tok::NUM)
                return err("ON ERROR: нет номера строки");
            if (!t.num.to_int(ln)) return err("ON ERROR: неверный номер строки");
        } else {
            return err("ON ERROR без GOTO, THEN или GOSUB");
        }
        return line_number(out, static_cast<unsigned>(ln));
    }

    if (lex_.take_word("ON")) {
        verb = 0x26;
        Encoder enc(lex_, out);
        if (!enc.expr()) return err(enc.error());
        Tok t;
        if (!enc.parser().take(t, false)) return err(enc.error());
        if (t.t == Tok::KW_GOSUB) out.push_back(0xCC);
        else if (t.t == Tok::KW_GOTO) out.push_back(0xCD);
        else return err("ON без GOTO или GOSUB");
        // Дальше номера строк сырыми парами BCD, без разделителей.
        for (;;) {
            unsigned n = 0;
            if (!lex_.take_uint(n)) return err("ON без номеров строк");
            if (!line_number(out, n)) return false;
            if (!lex_.take_char(',')) break;
        }
        return true;
    }

    // `COM CLEAR` — не форма `COM`, а свой глагол `37` (docs/format.md,
    // разд. 5). Проверяется раньше `COM`: длинные слова прежде коротких.
    // Помеченные подпрограммы: метка — двоичное число, а не BCD.
    if (lex_.take_word("DEFFN")) {
        // Без апострофа это определение числовой функции (разд. 4.8) —
        // совсем другой глагол.
        if (!lex_.take_char(0x27)) return deffn(verb, out);
        verb = 0x27;
        unsigned label = 0;
        if (!lex_.take_uint(label) || label > 255) return err("DEFFN': метка 0…255");
        out.push_back(static_cast<uint8_t>(label));
        // Четыре нуля — адрес возврата, машина заполняет его при вызове.
        for (unsigned i = 0; i < 4; ++i) out.push_back(0x00);
        if (lex_.take_char('(')) {
            Encoder enc(lex_, out);
            for (;;) {
                Tok t;
                if (!enc.parser().take(t, true)) return err(enc.error());
                if (t.t != Tok::VAR) return err("DEFFN': ждали формальный параметр");
                out.push_back(static_cast<uint8_t>(t.var));
                if (!enc.parser().take(t, false)) return err(enc.error());
                if (t.t == Tok::COMMA) continue;      // разделителей в потоке нет
                if (t.t != Tok::RPAR) return err("DEFFN': скобка не закрыта");
                break;
            }
            return true;
        }
        // Текст клавиши спецфункции — это другой глагол: 3A вместо 27
        // (docs/format.md, разд. 4; CHANAL 10).
        if (!lex_.at_end() && !lex_.at_colon()) {
            Encoder enc(lex_, out);
            Tok t;
            if (!enc.parser().peek(t, true)) return err(enc.error());
            if (t.t == Tok::STR) {
                enc.parser().consume();
                enc.literal(t.s);
                verb = 0x3A;
            }
        }
        return true;
    }

    if (lex_.take_word("GOSUB '") || lex_.take_word("GOSUB'")) {
        verb = 0x23;
        unsigned label = 0;
        if (!lex_.take_uint(label) || label > 255) return err("GOSUB': метка 0…255");
        out.push_back(static_cast<uint8_t>(label));
        if (lex_.take_char('(')) {
            Encoder enc(lex_, out);
            for (;;) {
                if (!enc.expr()) return err(enc.error());
                Tok t;
                if (!enc.parser().take(t, false)) return err(enc.error());
                if (t.t == Tok::COMMA) { out.push_back(0xDE); continue; }
                if (t.t != Tok::RPAR) return err("GOSUB': скобка не закрыта");
                break;
            }
        }
        return true;
    }

    // Поразрядные операции над байтами: `AND(B¤,DF)` = 43 03 23 DE DF
    // (EDITOR 3469), `AND(A¤,B¤)` = 43 02 1E 15 (DISSM 23571). Разделителя
    // между аргументами в потоке нет вовсе: `DE hh` — это и есть
    // однобайтовый литерал, как у INIT(.
    //
    // `BOOL` перед ними: у него впереди ещё цифра операции
    // (`BOOL 9(A¤,B¤)` = 45 03 09 53 55, LКОПДИСК 4243).
    //
    // **`ADD C` — не признак у `ADD`, а свой глагол `63`.** Установлено на
    // паре `SLIDE`/`SL2`: `SLIDE` 1430 = `K%=1+VAL(STR(A¤(),J+1)):ADD C
    // (N¤,STR(A¤(),J+2,4)):RETURN`, а `SL2..` 1500 — те же три оператора,
    // средний из которых `63 0F E1 E0 00 44 EA E8 02 DE E8 04 D0`. В обеих
    // формах `ADD` без `C` не встречается ни разу, а `ADD C` — дважды
    // против трёх `63` (редакции разные). Раньше признак кодировался
    // выдуманным `D4` по аналогии с `ROTATE C`.
    //
    // Пишут его и слитно (`ADDC(`, книга, пример 14.2), и раздельно
    // (`ADD C (`, `SLIDE`, `SIG`, `ROM`), поэтому берём оба написания.
    {
        static const struct { const char * word; uint8_t verb; int kind; } BITOPS[] = {
            // kind: 0 — обычная, 1 — BOOL с цифрой
            { "BOOL", 0x45, 1 }, { "ADDC", 0x63, 0 }, { "ADD C", 0x63, 0 },
            { "ADD", 0x4A, 0 },
            { "AND", 0x43, 0 }, { "OR", 0x61, 0 }, { "XOR", 0x62, 0 }
        };
        for (unsigned k = 0; k < sizeof(BITOPS) / sizeof(BITOPS[0]); ++k) {
            if (!lex_.take_word(BITOPS[k].word)) continue;
            verb = BITOPS[k].verb;
            Encoder enc(lex_, out);
            if (BITOPS[k].kind == 1) {
                unsigned x = 0;
                if (!lex_.take_hex_digit(x)) return err("BOOL без кода операции");
                out.push_back(static_cast<uint8_t>(x));
            }
            if (!lex_.take_char('(')) return err("поразрядная операция без скобки");
            if (!enc.lvalue()) return err(enc.error());
            if (!take_sep(enc, Tok::COMMA))
                return err("поразрядная операция без запятой");
            unsigned code = 0;
            if (lex_.take_hex_byte(code)) {
                out.push_back(0xDE);
                out.push_back(static_cast<uint8_t>(code));
            } else if (!enc.expr()) {
                return err(enc.error());
            }
            if (!take_sep(enc, Tok::RPAR) && !lex_.take_char(')'))
                return err("поразрядная операция: скобка не закрыта");
            return true;
        }
    }

    if (lex_.take_word("ASMB")) {
        // Список через DE, пропущенный первый аргумент тоже даёт DE:
        // `ASMB ,76200,A¤,M¤,Y¤,64` = 06 25 0F DE E5 55 76 20 00 DE 22 …
        // (EDITOR 2612). Сама микропрограмма — обычные выражения; что она
        // делает, эмулятору пока неважно.
        verb = 0x0625;
        Encoder enc(lex_, out);
        for (;;) {
            if (lex_.at_end() || lex_.at_colon()) break;
            Tok t;
            if (!enc.parser().peek(t, true)) return err(enc.error());
            if (t.t == Tok::COMMA) {
                enc.parser().consume();
                out.push_back(0xDE);
                continue;
            }
            // `ASMB Z¤*` (EDITOR 3738): звёздочка в конце оператора — не
            // умножение, а признак; выражением её не разобрать.
            if (t.t == Tok::VAR && at_trailing_star()) {
                enc.parser().consume();
                out.push_back(static_cast<uint8_t>(t.var));
                lex_.take_char('*');
                out.push_back(0xDF);
                break;
            }
            if (!enc.expr()) return err(enc.error());
            if (!take_sep(enc, Tok::COMMA)) break;
            out.push_back(0xDE);
        }
        return true;
    }

    if (lex_.take_word("$COPY")) {
        // `¤COPY /34,S¤()` = 06 1F 06 DC DE 34 DE E0 0C (EDITOR 1237).
        verb = 0x061F;
        Encoder enc(lex_, out);
        if (!disk_prefix(enc, out, false)) return false;
        return enc.expr() ? true : err(enc.error());
    }

    if (lex_.take_word("LABEL")) {
        // `LABEL B¤()3,,,"Q"` = 06 1E 0A E0 1F E8 03 DE DE DE E3 01 51
        // (VICT 6210): за буфером может идти число без разделителя, а
        // пропущенные аргументы всё равно дают DE.
        verb = 0x061E;
        Encoder enc(lex_, out);
        if (!enc.expr()) return err(enc.error());
        for (;;) {
            Tok t;
            if (!enc.parser().peek(t, true)) return err(enc.error());
            if (t.t == Tok::NUM) { enc.parser().consume(); if (!enc.number(t.num)) return err(enc.error()); continue; }
            if (t.t != Tok::COMMA) break;
            enc.parser().consume();
            out.push_back(0xDE);
            if (!enc.parser().peek(t, true)) return err(enc.error());
            if (t.t == Tok::COMMA || t.t == Tok::END) continue;  // пропущенный аргумент
            if (!enc.expr()) return err(enc.error());
        }
        return true;
    }

    if (lex_.take_word("COPY")) {
        // «Оператор COPY TO предназначен для копирования части или всего
        // диска на другой диск» (руководство, разд. 18.9.6).
        // `COPY T#H4,(H1,H1) TO T#H5,(H1)` = `6D 02 DB 4C DE 4F DE 4F D1
        // 02 DB 0C DE 4F` (LКОПИДИС 1260): `TO` — байт `D1`, скобки вокруг
        // границ не кодируются вовсе, как и у `VERIFY`.
        verb = 0x6D;
        Encoder enc(lex_, out);
        // Устройство источника может отсутствовать: `COPY (100,300) TO R(50)`
        // берёт его из таблицы устройств.
        bool bounds = lex_.take_char('(');
        if (!bounds) {
            if (!disk_prefix(enc, out, true)) return false;
            bounds = lex_.take_char('(');
        }
        if (bounds) {
            for (;;) {
                if (!enc.expr()) return err(enc.error());
                if (!take_sep(enc, Tok::COMMA)) break;
                out.push_back(0xDE);
            }
            take_sep(enc, Tok::RPAR);
        }
        Tok t;
        if (!enc.parser().take(t, false) || t.t != Tok::KW_TO)
            return err("COPY без TO");
        out.push_back(0xD1);

        if (!lex_.take_char('(')) {
            if (!disk_prefix(enc, out, true)) return false;
            if (!lex_.take_char('(')) return true;
        }
        if (!enc.expr()) return err(enc.error());
        take_sep(enc, Tok::RPAR);
        return true;
    }

    if (lex_.take_word("VERIFY")) {
        // `VERIFY T#D%(1),(X(1),Y(1))` = 83 10 02 DB 39 E8 01 D0 DE 16 …
        // (EDITOR 7710): скобки вокруг границ не кодируются вовсе.
        verb = 0x83;
        Encoder enc(lex_, out);
        if (!disk_prefix(enc, out, true)) return false;
        if (lex_.at_end() || lex_.at_colon()) return true;
        lex_.take_char('(');
        for (;;) {
            if (!enc.expr()) return err(enc.error());
            if (!take_sep(enc, Tok::COMMA)) break;
            out.push_back(0xDE);
        }
        take_sep(enc, Tok::RPAR);
        return true;
    }

    if (lex_.take_word("LIST DC")) {
        // `LIST DC R` = `7C 01 01` (STAT03 120) — только буква устройства;
        // но бывает и приставка с адресом, и имя файла:
        // `LIST DC F"CHANAL"` = `7C 09 00 E3 06 …` (CHANAL 1),
        // `LIST DC F/1C,"D0XM"` = `7C 0B 00 DC DE 1C DE E3 04 …` (М3 710).
        verb = 0x7C;
        Encoder enc(lex_, out);
        lex_.skip_spaces();
        const char c = (lex_.pos() < lex_.end()) ? lex_.text()[lex_.pos()] : ' ';
        if (!disk_prefix(enc, out, c == 'F' || c == 'R' || c == 'T'))
            return false;
        if (lex_.at_end() || lex_.at_colon()) return true;
        return enc.expr() ? true : err(enc.error());
    }

    if (lex_.take_word("LOAD DC")) {
        // `LOAD DC F/1C,P¤(E3%)` = 7D 08 00 DC DE 1C DE 51 33 D0 (VICT 7200).
        verb = 0x7D;
        Encoder enc(lex_, out);
        if (!disk_prefix(enc, out, true)) return false;
        if (lex_.at_end() || lex_.at_colon()) return true;
        if (!enc.expr()) return err(enc.error());
        // За именем — до трёх номеров строк: откуда, докуда и куда
        // продолжать исполнение (`ROM` 900, `SL2` 100).
        return line_tail(enc, out);
    }

    // `SAVE DA <буква> [#<строка>], (<сектор> [,<приёмник>])` — разд. 18.9.2.
    // Глагол `73` прочитан в таблице ключевых слов интерпретатора
    // (`docs/format.md`, разд. 4); операнды те же, что у `LOAD DA`, — в
    // корпусе его нет ни разу.
    if (lex_.take_word("SAVE DA")) {
        verb = 0x73;
        Encoder enc(lex_, out);
        lex_.skip_spaces();
        const char c = (lex_.pos() < lex_.end()) ? lex_.text()[lex_.pos()] : ' ';
        if (!disk_prefix(enc, out, c == 'F' || c == 'R' || c == 'T'))
            return false;
        if (!lex_.take_char('(')) return err("SAVE DA без адреса сектора");
        out.push_back(0xEB);
        if (!enc.expr()) return err(enc.error());
        // «Значение этого адреса можно считать в числовую или символьную
        // переменную, которая может быть указана после адреса начального
        // сектора» (разд. 18.9.1).
        if (take_sep(enc, Tok::COMMA)) {
            out.push_back(0xDE);
            if (!enc.lvalue()) return err(enc.error());
        }
        if (!take_sep(enc, Tok::RPAR)) return err("SAVE DA: скобка не закрыта");
        out.push_back(0xD0);
        return true;
    }

    if (lex_.take_word("LOAD DA")) {
        // «LOAD DA <тип диска> <устройство>, (<адрес> [,<переменная>])
        // [<номер строки 1>] [,[<номер строки 2>]] [<номер строки 3>]»
        // (руководство, разд. 19.1). `LL` 51 = `72 02 DB 0C DE EB 2D D0
        // 10 00 DE DE 00 53`, то есть `LOAD DA T#K¤,(S)1000,,53`.
        verb = 0x72;
        Encoder enc(lex_, out);
        lex_.skip_spaces();
        const char c = (lex_.pos() < lex_.end()) ? lex_.text()[lex_.pos()] : ' ';
        if (!disk_prefix(enc, out, c == 'F' || c == 'R' || c == 'T'))
            return false;
        if (!lex_.take_char('(')) return err("LOAD DA без адреса сектора");
        out.push_back(0xEB);
        if (!enc.expr()) return err(enc.error());
        // Приёмник — «адрес первого сектора, не занятого загружаемой
        // программой».
        if (take_sep(enc, Tok::COMMA)) {
            out.push_back(0xDE);
            if (!enc.lvalue()) return err(enc.error());
        }
        if (!take_sep(enc, Tok::RPAR)) return err("LOAD DA: скобка не закрыта");
        out.push_back(0xD0);
        return line_tail(enc, out);
    }

    if (lex_.take_word("SAVE DC")) {
        // `SAVE DC R¤T("VIC")"VIC"` = 80 0F 01 D6 D2 EB … D0 E3 03 …
        // (VICT 45): D2 — ключевое слово T, за ним диапазон в скобках.
        verb = 0x80;
        Encoder enc(lex_, out);
        // Устройства может не быть вовсе: `SAVE DC ("ПРОГ1") "КРУГ"`
        // (пример 5.3). Приставка та же, что у прочих дисковых, — бывает и
        // `/адрес` (`SAVE DC F/1C,("BUKWA")"BUKWA"`), и `#строка`
        // (`SAVE DC T#K¤,(D-3)…` = `80 … 02 DB 5C DE EB …`, LКОПДИСК 2270).
        lex_.skip_spaces();
        const char c = (lex_.pos() < lex_.end()) ? lex_.text()[lex_.pos()] : ' ';
        if (!disk_prefix(enc, out, c == 'F' || c == 'R' || c == 'T'))
            return false;
        // «Если специфицирован параметр Т, программа записывается на диск в
        // оттранслированной форме» (разд. 5.3). Параметры P и G в корпусе
        // не встречаются, и байтов у них нет.
        if (lex_.take_word("T")) out.push_back(0xD2);
        // Скобка от T не зависит: в ней либо имя вычеркнутого файла, на
        // место которого пишем, либо число запасных секторов — руководство,
        // разд. 5.3.
        if (lex_.take_char('(')) {
            out.push_back(0xEB);
            for (;;) {
                if (!enc.expr()) return err(enc.error());
                if (!take_sep(enc, Tok::COMMA)) break;
                out.push_back(0xDE);
            }
            if (!take_sep(enc, Tok::RPAR)) return err("SAVE DC: скобка не закрыта");
            out.push_back(0xD0);
        }
        if (lex_.at_end() || lex_.at_colon()) return true;
        if (!enc.expr()) return err(enc.error());
        return line_tail(enc, out);
    }

    // Обмен программой через символьный буфер: `SAVE Z¤5215,5215` =
    // `2A 07 DD 10 52 15 DE 52 15` (EDITOR 5195), `LOAD` — то же с глаголом
    // 2D (EDITOR 5210). DD — признак этой формы; между буфером и первым
    // номером строки разделителя нет.
    {
        const bool load = lex_.take_word("LOAD");
        if (load || lex_.take_word("SAVE")) {
            verb = load ? 0x2Du : 0x2Au;
            Encoder enc(lex_, out);
            out.push_back(0xDD);
            if (!enc.expr()) return err(enc.error());
            Tok t;
            if (!enc.parser().peek(t, false)) return err(enc.error());
            if (t.t != Tok::NUM) return true;          // буфер без диапазона
            enc.parser().consume();
            for (;;) {
                long ln = 0;
                if (t.t != Tok::NUM || !t.num.to_int(ln) || ln < 0)
                    return err("SAVE/LOAD: неверный номер строки");
                if (!line_number(out, static_cast<unsigned>(ln))) return false;
                if (!take_sep(enc, Tok::COMMA)) break;
                out.push_back(0xDE);
                if (!enc.parser().take(t, true)) return err(enc.error());
            }
            return true;
        }
    }

    if (lex_.take_word("BIN")) {
        // `BIN(<приёмник>[,2])=<а.в.>`: ни скобка, ни знак равенства не
        // кодируются, «,2» даёт пару DE DB (docs/format.md, разд. 5).
        verb = 0x4B;
        Encoder enc(lex_, out);
        if (!lex_.take_char('(')) return err("BIN без скобки");
        if (!enc.lvalue()) return err(enc.error());
        Tok t;
        if (!enc.parser().take(t, false)) return err(enc.error());
        if (t.t == Tok::COMMA) {
            out.push_back(0xDE);
            out.push_back(0xDB);
            if (!enc.parser().take(t, true)) return err(enc.error());
            long v = 0;
            if (t.t != Tok::NUM || !t.num.to_int(v) || v != 2)
                return err("у BIN( второй аргумент может быть только 2");
            if (!enc.parser().take(t, false)) return err(enc.error());
        }
        if (t.t != Tok::RPAR) return err("BIN: скобка не закрыта");
        if (!enc.parser().take(t, false) || t.t != Tok::EQ) return err("BIN без =");
        return enc.expr() ? true : err(enc.error());
    }

    if (lex_.take_word("INIT")) {
        // INIT(<код>)<приёмники> — запятых между приёмниками в потоке нет
        // (docs/format.md, разд. 5).
        verb = 0x64;
        if (!lex_.take_char('(')) return err("INIT без скобки");
        Encoder enc(lex_, out);
        unsigned code = 0;
        if (lex_.take_hex_byte(code)) {
            out.push_back(0xDE);                       // сырой байт
            out.push_back(static_cast<uint8_t>(code));
            if (!lex_.take_char(')')) return err("INIT: скобка не закрыта");
        } else {
            if (!enc.expr()) return err(enc.error());
            Tok t;
            if (!enc.parser().take(t, false) || t.t != Tok::RPAR)
                return err("INIT: скобка не закрыта");
        }
        for (;;) {
            if (!enc.lvalue()) return err(enc.error());
            if (!lex_.take_char(',')) break;
        }
        return true;
    }

    // --- родня MAT: двухбайтовые глаголы 06 <подкод> ------------------------

    if (lex_.take_word("MAT REDIM")) {
        // `E0 <индекс> EB <размерности через DE> D0 [<длина элемента>]`,
        // записи разделяются DE (STAT03 200, EDITOR 101).
        verb = 0x0602;
        Encoder enc(lex_, out);
        for (;;) {
            Tok t;
            if (!enc.parser().take(t, true)) return err(enc.error());
            if (t.t != Tok::VAR || !t.indexed) return err("MAT REDIM без размерностей");
            out.push_back(0xE0);
            out.push_back(static_cast<uint8_t>(t.var));
            out.push_back(0xEB);
            for (;;) {
                if (!enc.expr()) return err(enc.error());
                if (!take_sep(enc, Tok::COMMA)) break;
                out.push_back(0xDE);
            }
            if (!take_sep(enc, Tok::RPAR)) return err("MAT REDIM: скобка не закрыта");
            out.push_back(0xD0);
            // За скобкой у символьного массива стоит длина элемента —
            // числом или выражением (EDITOR 101 и 2850).
            if (!lex_.at_end() && !lex_.at_colon()) {
                if (!enc.parser().peek(t, true)) return err(enc.error());
                if (t.t != Tok::COMMA && !enc.expr()) return err(enc.error());
            }
            if (!take_sep(enc, Tok::COMMA)) break;
            out.push_back(0xDE);
        }
        return true;
    }

    if (lex_.take_word("MAT PRINT")) {
        // «MAT PRINT [/<устройство>,] <имя массива>[,|;] …» (разд. 12.2.1).
        // Запятая за именем значит зонный формат, точка с запятой — плотный:
        // `MAT PRINT A¤();` = `06 05 03 E0 <идекс> DD` (РЕГРЕСС 3195).
        verb = 0x0605;
        Encoder enc(lex_, out);
        if (lex_.take_char('/')) {
            out.push_back(0xDC);
            unsigned a = 0;
            if (!lex_.take_hex2(a, true)) return err("MAT PRINT: нет адреса устройства");
            out.push_back(0xDE);
            out.push_back(static_cast<uint8_t>(a));
            if (lex_.take_char(',')) out.push_back(0xDE);
        }
        for (;;) {
            Tok t;
            lex_.expect_array();
            if (!enc.parser().take(t, true)) return err(enc.error());
            if (t.t != Tok::VAR && t.t != Tok::ARRAY)
                return err("MAT PRINT: ждали массив");
            out.push_back(0xE0);
            out.push_back(static_cast<uint8_t>(t.var));
            if (lex_.take_char(';')) out.push_back(0xDD);
            else if (lex_.take_char(',')) out.push_back(0xDE);
            else return true;
            if (lex_.at_end() || lex_.at_colon()) return true;
        }
    }

    {
        // `MAT READ` и `MAT INPUT` — подкоды `06 03` и `06 04`
        // (docs/format.md, разд. 5, «Подкоды 06 03 и 06 04»). Грамматика у
        // них одна: массив, необязательные новые размерности в скобках и
        // необязательная длина элемента, записи через `DE`.
        const bool mread = lex_.take_word("MAT READ");
        const bool minput = !mread && lex_.take_word("MAT INPUT");
        if (mread || minput) {
            verb = mread ? 0x0603u : 0x0604u;
            Encoder enc(lex_, out);
            for (;;) {
                Tok t;
                // Имя может стоять и без скобок (`MAT READ G¤`, ROM 26), а
                // ключ переменной всё равно массивный.
                lex_.expect_array();
                if (!enc.parser().take(t, true)) return err(enc.error());
                if (t.t != Tok::VAR && t.t != Tok::ARRAY)
                    return err("MAT READ/INPUT: ждали массив");
                out.push_back(0xE0);
                out.push_back(static_cast<uint8_t>(t.var));

                // «<а.в.1>, <а.в.2> — выражения, определяющие новые
                // размерности» (разд. 12.2.2). Их может не быть вовсе.
                if (t.t == Tok::VAR && t.indexed) {
                    out.push_back(0xEB);
                    for (;;) {
                        if (!enc.expr()) return err(enc.error());
                        if (!take_sep(enc, Tok::COMMA)) break;
                        out.push_back(0xDE);
                    }
                    if (!take_sep(enc, Tok::RPAR))
                        return err("MAT READ/INPUT: скобка не закрыта");
                    out.push_back(0xD0);
                    // «<а.в.3> — выражение, определяющее максимальную длину
                    // элемента символьного массива».
                    if (!lex_.at_end() && !lex_.at_colon()) {
                        if (!enc.parser().peek(t, true)) return err(enc.error());
                        if (t.t != Tok::COMMA && !enc.expr()) return err(enc.error());
                    }
                }
                if (!take_sep(enc, Tok::COMMA)) break;
                out.push_back(0xDE);
            }
            return true;
        }
    }

    if (lex_.take_word("MAT SEARCH")) {
        // `<где> DE <знак> <что> D1 <куда> [D2 <шаг>]` (EDITOR 346).
        verb = 0x060A;
        Encoder enc(lex_, out);
        if (!enc.expr()) return err(enc.error());
        if (!take_sep(enc, Tok::COMMA)) return err("MAT SEARCH без запятой");
        out.push_back(0xDE);
        if (!relation(out)) return false;
        if (!enc.expr()) return err(enc.error());
        Tok t;
        if (!enc.parser().take(t, false) || t.t != Tok::KW_TO)
            return err("MAT SEARCH без TO");
        out.push_back(0xD1);
        if (!enc.expr()) return err(enc.error());
        if (take_sep(enc, Tok::KW_STEP)) {
            out.push_back(0xD2);
            if (!enc.expr()) return err(enc.error());
        }
        return true;
    }

    if (lex_.take_word("MAT COPY")) {
        // `<откуда> D1 <куда>`; минус перед источником — часть выражения
        // (EDITOR 1335, 2630).
        verb = 0x0606;
        Encoder enc(lex_, out);
        if (!enc.expr()) return err(enc.error());
        Tok t;
        if (!enc.parser().take(t, false) || t.t != Tok::KW_TO)
            return err("MAT COPY без TO");
        out.push_back(0xD1);
        return enc.expr() ? true : err(enc.error());
    }

    if (lex_.take_word("MAT")) {
        // `MAT <массив>=<что>`; `EF` `ZER` (STAT03 240), `F0` `CON` и
        // `EE` `IDN` — из таблицы ключевых слов интерпретатора
        // (docs/format.md, разд. 4). В корпусе последних двух нет ни разу.
        verb = 0x0601;
        Encoder enc(lex_, out);
        Tok t;
        // Имя без скобок, но переменная это массив (STAT03 240: `MAT Q=ZER`).
        lex_.expect_array();
        if (!enc.parser().take(t, true)) return err(enc.error());
        if (t.t != Tok::VAR && t.t != Tok::ARRAY) return err("MAT без массива");
        out.push_back(0xE0);
        out.push_back(static_cast<uint8_t>(t.var));
        if (!enc.parser().take(t, false) || t.t != Tok::EQ) return err("MAT без =");
        out.push_back(0xD9);
        if (lex_.take_word("ZER")) { out.push_back(0xEF); return true; }
        if (lex_.take_word("CON")) { out.push_back(0xF0); return true; }
        if (lex_.take_word("IDN")) { out.push_back(0xEE); return true; }
        // `TRN` и `INV` берут аргумент в скобках, и как он кодируется —
        // неизвестно: в корпусе их нет, а выдуманная разметка уехала бы на
        // дискету через SAVE DC.
        if (lex_.take_word("TRN") || lex_.take_word("INV"))
            return err("MAT: разметка операндов TRN и INV неизвестна");
        lex_.expect_array();
        if (!enc.parser().take(t, true)) return err(enc.error());
        if (t.t != Tok::VAR && t.t != Tok::ARRAY) return err("MAT: ждали массив");
        out.push_back(0xE0);
        out.push_back(static_cast<uint8_t>(t.var));
        return true;
    }

    if (lex_.take_word("REPLACE")) {
        // Список через DE: счётчик, где искать, что, на что (EDITOR 462).
        verb = 0x0626;
        Encoder enc(lex_, out);
        if (!enc.lvalue()) return err(enc.error());
        while (take_sep(enc, Tok::COMMA)) {
            out.push_back(0xDE);
            if (!enc.expr()) return err(enc.error());
        }
        return true;
    }

    if (lex_.take_word("LINPUT")) {
        // Подсказка, необязательный минус, один приёмник; запятая за
        // подсказкой не кодируется (VICT 5070, 6210).
        verb = 0x0624;
        Encoder enc(lex_, out);
        Tok t;
        if (!enc.parser().peek(t, true)) return err(enc.error());
        if (t.t == Tok::STR) {
            enc.parser().consume();
            enc.literal(t.s);
            lex_.take_char(',');
            if (!enc.parser().peek(t, true)) return err(enc.error());
        }
        if (t.t == Tok::MINUS) { enc.parser().consume(); out.push_back(0xE9); }
        return enc.lvalue() ? true : err(enc.error());
    }

    // `PLOT <x,y,перо>[,<…>]…` — подкод `06 00` (docs/format.md, разд. 5,
    // «Подкод 06 00 — это PLOT»). Группа ограничена `D7` … `D4`, элементы и
    // сами группы разделяются `DE`; любой элемент может быть пуст.
    if (lex_.take_word("PLOT")) {
        verb = 0x0600;
        Encoder enc(lex_, out);
        for (;;) {
            if (!lex_.take_char('<')) return err("PLOT: группа не открыта");
            out.push_back(0xD7);
            unsigned k = 0;
            for (;;) {
                if (lex_.take_char('>')) break;
                if (lex_.take_char(',')) {
                    // Пустой элемент: `PLOT <,,R>` (SLIDE 5650).
                    out.push_back(0xDE);
                    ++k;
                    continue;
                }

                // Третий элемент — перо. Буква там значит метку, а не
                // переменную: у машины та же неоднозначность, и решает её
                // только место в группе.
                unsigned pen = 0;
                if (k != 2 || !lex_.take_pen(pen)) {
                    // Границу элемента ищем сами: `>` внутри выражения —
                    // знак сравнения, и разборщик съел бы вместе с ним
                    // закрывающую скобку группы.
                    const unsigned stop = element_end();
                    const unsigned save = lex_.end();
                    lex_.set_end(stop);
                    const bool ok = enc.expr();
                    enc.parser().reset();
                    lex_.set_end(save);
                    lex_.set_pos(stop);
                    if (!ok) return err(enc.error());
                } else {
                    out.push_back(static_cast<uint8_t>(pen));
                }

                if (lex_.take_char('>')) break;
                if (!lex_.take_char(',')) return err("PLOT: группа не закрыта");
                out.push_back(0xDE);
                ++k;
            }
            out.push_back(0xD4);
            if (!lex_.take_char(',')) return true;
            out.push_back(0xDE);
        }
    }

    // --- операторы со знаком ¤ и графика ------------------------------------

    if (lex_.take_word("$TRAN")) {
        // Скобка не кодируется, аргументы через DE, закрывается D0
        // (EDITOR 290).
        verb = 0x060C;
        Encoder enc(lex_, out);
        if (!lex_.take_char('(')) return err("$TRAN без скобки");
        for (;;) {
            if (!enc.expr()) return err(enc.error());
            if (!take_sep(enc, Tok::COMMA)) break;
            out.push_back(0xDE);
        }
        if (!take_sep(enc, Tok::RPAR)) return err("$TRAN: скобка не закрыта");
        out.push_back(0xD0);
        // За скобкой бывает буква режима: `¤TRAN(Q¤,L0¤)R` кончается на
        // `DE 00` (EDITOR 4760 и 6837). Других букв в корпусе нет, так что
        // какой байт у прочих режимов — неизвестно.
        if (lex_.take_word("R")) {
            out.push_back(0xDE);
            out.push_back(0x00);
        }
        return true;
    }

    if (lex_.take_word("$OPEN")) {
        // Один буфер: `¤OPEN B¤()` = 06 0F 02 E0 1F (VICT 6150). Их бывает
        // и несколько — `¤OPEN A¤(),B¤()` (SIG 7580), — а первый бывает
        // пропущен: `¤OPEN ,B¤()` (SLIDE 220), и операнды тогда начинаются
        // прямо с разделителя.
        verb = 0x060F;
        Encoder enc(lex_, out);
        for (;;) {
            if (lex_.take_char(',')) { out.push_back(0xDE); continue; }
            if (!enc.expr()) return err(enc.error());
            if (!take_sep(enc, Tok::COMMA)) break;
            out.push_back(0xDE);
        }
        return true;
    }

    if (lex_.take_word("$LET")) {
        // `¤LET S¤()=S¤()` = 06 22 05 E0 0C D9 E0 0C (EDITOR 1236).
        verb = 0x0622;
        Encoder enc(lex_, out);
        if (!enc.lvalue()) return err(enc.error());
        Tok t;
        if (!enc.parser().take(t, false) || t.t != Tok::EQ) return err("$LET без =");
        out.push_back(0xD9);
        return enc.expr() ? true : err(enc.error());
    }

    // Графические операторы с однородным списком через DE (VICT 6150,
    // EDITOR 1235).
    {
        static const struct { const char * word; unsigned verb; } PLOT[] = {
            { "NPLOT", 0x0619 }, { "STRETCH", 0x061C },
            { "WINDOW", 0x0623 }, { "DDRAW", 0x0614 },
            { "DRAW", 0x0615 },   { "FRAME", 0x061D },
            { "DOT", 0x0613 },    { "$MOVE", 0x061A },
            { "TURN", 0x061B }
        };
        unsigned found = 0;
        for (unsigned k = 0; !found && k < sizeof(PLOT) / sizeof(PLOT[0]); ++k)
            if (lex_.take_word(PLOT[k].word)) found = PLOT[k].verb;
        if (found) {
            verb = found;
            Encoder enc(lex_, out);
            for (;;) {
                if (!enc.expr()) return err(enc.error());
                if (!take_sep(enc, Tok::COMMA)) break;
                out.push_back(0xDE);
            }
            return true;
        }
    }

    if (lex_.take_word("$GIO")) {
        // Две формы: `'` (текущее устройство, байт D5) и `/адрес,`. Скобки
        // вокруг пары «микропрограмма, буфер» и запятая внутри неё в потоке
        // не кодируются вовсе (VICT 2180, EDITOR 72 и 2511).
        verb = 0x40;
        Encoder enc(lex_, out);
        if (lex_.take_char('/')) {
            unsigned a = 0;
            if (!lex_.take_hex2(a)) return err("$GIO: нет адреса устройства");
            out.push_back(0xDC);
            out.push_back(0xDE);
            out.push_back(static_cast<uint8_t>(a));
            if (lex_.take_char(',')) out.push_back(0xDE);
        }
        lex_.take_char('(');
        if (lex_.take_char(0x27)) out.push_back(0xD5);
        Tok t;
        if (!enc.parser().take(t, true)) return err(enc.error());
        if (t.t != Tok::FN_HEX) return err("$GIO без микропрограммы канала");
        out.push_back(0xE2);
        out.push_back(static_cast<uint8_t>(t.s.size()));
        enc.emit(t.s);
        // Запятая перед буфером не кодируется, буфер необязателен.
        take_sep(enc, Tok::COMMA);
        if (lex_.at_end() || lex_.at_colon()) return true;
        if (take_sep(enc, Tok::RPAR)) return true;
        if (!enc.expr()) return err(enc.error());
        take_sep(enc, Tok::RPAR);
        return true;
    }

    // --- образы печати ------------------------------------------------------

    const bool unpack = lex_.take_word("UNPACK");
    if (unpack || lex_.take_word("PACK")) {
        // PACK(<образ>)<приёмник>FROM<а.в.>, UNPACK(<образ>)<а.в.>TO<приёмники>
        // (EDITOR 344, 378). CA — FROM, D1 — TO.
        const bool un = unpack;
        verb = un ? 0x5Du : 0x48u;
        Encoder enc(lex_, out);
        std::string image;
        if (!lex_.take_image(image)) return err("PACK без образа");
        enc.literal(image);
        if (un) {
            if (!enc.expr()) return err(enc.error());
            Tok t;
            if (!enc.parser().take(t, false) || t.t != Tok::KW_TO)
                return err("UNPACK без TO");
            out.push_back(0xD1);
            for (;;) {
                if (!enc.lvalue()) return err(enc.error());
                if (!take_sep(enc, Tok::COMMA)) break;
                out.push_back(0xDE);
            }
            return true;
        }
        if (!enc.lvalue()) return err(enc.error());
        if (!lex_.take_word("FROM")) return err("PACK без FROM");
        out.push_back(0xCA);
        for (;;) {
            if (!enc.expr()) return err(enc.error());
            if (!take_sep(enc, Tok::COMMA)) break;
            out.push_back(0xDE);
        }
        return true;
    }

    // --- ввод с клавиатуры и данные в тексте программы ----------------------

    if (lex_.take_word("KEYIN")) {
        // Приёмник и два номера строк сырыми парами BCD (EDITOR 242).
        verb = 0x25;
        Encoder enc(lex_, out);
        if (!enc.lvalue()) return err(enc.error());
        for (unsigned k = 0; k < 2; ++k) {
            if (!lex_.take_char(',')) return err("KEYIN без номеров строк");
            unsigned n = 0;
            if (!lex_.take_uint(n)) return err("KEYIN без номеров строк");
            if (!line_number(out, n)) return false;
        }
        return true;
    }

    if (lex_.take_word("RESTORE")) {
        // `[<а.в.>] [DE <номер строки>]`. Парами подтверждена только форма с
        // номером строки (EDITOR 4865, VICT 2190). Две другие книга описывает
        // (разд. 4.9), и `SLIDE` 230 пользуется голым `RESTORE`, но в токенах
        // их нет ни разу: принято, что запятая — тот же `DE`, а чего нет в
        // тексте, того нет и в потоке. См. «Допущения» в CLAUDE.md.
        verb = 0x51;
        Encoder enc(lex_, out);
        if (lex_.at_end() || lex_.at_colon()) return true;
        bool comma = lex_.take_char(',');
        if (!comma) {
            if (!enc.expr()) return err(enc.error());
            comma = take_sep(enc, Tok::COMMA);
        }
        if (!comma) return true;
        out.push_back(0xDE);
        unsigned n = 0;
        if (!lex_.take_uint(n)) return err("RESTORE без номера строки");
        return line_number(out, n);
    }

    // --- дисковые операторы без файла --------------------------------------

    if (lex_.take_word("SCRATCH DISK")) {
        // `[LS=<а.в.>[,]] END=<а.в.>`; 06 — LS, D7 — END (STAT03 140).
        verb = 0x82;
        Encoder enc(lex_, out);
        if (!disk_prefix(enc, out, true)) return false;
        if (lex_.take_word("LS")) {
            out.push_back(0x06);
            if (!lex_.take_char('=')) return err("SCRATCH DISK: LS без =");
            out.push_back(0xD9);
            if (!enc.expr()) return err(enc.error());
            if (take_sep(enc, Tok::COMMA)) out.push_back(0xDE);
        }
        if (!lex_.take_word("END")) return err("SCRATCH DISK без END");
        out.push_back(0xD7);
        if (!lex_.take_char('=')) return err("SCRATCH DISK: END без =");
        out.push_back(0xD9);
        return enc.expr() ? true : err(enc.error());
    }

    if (lex_.take_word("SCRATCH")) {
        // Имена исключаемых файлов через DE (VICT 45, 6358).
        verb = 0x81;
        Encoder enc(lex_, out);
        if (!disk_prefix(enc, out, true)) return false;
        for (;;) {
            if (!enc.expr()) return err(enc.error());
            if (!take_sep(enc, Tok::COMMA)) break;
            out.push_back(0xDE);
        }
        return true;
    }

    if (lex_.take_word("LIMITS")) {
        // Форма 1 начинается с имени файла, форма 2 — сразу с приёмников
        // (руководство, разд. 18.8.3). Разделителей между приёмниками нет.
        verb = 0x7B;
        Encoder enc(lex_, out);
        if (!disk_prefix(enc, out, true)) return false;
        if (at_string_name()) {
            if (!enc.expr()) return err(enc.error());
            if (!take_sep(enc, Tok::COMMA))
                return err("LIMITS: после имени нужна запятая");
        }
        for (;;) {
            if (!enc.lvalue()) return err(enc.error());
            if (!lex_.take_char(',')) break;
        }
        return true;
    }

    if (lex_.take_word("DBACKSPACE") || lex_.take_word("DSKIP")) {
        // D6 — BEG, D7 — END, иначе счётчик; 05 за ним значит «в секторах»
        // (STAT03 230, VICT 6360).
        verb = (lex_.text()[lex_.pos() - 1] == 'E') ? 0x79u : 0x7Au;
        Encoder enc(lex_, out);
        if (!disk_prefix(enc, out, false)) return false;
        if (lex_.take_word("BEG")) { out.push_back(0xD6); return true; }
        if (lex_.take_word("END")) { out.push_back(0xD7); return true; }
        if (!enc.expr()) return err(enc.error());
        if (lex_.take_word("S")) out.push_back(0x05);
        return true;
    }

    // Обмен по адресу: BT — блоками, BA/DA — по секторам. Устройства может
    // не быть вовсе (`DATA SAVE BT /34,W¤`, VICT 2190), номер сектора идёт
    // в скобках (`DATA LOAD DA T#D%(D),(X)V¤()`, EDITOR 1120).
    {
        static const struct { const char * word; uint8_t verb; bool load; } ADDR[] = {
            { "DATA LOAD BT", 0x66, true  }, { "DATA SAVE BT", 0x68, false },
            { "DATA LOAD BA", 0x70, true  }, { "DATA SAVE BA", 0x6E, false },
            { "DATA LOAD DA", 0x71, true  }, { "DATA SAVE DA", 0x6F, false }
        };
        for (unsigned k = 0; k < sizeof(ADDR) / sizeof(ADDR[0]); ++k) {
            if (!lex_.take_word(ADDR[k].word)) continue;
            verb = ADDR[k].verb;
            Encoder enc(lex_, out);
            lex_.skip_spaces();
            const char c = (lex_.pos() < lex_.end()) ? lex_.text()[lex_.pos()] : ' ';
            // У `BT` буквы устройства не бывает вовсе: во всех 793 операторах
            // корпуса приставка это `/адрес` (`DC`) либо `#строка` (`DB`).
            // Без приставки устройство берётся из группы `TAPE` (разд. 11.5),
            // и в потоке тогда просто нет её байтов — как у `RESTORE` без
            // номера строки.
            const bool block = (ADDR[k].verb == 0x66 || ADDR[k].verb == 0x68);
            const bool with_device = !block && c != '/' && c != '#' && c != '$';
            if (!disk_prefix(enc, out, with_device)) return false;
            if (lex_.take_char('(')) {
                out.push_back(0xEB);
                if (!enc.expr()) return err(enc.error());
                // Второй элемент в скобках — приёмник адреса «сектора,
                // следующего за последним использованным» (разд. 18.9.1,
                // `DATA SAVE DA F(P,P) A()`). В корпусе эта форма не
                // встречается: запятая взята той же, что везде, — `DE`.
                if (take_sep(enc, Tok::COMMA)) {
                    out.push_back(0xDE);
                    if (!enc.lvalue()) return err(enc.error());
                }
                if (!take_sep(enc, Tok::RPAR))
                    return err("обмен по адресу: скобка не закрыта");
                out.push_back(0xD0);
            }
            // Признак конца данных: `DATA SAVE DA F(P,P) END` (пример 18.29).
            if (!ADDR[k].load && lex_.take_word("END")) {
                out.push_back(0xD7);
                return true;
            }
            for (;;) {
                if (ADDR[k].load) {
                    // Приёмники идут вплотную, как в DATA LOAD DC.
                    if (!enc.lvalue()) return err(enc.error());
                    if (!take_sep(enc, Tok::COMMA)) break;
                } else {
                    if (!enc.expr()) return err(enc.error());
                    if (!take_sep(enc, Tok::COMMA)) break;
                    out.push_back(0xDE);
                }
            }
            return true;
        }
    }

    // Дисковые операторы: длинные слова раньше коротких.
    {
        static const struct { const char * word; uint8_t verb; int kind; } DISK[] = {
            // kind: 0 — имя файла, 1 — список приёмников, 2 — без операндов,
            //       3 — список выражений, 4 — размер и имя
            { "DATA LOAD DC OPEN", 0x75, 0 },
            { "DATA SAVE DC OPEN", 0x78, 4 },
            { "DATA SAVE DC CLOSE", 0x77, 2 },
            { "DATA SAVE DC END",  0x76, 5 },
            { "DATA LOAD DC",      0x74, 1 },
            { "DATA SAVE DC",      0x76, 3 }
        };
        for (unsigned k = 0; k < sizeof(DISK) / sizeof(DISK[0]); ++k) {
            if (!lex_.take_word(DISK[k].word)) continue;
            verb = DISK[k].verb;
            Encoder enc(lex_, out);
            const int kind = DISK[k].kind;

            if (kind == 5) { out.push_back(0xD7); return true; }   // END
            if (kind == 2) {
                if (!disk_prefix(enc, out, false)) return false;
                // `DATA SAVE DC CLOSE ALL` закрывает все файлы разом
                // (пример 18.19); байт `CB` тот же, что у RETURN CLEAR ALL.
                if (lex_.take_word("ALL")) out.push_back(0xCB);
                return true;
            }
            // Устройство есть только там, где файл ищется по имени.
            if (!disk_prefix(enc, out, kind == 0 || kind == 4)) return false;

            // `DATA SAVE DC #2,CLOSE` — та же форма, что `DATA SAVE DC
            // CLOSE #2`, только приставка стоит раньше слова (пример 18.19).
            if (verb == 0x76 && kind == 3 && lex_.take_word("CLOSE")) {
                verb = 0x77;
                if (lex_.take_word("ALL")) out.push_back(0xCB);
                return true;
            }
            if (kind == 4) {
                if (!lex_.take_char('(')) return err("DATA SAVE DC OPEN без размера");
                out.push_back(0xEB);
                if (!enc.expr()) return err(enc.error());
                Tok t;
                if (!enc.parser().take(t, false) || t.t != Tok::RPAR)
                    return err("DATA SAVE DC OPEN: скобка не закрыта");
                out.push_back(0xD0);
            }
            if (kind == 0 || kind == 4) return enc.expr() ? true : err(enc.error());
            if (kind == 1) {
                // Приёмники идут вплотную, без разделителей.
                for (;;) {
                    if (!enc.lvalue()) return err(enc.error());
                    if (!take_sep(enc, Tok::COMMA)) break;
                }
                return true;
            }
            // kind == 3: список выражений через DE.
            if (lex_.take_word("END")) { out.push_back(0xD7); return true; }
            for (;;) {
                if (!enc.expr()) return err(enc.error());
                if (!take_sep(enc, Tok::COMMA)) break;
                out.push_back(0xDE);
            }
            return true;
        }
    }

    // Плоский DATA — после всех форм `DATA LOAD/SAVE …`.
    if (lex_.take_word("DATA")) {
        // Значения идут вплотную, без разделителей; последние два байта —
        // адрес следующего оператора DATA, машина заполняет его сама
        // (docs/format.md, разд. 5, «Хвост оператора DATA»). У свежей
        // программы там нули, поэтому побайтово сойдётся только последний.
        verb = 0x29;
        Encoder enc(lex_, out);
        for (;;) {
            if (!enc.expr()) return err(enc.error());
            if (!take_sep(enc, Tok::COMMA)) break;
        }
        out.push_back(0x00);
        out.push_back(0x00);
        return true;
    }

    if (lex_.take_word("CONVERT")) {
        // Скобки вокруг образа и запятая перед ним не кодируются
        // (docs/format.md, разд. 7).
        verb = 0x47;
        Encoder enc(lex_, out);
        if (!enc.expr()) return err(enc.error());
        Tok t;
        if (!enc.parser().take(t, false) || t.t != Tok::KW_TO)
            return err("CONVERT без TO");
        out.push_back(0xD1);
        if (!enc.lvalue()) return err(enc.error());
        take_sep(enc, Tok::COMMA);
        if (lex_.at_end() || lex_.at_colon()) return true;
        std::string image;
        if (at_print_image() && lex_.take_image(image)) { enc.literal(image); return true; }
        // Вместо образа может стоять символьное выражение в скобках:
        // `CONVERT 0TOQ¤,(STR(F¤(I),1,LEN(F¤(I))))` (EDITOR 5137) — скобки
        // при этом не кодируются.
        if (!lex_.take_char('(')) return err("CONVERT: непонятный образ");
        if (!enc.expr()) return err(enc.error());
        take_sep(enc, Tok::RPAR);
        return true;
    }

    if (lex_.take_word("SELECT")) {
        // Первый байт записи — код группы устройств, записи разделяются DE
        // (docs/format.md, разд. 4). Порядок проверок важен: длинные слова
        // раньше коротких, иначе DISK разберётся как D.
        verb = 0x54;
        for (;;) {
            unsigned addr = 0, w = 0, row = 0;
            bool disk = false;
            if (lex_.take_char('#')) {
                if (!lex_.take_digit(row) || row > 7) return err("SELECT #: строка 0…7");
                if (!lex_.take_hex2(addr)) return err("SELECT #: нет адреса");
                out.push_back(0x00);
                out.push_back(static_cast<uint8_t>(row));
                out.push_back(static_cast<uint8_t>(addr));
                disk = true;
            } else if (lex_.take_word("DISK")) {
                if (!lex_.take_hex2(addr)) return err("SELECT DISK: нет адреса");
                out.push_back(0x0A);
                out.push_back(static_cast<uint8_t>(addr));
                disk = true;
            } else {
                static const struct { const char * word; uint8_t code; } GROUPS[] = {
                    { "PRINT", 0x07 }, { "PLOT", 0x08 }, { "LIST", 0x06 },
                    { "TAPE",  0x09 }, { "CO",   0x0C }
                };
                bool named = false;
                for (unsigned k = 0; k < sizeof(GROUPS) / sizeof(GROUPS[0]); ++k)
                    if (lex_.take_word(GROUPS[k].word)) {
                        if (!lex_.take_hex2(addr)) return err("SELECT: нет адреса");
                        out.push_back(GROUPS[k].code);
                        out.push_back(static_cast<uint8_t>(addr));
                        named = true;
                        break;
                    }
                if (!named) {
                    // Единицы измерения углов (разд. 4.6). Адреса за ними
                    // не идёт; коды из таблицы ключевых слов интерпретатора.
                    if (lex_.take_word("D")) out.push_back(0x01);
                    else if (lex_.take_word("R")) out.push_back(0x02);
                    else if (lex_.take_word("G")) out.push_back(0x03);
                    else {
                        if (!lex_.take_word("P"))
                            return err("SELECT: неизвестная группа");
                        out.push_back(0x05);
                        unsigned d = 0;
                        if (lex_.take_digit(d))
                            out.push_back(static_cast<uint8_t>(d));
                    }
                }
            }
            if (disk) {
                if (lex_.take_word("F")) out.push_back(0x00);
                else if (lex_.take_word("R")) out.push_back(0x01);
            } else if (lex_.take_char('(')) {
                // Ширина строки — двоичное 16-битное BE, не BCD.
                if (!lex_.take_uint(w)) return err("SELECT: нет ширины строки");
                if (!lex_.take_char(')')) return err("SELECT: не закрыта скобка");
                out.push_back(0xEB);
                out.push_back(static_cast<uint8_t>((w >> 8) & 0xFF));
                out.push_back(static_cast<uint8_t>(w & 0xFF));
            }
            if (!lex_.take_char(',')) break;
            out.push_back(0xDE);
        }
        return true;
    }

    if (lex_.take_word("HEXPRINT")) {
        // Список тот же, что у PRINT: `DE` запятая, `DD` точка с запятой
        // (DISSM 7584 = `50 04 1D 02 D0 DD`, 64 вхождения в корпусе).
        verb = 0x50;
        Encoder enc(lex_, out);
        if (lex_.at_end() || lex_.at_colon()) return true;
        for (;;) {
            if (!enc.expr()) return err(enc.error());
            Tok t;
            if (!enc.parser().peek(t, false)) return err(enc.error());
            if (t.t == Tok::COMMA) { enc.parser().consume(); out.push_back(0xDE); }
            else if (t.t == Tok::SEMI) { enc.parser().consume(); out.push_back(0xDD); }
            else { enc.parser().unpeek(); break; }
            if (lex_.at_end() || lex_.at_colon()) break;
        }
        return true;
    }

    // PRINTUSING раньше PRINT: иначе «USING» разберётся как выражение и
    // наберёт лишних имён — вся нумерация переменных уедет.
    if (lex_.take_word("PRINTUSING")) {
        // Номер строки образа — обычная числовая константа в форме E7,
        // дальше список через DE (STAT03 290, EDITOR 162 и 460).
        verb = 0x28;
        Encoder enc(lex_, out);
        unsigned n = 0;
        if (lex_.take_uint(n)) {
            if (n > 9999) return err("PRINTUSING: номер строки больше 9999");
            out.push_back(0xE7);
            out.push_back(to_bcd(n / 100));
            out.push_back(to_bcd(n % 100));
        } else {
            if (!enc.expr()) return err(enc.error());
        }
        // Разделители те же, что у PRINT: `PRINTUSING 5400,R(I,1),R(I,2);`
        // кончается точкой с запятой (VICT 5360).
        for (;;) {
            Tok t;
            if (!enc.parser().peek(t, false)) return err(enc.error());
            if (t.t == Tok::COMMA) { enc.parser().consume(); out.push_back(0xDE); }
            else if (t.t == Tok::SEMI) { enc.parser().consume(); out.push_back(0xDD); }
            else break;
            if (lex_.at_end() || lex_.at_colon()) break;
            if (!enc.expr()) return err(enc.error());
        }
        return true;
    }

    if (lex_.take_word("PRINT")) {
        verb = 0x4C;
        Encoder enc(lex_, out);
        if (lex_.at_end() || lex_.at_colon()) return true;
        // Зона может быть пустой и в начале: `PRINT ,"…"` = `4C 18 DE E3 15 …`
        // (EDITOR 3231).
        {
            Tok t;
            if (!enc.parser().peek(t, true)) return err(enc.error());
            while (t.t == Tok::COMMA || t.t == Tok::SEMI) {
                enc.parser().consume();
                out.push_back(t.t == Tok::COMMA ? 0xDEu : 0xDDu);
                if (lex_.at_end() || lex_.at_colon()) return true;
                if (!enc.parser().peek(t, true)) return err(enc.error());
            }
        }
        for (;;) {
            if (!enc.expr()) return err(enc.error());
            Tok t;
            if (!enc.parser().peek(t, false)) return err(enc.error());
            if (t.t == Tok::SEMI) { enc.parser().consume(); out.push_back(0xDD); }
            else if (t.t == Tok::COMMA) { enc.parser().consume(); out.push_back(0xDE); }
            else break;
            if (lex_.at_end() || lex_.at_colon()) break;
            // Зона может быть пустой и в середине списка: `PRINT "A",,B`
            // пропускает зону, `PRINT "A",;;C` — просто ничего не двигает.
            // В потоке это два разделителя подряд, и такого в корпусе
            // 1165 мест (`DE DE` 955, `DE DD` 208).
            if (!enc.parser().peek(t, true)) return err(enc.error());
            while (t.t == Tok::COMMA || t.t == Tok::SEMI) {
                enc.parser().consume();
                out.push_back(t.t == Tok::COMMA ? 0xDEu : 0xDDu);
                if (lex_.at_end() || lex_.at_colon()) return true;
                if (!enc.parser().peek(t, true)) return err(enc.error());
            }
        }
        return true;
    }

    if (lex_.take_word("GOTO") || lex_.take_word("GOSUB")) {
        const bool gosub = (lex_.text()[lex_.pos() - 1] == 'B');
        verb = gosub ? 0x22u : 0x21u;
        unsigned n = 0;
        if (!lex_.take_uint(n)) return err("переход без номера строки");
        return line_number(out, n);
    }

    if (lex_.take_word("STOP")) {
        // За STOP может стоять сообщение: `STOP "ВКЛ. ПРИНТЕР !"` =
        // `42 11 E3 0F …` (STAT03 420).
        verb = 0x42;
        if (lex_.at_end() || lex_.at_colon()) return true;
        // `STOP #` = `42 01 DB` — форма, которой книга не описывает вовсе.
        // Байт подтверждён с обеих сторон: в тексте она встречается в `SIG`
        // 8300 и `SLIDE` 4300/8080, в токенах — 13 раз, и оба раза сразу
        // за `ON … GOTO` как страховка от выхода за список. Что именно она
        // делает, неизвестно; исполняется как обычный `STOP`.
        if (lex_.take_char('#')) {
            out.push_back(0xDB);
            return true;
        }
        Encoder enc(lex_, out);
        return enc.expr() ? true : err(enc.error());
    }
    if (lex_.take_word("END"))  { verb = 0x59; return true; }
    // RETURN CLEAR — отдельный глагол, а не RETURN с параметром (EDITOR 100).
    // Команды диалога внутри программы. `CLEAR` — после `RETURN CLEAR`,
    // иначе тот разберётся как `RETURN` плюс отдельный `CLEAR`.
    // Виды: `14` это `P` (за ним бывает диапазон строк, DASB2 790),
    // `11` и `12` — `V` и `N`, без операндов — голый `CLEAR` (UDAW 363).
    if (lex_.take_word("RETURN CLEAR")) {
        verb = 0x30;
        if (lex_.take_word("ALL")) out.push_back(0xCB);
        return true;
    }
    if (lex_.take_word("RETURN")) { verb = 0x5E; return true; }

    if (lex_.take_word("CLEAR")) {
        verb = 0x2C;
        if (lex_.at_end() || lex_.at_colon()) return true;
        if (lex_.take_word("V")) { out.push_back(0x11); return true; }
        if (lex_.take_word("N")) { out.push_back(0x12); return true; }
        if (!lex_.take_word("P")) return err("CLEAR: непонятный вид");
        out.push_back(0x14);
        if (lex_.at_end() || lex_.at_colon()) return true;
        unsigned n = 0;
        if (!lex_.take_uint(n)) return err("CLEAR P: неверный номер строки");
        if (!line_number(out, n)) return false;
        if (!lex_.take_char(',')) return true;
        out.push_back(0xDE);
        if (!lex_.take_uint(n)) return err("CLEAR P: неверный номер строки");
        return line_number(out, n);
    }

    // Плоский LIST — после `LIST DC`. Приставка устройства тут только
    // адресом (`DASB2` 448 = `2E 06 DC DE 05 DE 95 02`), дальше один или
    // два номера строк сырыми парами BCD.
    if (lex_.take_word("LIST")) {
        // `LIST S` — покадровая выдача, и у неё **свой глагол** `33`, а не
        // признак у `2E`: так говорит таблица ключевых слов интерпретатора
        // (`docs/format.md`, разд. 4). Единственное вхождение в корпусе —
        // `L2` 2541 `33 02 60 00`, то есть `LIST S 6000`.
        verb = lex_.take_word("S") ? 0x33 : 0x2E;
        if (lex_.take_char('/')) {
            unsigned addr = 0;
            if (!lex_.take_hex2(addr)) return err("LIST: неверный адрес устройства");
            out.push_back(0xDC);
            out.push_back(0xDE);
            out.push_back(static_cast<uint8_t>(addr));
            if (lex_.take_char(',')) out.push_back(0xDE);
        }
        if (lex_.at_end() || lex_.at_colon()) return true;
        unsigned n = 0;
        if (!lex_.take_uint(n)) return err("LIST: неверный номер строки");
        if (!line_number(out, n)) return false;
        if (!lex_.take_char(',')) return true;
        out.push_back(0xDE);
        if (!lex_.take_uint(n)) return err("LIST: неверный номер строки");
        return line_number(out, n);
    }

    if (lex_.take_word("READ")) {
        // Только приёмники, вплотную (VICT 2200).
        verb = 0x44;
        Encoder enc(lex_, out);
        for (;;) {
            if (!enc.lvalue()) return err(enc.error());
            if (!take_sep(enc, Tok::COMMA)) break;
        }
        return true;
    }

    if (lex_.take_word("RUN")) {
        // Номер строки парой BCD, как у GOTO (EDITOR 30).
        verb = 0x2F;
        if (lex_.at_end() || lex_.at_colon()) return true;
        unsigned n = 0;
        if (!lex_.take_uint(n)) return err("RUN без номера строки");
        return line_number(out, n);
    }

    if (lex_.take_word("ROTATE")) {
        // Необязательное C — байт D4, дальше список в скобках через DE
        // (EDITOR 1315 и 2511).
        verb = 0x4D;
        Encoder enc(lex_, out);
        if (lex_.take_word("C")) out.push_back(0xD4);
        if (!lex_.take_char('(')) return err("ROTATE без скобки");
        out.push_back(0xEB);
        for (;;) {
            if (!enc.expr()) return err(enc.error());
            if (!take_sep(enc, Tok::COMMA)) break;
            out.push_back(0xDE);
        }
        if (!take_sep(enc, Tok::RPAR)) return err("ROTATE: скобка не закрыта");
        out.push_back(0xD0);
        return true;
    }

    // Раньше обычного IF: иначе `END` разберётся как выражение.
    if (lex_.take_word("IF END THEN")) {
        // `IF END THEN <номер строки: 2 байта BCD>` — отдельный глагол
        // (docs/format.md, разд. 5, дополнения к таблице глаголов).
        verb = 0x1E;
        unsigned n = 0;
        if (!lex_.take_uint(n)) return err("IF END THEN без номера строки");
        return line_number(out, n);
    }

    if (lex_.take_word("IF")) {
        verb = 0x24;
        Encoder enc(lex_, out);
        if (!enc.expr()) return err(enc.error());
        Tok t;
        if (!enc.parser().take(t, false) || t.t != Tok::KW_THEN)
            return err("IF без THEN");
        out.push_back(0xD3);
        long n = 0;
        if (!t.num.to_int(n)) return err("IF: неверный номер строки");
        return line_number(out, static_cast<unsigned>(n));
    }

    if (lex_.take_word("FOR")) {
        // Знак '=' после переменной цикла не кодируется.
        verb = 0x57;
        Encoder enc(lex_, out);
        Tok t;
        if (!enc.parser().take(t, true) || t.t != Tok::VAR)
            return err("FOR без переменной");
        out.push_back(static_cast<uint8_t>(t.var));
        if (!enc.parser().take(t, false) || t.t != Tok::EQ) return err("FOR без =");
        if (!enc.expr()) return err(enc.error());
        if (!enc.parser().take(t, false) || t.t != Tok::KW_TO) return err("FOR без TO");
        out.push_back(0xD1);
        if (!enc.expr()) return err(enc.error());
        if (!enc.parser().peek(t, false)) return err(enc.error());
        if (t.t == Tok::KW_STEP) {
            enc.parser().consume();
            out.push_back(0xD2);
            if (!enc.expr()) return err(enc.error());
        }
        return true;
    }

    if (lex_.take_word("NEXT")) {
        verb = 0x52;
        Encoder enc(lex_, out);
        Tok t;
        if (!enc.parser().take(t, true) || t.t != Tok::VAR)
            return err("NEXT без переменной");
        out.push_back(static_cast<uint8_t>(t.var));
        return true;
    }

    // Присваивание — всё остальное, с необязательным LET. Цели идут вплотную,
    // без разделителей, до первого знака равенства.
    const std::string head = leading_word();
    lex_.take_word("LET");
    {
        verb = 0x36;
        Encoder enc(lex_, out);
        for (;;) {
            if (!enc.lvalue()) return err("оператор не реализован: " + head);
            Tok t;
            if (!enc.parser().peek(t, false)) return err(enc.error());
            if (t.t == Tok::COMMA) { enc.parser().consume(); continue; }
            if (t.t != Tok::EQ) return err("оператор не реализован: " + head);
            enc.parser().consume();
            break;
        }
        out.push_back(0xD9);
        if (!enc.expr()) return err(enc.error());
    }
    return true;
}

} // namespace

bool tokenize_line(const std::string & koi8_line, NameTable & names,
                   unsigned & number, std::vector<uint8_t> & body,
                   std::string & error)
{
    body.clear();

    unsigned p = 0;
    const unsigned n = static_cast<unsigned>(koi8_line.size());
    while (p < n && koi8_line[p] == ' ') ++p;
    if (p >= n || !is_digit(koi8_line[p])) { error = "строка без номера"; return false; }
    number = 0;
    while (p < n && is_digit(koi8_line[p])) number = number * 10 + (koi8_line[p++] - '0');
    if (number > 9999) { error = "номер строки больше 9999"; return false; }

    const unsigned names_before = names.count();
    TextLexer lex(koi8_line, p, n, names);
    StmtEncoder se(lex);
    for (;;) {
        unsigned verb = 0;
        std::vector<uint8_t> ops;
        bool done = false;
        if (!se.encode(verb, ops, done)) {
            error = se.error();
            names.truncate(names_before);
            return false;
        }
        if (done) break;
        if (ops.size() > 255) {
            error = "оператор длиннее 255 байт";
            names.truncate(names_before);
            return false;
        }
        // Двухбайтовый глагол: `06 <подкод>` перед длиной (разд. 4).
        if (verb > 0xFF) body.push_back(static_cast<uint8_t>((verb >> 8) & 0xFF));
        body.push_back(static_cast<uint8_t>(verb));
        body.push_back(static_cast<uint8_t>(ops.size()));
        body.insert(body.end(), ops.begin(), ops.end());

        if (lex.at_end()) break;
        if (!lex.at_colon()) {
            // Что именно осталось — видно сразу, без этого причина неясна.
            std::string tail;
            for (unsigned i = lex.pos(); i < lex.end() && tail.size() < 12; ++i)
                tail += koi8_line[i];
            error = "лишний текст после оператора: " + tail;
            names.truncate(names_before);
            return false;
        }
        lex.set_pos(lex.pos() + 1);
        // Двоеточие в конце строки — пустого оператора за ним нет.
        if (lex.at_end()) break;
    }
    if (body.empty()) {
        error = "пустая строка";
        names.truncate(names_before);
        return false;
    }
    return true;
}

bool tokenize(const std::string & koi8, ProgramImage & out, NameTable & names,
              std::string & error)
{
    out.clear();
    unsigned p = 0;
    const unsigned n = static_cast<unsigned>(koi8.size());
    while (p < n) {
        unsigned e = p;
        while (e < n && !is_break(koi8[e])) ++e;
        const std::string line = koi8.substr(p, e - p);
        p = e;
        while (p < n && is_break(koi8[p])) ++p;

        bool blank = true;
        for (std::size_t i = 0; i < line.size(); ++i)
            if (line[i] != ' ') { blank = false; break; }
        if (blank) continue;

        unsigned number = 0;
        std::vector<uint8_t> body;
        std::string err;
        if (!tokenize_line(line, names, number, body, err)) {
            error = "строка «" + line.substr(0, 40) + "»: " + err;
            return false;
        }
        out.put_line(number, body.empty() ? 0 : &body[0],
                     static_cast<unsigned>(body.size()));
    }
    // Образ, собранный из текста, должен знать переменные так же, как знает
    // их образ, прочитанный с дискеты: там они берутся из таблиц файла, тут —
    // из таблицы имён. Сами байты таблиц пока не выписываются, они нужны
    // только для SAVE DC.
    out.vars() = names.vars();
    // Таблицы переменных собираются из описаний: без них образ нельзя
    // записать на диск оператором SAVE DC — размеры массивов и длины строк
    // хранятся только там (docs/format.md, разд. 6).
    out.rebuild_tables();
    return true;
}

} // namespace iskra