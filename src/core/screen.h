// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Mikhail Revzin <p3.141592653589793238462643@gmail.com>
// Part of the Iskra-226 project: https://github.com/Ptr314/Iskra-226
// Description: блок отображения символьной информации (БОСГИ, ФАУ 05)

#pragma once

#include <cstdint>

namespace iskra {

// «24 строки по 80 символов в каждой» — руководство, разд. 4.
const unsigned SCREEN_COLS = 80;
const unsigned SCREEN_ROWS = 24;

// Коды управления БОСГИ, приложение 1 руководства.
enum ControlCode {
    CC_HOME     = 0x01,   // НЗ  — курсор в левый верхний угол
    CC_CLEAR    = 0x03,   // КТ  — очистка экрана и курсор в левый верхний угол
    CC_BELL     = 0x07,   // ЗВ  — звуковой сигнал
    CC_LEFT     = 0x08,   // ВШ  — курсор на символ влево
    CC_RIGHT    = 0x09,   // ГТ  — курсор на символ вправо
    CC_DOWN     = 0x0A,   // ПС  — курсор на строку вниз
    CC_UP       = 0x0C,   // ПФ  — курсор на строку вверх
    CC_CR       = 0x0D,   // ВК  — курсор в первую позицию текущей строки
    CC_POSITIVE = 0x11,   // СУ1 — позитивное изображение
    CC_NEGATIVE = 0x12    // СУ2 — негативное изображение
};

// Атрибут ячейки. Экран «Искры» знает только позитив и негатив.
enum Attr {
    ATTR_POSITIVE = 0,
    ATTR_NEGATIVE = 1
};

struct Cell {
    uint8_t ch;     // код КОИ-8
    uint8_t attr;
};

// Знакоместный буфер экрана. Ничего не знает ни про SDL, ни про растр:
// хост забирает отсюда ячейки и рисует их своим знакогенератором.
//
// Строки и позиции нумеруются с единицы — как в операторе AT(строка, позиция).
class Screen
{
public:
    Screen();

    // Пробелы во всех ячейках, курсор в (1,1), атрибут — позитив.
    void clear();

    // Печать символа или отработка кода управления.
    void put(uint8_t ch);
    void write(const uint8_t * data, unsigned len);

    // AT(строка, позиция): установка курсора. Значения вне экрана
    // отсекаются по краям.
    void at(unsigned row, unsigned col);

    // Третий аргумент AT: стереть count символов вправо от курсора,
    // с переходом на следующие строки. Курсор остаётся на месте.
    void erase(unsigned count);

    unsigned row() const { return row_; }
    unsigned col() const { return col_; }
    uint8_t attr() const { return attr_; }

    const Cell & cell(unsigned row, unsigned col) const;

    // Признак того, что с прошлого сброса содержимое менялось;
    // хост перерисовывает кадр только при необходимости.
    bool dirty() const { return dirty_; }
    void clear_dirty() { dirty_ = false; }

    // Звонок ЗВ: счётчик неотработанных сигналов для хоста.
    unsigned take_bells();

private:
    void advance();
    void scroll_up();
    Cell & at_(unsigned row, unsigned col);

    Cell cells_[SCREEN_ROWS * SCREEN_COLS];
    unsigned row_;
    unsigned col_;
    uint8_t attr_;
    bool dirty_;
    unsigned bells_;
};

} // namespace iskra