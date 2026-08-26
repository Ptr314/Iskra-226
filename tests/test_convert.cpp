// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Mikhail Revzin <p3.141592653589793238462643@gmail.com>
// Part of the Iskra-226 project: https://github.com/Ptr314/Iskra-226
// Description: CONVERT, LINPUT и MAT REDIM

#include <cstdio>
#include <string>
#include <vector>

#include "check.h"
#include "core/names.h"
#include "core/tokenize.h"
#include "core/interp.h"
#include "core/keys.h"
#include "core/koi8.h"
#include "host_headless/headless_host.h"

using namespace iskra;

namespace {

bool run_program(ProgramImage & img, const char * input, std::string & screen,
                 std::string & error)
{
    HeadlessHost host;
    if (input) {
        std::string keys;
        utf8_to_koi8(input, keys);
        // Конец набора в сценариях пишется как `\\r`, а у клавиши
        // CR/LF свой код (`core/keys.h`).
        for (std::size_t i = 0; i < keys.size(); ++i)
            if (keys[i] == '\r') keys[i] = static_cast<char>(KEY_CR);
        host.feed_keys(reinterpret_cast<const uint8_t *>(keys.data()),
                       static_cast<unsigned>(keys.size()));
    }
    Interp interp(img, host);
    if (!interp.run(error)) return false;
    screen = host.dump();
    return true;
}

bool run_text(const char * utf8_source, std::string & screen, std::string & error,
              const char * input = 0)
{
    std::string koi8;
    utf8_to_koi8(utf8_source, koi8);

    NameTable names;
    // Текст исполняется не сам по себе: он сначала транслируется в токены,
    // как и в машине (docs/DECISIONS.md, разд. 12).
    ProgramImage img;
    if (!tokenize(koi8, img, names, error)) return false;
    return run_program(img, input, screen, error);
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

// --- CONVERT ---------------------------------------------------------------

// Разд. 13.6: символьная запись числа в числовое значение.
void test_to_number()
{
    std::string screen, error;
    const char * src =
        "10 A¤=\"1200.50\"\n"
        "20 CONVERT A¤ TO A\n"
        "30 PRINT A;2*A\n"
        "40 B¤=\"-123.05E+14\"\n"
        "50 CONVERT B¤ TO X\n"
        "60 PRINT X\n";
    if (!run_text(src, screen, error)) { std::printf("  %s\n", error.c_str()); CHECK(false); return; }

    // В книге на этой строке напечатано 2400, но 2 x 1200.5 это 2401;
    // считаем распознавание скана неточным, а арифметику верной.
    CHECK_STR(line_of(screen, 1), " 1200.5  2401");
    CHECK_STR(line_of(screen, 2), "-1.23050000E+16");

    // Не число — ошибка, а не молчаливый ноль.
    CHECK(!run_text("10 A¤=\"XYZ\"\n20 CONVERT A¤ TO A\n", screen, error));
}

// Разд. 13.6: примеры образов, все из руководства.
void test_images()
{
    std::string screen, error;
    const char * src =
        "10 A=12.196\n"
        "20 CONVERT A TO A¤,(###)\n"
        "30 PRINT A¤\n"
        "40 CONVERT A TO A¤,(+####.####)\n"
        "50 PRINT A¤\n"
        "60 CONVERT A TO A¤,(##.##)\n"
        "70 PRINT A¤\n"
        "80 CONVERT A+.005 TO A¤,(##.##)\n"
        "90 PRINT A¤\n"
        "100 CONVERT A TO A¤,(-#.#^^^^)\n"
        "110 PRINT A¤\n"
        "120 CONVERT -A/100 TO A¤,(-#.#^^^^)\n"
        "130 PRINT A¤\n";
    if (!run_text(src, screen, error)) { std::printf("  %s\n", error.c_str()); CHECK(false); return; }

    CHECK_STR(line_of(screen, 1), "012");
    CHECK_STR(line_of(screen, 2), "+0012.1960");
    // «Младшие разряды, выходящие за пределы формата, отбрасываются» —
    // не округляются, поэтому 12.19, а с добавленной половиной разряда 12.20.
    CHECK_STR(line_of(screen, 3), "12.19");
    CHECK_STR(line_of(screen, 4), "12.20");
    // Знак «минус» в образе означает пробел у неотрицательных.
    CHECK_STR(line_of(screen, 5), " 1.2E+01");
    CHECK_STR(line_of(screen, 6), "-1.2E-01");
}

void test_image_overflow()
{
    // «Если количество целых цифр числа больше заданного формата,
    // выдается сообщение об ошибке».
    std::string screen, error;
    CHECK(!run_text("10 A=1234\n20 CONVERT A TO A¤,(##)\n", screen, error));
    CHECK(error.find("не помещается") != std::string::npos);
}

void test_convert_into_substring()
{
    // Форма из корпуса (BAM*): приёмник — часть строки, образа нет.
    std::string screen, error;
    const char * src =
        "10 A¤=\"DIM J¤(    )\"\n"
        "20 N=12\n"
        "30 CONVERT N TO STR(A¤,8,4)\n"
        "40 PRINT A¤\n";
    if (!run_text(src, screen, error)) { std::printf("  %s\n", error.c_str()); CHECK(false); return; }
    CHECK_STR(line_of(screen, 1), "DIM J¤(  12)");
}

// --- LINPUT -----------------------------------------------------------------

void test_linput()
{
    std::string screen, error;
    const char * src =
        "10 LINPUT \"IMYA\",T¤\n"
        "20 PRINT LEN(T¤)\n";
    if (!run_text(src, screen, error, "PETROV, I.\r")) {
        std::printf("  %s\n", error.c_str());
        CHECK(false);
        return;
    }
    // Строка принимается целиком: запятая в ней не разделитель.
    CHECK_STR(line_of(screen, 2), " 10");
}

// --- MAT REDIM --------------------------------------------------------------

void test_redim()
{
    std::string screen, error;
    const char * src =
        "10 DIM Q(2,2)\n"
        "20 N=4\n"
        "30 MAT REDIM Q(N,3)\n"
        "40 Q(4,3)=77\n"
        "50 PRINT Q(4,3)\n";
    if (!run_text(src, screen, error)) { std::printf("  %s\n", error.c_str()); CHECK(false); return; }
    CHECK_STR(line_of(screen, 1), " 77");

    // Прежние границы после переопределения больше не действуют.
    const char * src2 =
        "10 DIM Q(2)\n"
        "20 Q(3)=1\n";
    CHECK(!run_text(src2, screen, error));
}

// --- Оттранслированная форма ------------------------------------------------

// У токенов своя кодировка: скобки вокруг образа CONVERT не пишутся,
// запятая перед ним неявная, а двухбайтовый глагол занимает 06 <подкод>.
class TokenBuilder
{
public:
    void add_numeric_var() { flags_.push_back(0x10); }

    void add_numeric_array(unsigned elements)
    {
        flags_.push_back(0x11);                 // действительная + дескриптор
        push_t1(elements, 16);
    }

    void add_string_var(unsigned elements, unsigned elem_len)
    {
        flags_.push_back(0x21);                 // символьная + дескриптор
        push_t1(elements, elem_len);
    }

    void add_line(unsigned number, const int * body, unsigned n)
    {
        if (!lines_.empty()) lines_.push_back(0xFE);
        lines_.push_back(static_cast<uint8_t>(((number / 1000) % 10) * 16 + (number / 100) % 10));
        lines_.push_back(static_cast<uint8_t>(((number / 10) % 10) * 16 + number % 10));
        lines_.push_back(static_cast<uint8_t>(n + 1));
        for (unsigned i = 0; i < n; ++i) lines_.push_back(static_cast<uint8_t>(body[i]));
    }

    std::vector<uint8_t> file() const
    {
        std::vector<uint8_t> stream;
        const unsigned L1 = static_cast<unsigned>(t1_.size());
        const unsigned L2 = static_cast<unsigned>(flags_.size()) * 4;

        stream.push_back(L1 >> 8); stream.push_back(L1 & 0xFF);
        stream.push_back(L2 >> 8); stream.push_back(L2 & 0xFF);
        stream.push_back(0); stream.push_back(0);
        stream.insert(stream.end(), t1_.begin(), t1_.end());
        // Записи таблиц 2/3 идут в порядке убывания индекса переменной.
        for (unsigned i = flags_.size(); i-- > 0; ) {
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
    void push_t1(unsigned elements, unsigned elem_len)
    {
        t1_.push_back(0x00); t1_.push_back(0x00);              // адрес
        t1_.push_back(0x00); t1_.push_back(0x08);              // одномерная
        t1_.push_back(elements & 0xFF); t1_.push_back(elements >> 8);
        const unsigned code = elem_len * 2 + 1;
        t1_.push_back(code & 0xFF); t1_.push_back(code >> 8);
    }

    std::vector<uint8_t> t1_;
    std::vector<uint8_t> flags_;
    std::vector<uint8_t> lines_;
};

// A=12 : CONVERT A TO A¤,(###) : PRINT A¤
void test_tokenized_convert()
{
    TokenBuilder b;
    b.add_numeric_var();                         // переменная 0: A
    b.add_string_var(1, 16);                     // переменная 1: A¤

    static const int l10[] = { 0x36, 0x04, 0x00, 0xD9, 0xE8, 0x12 };   // A=12
    b.add_line(10, l10, 6);

    // 47 08 | 00 D1 01 E3 03 '###'  — ни скобок вокруг образа, ни запятой
    static const int l20[] = { 0x47, 0x08, 0x00, 0xD1, 0x01,
                               0xE3, 0x03, 0x23, 0x23, 0x23 };
    b.add_line(20, l20, 10);

    static const int l30[] = { 0x4C, 0x01, 0x01 };                     // PRINT A¤
    b.add_line(30, l30, 3);

    ProgramImage img;
    std::string error;
    if (!img.load_file(b.file(), error)) {
        std::printf("  разбор: %s\n", error.c_str());
        CHECK(false);
        return;
    }
    CHECK_EQ(img.line_count(), 3u);

    std::string screen;
    if (!run_program(img, 0, screen, error)) {
        std::printf("  исполнение: %s\n", error.c_str());
        CHECK(false);
        return;
    }
    CHECK_STR(line_of(screen, 1), "012");
}

// MAT REDIM Q(5) : Q(5)=7 : PRINT Q(5)
//
// Проверяет заодно и разбор двухбайтового глагола: 06 <подкод> <длина>.
void test_tokenized_redim()
{
    TokenBuilder b;
    b.add_numeric_array(2);                      // переменная 0: Q(2)

    // 06 02 06 | E0 00 EB E8 05 D0   — MAT REDIM Q(5)
    static const int l10[] = { 0x06, 0x02, 0x06, 0xE0, 0x00, 0xEB, 0xE8, 0x05, 0xD0 };
    b.add_line(10, l10, 9);

    // 36 07 | 00 E8 05 D0 D9 E8 07   — Q(5)=7
    static const int l20[] = { 0x36, 0x07, 0x00, 0xE8, 0x05, 0xD0, 0xD9, 0xE8, 0x07 };
    b.add_line(20, l20, 9);

    // 4C 04 | 00 E8 05 D0            — PRINT Q(5)
    static const int l30[] = { 0x4C, 0x04, 0x00, 0xE8, 0x05, 0xD0 };
    b.add_line(30, l30, 6);

    ProgramImage img;
    std::string error;
    if (!img.load_file(b.file(), error)) {
        std::printf("  разбор: %s\n", error.c_str());
        CHECK(false);
        return;
    }
    CHECK_EQ(img.line_count(), 3u);

    std::string screen;
    if (!run_program(img, 0, screen, error)) {
        std::printf("  исполнение: %s\n", error.c_str());
        CHECK(false);
        return;
    }
    CHECK_STR(line_of(screen, 1), " 7");
}

} // namespace

int main()
{
    test_to_number();
    test_images();
    test_image_overflow();
    test_convert_into_substring();
    test_linput();
    test_redim();
    test_tokenized_convert();
    test_tokenized_redim();
    return test::summary("CONVERT, LINPUT и MAT REDIM");
}