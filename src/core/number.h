// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Mikhail Revzin <p3.141592653589793238462643@gmail.com>
// Part of the Iskra-226 project: https://github.com/Ptr314/Iskra-226
// Description: число BASIC 02 — десятичное, 13 значащих разрядов

#pragma once

#include <cstdint>
#include <string>

namespace iskra {

// Арифметика «Искры» двоично-десятичная: 13 значащих разрядов, порядок
// от -99 до 99. Считать в double нельзя — расхождения видны сразу: .01
// в двоичном виде непредставима, и цикл FOR T=-6 TO 60 STEP .01 набегает
// не туда. Поэтому сложение, вычитание, умножение и деление сделаны в
// десятичной системе точно.
//
// SQR и возведение в дробную степень считаются через double и округляются
// до 13 разрядов: своих алгоритмов ПЗУ у нас нет, и повторить его округление
// невозможно. Целые степени возводятся точно, повторным умножением.
class Number
{
public:
    static const unsigned DIGITS = 13;
    static const int EXP_MAX = 99;
    static const int EXP_MIN = -99;

    Number();

    static Number from_int(long v);
    static Number from_double(double v);

    // Разбор десятичной записи: [-]цифры[.цифры][E[-]цифры].
    // Возвращает false, если запись не разобралась целиком.
    static bool parse(const std::string & s, Number & out);

    bool is_zero() const { return d_[0] == 0; }
    bool is_negative() const { return neg_ && !is_zero(); }

    // Целое ли число и помещается ли в long.
    bool to_int(long & out) const;
    double to_double() const;

    // Знак и цифры так, как их печатает PRINT: для неотрицательных на месте
    // знака пробел. Пробел после числа добавляет уже оператор печати —
    // «числа выведены вплотную друг за другом с учетом знака перед числом
    // и пробела после числа» (руководство, разд. 4.4).
    std::string to_display() const;

    Number negated() const;

    // Все операции возвращают false при переполнении порядка или делении
    // на нуль — исключений в ядре нет.
    static bool add(const Number & a, const Number & b, Number & r);
    static bool sub(const Number & a, const Number & b, Number & r);
    static bool mul(const Number & a, const Number & b, Number & r);
    static bool div(const Number & a, const Number & b, Number & r);
    static bool pow(const Number & a, const Number & b, Number & r);
    static bool sqrt(const Number & a, Number & r);

    // -1, 0, 1
    int compare(const Number & b) const;

    static const Number & pi();

private:
    // Значение = ±d_[0].d_[1]…d_[12] * 10^exp_, d_[0] в 1..9.
    // Нуль: все разряды нулевые, exp_ = 0.
    bool neg_;
    int exp_;
    uint8_t d_[DIGITS];

    void set_zero();
    // Собрать из буфера разрядов: значение = buf[0].buf[1]… * 10^e.
    // Разряды могут быть любыми неотрицательными — переносы разбираются тут.
    bool set_from(const int * buf, unsigned n, int e, bool neg);
    static int cmp_mag(const Number & a, const Number & b);
    static bool add_mag(const Number & a, const Number & b, bool neg, Number & r);
    static bool sub_mag(const Number & a, const Number & b, bool neg, Number & r);
};

} // namespace iskra