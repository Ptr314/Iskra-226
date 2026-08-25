// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Mikhail Revzin <p3.141592653589793238462643@gmail.com>
// Part of the Iskra-226 project: https://github.com/Ptr314/Iskra-226
// Description: образ печати — общая часть PRINTUSING, % и CONVERT

#include "core/image.h"

#include <cstdio>

namespace iskra {

namespace {
    // Показатель степени в листингах корпуса изображается как ^^^^, а в
    // книге тот же знак распознан как /\/\/\/\ — принимаем оба написания.
    bool is_caret(char c) { return c == '^' || c == '\\' || c == '/'; }
}

bool image_next_field(const std::string & image, unsigned from, ImageField & f)
{
    const unsigned n = static_cast<unsigned>(image.size());
    for (unsigned p = from; p < n; ++p) {
        unsigned q = p;
        unsigned sign = 0;
        if (image[q] == '+' || image[q] == '-') {
            // Знак — часть описания только перед самими разрядами: иначе
            // это обычный текст, как дефис в `%U-КРИТ.=-######.####`.
            if (q + 1 >= n || image[q + 1] != '#') continue;
            sign = (image[q] == '+') ? 1u : 2u;
            ++q;
        } else if (image[q] != '#') {
            continue;
        }

        ImageField r;
        r.at = p;
        r.sign = sign;
        while (q < n && image[q] == '#') { ++r.ip; ++q; }
        if (q < n && image[q] == '.') {
            r.dot = true;
            ++q;
            while (q < n && image[q] == '#') { ++r.fp; ++q; }
        }
        if (q < n && is_caret(image[q])) {
            r.exponential = true;
            while (q < n && is_caret(image[q])) ++q;
        }
        r.len = q - p;
        f = r;
        return true;
    }
    return false;
}

bool image_single_field(const std::string & image, ImageField & f)
{
    if (!image_next_field(image, 0, f)) return false;
    return f.at == 0 && f.len == image.size();
}

bool image_number(const Number & value, const ImageField & f, bool pad_zero,
                  std::string & out)
{
    Number v = value;
    const bool negative = v.is_negative();
    if (negative) v = v.negated();

    std::string digits;
    int point = 0;
    v.to_digits(digits, point);           // значение = 0.digits * 10^point

    int exponent = 0;
    if (f.exponential) {
        // Мантисса приводится к виду с ip разрядами до точки, остальное
        // уходит в показатель степени.
        if (!v.is_zero()) exponent = point - static_cast<int>(f.ip);
        point = static_cast<int>(f.ip);
    }

    const int nd = static_cast<int>(digits.size());
    std::string whole, frac;
    for (int i = 0; i < point; ++i)
        whole += (i < nd) ? digits[static_cast<unsigned>(i)] : '0';
    for (int i = point; i < nd; ++i)
        frac += (i < 0) ? '0' : digits[static_cast<unsigned>(i)];

    // «При печати чисел, являющихся дробями, вместо первой цифры целой
    // части подставляется нуль» (разд. 16.2).
    if (whole.empty() && f.ip) whole = "0";
    if (whole.size() > f.ip) return false;
    while (whole.size() < f.ip) whole.insert(whole.begin(), pad_zero ? '0' : ' ');

    // «Младшие разряды, выходящие за пределы формата, отбрасываются» —
    // не округляются (разд. 13.6 и 16.2).
    if (frac.size() > f.fp) frac.resize(f.fp);
    while (frac.size() < f.fp) frac += '0';

    out.clear();
    if (f.sign == 1) out += negative ? '-' : '+';
    else if (f.sign == 2) out += negative ? '-' : ' ';
    out += whole;
    if (f.dot) { out += '.'; out += frac; }

    if (f.exponential) {
        char buf[16];
        std::sprintf(buf, "E%c%02d", exponent < 0 ? '-' : '+',
                     exponent < 0 ? -exponent : exponent);
        out += buf;
    }
    return true;
}

void image_string(const std::string & s, const ImageField & f, std::string & out)
{
    const unsigned w = f.width();
    out.assign(s, 0, w);
    out.resize(w, ' ');
}

} // namespace iskra
