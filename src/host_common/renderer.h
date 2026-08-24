// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Mikhail Revzin <p3.141592653589793238462643@gmail.com>
// Part of the Iskra-226 project: https://github.com/Ptr314/Iskra-226
// Description: знакоместа экрана — в точки; общее для всех оконных хостов

#pragma once

#include <cstdint>

#include "core/screen.h"
#include "font/font.h"

namespace iskra {

// Цвет одним словом — ровно так, как его кладут в буфер кадра. Порядок байт
// у Win32 и X11 обычно 0x00RRGGBB, а у канвы браузера иной; поэтому
// растеризатор цветов не толкует вовсе, а берёт готовые слова, и хост
// упаковывает их по-своему. Так нигде не нужен лишний проход по кадру.
inline uint32_t pack_rgb(uint8_t r, uint8_t g, uint8_t b)
{
    return (static_cast<uint32_t>(r) << 16) |
           (static_cast<uint32_t>(g) << 8)  |
            static_cast<uint32_t>(b);
}

// Превращает буфер знакомест в прямоугольник точек. Про окно, события и
// систему не знает ничего — оконному хосту остаётся завести окно, разобрать
// события и вывалить готовый кадр.
//
// Увеличение только целое: экран знакоместный, и сглаживание ему во вред.
// Хосты, у которых система умеет растягивать сама и без сглаживания (Win32
// с COLORONCOLOR, канва с image-rendering: pixelated), держат scale = 1 и
// растягивают окном; тем, у кого такого нет (X11), растягивает растеризатор.
class Renderer
{
public:
    Renderer();

    void set_font(const Font & f) { font_ = &f; }
    void set_scale(unsigned n) { scale_ = n ? n : 1; }
    void set_colors(uint32_t fg, uint32_t bg) { fg_ = fg; bg_ = bg; }

    // Курсор — подстрочная черта: «место, на котором высветится очередной
    // символ, автоматически указывается с помощью курсора (подстрочной
    // черты)» (руководство, разд. 2.1). Он занимает нижнюю строку развёртки
    // знакоместа во всю его ширину. Мигание — забота хоста: у него часы,
    // он и переключает.
    void set_cursor(bool on) { cursor_ = on; }

    const Font & font() const { return *font_; }
    unsigned scale() const { return scale_; }

    // Кадр меряется знакоместами, а не глифами: у достоверного шрифта поле
    // шире и выше глифа, и межбуквенный просвет живёт именно там.
    unsigned width() const  { return SCREEN_COLS * font_->cell_width()  * scale_; }
    unsigned height() const { return SCREEN_ROWS * font_->cell_height() * scale_; }
    unsigned pixels() const { return width() * height(); }

    // Нарисовать кадр целиком. pitch — длина строки буфера в точках, а не
    // в байтах: у Windows строки DIB выровнены, и она бывает больше width().
    void draw(const Screen & s, uint32_t * out, unsigned pitch) const;

private:
    const Font * font_;
    unsigned scale_;
    uint32_t fg_;
    uint32_t bg_;
    bool cursor_;
};

} // namespace iskra
