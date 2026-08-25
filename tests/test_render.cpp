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

    Frame(const Renderer & r, const Screen & s, const Raster * g = 0)
        : px(static_cast<std::size_t>(r.width() + 7) * r.height(), 0xDEADBEEF),
          pitch(r.width() + 7)
    {
        r.draw(s, &px[0], pitch, g);
    }

    uint32_t at(unsigned x, unsigned y) const
    {
        return px[static_cast<std::size_t>(y) * pitch + x];
    }
};

// Точка текстового блока: он стоит посреди растра трубки, и от начала кадра
// его отделяют поля.
uint32_t text_at(const Frame & f, const Renderer & r, unsigned x, unsigned y)
{
    return f.at(r.margin_x() * r.scale_x() + x, r.margin_y() * r.scale_y() + y);
}

Renderer make(unsigned sx, unsigned sy)
{
    Renderer r;
    r.set_scale(sx, sy);
    r.set_colors(FG, BG);
    return r;
}

Renderer make(unsigned scale) { return make(scale, scale); }

// Сверка знакоместа с знакогенератором: точка в точку, включая поля.
// Возвращает число расхождений. inv — знакоместо выделено позитивом.
unsigned check_cell(const Frame & f, const Renderer & r, unsigned col,
                    unsigned row, unsigned char code, bool inv)
{
    const Font & font = r.font();
    unsigned bad = 0;
    for (unsigned y = 0; y < font.cell_height(); ++y)
        for (unsigned x = 0; x < font.cell_width(); ++x) {
            bool on = false;
            if (x >= font.offset_x() && x - font.offset_x() < font.width() &&
                y >= font.offset_y() && y - font.offset_y() < font.height())
                on = font.dot(code, x - font.offset_x(), y - font.offset_y());
            const uint32_t want = (on != inv) ? FG : BG;
            if (text_at(f, r, col * font.cell_width() + x,
                        row * font.cell_height() + y) != want) ++bad;
        }
    return bad;
}

// Знакогенератор «Искры» — глиф 5x8 в знакоместе 7x10, значит 80x24
// знакомест дают текстовый блок 560x240. Кадр при этом — растр трубки,
// 560x256: по ширине блок занимает его целиком, полей остаётся по 8 точек
// сверху и снизу.
void test_size()
{
    Renderer r = make(1);
    CHECK_EQ(r.font().width(), 5u);
    CHECK_EQ(r.font().height(), 8u);
    CHECK_EQ(r.font().cell_width(), 7u);
    CHECK_EQ(r.font().cell_height(), 10u);
    CHECK_EQ(r.font().offset_x(), 0u);
    CHECK_EQ(r.font().offset_y(), 0u);

    CHECK_EQ(r.text_width(), 560u);
    CHECK_EQ(r.text_height(), 240u);
    CHECK_EQ(r.frame_width(), RASTER_WIDTH);
    CHECK_EQ(r.frame_height(), RASTER_HEIGHT);
    CHECK_EQ(r.margin_x(), 0u);
    CHECK_EQ(r.margin_y(), 8u);

    CHECK_EQ(r.width(), 560u);
    CHECK_EQ(r.height(), 256u);
    CHECK_EQ(r.pixels(), 560u * 256u);

    // Точка квадратная: сжатий больше нет.
    CHECK_EQ(DOT_TALL, 1u);
    for (unsigned k = 1; k <= 4; ++k) {
        r.set_scale(k, k * DOT_TALL);
        CHECK_EQ(r.width(), 560u * k);
        CHECK_EQ(r.height(), 256u * k);
    }
}

// Чистый экран — это 80x24 пробела, и ни одной светлой точки на кадре.
// Поля вокруг текстового блока тоже тёмные, а не мусор буфера.
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

    CHECK_EQ(check_cell(f, r, 2, 1, 0x41, false), 0u);
    // Соседние знакоместа остались пробелами.
    CHECK_EQ(check_cell(f, r, 3, 1, 0x20, false), 0u);
    CHECK_EQ(check_cell(f, r, 2, 0, 0x20, false), 0u);

    unsigned lit = 0;
    for (unsigned y = 0; y < font.height(); ++y)
        for (unsigned x = 0; x < font.width(); ++x)
            if (font.dot(0x41, x, y)) ++lit;
    CHECK(lit > 0);                    // глиф «A» не может быть пустым
}

