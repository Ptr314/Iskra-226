// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Mikhail Revzin <p3.141592653589793238462643@gmail.com>
// Part of the Iskra-226 project: https://github.com/Ptr314/Iskra-226
// Description: знакоместа экрана — в точки; общее для всех оконных хостов

#include "host_common/renderer.h"

#include <cstring>

namespace iskra {

Renderer::Renderer()
    : font_(&Font::standard()),
      scale_x_(1),
      scale_y_(1),
      // Цвет люминофора «Искры» не установлен: в руководстве его нет, а по
      // чёрно-белым снимкам не разобрать. Взят нейтральный белый — он ничего
      // не утверждает; хост цвета меняет.
      fg_(pack_rgb(0xD0, 0xD0, 0xD0)),
      bg_(pack_rgb(0x00, 0x00, 0x00)),
      cursor_(false)
{
}

void Renderer::draw(const Screen & s, uint32_t * out, unsigned pitch) const
{
    const unsigned cw = font_->cell_width();
    const unsigned ch = font_->cell_height();
    const unsigned gw = font_->width();
    const unsigned gh = font_->height();
    const unsigned ox = font_->offset_x();
    const unsigned oy = font_->offset_y();
    const unsigned sx = scale_x_;
    const unsigned sy = scale_y_;
    const unsigned cur_row = s.row();
    const unsigned cur_col = s.col();

    // Поля вокруг текстового блока — часть растра трубки, а не пустота за
    // краем кадра: их надо закрасить, иначе там останется мусор буфера.
    // Закрашивается кадр целиком, вместе с местом под знаки: так короче, а
    // лишний проход по 512x256 не стоит и десятой доли мига.
    const unsigned mx = margin_x() * sx;
    const unsigned my = margin_y() * sy;
    if (mx || my)
        for (unsigned y = 0; y < height(); ++y) {
            uint32_t * line = out + y * pitch;
            for (unsigned x = 0; x < width(); ++x) line[x] = bg_;
        }

    for (unsigned r = 1; r <= SCREEN_ROWS; ++r) {
        for (unsigned c = 1; c <= SCREEN_COLS; ++c) {
            const Cell & cell = s.cell(r, c);

            // Позитив — выделение: тёмные знаки на светлом фоне, то есть
            // перестановка цветов относительно обычного состояния экрана.
            // Поля знакоместа переставляются вместе со знаком, иначе
            // выделенная строка вышла бы в полоску.
            const bool inv = cell.attr == ATTR_POSITIVE;
            const bool cursor_here = cursor_ && r == cur_row && c == cur_col;

            uint32_t * cell_out = out + (my + (r - 1) * ch * sy) * pitch
                                      + mx + (c - 1) * cw * sx;

            for (unsigned y = 0; y < ch; ++y) {
                uint32_t * line = cell_out + y * sy * pitch;
                // Подстрочная черта закрашивает нижнюю строку знакоместа
                // целиком — и на выделенном знакоместе тоже, где она выходит
                // тёмной на светлом. Пропасть она не может ни там, ни там.
                const bool underline = cursor_here && y + 1 == ch;
                const bool in_glyph = y >= oy && y - oy < gh;

                for (unsigned x = 0; x < cw; ++x) {
                    bool on = underline;
                    if (!on && in_glyph && x >= ox && x - ox < gw)
                        on = font_->dot(cell.ch, x - ox, y - oy);
                    const uint32_t color = (on != inv) ? fg_ : bg_;
                    for (unsigned k = 0; k < sx; ++k) line[x * sx + k] = color;
                }
                // При увеличении строка развёртки повторяется как есть.
                for (unsigned k = 1; k < sy; ++k)
                    std::memcpy(line + k * pitch, line,
                                cw * sx * sizeof(uint32_t));
            }
        }
    }
}

} // namespace iskra
