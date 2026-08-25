// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Mikhail Revzin <p3.141592653589793238462643@gmail.com>
// Part of the Iskra-226 project: https://github.com/Ptr314/Iskra-226
// Description: графический растр 560x256 — вторая половина БОСГИ, ФАУ 10

#pragma once

#include <cstdint>
#include <vector>

namespace iskra {

// **Растр — 560x256.** Ширину даёт корпус прямым текстом: `SLIDE` 170
// предлагает границей экрана 559 и выкладывает свой буфер на `/10` (трубка,
// а не графопостроитель), `SIG` 7320 задаёт `WINDOW 0,559,0,255` и строкой
// ниже рисует `NPLOT B¤(),550,50`. Живая картинка с образа добавляет третий
// довод: приращение надписи там 7 дискрет на знак, и 80 знакомест строки
// дают ровно 560 (`docs/format.md`, разд. 5, «Растр графики»).
const unsigned RASTER_WIDTH  = 560;
const unsigned RASTER_HEIGHT = 256;

// Точки светятся или не светятся — полутонов у машины нет. Байт на точку, а
// не бит: 140 килобайт нынче не деньги, зато нет ни сдвигов, ни масок.
//
// **Ось Y смотрит вверх.** Это видно по корпусу: `VICT` 6150 ставит начало
// оси в `NPLOT B¤(),P6,248` и тянет `DRAW B¤(),P6,0` вниз, к нулю. У
// знакомест наоборот, строка 1 сверху, и перевод — забота растеризатора.
class Raster
{
public:
    Raster() : bits_(RASTER_WIDTH * RASTER_HEIGHT, 0), lit_(0), dirty_(true) {}

    void clear();

    // Точка вне растра просто не рисуется: обрезка по границам — дело
    // устройства, и ошибкой она не является. Выход за экран ловят сами
    // операторы преобразования (`SLIDE` 5090), а не вывод.
    void plot(long x, long y);

    // Отрезок по Брезенхему: у машины перо, и линия между двумя точками —
    // единственное, что она умеет рисовать сама.
    void line(long x0, long y0, long x1, long y1);

    bool at(unsigned x, unsigned y) const
    {
        if (x >= RASTER_WIDTH || y >= RASTER_HEIGHT) return false;
        return bits_[(RASTER_HEIGHT - 1 - y) * RASTER_WIDTH + x] != 0;
    }

    // Ни одной светящейся точки: растеризатору тогда нечего накладывать на
    // знакоместа, а оконному хосту незачем открывать второе окно.
    bool empty() const { return lit_ == 0; }

    bool dirty() const { return dirty_; }
    void clear_dirty() { dirty_ = false; }

private:
    std::vector<uint8_t> bits_;
    unsigned lit_;
    bool dirty_;
};

} // namespace iskra
