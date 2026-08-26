// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Mikhail Revzin <p3.141592653589793238462643@gmail.com>
// Part of the Iskra-226 project: https://github.com/Ptr314/Iskra-226
// Description: знакоместа экрана — в точки; общее для всех оконных хостов

#pragma once

#include <cstdint>

#include "core/raster.h"
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
// **Растр экрана — 560x256**, и объявлен он в `core/raster.h`: это размер
// трубки, то есть свойство машины, а не хоста. Знакоместа живут на том же
// растре — 80x24 при поле 7x10 дают 560x240, то есть текстовый блок занимает
// кадр по ширине целиком, а полей остаётся по 8 точек сверху и снизу.

// Точка квадратная, и это не догадка: `SLIDE` 190 спрашивает «ОТНОШЕНИЕ ДЛИН
// ДИСКРЕТ X/Y» и предлагает по умолчанию единицу. Прежде здесь стояла
// двойка — она подгоняла кадр 640x240 под 4:3, — но подгонять нечего.
const unsigned DOT_TALL = 1;

// Увеличение только целое: экран знакоместный, и сглаживание ему во вред.
// Дробное растяжение однопиксельному шрифту противопоказано — штрихи выходят
// разной толщины.
//
// Хосты, у которых система умеет растягивать сама и без сглаживания (Win32
// с COLORONCOLOR, канва с image-rendering: pixelated), держат увеличение 1x1
// и растягивают окном; тем, у кого такого нет (X11), растягивает
// растеризатор — и тогда по вертикали надо брать в DOT_TALL раз больше.
class Renderer
{
public:
    Renderer();

    // Увеличение по осям порознь: точка сейчас квадратная, но `DOT_TALL`
    // держится отдельной величиной, и хост множит вертикаль на неё.
    void set_scale(unsigned x, unsigned y)
    {
        scale_x_ = x ? x : 1;
        scale_y_ = y ? y : 1;
    }
    void set_colors(uint32_t fg, uint32_t bg) { fg_ = fg; bg_ = bg; }

    // Бумага графопостроителя: своё окно, свои цвета. Тёмное перо на светлом
    // листе — это не экран, и люминофора у неё нет.
    void set_paper_colors(uint32_t ink, uint32_t paper)
    { ink_ = ink; paper_ = paper; }

    // Как графика ложится на знакоместа. Трубка одна, а устройств в таблице
    // ФАУ два (`05` символьное и `10` графическое), и **как именно они
    // смешиваются, книга не говорит вовсе**. Сложением («горит хоть одно»)
    // линия по тексту пропадала бы в светлых местах, поэтому по умолчанию
    // взято исключающее или: графика видна и на пустом поле, и поверх знака.
    // Вернуть сложение — одна строка у хоста.
    enum Overlay { OVERLAY_OR, OVERLAY_XOR };
    void set_overlay(Overlay m) { overlay_ = m; }
    Overlay overlay() const { return overlay_; }

    // Курсор — подстрочная черта: «место, на котором высветится очередной
    // символ, автоматически указывается с помощью курсора (подстрочной
    // черты)» (руководство, разд. 2.1). Он занимает нижнюю строку развёртки
    // знакоместа во всю его ширину. Мигание — забота хоста: у него часы,
    // он и переключает.
    void set_cursor(bool on) { cursor_ = on; }

    const Font & font() const { return *font_; }
    unsigned scale_x() const { return scale_x_; }
    unsigned scale_y() const { return scale_y_; }

    // Текстовый блок меряется знакоместами, а не глифами: у знакогенератора
    // «Искры» поле шире и выше глифа, и межбуквенный просвет живёт именно
    // там. При поле 7x10 это 560x240.
    unsigned text_width() const  { return SCREEN_COLS * font_->cell_width(); }
    unsigned text_height() const { return SCREEN_ROWS * font_->cell_height(); }

    // Кадр — растр трубки, 560x256. Блок в него влезает по построению;
    // максимум здесь страхует поля от ухода в минус, если знакоместо
    // когда-нибудь окажется больше.
    unsigned frame_width() const
    { return text_width() > RASTER_WIDTH ? text_width() : RASTER_WIDTH; }
    unsigned frame_height() const
    { return text_height() > RASTER_HEIGHT ? text_height() : RASTER_HEIGHT; }

    // Поля вокруг текстового блока: он стоит посреди растра.
    unsigned margin_x() const { return (frame_width()  - text_width())  / 2; }
    unsigned margin_y() const { return (frame_height() - text_height()) / 2; }

    unsigned width() const  { return frame_width()  * scale_x_; }
    unsigned height() const { return frame_height() * scale_y_; }
    unsigned pixels() const { return width() * height(); }

    // Нарисовать кадр целиком. pitch — длина строки буфера в точках, а не
    // в байтах: у Windows строки DIB выровнены, и она бывает больше width().
    //
    // Трубка одна, а устройств в таблице ФАУ два — `05` символьное и `10`
    // графическое, — поэтому графический растр накладывается поверх
    // знакомест: точка светится, если светится хоть одно из двух. Ноль
    // значит «графики нет».
    void draw(const Screen & s, uint32_t * out, unsigned pitch,
              const Raster * g = 0) const;

    // Один только растр, без знакомест: так показывается лист
    // графопостроителя, где знаков экрана нет вовсе.
    void draw_raster(const Raster & g, uint32_t * out, unsigned pitch) const;

    // Лента АЦПУ: те же знакоместа, но цветами бумаги и без курсора.
    // Печать идёт на бумагу, а не на люминофор, и мигать там нечему.
    void draw_paper(const Screen & s, uint32_t * out, unsigned pitch) const
    { draw_cells(s, out, pitch, 0, ink_, paper_, false); }

private:
    // Общий проход: экран и лента отличаются только цветами и курсором.
    void draw_cells(const Screen & s, uint32_t * out, unsigned pitch,
                    const Raster * g, uint32_t fg, uint32_t bg,
                    bool cursor) const;

    const Font * font_;
    unsigned scale_x_;
    unsigned scale_y_;
    uint32_t fg_;
    uint32_t bg_;
    uint32_t ink_;
    uint32_t paper_;
    Overlay overlay_;
    bool cursor_;
};

} // namespace iskra