// Поля знакоместа — не часть глифа: столбец справа от глифа и две нижние
// строки развёртки остаются фоном, что бы в знакоместе ни стояло. Из этого
// и получается просвет между буквами и между строками. Глиф 5 точек в
// знакоместе 6 — значит между соседними знаками ровно один тёмный столбец,
// и подряд идущие `Ш` не сливаются.
void test_cell_margins()
{
    Screen s;
    const uint8_t text[] = { 0xFB, 0xFB, 0xFB };     // ШШШ — глиф во всю ширину
    s.write(text, sizeof(text));

    const Renderer r = make(1);
    const Frame f(r, s);
    const Font & font = r.font();
    const unsigned cw = font.cell_width(), chh = font.cell_height();

    // Столбцы поля — те, что вне глифа.
    unsigned lit = 0;
    for (unsigned c = 0; c < 3; ++c)
        for (unsigned y = 0; y < chh; ++y)
            for (unsigned x = 0; x < cw; ++x) {
                if (x >= font.offset_x() && x - font.offset_x() < font.width())
                    continue;
                if (text_at(f, r, c * cw + x, y) != BG) ++lit;
            }
    CHECK_EQ(lit, 0u);
    CHECK_EQ(cw - font.width(), 2u);   // ровно два столбца просвета

    for (unsigned x = 0; x < 3 * cw; ++x) {
        CHECK_EQ(text_at(f, r, x, font.height()), BG); // межстрочный интервал
        CHECK_EQ(text_at(f, r, x, chh - 1), BG);       // строка курсора
    }

    // Крайние столбцы самого `Ш` горят — значит просвет держится полем, а
    // не пустотой внутри глифа.
    CHECK(font.dot(0xFB, 0, 1));
    CHECK(font.dot(0xFB, font.width() - 1, 1));
}

// Кириллица — половина всего, что «Искра» показывает. Экран семибитный, и
// строчных букв у него нет вовсе: «а» высвечивается как «А».
void test_cyrillic()
{
    Screen s;
    const uint8_t text[] = { 0xE1, 0xC1, 0xF0, 0xFF };   // «АаПЪ» в КОИ-8
    const uint8_t want[] = { 0x61, 0x61, 0x70, 0x5F };   // то же в КОИ-7 Н2
    s.write(text, sizeof(text));

    const Renderer r = make(1);
    const Frame f(r, s);
    const Font & font = r.font();

    for (unsigned i = 0; i < sizeof(text); ++i) {
        CHECK_EQ(s.cell(1, i + 1).ch, want[i]);
        CHECK_EQ(check_cell(f, r, i, 0, want[i], false), 0u);
        unsigned lit = 0;
        for (unsigned y = 0; y < font.height(); ++y)
            for (unsigned x = 0; x < font.width(); ++x)
                if (font.dot(want[i], x, y)) ++lit;
        CHECK(lit > 0);
    }

    // Ъ у «Искры» стоит в позиции 5F — там, где у КОИ-7 Н2 подчёркивание, —
    // и это буква, а не заливка. От Ь он отличается тем, что сдвинут на
    // столбец вправо, а слева вверху добавлена перекладина.
    unsigned full = 0;
    for (unsigned y = 0; y < font.height(); ++y) {
        unsigned row = 0;
        for (unsigned x = 0; x < font.width(); ++x)
            if (font.dot(0x5F, x, y)) ++row;
        if (row == font.width()) ++full;
    }
    CHECK_EQ(full, 0u);
    CHECK(font.dot(0x5F, 0, 1) && font.dot(0x5F, 1, 1));
    CHECK(!font.dot(0x5F, 0, 2) && font.dot(0x5F, 1, 2));

    // А позиция 7F, где у КОИ-7 Н2 стоит Ъ, у «Искры» пуста.
    bool del_blank = true;
    for (unsigned y = 0; y < font.height(); ++y)
        if (font.glyph(0x7F)[y]) del_blank = false;
    CHECK(del_blank);
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
    CHECK_EQ(check_cell(f, r, 0, 0, 0x41, true), 0u);
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
    CHECK_EQ(check_cell(Frame(r, s), r, 0, 0, 0x41, false), 0u);

    r.set_cursor(true);
    {
        const Frame f(r, s);
        // Нижняя строка знакоместа закрашена целиком, во всю его ширину.
        for (unsigned x = 0; x < font.cell_width(); ++x)
            CHECK_EQ(text_at(f, r, x, last), FG);
        // Сам знак курсор не трогает: строки выше остались как были.
        unsigned bad = 0;
        for (unsigned y = 0; y < font.height(); ++y)
            for (unsigned x = 0; x < font.width(); ++x) {
                const uint32_t want = font.dot(0x41, x, y) ? FG : BG;
                if (text_at(f, r, font.offset_x() + x,
                            font.offset_y() + y) != want) ++bad;
            }
        CHECK_EQ(bad, 0u);
        // Соседнее знакоместо курсором не задето.
        CHECK_EQ(text_at(f, r, font.cell_width() + 3, last), BG);
    }

    // На выделенном знакоместе черта выходит тёмной на светлом — и всё равно
    // видна.
    Screen p;
    p.put(CC_POSITIVE);
    p.put(0x20);                       // выделенный пробел
    p.at(1, 1);
    const Frame f(r, p);
    for (unsigned x = 0; x < font.cell_width(); ++x) {
        CHECK_EQ(text_at(f, r, x, last), BG);
        // Остальное знакоместо — светлое поле; а вот за его краем снова
        // тёмный растр: поля кадра выделением не задеты.
        CHECK_EQ(text_at(f, r, x, 0), FG);
    }
    CHECK_EQ(f.at(0, 0), BG);
}

