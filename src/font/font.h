// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Mikhail Revzin <p3.141592653589793238462643@gmail.com>
// Part of the Iskra-226 project: https://github.com/Ptr314/Iskra-226
// Description: знакогенератор КОИ-7 Н2

#pragma once

#include <cstddef>

namespace iskra {

// Число глифов в знакогенераторе. Экран «Искры» семибитный: старший бит кода
// он сбрасывает (Screen::put), и знаков ровно 128 — коды 20-7F кодировки
// КОИ-7 Н2, ниже пробела одни коды управления.
const unsigned FONT_GLYPHS = 128;

// Знакогенератор: 128 глифов, по байту на строку развёртки. Индекс глифа
// равен семибитному коду символа.
//
// **Глиф и знакоместо — разные вещи.** Глиф 5x8 сидит в поле 6x10: столбец
// просвета справа, снизу межстрочный интервал и строка, в которой стоит
// курсор (подстрочная черта). Растеризатор меряет кадр знакоместами, а не
// глифами — просвет живёт именно в поле.
class Font
{
public:
    // Знакогенератор самой «Искры»: глиф 5x8 в поле 6x10, текстовый блок
    // 480x240 при 80x24. Снят с рис. 3.1 руководства по БОСГИ —
    // «Номенклатура символов» (src/font/koi7-5x8.txt, разбор —
    // tools/probes/bosgi_font.py).
    static const Font & standard();

    unsigned width() const { return width_; }
    unsigned height() const { return height_; }

    unsigned cell_width() const { return cell_w_; }
    unsigned cell_height() const { return cell_h_; }
    unsigned offset_x() const { return off_x_; }
    unsigned offset_y() const { return off_y_; }

    // Точка глифа: x < width(), y < height(). **Старший бит байта развёртки
    // — левая точка глифа независимо от его ширины**: у пятиточечного заняты
    // биты 7-3, три младших всегда нулевые.
    bool dot(unsigned char code, unsigned x, unsigned y) const
    {
        return ((glyph(code)[y] >> (7 - x)) & 1) != 0;
    }

    // height() байт, по байту на строку развёртки. Старший бит кода
    // отбрасывается: у машины его нет вовсе, а в буфер экрана он попасть и
    // не может — Screen::put сбрасывает его на входе.
    const unsigned char * glyph(unsigned char code) const
    {
        return glyphs_ + static_cast<std::size_t>(code & 0x7F) * height_;
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
