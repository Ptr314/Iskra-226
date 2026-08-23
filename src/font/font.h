// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Mikhail Revzin <p3.141592653589793238462643@gmail.com>
// Part of the Iskra-226 project: https://github.com/Ptr314/Iskra-226
// Description: знакогенератор КОИ-8

#pragma once

#include <cstddef>

namespace iskra {

// Знакогенератор: 256 глифов шириной 8 точек, старший бит — левая точка.
// Индекс глифа равен коду символа в КОИ-8.
class Font
{
public:
    // Высота 8, 14 или 16. Другое значение даёт нулевой указатель.
    static const Font * by_height(unsigned height);

    // Знакогенератор по умолчанию: 8x16, экран 640x384 при 80x24.
    static const Font & standard();

    unsigned width() const { return 8; }
    unsigned height() const { return height_; }

    // height() байт, по байту на строку развёртки.
    const unsigned char * glyph(unsigned char code) const
    {
        return glyphs_ + static_cast<std::size_t>(code) * height_;
    }

private:
    Font(unsigned height, const unsigned char * glyphs)
        : height_(height), glyphs_(glyphs) {}

    unsigned height_;
    const unsigned char * glyphs_;
};

} // namespace iskra