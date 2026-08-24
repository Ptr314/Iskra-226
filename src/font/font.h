// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Mikhail Revzin <p3.141592653589793238462643@gmail.com>
// Part of the Iskra-226 project: https://github.com/Ptr314/Iskra-226
// Description: знакогенератор КОИ-8

#pragma once

#include <cstddef>

namespace iskra {

// Знакогенератор: 256 глифов, по байту на строку развёртки. Индекс глифа
// равен коду символа в КОИ-8.
//
// **Глиф и знакоместо — разные вещи.** У достоверного шрифта глиф 7x8 сидит
// в поле 9x10: по столбцу поля слева и справа, снизу межстрочный интервал и
// строка, в которой стоит курсор (подстрочная черта). У крупных шрифтов,
// взятых из консольных PSF, поля нет вовсе — знакоместо равно глифу.
class Font
{
public:
    // Достоверный знакогенератор: 7x8 в поле 9x10, экран 720x240 при 80x24.
    // Снят с ПЗУ советского терминала (src/font/koi8-7x8.txt) — не с самой
    // «Искры», см. CLAUDE.md, «Допущения».
    static const Font & standard();

    // Крупные шрифты для читаемости: высота 8, 14 или 16, знакоместо равно
    // глифу. Другое значение даёт нулевой указатель.
    static const Font * by_height(unsigned height);

    unsigned width() const { return width_; }
    unsigned height() const { return height_; }

    unsigned cell_width() const { return cell_w_; }
    unsigned cell_height() const { return cell_h_; }
    unsigned offset_x() const { return off_x_; }
    unsigned offset_y() const { return off_y_; }

    // Точка глифа: x < width(), y < height(). **Старший бит байта развёртки
    // — левая точка глифа независимо от его ширины**: у семиточечного заняты
    // биты 7-1, младший всегда нулевой.
    bool dot(unsigned char code, unsigned x, unsigned y) const
    {
        return ((glyph(code)[y] >> (7 - x)) & 1) != 0;
    }

    // height() байт, по байту на строку развёртки.
    const unsigned char * glyph(unsigned char code) const
    {
        return glyphs_ + static_cast<std::size_t>(code) * height_;
    }

private:
    Font(unsigned width, unsigned height,
         unsigned cell_w, unsigned cell_h, unsigned off_x, unsigned off_y,
         const unsigned char * glyphs)
        : width_(width), height_(height),
          cell_w_(cell_w), cell_h_(cell_h), off_x_(off_x), off_y_(off_y),
          glyphs_(glyphs) {}

    unsigned width_;
    unsigned height_;
    unsigned cell_w_;
    unsigned cell_h_;
    unsigned off_x_;
    unsigned off_y_;
    const unsigned char * glyphs_;
};

} // namespace iskra
