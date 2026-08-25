// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Mikhail Revzin <p3.141592653589793238462643@gmail.com>
// Part of the Iskra-226 project: https://github.com/Ptr314/Iskra-226
// Description: копирование диска на диск — оператор COPY TO

#include <cstdio>
#include <string>
#include <vector>

#include "check.h"
#include "core/host.h"
#include "core/interp.h"
#include "core/koi8.h"
#include "core/names.h"
#include "core/tokenize.h"
#include "host_headless/headless_host.h"

using namespace iskra;

namespace {

const unsigned SEC = Host::SECTOR_SIZE;
const unsigned SECTORS = 8;              // маленький «диск»: считать проще

// Образ, у которого каждый сектор заполнен своим байтом: так видно, что и
// куда переехало.
std::vector<uint8_t> disk(uint8_t base)
{
    std::vector<uint8_t> d(static_cast<std::size_t>(SECTORS) * SEC, 0);
    for (unsigned s = 0; s < SECTORS; ++s)
        for (unsigned i = 0; i < SEC; ++i)
            d[static_cast<std::size_t>(s) * SEC + i] =
                static_cast<uint8_t>(base + s);
    return d;
}

uint8_t at(const std::vector<uint8_t> & d, unsigned sector)
{
    return d[static_cast<std::size_t>(sector) * SEC];
}

bool run(HeadlessHost & host, const char * utf8, std::string & error)
{
    std::string koi8;
    utf8_to_koi8(utf8, koi8);
    NameTable names;
    ProgramImage img;
    if (!tokenize(koi8, img, names, error)) return false;
    Interp interp(img, host);
    return interp.run(error);
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

// --- исполнение -------------------------------------------------------------

// «Если эти адреса не указаны, то копируется весь диск полностью»
// (руководство, разд. 18.9.6). Устройства — буквы F и R при адресе 18
// строки #0 таблицы устройств.
void test_whole_disk()
{
    HeadlessHost host;
    host.mount(0, disk(0x10));
    host.mount(1, disk(0x80));
    std::string error;
    if (!run(host, "10 COPY F TO R\n", error))
        { std::printf("  %s\n", error.c_str()); CHECK(false); return; }

    for (unsigned s = 0; s < SECTORS; ++s)
        CHECK_EQ(at(host.image(1), s), static_cast<uint8_t>(0x10 + s));
    // Источник не тронут.
    CHECK_EQ(at(host.image(0), 0), 0x10u);
}

// «COPY (100, 300) TO R (50) — содержимое 100-го сектора левого диска
// копируется в 50-й правого, 101-го — в 51-й и т. д.»
void test_range_with_offset()
{
    HeadlessHost host;
    host.mount(0, disk(0x10));
    host.mount(1, disk(0x80));
    std::string error;
    if (!run(host, "10 COPY F(2,4) TO R(5)\n", error))
        { std::printf("  %s\n", error.c_str()); CHECK(false); return; }

    CHECK_EQ(at(host.image(1), 5), 0x12u);
    CHECK_EQ(at(host.image(1), 6), 0x13u);
    CHECK_EQ(at(host.image(1), 7), 0x14u);
    // Всё, что вне области, осталось прежним.
    CHECK_EQ(at(host.image(1), 4), 0x84u);
    CHECK_EQ(at(host.image(1), 0), 0x80u);
}

// Границы — выражения: «адреса граничных секторов могут задаваться в виде
// выражений».
void test_bounds_are_expressions()
{
    HeadlessHost host;
    host.mount(0, disk(0x10));
    host.mount(1, disk(0x80));
    std::string error;
    const char * src =
        "10 A=1\n"
        "20 COPY F(A,A+1) TO R(A*2)\n";
    if (!run(host, src, error))
        { std::printf("  %s\n", error.c_str()); CHECK(false); return; }
    CHECK_EQ(at(host.image(1), 2), 0x11u);
    CHECK_EQ(at(host.image(1), 3), 0x12u);
}

// Без адреса приёмника сектор ложится на своё же место — книга такой формы
// не разбирает, принято по смыслу «копируется весь диск».
void test_no_destination_address()
{
    HeadlessHost host;
    host.mount(0, disk(0x10));
    host.mount(1, disk(0x80));
    std::string error;
    if (!run(host, "10 COPY F(3,4) TO R\n", error))
        { std::printf("  %s\n", error.c_str()); CHECK(false); return; }
    CHECK_EQ(at(host.image(1), 3), 0x13u);
    CHECK_EQ(at(host.image(1), 4), 0x14u);
    CHECK_EQ(at(host.image(1), 2), 0x82u);
}

// Тот же дисковод, области налагаются: писать надо с конца, иначе хвост
// источника затрётся раньше, чем его прочитают.
void test_overlap_on_one_drive()
{
    HeadlessHost host;
    host.mount(0, disk(0x10));
    std::string error;
    if (!run(host, "10 COPY F(0,3) TO F(1)\n", error))
        { std::printf("  %s\n", error.c_str()); CHECK(false); return; }
    CHECK_EQ(at(host.image(0), 1), 0x10u);
    CHECK_EQ(at(host.image(0), 2), 0x11u);
    CHECK_EQ(at(host.image(0), 3), 0x12u);
    CHECK_EQ(at(host.image(0), 4), 0x13u);
    CHECK_EQ(at(host.image(0), 0), 0x10u);
}

// --- отказы -----------------------------------------------------------------

// Ошибка машины, а не ограничение эмулятора: её ловит ON ERROR — так и
// сделано в LКОПИДИС 1240, где обработчик стоит прямо перед COPY.
void test_reversed_bounds_are_catchable()
{
    HeadlessHost host;
    host.mount(0, disk(0x10));
    host.mount(1, disk(0x80));
    std::string error;
    const char * src =
        "10 ON ERROR GOTO 40\n"
        "20 COPY F(4,2) TO R(0)\n"
        "30 STOP\n"
        "40 PRINT \"POIMANO\"\n";
    if (!run(host, src, error))
        { std::printf("  %s\n", error.c_str()); CHECK(false); return; }
    CHECK_STR(line_of(host.dump(), 1), "POIMANO");
    // Приёмник не тронут: до записи дело не дошло.
    CHECK_EQ(at(host.image(1), 0), 0x80u);
}

void test_past_the_end()
{
    HeadlessHost host;
    host.mount(0, disk(0x10));
    host.mount(1, disk(0x80));
    std::string error;
    CHECK(!run(host, "10 COPY F(6,9) TO R(0)\n", error));
    CHECK(error.find("конц") != std::string::npos);
}

void test_does_not_fit()
{
    HeadlessHost host;
    host.mount(0, disk(0x10));
    host.mount(1, disk(0x80));
    std::string error;
    CHECK(!run(host, "10 COPY F(0,7) TO R(4)\n", error));
    CHECK(error.find("вмещается") != std::string::npos);
}

// Дисковода нет вовсе — это уже ограничение хоста, и ON ERROR его не ловит.
void test_missing_drive()
{
    HeadlessHost host;
    host.mount(0, disk(0x10));
    std::string error;
    CHECK(!run(host, "10 COPY F TO R\n", error));
}

// --- оттранслированная форма ------------------------------------------------

// `COPY T#G9%,(H1,H1) TO T#B0%,(H1)` = `6D 02 DB <G9%> DE <H1> DE <H1> D1
// 02 DB <B0%> DE <H1>` — LКОПИДИС 1260 слово в слово. `TO` — байт D1,
// скобок вокруг границ в потоке нет вовсе.
void test_tokenized()
{
    std::string koi8, error;
    utf8_to_koi8("10 COPY T#A,(B,B) TO T#C,(B)\n", koi8);
    NameTable names;
    ProgramImage img;
    if (!tokenize(koi8, img, names, error))
        { std::printf("  %s\n", error.c_str()); CHECK(false); return; }
    CHECK_EQ(img.line_count(), 1u);

    static const uint8_t want[] = {
        0x6D, 0x0D,
        0x02, 0xDB, 0x00, 0xDE, 0x01, 0xDE, 0x01,
        0xD1,
        0x02, 0xDB, 0x02, 0xDE, 0x01
    };
    const std::vector<uint8_t> & b = img.line(0).body;
    CHECK_EQ(b.size(), sizeof(want));
    for (unsigned i = 0; i < sizeof(want) && i < b.size(); ++i)
        CHECK_EQ(b[i], want[i]);
}

// Голая форма книги: `COPY F TO R` — ни границ, ни адреса приёмника.
void test_tokenized_bare()
{
    std::string koi8, error;
    utf8_to_koi8("10 COPY F TO R\n", koi8);
    NameTable names;
    ProgramImage img;
    if (!tokenize(koi8, img, names, error))
        { std::printf("  %s\n", error.c_str()); CHECK(false); return; }

    static const uint8_t want[] = { 0x6D, 0x03, 0x00, 0xD1, 0x01 };
    const std::vector<uint8_t> & b = img.line(0).body;
    CHECK_EQ(b.size(), sizeof(want));
    for (unsigned i = 0; i < sizeof(want) && i < b.size(); ++i)
        CHECK_EQ(b[i], want[i]);
}

// Оттранслированная форма исполняется так же, как текстовая.
void test_runs_from_tokens()
{
    HeadlessHost host;
    host.mount(0, disk(0x10));
    host.mount(1, disk(0x80));

    ProgramImage img;
    std::vector<uint8_t> body;
    body.push_back(0x6D); body.push_back(0x08);
    body.push_back(0x00);                          // F
    body.push_back(0xE8); body.push_back(0x02);    // 2
    body.push_back(0xDE);
    body.push_back(0xE8); body.push_back(0x03);    // 3
    body.push_back(0xD1);                          // TO
    body.push_back(0x01);                          // R
    img.put_line(10, &body[0], static_cast<unsigned>(body.size()));

    std::string error;
    Interp interp(img, host);
    if (!interp.run(error))
        { std::printf("  %s\n", error.c_str()); CHECK(false); return; }
    CHECK_EQ(at(host.image(1), 2), 0x12u);
    CHECK_EQ(at(host.image(1), 3), 0x13u);
    CHECK_EQ(at(host.image(1), 1), 0x81u);
}

} // namespace

int main()
{
    test_whole_disk();
    test_range_with_offset();
    test_bounds_are_expressions();
    test_no_destination_address();
    test_overlap_on_one_drive();
    test_reversed_bounds_are_catchable();
    test_past_the_end();
    test_does_not_fit();
    test_missing_drive();
    test_tokenized();
    test_tokenized_bare();
    test_runs_from_tokens();
    return test::summary("копирование диска на диск");
}
