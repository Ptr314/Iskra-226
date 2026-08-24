// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Mikhail Revzin <p3.141592653589793238462643@gmail.com>
// Part of the Iskra-226 project: https://github.com/Ptr314/Iskra-226
// Description: растеризатор экрана — знакоместо, глифы, выделение, курсор

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

// Сверка знакоместа с знакогенератором: точка в точку, включая поля.
// Возвращает число расхождений. inv — знакоместо выделено позитивом.
unsigned check_cell(const Frame & f, const Font & font, unsigned col,
                    unsigned row, unsigned char code, bool inv)
{
    unsigned bad = 0;
    for (unsigned y = 0; y < font.cell_height(); ++y)
        for (unsigned x = 0; x < font.cell_width(); ++x) {
            bool on = false;
            if (x >= font.offset_x() && x - font.offset_x() < font.width() &&
                y >= font.offset_y() && y - font.offset_y() < font.height())
                on = font.dot(code, x - font.offset_x(), y - font.offset_y());
            const uint32_t want = (on != inv) ? FG : BG;
            if (f.at(col * font.cell_width() + x,
                     row * font.cell_height() + y) != want) ++bad;
        }
    return bad;
}

// Достоверный знакогенератор — глиф 7x8 в знакоместе 9x10, значит 80x24
// знакомест дают 720x240 точки. Целое увеличение умножает обе стороны.
void test_size()
{
    Renderer r = make(1);
    CHECK_EQ(r.font().width(), 7u);
    CHECK_EQ(r.font().height(), 8u);
    CHECK_EQ(r.font().cell_width(), 9u);
    CHECK_EQ(r.font().cell_height(), 10u);
    CHECK_EQ(r.font().offset_x(), 1u);
    CHECK_EQ(r.font().offset_y(), 0u);

    CHECK_EQ(r.width(), 720u);
    CHECK_EQ(r.height(), 240u);
    CHECK_EQ(r.pixels(), 720u * 240u);

    r.set_scale(3);
    CHECK_EQ(r.width(), 2160u);
    CHECK_EQ(r.height(), 720u);
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

// Точка в точку со знакогенератором, с учётом полей знакоместа.
void test_glyph()
{
    Screen s;
    s.at(2, 3);
    s.put(0x41);                       // «A» в КОИ-8 — та же, что в ASCII

    const Renderer r = make(1);
    const Frame f(r, s);
    const Font & font = r.font();

    CHECK_EQ(check_cell(f, font, 2, 1, 0x41, false), 0u);
    // Соседние знакоместа остались пробелами.
    CHECK_EQ(check_cell(f, font, 3, 1, 0x20, false), 0u);
    CHECK_EQ(check_cell(f, font, 2, 0, 0x20, false), 0u);

    unsigned lit = 0;
    for (unsigned y = 0; y < font.height(); ++y)
        for (unsigned x = 0; x < font.width(); ++x)
            if (font.dot(0x41, x, y)) ++lit;
    CHECK(lit > 0);                    // глиф «A» не может быть пустым
}

// Поля знакоместа — не часть глифа: столбцы слева и справа и две нижние
// строки развёртки остаются фоном, что бы в знакоместе ни стояло. Из этого
// и получается межбуквенный просвет.
void test_cell_margins()
{
    Screen s;
    const uint8_t text[] = { 0xFB, 0xFB, 0xFB };     // ШШШ — глиф во всю ширину
    s.write(text, sizeof(text));

    const Renderer r = make(1);
    const Frame f(r, s);
    const Font & font = r.font();
    const unsigned cw = font.cell_width(), chh = font.cell_height();

    unsigned lit = 0;
    for (unsigned c = 0; c < 3; ++c)
        for (unsigned y = 0; y < chh; ++y) {
            if (f.at(c * cw, y) != BG) ++lit;                    // столбец слева
            if (f.at(c * cw + cw - 1, y) != BG) ++lit;           // и справа
        }
    CHECK_EQ(lit, 0u);

    for (unsigned x = 0; x < 3 * cw; ++x) {
        CHECK_EQ(f.at(x, font.height()), BG);          // межстрочный интервал
        CHECK_EQ(f.at(x, chh - 1), BG);                // строка курсора
    }
}

// Кириллица — половина всего, что «Искра» показывает. Ъ в исходном ПЗУ не
// было (там сплошная заливка), он дорисован — и должен быть буквой.
void test_cyrillic()
{
    Screen s;
    const uint8_t text[] = { 0xE1, 0xC1, 0xF0, 0xFF };   // «АаПЪ» в КОИ-8
    s.write(text, sizeof(text));

    const Renderer r = make(1);
    const Frame f(r, s);
    const Font & font = r.font();

    for (unsigned i = 0; i < sizeof(text); ++i) {
        CHECK_EQ(check_cell(f, font, i, 0, text[i], false), 0u);
        unsigned lit = 0;
        for (unsigned y = 0; y < font.height(); ++y)
            for (unsigned x = 0; x < font.width(); ++x)
                if (font.dot(text[i], x, y)) ++lit;
        CHECK(lit > 0);
    }

    // «А» и «а» — разные знаки, а не один глиф на оба регистра.
    bool same = true;
    for (unsigned y = 0; y < font.height(); ++y)
        if (font.glyph(0xE1)[y] != font.glyph(0xC1)[y]) same = false;
    CHECK(!same);

    // Ъ — не заливка: в исходном ПЗУ FF был закрашен целиком.
    unsigned full = 0;
    for (unsigned y = 0; y < font.height(); ++y) {
        unsigned row = 0;
        for (unsigned x = 0; x < font.width(); ++x)
            if (font.dot(0xFF, x, y)) ++row;
        if (row == font.width()) ++full;
    }
    CHECK_EQ(full, 0u);
    // И он отличается от Ь ровно перекладиной в верхней строке.
    unsigned diff = 0;
    for (unsigned y = 0; y < font.height(); ++y)
        if (font.glyph(0xFF)[y] != font.glyph(0xF8)[y]) ++diff;
    CHECK_EQ(diff, 1u);
    CHECK(font.dot(0xFF, 0, 0));
}

// Позитив — выделение: тёмный знак на светлом поле (руководство, разд. 17.1).
// Переставляется всё знакоместо целиком, вместе с полями, иначе выделенная
// строка вышла бы в полоску.
void test_positive()
{
    Screen s;
    s.put(CC_POSITIVE);
    s.put(0x41);

    const Renderer r = make(1);
    const Frame f(r, s);
    CHECK_EQ(check_cell(f, r.font(), 0, 0, 0x41, true), 0u);
}

// Курсор — подстрочная черта (руководство, разд. 2.1: «указывается с помощью
// курсора (подстрочной черты)»), а не заливка знакоместа: в разд. 3.5 книги
// его «подводят под» символ и стирают символ «над курсором».
void test_cursor()
{
    Screen s;
    s.put(0x41);                       // знак в первом знакоместе
    s.at(1, 1);                        // курсор вернулся под него

    Renderer r = make(1);
    const Font & font = r.font();
    const unsigned last = font.cell_height() - 1;

    r.set_cursor(false);
    CHECK_EQ(check_cell(Frame(r, s), font, 0, 0, 0x41, false), 0u);

    r.set_cursor(true);
    {
        const Frame f(r, s);
        // Нижняя строка знакоместа закрашена целиком, во всю его ширину.
        for (unsigned x = 0; x < font.cell_width(); ++x)
            CHECK_EQ(f.at(x, last), FG);
        // Сам знак курсор не трогает: строки выше остались как были.
        unsigned bad = 0;
        for (unsigned y = 0; y < font.height(); ++y)
            for (unsigned x = 0; x < font.width(); ++x) {
                const uint32_t want = font.dot(0x41, x, y) ? FG : BG;
                if (f.at(font.offset_x() + x, font.offset_y() + y) != want) ++bad;
            }
        CHECK_EQ(bad, 0u);
        // Соседнее знакоместо курсором не задето.
        CHECK_EQ(f.at(font.cell_width() + 3, last), BG);
    }

    // На выделенном знакоместе черта выходит тёмной на светлом — и всё равно
    // видна.
    Screen p;
    p.put(CC_POSITIVE);
    p.put(0x20);                       // выделенный пробел
    p.at(1, 1);
    const Frame f(r, p);
    for (unsigned x = 0; x < font.cell_width(); ++x) {
        CHECK_EQ(f.at(x, last), BG);
        CHECK_EQ(f.at(x, 0), FG);      // остальное знакоместо — светлое поле
    }
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
    for (unsigned y = 0; y < r1.font().cell_height(); ++y)
        for (unsigned x = 0; x < r1.font().cell_width(); ++x) {
            const uint32_t want = f1.at(x, y);
            for (unsigned dy = 0; dy < SC; ++dy)
                for (unsigned dx = 0; dx < SC; ++dx)
                    if (f3.at(x * SC + dx, y * SC + dy) != want) ++mismatch;
        }
    CHECK_EQ(mismatch, 0u);
}

// Крупные шрифты остаются доступными; у них знакоместо равно глифу.
void test_large_font()
{
    const Font * big = Font::by_height(16);
    CHECK(big != 0);
    if (!big) return;

    CHECK_EQ(big->width(), 8u);
    CHECK_EQ(big->cell_width(), 8u);
    CHECK_EQ(big->cell_height(), 16u);

    Renderer r = make(1);
    r.set_font(*big);
    CHECK_EQ(r.width(), 640u);
    CHECK_EQ(r.height(), 384u);

    Screen s;
    s.put(0x41);
    CHECK_EQ(check_cell(Frame(r, s), *big, 0, 0, 0x41, false), 0u);
}

} // namespace

int main()
{
    test_size();
    test_blank();
    test_glyph();
    test_cell_margins();
    test_cyrillic();
    test_positive();
    test_cursor();
    test_scale();
    test_large_font();
    return test::summary("test_render");
}
