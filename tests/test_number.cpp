// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Mikhail Revzin <p3.141592653589793238462643@gmail.com>
// Part of the Iskra-226 project: https://github.com/Ptr314/Iskra-226
// Description: десятичная арифметика

#include "check.h"
#include "core/number.h"

using namespace iskra;

namespace {

Number num(const char * s)
{
    Number n;
    if (!Number::parse(s, n))
        std::printf("  не разобралось: %s\n", s);
    return n;
}

std::string disp(const char * s)
{
    return num(s).to_display();
}

std::string op(char o, const char * a, const char * b)
{
    Number r;
    bool ok = false;
    switch (o) {
        case '+': ok = Number::add(num(a), num(b), r); break;
        case '-': ok = Number::sub(num(a), num(b), r); break;
        case '*': ok = Number::mul(num(a), num(b), r); break;
        case '/': ok = Number::div(num(a), num(b), r); break;
        case '^': ok = Number::pow(num(a), num(b), r); break;
    }
    return ok ? r.to_display() : std::string("ОШИБКА");
}

void test_parse_and_display()
{
    CHECK_STR(disp("0"), " 0");
    CHECK_STR(disp("13"), " 13");
    CHECK_STR(disp("-13"), "-13");
    CHECK_STR(disp("2.718281828"), " 2.718281828");
    CHECK_STR(disp(".01"), " .01");
    CHECK_STR(disp("-2197"), "-2197");
    CHECK_STR(disp("51840"), " 51840");
    CHECK_STR(disp("2488320"), " 2488320");

    // Хвостовые нули не печатаются.
    CHECK_STR(disp("1.500"), " 1.5");
    CHECK_STR(disp("100"), " 100");

    // Тринадцать разрядов сохраняются, четырнадцатый округляется.
    CHECK_STR(disp("1234567890123"), " 1234567890123");
    CHECK_STR(disp("1.234567890123"), " 1.234567890123");
    CHECK_STR(disp("1.2345678901235"), " 1.234567890124");

    // Показатель степени у совсем больших и малых. Мантисса печатается с
    // восемью знаками после точки — так в единственном примере свободного
    // формата в руководстве (разд. 13.6).
    CHECK_STR(disp("1E20"), " 1.00000000E+20");
    CHECK_STR(disp("1E-20"), " 1.00000000E-20");
    CHECK_STR(disp("-123.05E+14"), "-1.23050000E+16");

    Number n;
    CHECK(!Number::parse("", n));
    CHECK(!Number::parse("12x", n));
    CHECK(Number::parse(" 42 ", n));
}

void test_arithmetic()
{
    CHECK_STR(op('+', "1", "2"), " 3");
    CHECK_STR(op('-', "1", "2"), "-1");
    CHECK_STR(op('*', "3", "4"), " 12");
    CHECK_STR(op('/', "1", "8"), " .125");

    // Перенос и заём через разряды.
    CHECK_STR(op('+', "999", "1"), " 1000");
    CHECK_STR(op('-', "1000", "1"), " 999");
    CHECK_STR(op('+', "-5", "5"), " 0");
    CHECK_STR(op('-', "-5", "-5"), " 0");
    CHECK_STR(op('+', ".1", ".2"), " .3");           // в double было бы .30000000000000004

    // Разные порядки.
    CHECK_STR(op('+', "1", ".0001"), " 1.0001");
    CHECK_STR(op('*', ".01", ".01"), " .0001");
    CHECK_STR(op('/', "1", "3"), " .3333333333333");
    CHECK_STR(op('*', "-3", "4"), "-12");
    CHECK_STR(op('*', "-3", "-4"), " 12");

    // Слагаемое, не достающее до младшего разряда, теряется.
    CHECK_STR(op('+', "1", "1E-20"), " 1");

    // Деление на нуль — ошибка, а не аварийное завершение.
    CHECK_STR(op('/', "1", "0"), "ОШИБКА");
}

void test_power_and_root()
{
    CHECK_STR(op('^', "2", "10"), " 1024");
    CHECK_STR(op('^', "-13", "2"), " 169");
    CHECK_STR(op('^', "-13", "3"), "-2197");
    CHECK_STR(op('^', "2", "-1"), " .5");
    CHECK_STR(op('^', "5", "0"), " 1");

    Number r;
    CHECK(Number::sqrt(num("4"), r));
    CHECK_STR(r.to_display(), " 2");
    CHECK(Number::sqrt(num("2"), r));
    CHECK_STR(r.to_display().substr(0, 8), " 1.41421");
    CHECK(!Number::sqrt(num("-1"), r));

    // Дробная степень идёт через double и потому приблизительна.
    CHECK(Number::pow(num("2"), num(".5"), r));
    CHECK_STR(r.to_display().substr(0, 8), " 1.41421");
}

void test_compare()
{
    CHECK_EQ(num("1").compare(num("2")) < 0, true);
    CHECK_EQ(num("2").compare(num("1")) > 0, true);
    CHECK_EQ(num("2").compare(num("2")) == 0, true);
    CHECK_EQ(num("-1").compare(num("1")) < 0, true);
    CHECK_EQ(num("-2").compare(num("-1")) < 0, true);
    CHECK_EQ(num("0").compare(num("-0")) == 0, true);
    CHECK_EQ(num(".1").compare(num(".09")) > 0, true);
}

void test_int_conversion()
{
    long v = -1;
    CHECK(num("42").to_int(v));       CHECK_EQ(v, 42);
    CHECK(num("-42").to_int(v));      CHECK_EQ(v + 100, 58);
    CHECK(num("0").to_int(v));        CHECK_EQ(v, 0);
    CHECK(!num("1.5").to_int(v));
    CHECK(!num("1E30").to_int(v));
}

// «Ближайшее меньшее целое число» (разд. 4.3). Считать это через double
// нельзя по той же причине, по какой нельзя считать в нём всё остальное:
// 255 — это 2.55 x 10^2, в двоичной плавающей точке 254.99999999999997,
// и INT(255) выходил 254. Список ниже — те значения, где round-trip через
// double врал, плюс оба примера книги.
// --- представление числа в записи файла данных ---------------------------

// "00 10 00 00 00 00 00 00" -> восемь байт
void hex8(const char * s, uint8_t out[8])
{
    unsigned n = 0;
    for (const char * p = s; *p && n < 8; ) {
        while (*p == ' ') ++p;
        if (!*p) break;
        unsigned v = 0;
        for (unsigned k = 0; k < 2 && *p; ++k, ++p)
            v = v * 16 + (*p <= '9' ? *p - '0' : (*p | 32) - 'a' + 10);
        out[n++] = static_cast<uint8_t>(v);
    }
}

std::string hexs8(const uint8_t b[8])
{
    static const char * D = "0123456789ABCDEF";
    std::string r;
    for (unsigned i = 0; i < 8; ++i) {
        if (i) r += ' ';
        r += D[b[i] >> 4];
        r += D[b[i] & 15];
    }
    return r;
}

// Байты -> десятичная запись через to_display().
std::string from8(const char * hex)
{
    uint8_t b[8];
    hex8(hex, b);
    Number n;
    if (!Number::from_disk8(b, n)) return "ОШИБКА";
    return n.to_display();
}

// Десятичная запись -> байты.
std::string to8(const char * dec)
{
    uint8_t b[8];
    num(dec).to_disk8(b);
    return hexs8(b);
}

void test_disk8()
{
    // Значения взяты с реальных образов и сверены с tools/probes/disk.py.
    CHECK_STR(from8("00 10 00 00 00 00 00 00"), " 1");
    CHECK_STR(from8("00 80 00 00 00 00 00 00"), " 8");
    CHECK_STR(from8("00 70 00 00 00 00 00 00"), " 7");        // klerk/D21
    CHECK_STR(from8("00 21 00 00 00 00 01 00"), " 2100");
    CHECK_STR(from8("00 99 00 00 00 00 01 00"), " 9900");
    CHECK_STR(from8("00 75 00 00 00 00 FF 00"), " .0075");
    CHECK_STR(from8("40 11 75 53 43 58 02 40"), " 401175534.3584");
    CHECK_STR(from8("35 11 77 78 61 42 03 91"), "-351177786142.9");
    CHECK_STR(from8("44 15 46 99 22 95 00 01"), "-441.546992295");
    CHECK_STR(from8("00 00 00 00 00 00 00 00"), " 0");

    // Обратно — те же байты.
    CHECK_STR(to8("1"),                "00 10 00 00 00 00 00 00");
    CHECK_STR(to8("7"),                "00 70 00 00 00 00 00 00");
    CHECK_STR(to8("2100"),             "00 21 00 00 00 00 01 00");
    CHECK_STR(to8(".0075"),            "00 75 00 00 00 00 FF 00");
    CHECK_STR(to8("9.7E-9"),           "00 97 00 00 00 00 FD 00");
    CHECK_STR(to8("401175534.3584"),   "40 11 75 53 43 58 02 40");
    CHECK_STR(to8("-351177786142.9"),  "35 11 77 78 61 42 03 91");
    CHECK_STR(to8("-441.546992295"),   "44 15 46 99 22 95 00 01");
    CHECK_STR(to8("0"),                "00 00 00 00 00 00 00 00");

    // Порядок — степень 1000, поэтому соседние десятичные порядки лежат в
    // одном байте порядка, а мантисса съезжает на разряд.
    CHECK_STR(to8("1"),    "00 10 00 00 00 00 00 00");
    CHECK_STR(to8("10"),   "01 00 00 00 00 00 00 00");
    CHECK_STR(to8("100"),  "10 00 00 00 00 00 00 00");
    CHECK_STR(to8("1000"), "00 10 00 00 00 00 01 00");

    // Когда старшая цифра встаёт на третью позицию, в поле остаётся только
    // 11 значащих разрядов, и два младших теряются.
    CHECK_STR(to8("1.234567890123"), "00 12 34 56 78 90 00 10");
    CHECK_STR(from8("00 12 34 56 78 90 00 10"), " 1.2345678901");
    // А при старшей цифре на первой позиции сохраняются все тринадцать.
    CHECK_STR(to8("123.4567890123"), "12 34 56 78 90 12 00 30");
    CHECK_STR(from8("12 34 56 78 90 12 00 30"), " 123.4567890123");

    Number n;
    uint8_t b[8];
    hex8("00 1A 00 00 00 00 00 00", b);
    CHECK(!Number::from_disk8(b, n));           // не BCD
    hex8("00 10 00 00 00 00 00 02", b);
    CHECK(!Number::from_disk8(b, n));           // недопустимый знак
    hex8("00 10 00 00 00 00 7F 00", b);
    CHECK(!Number::from_disk8(b, n));           // порядок вне Number
}

void test_floor()
{
    long v = -1;

    CHECK(num("255").floor_to_int(v));      CHECK_EQ(v, 255);
    CHECK(num("256.9").floor_to_int(v));    CHECK_EQ(v, 256);
    CHECK(num("47").floor_to_int(v));       CHECK_EQ(v, 47);
    CHECK(num("65535").floor_to_int(v));    CHECK_EQ(v, 65535);
    CHECK(num("1000000").floor_to_int(v));  CHECK_EQ(v, 1000000);
    CHECK(num("0").floor_to_int(v));        CHECK_EQ(v, 0);
    CHECK(num(".9").floor_to_int(v));       CHECK_EQ(v, 0);

    // Вниз, а не к нулю: INT(-17.2) = -18.
    CHECK(num("-17.2").floor_to_int(v));    CHECK_EQ(v + 100, 82);
    CHECK(num("-17").floor_to_int(v));      CHECK_EQ(v + 100, 83);
    CHECK(num("-.5").floor_to_int(v));      CHECK_EQ(v + 100, 99);

    CHECK_STR(num("256.9").floor().to_display(), " 256");
    CHECK_STR(num("-17.2").floor().to_display(), "-18");
    CHECK_STR(num("255").floor().to_display(), " 255");
    // Целое за пределами long остаётся собой, а не портится.
    CHECK_STR(num("1E30").floor().to_display(), " 1.00000000E+30");
}

// Главное, ради чего десятичная арифметика: цикл FOR со дробным шагом.
// В double .01 непредставима, и после 6600 сложений набегает заметная
// ошибка — счётчик цикла промахивается мимо границы.
void test_for_loop_accumulation()
{
    Number t = num("-6");
    const Number step = num(".01");
    unsigned count = 0;
    while (t.compare(num("60")) <= 0) {
        CHECK(Number::add(t, step, t));
        if (++count > 7000) break;
    }
    CHECK_EQ(count, 6601u);

    // И значение после цикла ровно то, что ожидается.
    CHECK_STR(t.to_display(), " 60.01");
}

} // namespace

int main()
{
    test_parse_and_display();
    test_arithmetic();
    test_power_and_root();
    test_compare();
    test_int_conversion();
    test_floor();
    test_disk8();
    test_for_loop_accumulation();
    return test::summary("десятичная арифметика");
}