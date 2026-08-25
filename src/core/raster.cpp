// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Mikhail Revzin <p3.141592653589793238462643@gmail.com>
// Part of the Iskra-226 project: https://github.com/Ptr314/Iskra-226
// Description: графический растр 560x256 — вторая половина БОСГИ, ФАУ 10

#include "core/raster.h"

namespace iskra {

void Raster::clear()
{
    if (lit_) {
        bits_.assign(RASTER_WIDTH * RASTER_HEIGHT, 0);
        lit_ = 0;
    }
    dirty_ = true;
}

void Raster::plot(long x, long y)
{
    if (x < 0 || y < 0) return;
    if (x >= static_cast<long>(RASTER_WIDTH)) return;
    if (y >= static_cast<long>(RASTER_HEIGHT)) return;

    const unsigned row = RASTER_HEIGHT - 1 - static_cast<unsigned>(y);
    uint8_t & b = bits_[row * RASTER_WIDTH + static_cast<unsigned>(x)];
    if (!b) { b = 1; ++lit_; }
    dirty_ = true;
}

void Raster::line(long x0, long y0, long x1, long y1)
{
    // Брезенхем в целых: у машины растр, и дробностей тут взяться неоткуда.
    long dx = x1 - x0, dy = y1 - y0;
    const long sx = (dx < 0) ? -1 : 1;
    const long sy = (dy < 0) ? -1 : 1;
    if (dx < 0) dx = -dx;
    if (dy < 0) dy = -dy;

    long err = dx - dy;
    for (;;) {
        plot(x0, y0);
        if (x0 == x1 && y0 == y1) break;
        const long e2 = 2 * err;
        if (e2 > -dy) { err -= dy; x0 += sx; }
        if (e2 < dx)  { err += dx; y0 += sy; }
    }
}

} // namespace iskra
