// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Mikhail Revzin <p3.141592653589793238462643@gmail.com>
// Part of the Iskra-226 project: https://github.com/Ptr314/Iskra-226
// Description: растеризатор экрана — размеры кадра, глифы, негатив, курсор

#include <vector>

#include "check.h"
#include "core/screen.h"
#include "host_common/renderer.h"

using namespace iskra;

namespace {

const uint32_t FG = pack_rgb(0xFF, 0xFF, 0xFF);
const uint32_t BG = pack_rgb(0x00, 0x00, 0x00);

// Кадр с запасом по длине строки: pitch нарочно больше ширины, чтобы поймать
// путаницу между ними — у Windows строки DIB выровнены, и там так и будет.
struct Frame
{
    std::vector<uint32_t> px;
    unsigned pitch;

    Frame(const Renderer & r, const Screen & s)
        : px(static_cast<std::size_t>(r.width() + 7) * r.height(), 0xDEADBEEF),
          pitch(r.width() + 7)
    {
        r.draw(s, &px[0], pitch);
    }

    uint32_t at(unsigned x, unsigned y) const
    {
        return px[static_cast<std::size_t>(y) * pitch + x];
    }
};

Renderer make(unsigned scale)
{
    Renderer r;
    r.set_scale(scale);
    r.set_colors(FG, BG);
    return r;
}

// 80x24 знакоместа шрифтом 8x16 — это 640x384 точки, и целое увеличение
// умножает обе стороны.
void test_size()
{
    Renderer r = make(1);
    CHECK_EQ(r.width(), 640u);
    CHECK_EQ(r.height(), 384u);
    CHECK_EQ(r.pixels(), 640u * 384u);

    r.set_scale(3);
    CHECK_EQ(r.width(), 1920u);
    CHECK_EQ(r.height(), 1152u);
}

// Чистый экран — это 80x24 пробела, и ни одной светлой точки на кадре.
void test_blank()
{
    Screen s;
    const Renderer r = make(1);
    const Frame f(r, s);

    unsigned lit = 0;
    for (unsigned y = 0; y < r.height(); ++y)
        for (unsigned x = 0; x < r.width(); ++x)
            if (f.at(x, y) != BG) ++lit;
    CHECK_EQ(lit, 0u);
}

// Точка в точку со знакогенератором: старший бит байта развёртки — левая
// точка знакоместа.
void test_glyph()
{
    Screen s;
    s.at(2, 3);
    s.put(0x41);                       // «A» в КОИ-8 — та же, что в ASCII

    const Renderer r = make(1);
    const Frame f(r, s);
    const Font & font = r.font();
    const unsigned char * glyph = font.glyph(0x41);

    const unsigned x0 = 2 * font.width();     // третья позиция
    const unsigned y0 = 1 * font.height();    // вторая строка

    unsigned mismatch = 0, lit = 0;
    for (unsigned y = 0; y < font.height(); ++y)
        for (unsigned x = 0; x < font.width(); ++x) {
            const bool on = ((glyph[y] >> (font.width() - 1 - x)) & 1) != 0;
            if (on) ++lit;
            if (f.at(x0 + x, y0 + y) != (on ? FG : BG)) ++mismatch;
        }
    CHECK_EQ(mismatch, 0u);
    CHECK(lit > 0);                    // глиф «A» не может быть пустым

    // Соседнее знакоместо осталось пробелом.
    CHECK_EQ(f.at(x0 + font.width() + 1, y0 + 4), BG);
}

// Кириллица — половина всего, что «Искра» показывает, и знакогенератор у неё
// свой: коды C0-FF должны рисоваться, а не проваливаться в пустоту.
void test_cyrillic()
{
    Screen s;
    const uint8_t text[] = { 0xE1, 0xC1, 0xF0, 0xD0 };   // «АаПп» в КОИ-8
    s.write(text, sizeof(text));

    const Renderer r = make(1);
    const Frame f(r, s);
    const Font & font = r.font();

    for (unsigned i = 0; i < sizeof(text); ++i) {
        unsigned lit = 0, mismatch = 0;
        const unsigned char * glyph = font.glyph(text[i]);
        for (unsigned y = 0; y < font.height(); ++y)
            for (unsigned x = 0; x < font.width(); ++x) {
                const bool on = ((glyph[y] >> (font.width() - 1 - x)) & 1) != 0;
                if (on) ++lit;
                if (f.at(i * font.width() + x, y) != (on ? FG : BG)) ++mismatch;
            }
        CHECK_EQ(mismatch, 0u);
        CHECK(lit > 0);
    }

    // «А» и «а» — разные знаки, а не один глиф на оба регистра.
    bool same = true;
    for (unsigned y = 0; y < font.height(); ++y)
        if (font.glyph(0xE1)[y] != font.glyph(0xC1)[y]) same = false;
    CHECK(!same);
}

// Негатив меняет местами фон и цвет знака во всём знакоместе.
void test_negative()
{
    Screen s;
    s.put(CC_NEGATIVE);
    s.put(0x41);

    const Renderer r = make(1);
    const Frame f(r, s);
    const Font & font = r.font();
    const unsigned char * glyph = font.glyph(0x41);

    unsigned mismatch = 0;
    for (unsigned y = 0; y < font.height(); ++y)
        for (unsigned x = 0; x < font.width(); ++x) {
            const bool on = ((glyph[y] >> (font.width() - 1 - x)) & 1) != 0;
            if (f.at(x, y) != (on ? BG : FG)) ++mismatch;
        }
    CHECK_EQ(mismatch, 0u);
}

// Курсор — негатив того знакоместа, где он стоит. На негативном знакоместе
// он гасит негатив, а не пропадает: иначе его не было бы видно вовсе.
void test_cursor()
{
    Screen s;
    s.at(1, 1);

    Renderer r = make(1);
    const Font & font = r.font();

    r.set_cursor(false);
    CHECK_EQ(Frame(r, s).at(3, 3), BG);

    r.set_cursor(true);
    CHECK_EQ(Frame(r, s).at(3, 3), FG);

    // Второе знакоместо курсором не задето.
    CHECK_EQ(Frame(r, s).at(font.width() + 3, 3), BG);

    s.put(CC_NEGATIVE);
    s.put(0x20);                       // негативный пробел, курсор ушёл правее
    s.at(1, 1);
    CHECK_EQ(Frame(r, s).at(3, 3), BG);
}

// При увеличении каждая точка становится квадратом scale x scale.
void test_scale()
{
    Screen s;
    s.put(0x41);

    const unsigned SC = 3;
    const Renderer r1 = make(1), r3 = make(SC);
    const Frame f1(r1, s), f3(r3, s);

    unsigned mismatch = 0;
    for (unsigned y = 0; y < r1.font().height(); ++y)
        for (unsigned x = 0; x < r1.font().width(); ++x) {
            const uint32_t want = f1.at(x, y);
            for (unsigned dy = 0; dy < SC; ++dy)
                for (unsigned dx = 0; dx < SC; ++dx)
                    if (f3.at(x * SC + dx, y * SC + dy) != want) ++mismatch;
        }
    CHECK_EQ(mismatch, 0u);
}

} // namespace

int main()
{
    test_size();
    test_blank();
    test_glyph();
    test_cyrillic();
    test_negative();
    test_cursor();
    test_scale();
    return test::summary("test_render");
}
