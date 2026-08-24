// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Mikhail Revzin <p3.141592653589793238462643@gmail.com>
// Part of the Iskra-226 project: https://github.com/Ptr314/Iskra-226
// Description: знакогенератор КОИ-8, поправки под «Искру»

#include "font/font.h"

#include <cstring>

namespace iskra {

namespace font_data {
    extern const unsigned char glyphs_8x8[256 * 8];
    extern const unsigned char glyphs_8x14[256 * 14];
    extern const unsigned char glyphs_8x16[256 * 16];
    extern const unsigned char glyphs_7x8[256 * 8];
}

namespace {

// Позиция 0x24: в КОИ-8 там доллар, а «Искра» высвечивает знак ¤
// (кодовая таблица, приложение 1 руководства 1987 г.). Знак значимый —
// им обозначаются символьные переменные, — поэтому глиф подменяем.
const unsigned char CURRENCY_8[8] = {
    0x00,
    0x42,   // .#....#.
    0x3C,   // ..####..
    0x24,   // ..#..#..
    0x3C,   // ..####..
    0x42,   // .#....#.
    0x00,
    0x00
};

const unsigned char CURRENCY_14[14] = {
    0x00, 0x00, 0x00,
    0x42,   // .#....#.
    0x3C,   // ..####..
    0x66,   // .##..##.
    0x66,   // .##..##.
    0x66,   // .##..##.
    0x3C,   // ..####..
    0x42,   // .#....#.
    0x00, 0x00, 0x00, 0x00
};

const unsigned char CURRENCY_16[16] = {
    0x00, 0x00, 0x00, 0x00,
    0x42,   // .#....#.
    0x3C,   // ..####..
    0x66,   // .##..##.
    0x66,   // .##..##.
    0x66,   // .##..##.
    0x3C,   // ..####..
    0x42,   // .#....#.
    0x00, 0x00, 0x00, 0x00, 0x00
};

// Позиция FF: в КОИ-8 там Ъ, а в исходном ПЗУ лежит сплошная заливка (тем
// же занят и 7F — там это не буква, и трогать его незачем). Букву
// дорисовываем: Ъ — это Ь с перекладиной слева вверху, а Ь в шрифте есть,
// и стойка у него отступает на столбец — как раз под перекладину.
const unsigned char HARD_SIGN_7[8] = {
    0xC0,   // ##.....
    0x40,   // .#.....
    0x40,   // .#.....
    0x78,   // .####..
    0x44,   // .#...#.
    0x44,   // .#...#.
    0x44,   // .#...#.
    0x78    // .####..
};

const unsigned char * patch(unsigned char * dst, const unsigned char * src,
                            unsigned height, unsigned code,
                            const unsigned char * glyph)
{
    std::memcpy(dst, src, 256u * height);
    std::memcpy(dst + code * height, glyph, height);
    return dst;
}

const unsigned char * glyphs_8()
{
    static unsigned char buf[256 * 8];
    static const unsigned char * p = patch(buf, font_data::glyphs_8x8, 8, 0x24, CURRENCY_8);
    return p;
}

const unsigned char * glyphs_14()
{
    static unsigned char buf[256 * 14];
    static const unsigned char * p = patch(buf, font_data::glyphs_8x14, 14, 0x24, CURRENCY_14);
    return p;
}

const unsigned char * glyphs_16()
{
    static unsigned char buf[256 * 16];
    static const unsigned char * p = patch(buf, font_data::glyphs_8x16, 16, 0x24, CURRENCY_16);
    return p;
}

const unsigned char * glyphs_7()
{
    static unsigned char buf[256 * 8];
    static const unsigned char * p =
        patch(buf, font_data::glyphs_7x8, 8, 0xFF, HARD_SIGN_7);
    return p;
}

} // namespace

const Font * Font::by_height(unsigned height)
{
    // Знакоместо у крупных шрифтов равно глифу: полей в консольных PSF нет,
    // они заложены в сами глифы.
    switch (height) {
        case 8:  { static const Font f(8, 8,  8, 8,  0, 0, glyphs_8());  return &f; }
        case 14: { static const Font f(8, 14, 8, 14, 0, 0, glyphs_14()); return &f; }
        case 16: { static const Font f(8, 16, 8, 16, 0, 0, glyphs_16()); return &f; }
        default: return 0;
    }
}

const Font & Font::standard()
{
    // Глиф 7x8 в поле 8x10: столбец поля справа, снизу межстрочный интервал
    // и строка курсора. При 80x24 это кадр 640x240, а с точкой вдвое выше
    // своей ширины — ровно 4:3 при целом увеличении обеих сторон.
    //
    // Восемь, а не девять: в дампе ПЗУ запись девятизначная и крайние
    // столбцы пусты у всех 256 глифов, но это поля записи, а не обязательно
    // два поля знакоместа. Шаг 7 отпадает — широкие буквы слипаются; из
    // оставшихся 8 и 9 взята восьмёрка, потому что она даёт 4:3 без дробного
    // растяжения. Выбор, а не находка (CLAUDE.md, «Допущения»).
    static const Font f(7, 8, 8, 10, 0, 0, glyphs_7());
    return f;
}

} // namespace iskra