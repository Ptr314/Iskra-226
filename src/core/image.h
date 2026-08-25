// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Mikhail Revzin <p3.141592653589793238462643@gmail.com>
// Part of the Iskra-226 project: https://github.com/Ptr314/Iskra-226
// Description: образ печати — общая часть PRINTUSING, % и CONVERT

#pragma once

#include <string>

#include "core/number.h"

namespace iskra {

// Описание формата внутри образа: `[+|-] ### [. ###] [^^^^]` (руководство,
// разд. 13.6 и 16.2). У `CONVERT` такое описание одно и занимает весь образ;
// у оператора `%` их может быть несколько, а между ними стоит текст, который
// «воспроизводится при печати» как есть.
struct ImageField {
    ImageField() : at(0), len(0), sign(0), ip(0), fp(0), dot(false),
                   exponential(false) {}

    unsigned at;            // смещение начала в образе
    unsigned len;           // сколько знаков образа занято
    unsigned sign;          // 0 нет, 1 «+», 2 «−»
    unsigned ip, fp;        // знаков # до и после точки
    bool dot;               // точка есть — она печатается и без дробных цифр
    bool exponential;       // за мантиссой стоит ^^^^

    // Ширина поля в знаках печати. От len отличается тем, что показатель
    // степени в листингах пишут и как `^^^^`, и как `/\/\/\/\`.
    unsigned width() const
    {
        return (sign ? 1u : 0u) + ip + (dot ? 1u : 0u) + fp +
               (exponential ? 4u : 0u);
    }
};

// Следующее описание формата, начиная с позиции from. false — дальше в
// образе описаний нет вовсе.
bool image_next_field(const std::string & image, unsigned from, ImageField & f);

// Весь образ — одно описание и ничего кроме него. Это форма `CONVERT`.
bool image_single_field(const std::string & image, ImageField & f);

// Подставить число в описание. pad_zero задаёт, чем заполнены незанятые
// разряды целой части: `CONVERT` ставит нули (разд. 13.6), `PRINTUSING` —
// пробелы (разд. 16.2). false — целая часть в описание не помещается.
bool image_number(const Number & value, const ImageField & f, bool pad_zero,
                  std::string & out);

// Подставить символьное значение: оно прижимается влево, а лишнее
// отбрасывается — «если длина символьного элемента больше числа символов #,
// остальные символы не печатаются» (разд. 16.2).
void image_string(const std::string & s, const ImageField & f, std::string & out);

} // namespace iskra