// При увеличении каждая точка становится квадратом scale x scale.
void test_scale()
{
    Screen s;
    s.put(0x41);

    // Нарочно разные множители по осям: точка не квадратная.
    const unsigned SX = 3, SY = 3 * DOT_TALL;
    const Renderer r1 = make(1), r3 = make(SX, SY);
    const Frame f1(r1, s), f3(r3, s);

    unsigned mismatch = 0;
    for (unsigned y = 0; y < r1.font().cell_height(); ++y)
        for (unsigned x = 0; x < r1.font().cell_width(); ++x) {
            const uint32_t want = text_at(f1, r1, x, y);
            for (unsigned dy = 0; dy < SY; ++dy)
                for (unsigned dx = 0; dx < SX; ++dx)
                    if (text_at(f3, r3, x * SX + dx, y * SY + dy) != want)
                        ++mismatch;
        }
    CHECK_EQ(mismatch, 0u);
}

// Графика поверх знакомест. Трубка одна, а устройств два, и как она их
// смешивает, книга не говорит вовсе: взято исключающее или, чтобы линия была
// видна и на пустом поле, и поверх знака. Сложение остаётся под рукой.
void test_overlay()
{
    Screen s;
    s.at(1, 1);
    const char * t = "\xE1";                  // «А» в КОИ-8
    s.write(reinterpret_cast<const uint8_t *>(t), 1);

    Renderer r = make(1);
    const Font & font = r.font();

    // Точка, которая заведомо горит в глифе, и точка, которая заведомо нет.
    unsigned gx = 0, gy = 0;
    bool found = false;
    for (unsigned y = 0; !found && y < font.height(); ++y)
        for (unsigned x = 0; !found && x < font.width(); ++x)
            if (font.dot(0x61, x, y)) { gx = x; gy = y; found = true; }
    CHECK(found);

    // Растр: зажигаем ту же точку кадра, что и глиф. Знакоместо первое,
    // поля кадра — сверху; у растра счёт снизу вверх.
    Raster g;
    const unsigned fx = r.margin_x() + gx;
    const unsigned fy = r.margin_y() + gy;
    g.plot(fx, RASTER_HEIGHT - 1 - fy);

    {
        const Frame f(r, s, &g);
        // Исключающее или: знак и графика в одной точке гасят друг друга.
        CHECK_EQ(f.at(fx, fy), BG);
    }
    {
        r.set_overlay(Renderer::OVERLAY_OR);
        const Frame f(r, s, &g);
        CHECK_EQ(f.at(fx, fy), FG);
    }
}

// Лист графопостроителя — не экран: тёмное перо на светлой бумаге, и знаков
// на нём нет вовсе.
void test_paper()
{
    Renderer r = make(1);
    const uint32_t INK = pack_rgb(0x11, 0x22, 0x33);
    const uint32_t PAPER = pack_rgb(0xEE, 0xDD, 0xCC);
    r.set_paper_colors(INK, PAPER);

    Raster g;
    g.plot(5, 7);

    std::vector<uint32_t> px(static_cast<std::size_t>(r.width() + 7) * r.height(),
                             0xDEADBEEF);
    r.draw_raster(g, &px[0], r.width() + 7);

    const std::size_t pitch = r.width() + 7;
    const unsigned row = RASTER_HEIGHT - 1 - 7;
    CHECK_EQ(px[row * pitch + 5], INK);
    CHECK_EQ(px[row * pitch + 6], PAPER);
    CHECK_EQ(px[0], PAPER);
}

} // namespace

int main()
{
    test_size();
    test_overlay();
    test_paper();
    test_blank();
    test_glyph();
    test_cell_margins();
    test_cyrillic();
    test_positive();
    test_cursor();
    test_scale();
    return test::summary("test_render");
}
