// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Mikhail Revzin <p3.141592653589793238462643@gmail.com>
// Part of the Iskra-226 project: https://github.com/Ptr314/Iskra-226
// Description: блок отображения символьной информации (ФАУ 05, половина БОСГИ)

#include "core/screen.h"

#include "core/koi8.h"

namespace iskra {

namespace {
    const uint8_t SPACE = 0x20;
}

Screen::Screen()
{
    clear();
}

void Screen::clear()
{
    for (unsigned i = 0; i < SCREEN_ROWS * SCREEN_COLS; ++i) {
        cells_[i].ch = SPACE;
        cells_[i].attr = ATTR_NEGATIVE;
    }
    row_ = 1;
    col_ = 1;
    attr_ = ATTR_NEGATIVE;
    dirty_ = true;
    cleared_ = true;
    bells_ = 0;
}

Cell & Screen::at_(unsigned row, unsigned col)
{
    return cells_[(row - 1) * SCREEN_COLS + (col - 1)];
}

const Cell & Screen::cell(unsigned row, unsigned col) const
{
    if (row < 1) row = 1;
    if (row > SCREEN_ROWS) row = SCREEN_ROWS;
    if (col < 1) col = 1;
    if (col > SCREEN_COLS) col = SCREEN_COLS;
    return cells_[(row - 1) * SCREEN_COLS + (col - 1)];
}

void Screen::scroll_up()
{
    for (unsigned i = 0; i < (SCREEN_ROWS - 1) * SCREEN_COLS; ++i)
        cells_[i] = cells_[i + SCREEN_COLS];
    for (unsigned i = (SCREEN_ROWS - 1) * SCREEN_COLS;
         i < SCREEN_ROWS * SCREEN_COLS; ++i) {
        cells_[i].ch = SPACE;
        cells_[i].attr = ATTR_NEGATIVE;
    }
    dirty_ = true;
}

void Screen::advance()
{
    if (++col_ > SCREEN_COLS) {
        // «Если текст выходит за пределы строки экрана (80 символов), то
        // курсор автоматически переходит в начало следующей строки»
        col_ = 1;
        if (++row_ > SCREEN_ROWS) {
            row_ = SCREEN_ROWS;
            scroll_up();
        }
    }
}

void Screen::put(uint8_t ch)
{
    switch (ch) {
        case CC_HOME:
            row_ = 1; col_ = 1;
            return;
        case CC_CLEAR:
            clear();
            return;
        case CC_BELL:
            ++bells_;
            return;
        case CC_LEFT:
            if (col_ > 1) --col_;
            return;
        case CC_RIGHT:
            if (col_ < SCREEN_COLS) ++col_;
            return;
        case CC_DOWN:
            if (row_ < SCREEN_ROWS) ++row_; else scroll_up();
            return;
        case CC_UP:
            if (row_ > 1) --row_;
            return;
        case CC_CR:
            col_ = 1;
            return;
        case CC_POSITIVE:
            attr_ = ATTR_POSITIVE;
            return;
        case CC_NEGATIVE:
            attr_ = ATTR_NEGATIVE;
            return;
        default:
            break;
    }

    // Прочие коды ниже пробела на экран не выводятся: часть из них
    // (НС, СУ...) пока не опознана, остальные позиции таблицы пусты.
    if (ch < SPACE) return;

    Cell & c = at_(row_, col_);
    // **Экран семибитный.** Знакогенератор «Искры» держит 96 знаков — коды
    // 20-7F кодировки КОИ-7 Н2 (рис. 3.1 руководства по БОСГИ), — и старший
    // бит кода до трубки не доходит вовсе. Внутри эмулятора текст живёт в
    // КОИ-8, а там прописная кириллица лежит в E0-FF, то есть ровно с
    // единицей в старшем бите: сброс его и даёт КОИ-7 Н2 знак в знак.
    //
    // Заодно строчные буквы приводятся к прописным: строчных у машины нет
    // ни одной, показать их нечем, а без приведения строчная кириллица
    // высветилась бы латиницей — у неё старший бит тоже единица, но лежит
    // она в КОИ-8 ниже прописной (koi8.h, «семибитная граница машины»).
    c.ch = koi8_to_koi7(ch);
    c.attr = attr_;
    dirty_ = true;
    advance();
}

void Screen::write(const uint8_t * data, unsigned len)
{
    for (unsigned i = 0; i < len; ++i) put(data[i]);
}

void Screen::at(unsigned row, unsigned col)
{
    if (row < 1) row = 1;
    if (row > SCREEN_ROWS) row = SCREEN_ROWS;
    if (col < 1) col = 1;
    if (col > SCREEN_COLS) col = SCREEN_COLS;
    row_ = row;
    col_ = col;
}

void Screen::erase(unsigned count)
{
    unsigned r = row_;
    unsigned c = col_;
    while (count-- > 0) {
        Cell & cell = at_(r, c);
        cell.ch = SPACE;
        cell.attr = ATTR_NEGATIVE;
        if (++c > SCREEN_COLS) {
            c = 1;
            if (++r > SCREEN_ROWS) break;
        }
    }
    dirty_ = true;
}

unsigned Screen::take_bells()
{
    unsigned n = bells_;
    bells_ = 0;
    return n;
}

} // namespace iskra