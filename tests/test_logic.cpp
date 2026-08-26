// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Mikhail Revzin <p3.141592653589793238462643@gmail.com>
// Part of the Iskra-226 project: https://github.com/Ptr314/Iskra-226
// Description: связки условий AND, OR и XOR

#include <cstdio>
#include <string>
#include <vector>

#include "check.h"
#include "core/names.h"
#include "core/tokenize.h"
#include "core/interp.h"
#include "core/koi8.h"
#include "host_headless/headless_host.h"

using namespace iskra;

namespace {

bool run_text(const char * utf8, std::string & screen, std::string & error)
{
    std::string koi8;
    utf8_to_koi8(utf8, koi8);

    NameTable names;
    // Текст исполняется не сам по себе: он сначала транслируется в токены,
    // как и в машине (docs/DECISIONS.md, разд. 12).
    ProgramImage img;
    if (!tokenize(koi8, img, names, error)) return false;

    HeadlessHost host;
    Interp interp(img, host);
    if (!interp.run(error)) return false;
    screen = host.dump();
    return true;
}

std::string line_of(const std::string & screen, unsigned n)
{
    std::size_t p = 0;
    for (unsigned i = 1; i < n; ++i) {
        const std::size_t e = screen.find('\n', p);
        if (e == std::string::npos) return std::string();
        p = e + 1;
    }
    const std::size_t e = screen.find('\n', p);
    return screen.substr(p, e - p);
}

// Печатает 1, если условие выполнилось, иначе 0.
std::string cond(const char * expr)
{
    std::string src = "10 IF ";
    src += expr;
    src += "THEN 100\n20 PRINT \"0\"\n30 STOP\n100 PRINT \"1\"\n";

    std::string screen, error;
    if (!run_text(src.c_str(), screen, error)) {
        std::printf("  %s\n", error.c_str());
        return "ОШИБКА";
    }
    return line_of(screen, 1);
}

// «Если два отношения объединены связкой AND, условие выполняется, когда
// выполняются оба; OR — когда хотя бы одно; XOR — когда одно, но не оба»
// (руководство, разд. 4.5).
void test_truth_tables()
{
    CHECK_STR(cond("1=1 AND 1=1 "), "1");
    CHECK_STR(cond("1=1 AND 1=2 "), "0");
    CHECK_STR(cond("1=2 AND 1=1 "), "0");
    CHECK_STR(cond("1=2 AND 1=2 "), "0");

    CHECK_STR(cond("1=1 OR 1=1 "), "1");
    CHECK_STR(cond("1=1 OR 1=2 "), "1");
    CHECK_STR(cond("1=2 OR 1=1 "), "1");
    CHECK_STR(cond("1=2 OR 1=2 "), "0");

    // Связка `XOR` — байт `E5`, прочитан в таблице ключевых слов
    // интерпретатора (docs/format.md, разд. 4). В корпусе она не
    // встречается ни разу, так что таблица истинности — единственная
    // проверка, какая тут возможна.
    CHECK_STR(cond("1=1 XOR 1=1 "), "0");
    CHECK_STR(cond("1=1 XOR 1=2 "), "1");
    CHECK_STR(cond("1=2 XOR 1=1 "), "1");
    CHECK_STR(cond("1=2 XOR 1=2 "), "0");
}

// Главное отличие от привычных языков: связки равноправны и вычисляются
// слева направо (руководство, разд. 4.5). `A OR B AND C` — это
// `(A OR B) AND C`, а не `A OR (B AND C)`.
void test_left_to_right()
{
    // При обычном старшинстве было бы «истина», при левой свёртке — «ложь».
    CHECK_STR(cond("1=1 OR 1=2 AND 1=2 "), "0");
    // И наоборот.
    CHECK_STR(cond("1=2 AND 1=2 OR 1=1 "), "1");

    // Пример из книги, разд. 4.5: четыре отношения подряд.
    CHECK_STR(cond("1=2 OR 2=3 OR 3=4 OR 4=4 "), "1");
    CHECK_STR(cond("1=2 OR 2=3 OR 3=4 OR 4=5 "), "0");
}

// Связки соединяют отношения, а те считаются до них.
void test_below_comparison()
{
    CHECK_STR(cond("2+2=4 AND 3*3=9 "), "1");
    CHECK_STR(cond("2+2=5 AND 3*3=9 "), "0");
}

void test_strings()
{
    const char * src =
        "10 DIM A$4,B$4\n"
        "20 A$=\"DA\"\n"
        "30 B$=\"NET\"\n"
        "40 IF A$=\"DA\" AND B$=\"NET\" THEN 100\n"
        "50 PRINT \"0\"\n"
        "60 STOP\n"
        "100 PRINT \"1\"\n";

    std::string screen, error;
    if (!run_text(src, screen, error)) { std::printf("  %s\n", error.c_str()); CHECK(false); return; }
    CHECK_STR(line_of(screen, 1), "1");
}

// --- оттранслированная форма ----------------------------------------------

class TokenBuilder
{
public:
    void add_numeric_var() { flags_.push_back(0x10); }

    void add_line(unsigned number, const int * body, unsigned n)
    {
        if (!lines_.empty()) lines_.push_back(0xFE);
        lines_.push_back(static_cast<uint8_t>(((number / 1000) % 10) * 16
                                              + (number / 100) % 10));
        lines_.push_back(static_cast<uint8_t>(((number / 10) % 10) * 16 + number % 10));
        lines_.push_back(static_cast<uint8_t>(n + 1));
        for (unsigned i = 0; i < n; ++i) lines_.push_back(static_cast<uint8_t>(body[i]));
    }

