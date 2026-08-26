// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Mikhail Revzin <p3.141592653589793238462643@gmail.com>
// Part of the Iskra-226 project: https://github.com/Ptr314/Iskra-226
// Description: АЦПУ — лента, её файл и растеризация бумаги

#include <cstdio>
#include <string>
#include <vector>

#include "check.h"
#include "core/screen.h"
#include "host_common/fileio.h"
#include "host_common/printer.h"
#include "host_common/renderer.h"

using namespace iskra;

namespace {

const char * PATH = "test_printer_tape.txt";
// Имя с кириллицей: тот же случай, что у образов дискет — узкое имя на
// Windows ушло бы в кодовую страницу системы.
const char * PATH_CYR = "test_printer_ЛЕНТА.txt";

void drop(const char * path) { remove_utf8(path); }

std::string read_all(const char * path)
{
    std::FILE * f = open_utf8(path, "rb");
    if (!f) return std::string();
    std::string s;
    char buf[512];
    std::size_t n;
    while ((n = std::fread(buf, 1, sizeof(buf), f)) > 0) s.append(buf, n);
    std::fclose(f);
    return s;
}

void put_all(Printer & p, const char * koi8)
{
    for (const char * s = koi8; *s; ++s)
        p.put(static_cast<uint8_t>(*s));
}

// Лента копится и без файла: на ней стоит вывод командной строки, а до
// августа 2026 напечатанное пропадало вовсе.
void test_tape()
{
    Printer p;
    CHECK(p.empty());
    CHECK(!p.to_file());

    put_all(p, "AB");
    p.put(0xE9);                      // И в КОИ-8
    CHECK(!p.empty());
    CHECK_EQ(static_cast<unsigned>(p.tape().size()), 3u);
    CHECK_STR(p.utf8(), "ABИ");
}

// Файл ленты: байты уходят в него сразу, не дожидаясь закрытия, — как и
// запись на дискету. Программа на машине может уронить эмулятор.
void test_file_write_through()
{
    drop(PATH);
    {
        Printer p;
        std::string err;
        CHECK(p.open(PATH, err));
        CHECK_STR(err, "");
        CHECK(p.to_file());

        put_all(p, "AB");
        // Файл ещё не закрыт, а прочитать написанное уже можно.
        CHECK_STR(read_all(PATH), "AB");

        p.put(0xE9);
        CHECK_STR(read_all(PATH), "ABИ");   // в файле UTF-8
    }
    drop(PATH);
}

// Дозапись, а не перезапись: лента не перематывается, и два прогона подряд
// ложатся один за другим.
void test_file_append()
{
    drop(PATH);
    for (int pass = 0; pass < 2; ++pass) {
        Printer p;
        std::string err;
        CHECK(p.open(PATH, err));
        put_all(p, "X");
    }
    CHECK_STR(read_all(PATH), "XX");
    drop(PATH);
}

void test_file_cyrillic_name()
{
    drop(PATH_CYR);
    {
        Printer p;
        std::string err;
        CHECK(p.open(PATH_CYR, err));
        CHECK_STR(err, "");
        put_all(p, "Q");
    }
    CHECK_STR(read_all(PATH_CYR), "Q");
    drop(PATH_CYR);
}

void test_file_bad_path()
{
    Printer p;
    std::string err;
    CHECK(!p.open("нет-такого-каталога/лента.txt", err));
    CHECK(err.find("не открыть файл ленты") != std::string::npos);
    CHECK(!p.to_file());
    // Отказавший файл ленту не ломает: она по-прежнему копится в памяти.
    p.put('Z');
    CHECK_STR(p.utf8(), "Z");
}

// Бумага рисуется цветами бумаги, а не люминофора, и курсора на ней нет:
// печатает АЦПУ, а не трубка.
void test_draw_paper()
{
    Renderer r;
    const uint32_t ink = 0x00112233, paper = 0x00FFEEDD;
    r.set_paper_colors(ink, paper);
    r.set_cursor(true);               // на бумаге курсора всё равно быть не должно

    Screen s;
    s.put('A');

    std::vector<uint32_t> frame(r.pixels(), 0);
    r.draw_paper(s, &frame[0], r.width());

    // Цветов ровно два, и оба — бумажные.
    unsigned other = 0, inked = 0;
    for (std::size_t i = 0; i < frame.size(); ++i) {
        if (frame[i] == ink) ++inked;
        else if (frame[i] != paper) ++other;
    }
    CHECK_EQ(other, 0u);
    CHECK(inked > 0);

    // Курсор стоит во второй позиции первой строки. Если бы он рисовался,
    // нижняя строка развёртки этого знакоместа была бы залита целиком.
    const unsigned cw = r.font().cell_width();
    const unsigned chh = r.font().cell_height();
    const unsigned y = r.margin_y() + chh - 1;      // нижняя строка знакоместа
    unsigned lit = 0;
    for (unsigned x = 0; x < cw; ++x)
        if (frame[y * r.width() + r.margin_x() + cw + x] == ink) ++lit;
    CHECK_EQ(lit, 0u);

    // Экранная отрисовка того же места курсор ставит — значит проверка выше
    // ловит именно его отсутствие, а не пустое знакоместо.
    r.set_colors(ink, paper);
    r.draw(s, &frame[0], r.width());
    lit = 0;
    for (unsigned x = 0; x < cw; ++x)
        if (frame[y * r.width() + r.margin_x() + cw + x] == ink) ++lit;
    CHECK_EQ(lit, cw);
}

} // namespace

int main()
{
    test_tape();
    test_file_write_through();
    test_file_append();
    test_file_cyrillic_name();
    test_file_bad_path();
    test_draw_paper();
    drop(PATH);
    drop(PATH_CYR);
    return test::summary("test_printer");
}
