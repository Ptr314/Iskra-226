// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Mikhail Revzin <p3.141592653589793238462643@gmail.com>
// Part of the Iskra-226 project: https://github.com/Ptr314/Iskra-226
// Description: экран, коды управления, знакогенератор, перекодировка

#include <cstring>

#include "check.h"
#include "core/koi8.h"
#include "core/screen.h"
#include "font/font.h"
#include "host_headless/headless_host.h"

using namespace iskra;

namespace {

void put(Screen & s, const char * text)
{
    s.write(reinterpret_cast<const uint8_t *>(text),
            static_cast<unsigned>(std::strlen(text)));
}

// --- Экран -----------------------------------------------------------------

void test_geometry()
{
    // «24 строки по 80 символов в каждой» — руководство, разд. 4.
    CHECK_EQ(SCREEN_ROWS, 24u);
    CHECK_EQ(SCREEN_COLS, 80u);
}

void test_printing()
{
    Screen s;
    CHECK_EQ(s.row(), 1u);
    CHECK_EQ(s.col(), 1u);

    put(s, "AB");
    CHECK_EQ(s.cell(1, 1).ch, 'A');
    CHECK_EQ(s.cell(1, 2).ch, 'B');
    CHECK_EQ(s.col(), 3u);
}

void test_wrap()
{
    // «Если текст выходит за пределы строки экрана (80 символов), то курсор
    // автоматически переходит в начало следующей строки» — разд. 5.
    Screen s;
    s.at(1, 80);
    put(s, "XY");
    CHECK_EQ(s.cell(1, 80).ch, 'X');
    CHECK_EQ(s.cell(2, 1).ch, 'Y');
    CHECK_EQ(s.row(), 2u);
    CHECK_EQ(s.col(), 2u);
}

void test_scroll()
{
    Screen s;
    s.at(1, 1);
    put(s, "первая");                  // латиницей неважно, важен сдвиг
    s.at(24, 1);
    put(s, "Z");
    s.put(CC_DOWN);                    // ПС на нижней строке — прокрутка
    CHECK_EQ(s.row(), 24u);
    CHECK_EQ(s.cell(23, 1).ch, 'Z');
    CHECK_EQ(s.cell(24, 1).ch, 0x20);
}

void test_control_codes()
{
    Screen s;

    s.at(5, 5);
    s.put(CC_HOME);                    // НЗ
    CHECK_EQ(s.row(), 1u);
    CHECK_EQ(s.col(), 1u);

    put(s, "ABC");
    s.put(CC_CR);                      // ВК — в первую позицию текущей строки
    CHECK_EQ(s.col(), 1u);
    CHECK_EQ(s.row(), 1u);
    CHECK_EQ(s.cell(1, 3).ch, 'C');    // ВК ничего не стирает

    s.put(CC_CLEAR);                   // КТ — очистка и в левый верхний угол
    CHECK_EQ(s.cell(1, 3).ch, 0x20);
    CHECK_EQ(s.row(), 1u);

    s.at(3, 3);
    s.put(CC_LEFT);  CHECK_EQ(s.col(), 2u);
    s.put(CC_RIGHT); CHECK_EQ(s.col(), 3u);
    s.put(CC_DOWN);  CHECK_EQ(s.row(), 4u);
    s.put(CC_UP);    CHECK_EQ(s.row(), 3u);

    // Курсор упирается в края, а не заворачивается
    s.at(1, 1);
    s.put(CC_LEFT); CHECK_EQ(s.col(), 1u);
    s.put(CC_UP);   CHECK_EQ(s.row(), 1u);

    CHECK_EQ(s.take_bells(), 0u);
    s.put(CC_BELL);
    CHECK_EQ(s.take_bells(), 1u);
    CHECK_EQ(s.take_bells(), 0u);
}

void test_attributes()
{
    Screen s;
    s.put(CC_POSITIVE);                // СУ2 — выделение
    put(s, "A");
    s.put(CC_NEGATIVE);                // СУ1 — обычное состояние
    put(s, "B");
    CHECK_EQ(s.cell(1, 1).attr, ATTR_POSITIVE);
    CHECK_EQ(s.cell(1, 2).attr, ATTR_NEGATIVE);
}

void test_at_erase()
{
    // Пример 17.6 из книги: PRINT AT(8,20,15) ставит курсор в 20-ю позицию
    // восьмой строки и стирает вправо пятнадцать символов.
    Screen s;
    s.at(8, 1);
    for (unsigned i = 0; i < 40; ++i) put(s, "*");

    s.at(8, 20);
    s.erase(15);
    CHECK_EQ(s.row(), 8u);
    CHECK_EQ(s.col(), 20u);
    CHECK_EQ(s.cell(8, 19).ch, '*');
    CHECK_EQ(s.cell(8, 20).ch, 0x20);
    CHECK_EQ(s.cell(8, 34).ch, 0x20);
    CHECK_EQ(s.cell(8, 35).ch, '*');

    // Стирание переходит на следующую строку: «если строка не помещается,
    // то стираются символы следующей строки» — разд. 17.
    s.at(10, 1);
    for (unsigned i = 0; i < 80; ++i) put(s, "#");
    s.at(11, 1);
    put(s, "#");
    s.at(10, 79);
    s.erase(4);
    CHECK_EQ(s.cell(10, 78).ch, '#');
    CHECK_EQ(s.cell(10, 79).ch, 0x20);
    CHECK_EQ(s.cell(10, 80).ch, 0x20);
    CHECK_EQ(s.cell(11, 1).ch, 0x20);
}

void test_at_clamped()
{
    // AT(25,...) в корпусе встречается, хотя строк 24: загоняем на экран.
    Screen s;
    s.at(25, 200);
    CHECK_EQ(s.row(), 24u);
    CHECK_EQ(s.col(), 80u);
}

// --- Перекодировка ---------------------------------------------------------

void test_koi8()
{
    CHECK_EQ(koi8_to_unicode('A'), 0x41u);
    CHECK_EQ(koi8_to_unicode(0xE1), 0x0410u);   // А прописная
    CHECK_EQ(koi8_to_unicode(0xC1), 0x0430u);   // а строчная
    CHECK_EQ(koi8_to_unicode(0xFF), 0x042Au);   // Ъ
    CHECK_EQ(koi8_to_unicode(0xDF), 0x044Au);   // ъ

    // 0x24 — на «Искре» это ¤, а не доллар (таблица кодов, прил. 1)
    CHECK_EQ(koi8_to_unicode(0x24), 0x00A4u);

    const uint8_t koi[] = {0xE9, 0xF3, 0xEB, 0xF2, 0xE1};
    CHECK_STR(koi8_to_utf8(koi, 5), "ИСКРА");
}

// --- Знакогенератор --------------------------------------------------------

void test_font()
{
    // Знакогенератор самой «Искры» — глиф 5x8 в знакоместе 6x10.
    const Font & f = Font::standard();
    CHECK_EQ(f.width(), 5u);
    CHECK_EQ(f.height(), 8u);
    CHECK_EQ(f.cell_width(), 6u);
    CHECK_EQ(f.cell_height(), 10u);

    // Пробел пуст, буквы — нет.
    bool space_blank = true, letter_blank = true, cyr_blank = true;
    for (unsigned y = 0; y < f.height(); ++y) {
        if (f.glyph(0x20)[y]) space_blank = false;
        if (f.glyph('A')[y])  letter_blank = false;
        if (f.glyph(0x61)[y]) cyr_blank = false;      // А кириллическая
    }
    CHECK(space_blank);
    CHECK(!letter_blank);
    CHECK(!cyr_blank);

    // Три младших бита байта развёртки не заняты вовсе: глиф пятиточечный
    // и прижат влево.
    unsigned stray = 0;
    for (unsigned c = 0; c < FONT_GLYPHS; ++c)
        for (unsigned y = 0; y < f.height(); ++y)
            if (f.glyph(static_cast<unsigned char>(c))[y] & 0x07) ++stray;
    CHECK_EQ(stray, 0u);

    // Старший бит кода отбрасывается: КОИ-8 E1 и КОИ-7 Н2 61 — одна и та же
    // прописная А, и глиф у них общий.
    bool aliased = true;
    for (unsigned y = 0; y < f.height(); ++y)
        if (f.glyph(0xE1)[y] != f.glyph(0x61)[y]) aliased = false;
    CHECK(aliased);

    // Верхняя строка знакоместа пуста у всех 96 знаков: сам глиф 5x7.
    unsigned topmost = 0;
    for (unsigned c = 0; c < FONT_GLYPHS; ++c)
        if (f.glyph(static_cast<unsigned char>(c))[0]) ++topmost;
    CHECK_EQ(topmost, 0u);

    // В позиции 0x24 «Искра» высвечивает ¤, а не доллар, — так на рисунке
    // 3.1 и есть, подменять нечего.
    CHECK(f.dot(0x24, 0, 1) && f.dot(0x24, 4, 1));
    CHECK(!f.dot(0x24, 0, 2) && !f.dot(0x24, 4, 2));

    // 5F — не подчёркивание, а Ъ; 7F пуст. Оба отличия от КОИ-7 Н2 видны
    // прямо на рисунке.
    bool hard_blank = true, del_blank = true;
    for (unsigned y = 0; y < f.height(); ++y) {
        if (f.glyph(0x5F)[y]) hard_blank = false;
        if (f.glyph(0x7F)[y]) del_blank = false;
    }
    CHECK(!hard_blank);
    CHECK(del_blank);
}

// --- Хост ------------------------------------------------------------------

void test_headless_host()
{
    HeadlessHost host;

    // Экран выгружается текстом, хвостовые пробелы срезаны.
    host.screen().at(2, 3);
    put(host.screen(), "AB");
    const std::string d = host.dump();
    CHECK_STR(d.substr(0, 6), "\n  AB\n");

    // Клавиатура: очередь заготовленных нажатий.
    uint8_t code = 0;
    CHECK(!host.poll_key(code));
    const uint8_t keys[] = {'R', 'U', 'N', 0x0D};
    host.feed_keys(keys, 4);
    for (unsigned i = 0; i < 4; ++i) {
        CHECK(host.poll_key(code));
        CHECK_EQ(code, keys[i]);
    }
    CHECK(!host.poll_key(code));

    // АЦПУ копит вывод и отдаёт его в UTF-8.
    host.print_char(0xE1);
    host.print_char(0xE2);
    CHECK_STR(host.printer(), "АБ");

    // Дисковод: секторы по 256 байт, пустой отсутствует.
    CHECK_EQ(host.disk_sectors(0), 0u);
    std::vector<uint8_t> img(Host::SECTOR_SIZE * 4, 0);
    img[Host::SECTOR_SIZE * 2] = 0x5A;
    host.mount(0, img);
    CHECK_EQ(host.disk_sectors(0), 4u);

    uint8_t buf[Host::SECTOR_SIZE];
    CHECK(host.disk_read(0, 2, buf));
    CHECK_EQ(buf[0], 0x5Au);
    CHECK(!host.disk_read(0, 4, buf));

    buf[0] = 0xA5;
    CHECK(host.disk_write(0, 3, buf));
    CHECK(host.disk_read(0, 3, buf));
    CHECK_EQ(buf[0], 0xA5u);

    // Время двигается только вручную: прогоны воспроизводимы.
    CHECK_EQ(host.ticks_ms(), 0u);
    host.advance_ms(50);
    CHECK_EQ(host.ticks_ms(), 50u);
}

} // namespace

int main()
{
    test_geometry();
    test_printing();
    test_wrap();
    test_scroll();
    test_control_codes();
    test_attributes();
    test_at_erase();
    test_at_clamped();
    test_koi8();
    test_font();
    test_headless_host();
    return test::summary("экран, знакогенератор, хост");
}