    std::vector<uint8_t> file() const
    {
        std::vector<uint8_t> stream;
        const unsigned L2 = static_cast<unsigned>(flags_.size()) * 4;
        stream.push_back(0); stream.push_back(0);
        stream.push_back(static_cast<uint8_t>(L2 >> 8));
        stream.push_back(static_cast<uint8_t>(L2 & 0xFF));
        stream.push_back(0); stream.push_back(0);
        for (unsigned i = static_cast<unsigned>(flags_.size()); i-- > 0; ) {
            stream.push_back(0); stream.push_back(0);
            stream.push_back(flags_[i]);
            stream.push_back(0);
        }
        stream.insert(stream.end(), lines_.begin(), lines_.end());

        std::vector<uint8_t> file(256, 0);
        file[0] = 1;
        file[9] = 0x21;
        for (std::size_t p = 0; p < stream.size(); p += 254) {
            file.push_back(0x00);
            file.push_back(0x80);
            for (unsigned i = 0; i < 254; ++i)
                file.push_back(p + i < stream.size() ? stream[p + i] : 0);
        }
        return file;
    }

private:
    std::vector<uint8_t> flags_;
    std::vector<uint8_t> lines_;
};

bool run_tokens(const TokenBuilder & b, std::string & screen, std::string & error)
{
    ProgramImage img;
    if (!img.load_file(b.file(), error)) return false;

    HeadlessHost host;
    Interp interp(img, host);
    if (!interp.run(error)) return false;
    screen = host.dump();
    return true;
}

// Байты взяты с настоящей строки EDITOR 1360:
//     IF C>0ANDA5<2820THEN1350
//     24 | 5E D4 E8 00 E7 5D D7 E7 28 20 D3 13 50
// В одном операторе E7 встречается дважды: сначала как AND в позиции
// операции, потом как двухбайтовая константа 2820 в позиции операнда.
void test_tokens_two_valued()
{
    TokenBuilder b;
    b.add_numeric_var();                                    // 00: C
    b.add_numeric_var();                                    // 01: A5

    static const int l10[] = { 0x36, 0x04, 0x00, 0xD9, 0xE8, 0x05 };   // C=5
    b.add_line(10, l10, 6);
    static const int l20[] = { 0x36, 0x05, 0x01, 0xD9, 0xE7, 0x01, 0x00 };  // A5=100
    b.add_line(20, l20, 7);
    // IF C>0 AND A5<2820 THEN 100
    static const int l30[] = { 0x24, 0x0D, 0x00, 0xD4, 0xE8, 0x00, 0xE7,
                               0x01, 0xD7, 0xE7, 0x28, 0x20, 0xD3, 0x01, 0x00 };
    b.add_line(30, l30, 15);
    static const int l40[] = { 0x4C, 0x03, 0xE3, 0x01, '0' };
    b.add_line(40, l40, 5);
    static const int l50[] = { 0x42, 0x00 };
    b.add_line(50, l50, 2);
    static const int l100[] = { 0x4C, 0x03, 0xE3, 0x01, '1' };
    b.add_line(100, l100, 5);

    std::string tokens, error;
    if (!run_tokens(b, tokens, error)) { std::printf("  %s\n", error.c_str()); CHECK(false); return; }
    CHECK_STR(line_of(tokens, 1), "1");

    const char * src =
        "10 C=5\n"
        "20 A5=100\n"
        "30 IF C>0 AND A5<2820 THEN 100\n"
        "40 PRINT \"0\"\n"
        "50 STOP\n"
        "100 PRINT \"1\"\n";
    std::string text;
    if (!run_text(src, text, error)) { std::printf("  %s\n", error.c_str()); CHECK(false); return; }
    CHECK_STR(tokens, text);
}

// EDITOR 360: IF T%<1ORT%>40THEN362 — тот же приём для OR.
void test_tokens_or()
{
    TokenBuilder b;
    b.add_numeric_var();                                    // 00: T%

    static const int l10[] = { 0x36, 0x04, 0x00, 0xD9, 0xE8, 0x50 };   // T%=50
    b.add_line(10, l10, 6);
    // IF T%<1 OR T%>40 THEN 100
    static const int l20[] = { 0x24, 0x0C, 0x00, 0xD7, 0xE8, 0x01, 0xE6,
                               0x00, 0xD4, 0xE8, 0x40, 0xD3, 0x01, 0x00 };
    b.add_line(20, l20, 14);
    static const int l30[] = { 0x4C, 0x03, 0xE3, 0x01, '0' };
    b.add_line(30, l30, 5);
    static const int l40[] = { 0x42, 0x00 };
    b.add_line(40, l40, 2);
    static const int l100[] = { 0x4C, 0x03, 0xE3, 0x01, '1' };
    b.add_line(100, l100, 5);

    std::string tokens, error;
    if (!run_tokens(b, tokens, error)) { std::printf("  %s\n", error.c_str()); CHECK(false); return; }
    CHECK_STR(line_of(tokens, 1), "1");                     // 50 > 40

    const char * src =
        "10 T%=50\n"
        "20 IF T%<1 OR T%>40 THEN 100\n"
        "30 PRINT \"0\"\n"
        "40 STOP\n"
        "100 PRINT \"1\"\n";
    std::string text;
    if (!run_text(src, text, error)) { std::printf("  %s\n", error.c_str()); CHECK(false); return; }
    CHECK_STR(tokens, text);
}

} // namespace

int main()
{
    test_truth_tables();
    test_left_to_right();
    test_below_comparison();
    test_strings();
    test_tokens_two_valued();
    test_tokens_or();
    return test::summary("связки условий");
}