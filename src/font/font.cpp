// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Mikhail Revzin <p3.141592653589793238462643@gmail.com>
// Part of the Iskra-226 project: https://github.com/Ptr314/Iskra-226
// Description: знакогенератор КОИ-7 Н2

#include "font/font.h"

namespace iskra {

namespace font_data {
    extern const unsigned char glyphs_5x8[FONT_GLYPHS * 8];
}

const Font & Font::standard()
{
    // Глиф 5x8 в поле 6x10: столбец просвета справа, снизу межстрочный
    // интервал и строка курсора. При 80x24 это текстовый блок 480x240, и он
    // стоит посреди растра трубки 512x256 (host_common/renderer.h).
    //
    // Знакогенератор снят с рис. 3.1 руководства по БОСГИ и никаких поправок
    // не требует: и знак ¤ в позиции 24, и Ъ в позиции 5F стоят там у самой
    // машины (src/font/koi7-5x8.txt).
    static const Font f(5, 8, 6, 10, 0, 0, font_data::glyphs_5x8);
    return f;
}

} // namespace iskra
