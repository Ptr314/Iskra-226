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

const unsigned char * patch(unsigned char * dst, const unsigned char * src,
                            unsigned height, const unsigned char * currency)
{
    std::memcpy(dst, src, 256u * height);
    std::memcpy(dst + 0x24u * height, currency, height);
    return dst;
}

const unsigned char * glyphs_8()
{
    static unsigned char buf[256 * 8];
    static const unsigned char * p = patch(buf, font_data::glyphs_8x8, 8, CURRENCY_8);
    return p;
}

const unsigned char * glyphs_14()
{
    static unsigned char buf[256 * 14];
    static const unsigned char * p = patch(buf, font_data::glyphs_8x14, 14, CURRENCY_14);
    return p;
}

const unsigned char * glyphs_16()
{
    static unsigned char buf[256 * 16];
    static const unsigned char * p = patch(buf, font_data::glyphs_8x16, 16, CURRENCY_16);
    return p;
}

} // namespace

const Font * Font::by_height(unsigned height)
{
    switch (height) {
        case 8:  { static const Font f(8,  glyphs_8());  return &f; }
        case 14: { static const Font f(14, glyphs_14()); return &f; }
        case 16: { static const Font f(16, glyphs_16()); return &f; }
        default: return 0;
    }
}

const Font & Font::standard()
{
    return *by_height(16);
}

} // namespace iskra