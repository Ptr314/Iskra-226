// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Mikhail Revzin <p3.141592653589793238462643@gmail.com>
// Part of the Iskra-226 project: https://github.com/Ptr314/Iskra-226
// Description: вычисление выражений прямо из потока токенов

#include <cstdio>
#include <string>
#include <vector>

#include "check.h"
#include "core/byte_source.h"
#include "core/eval.h"
#include "core/koi8.h"
#include "core/program.h"
#include "core/tokenize.h"
#include "core/vars.h"

using namespace iskra;

namespace {

std::string trim(const std::string & s)
{
    std::size_t a = 0, b = s.size();
    while (a < b && s[a] == ' ') ++a;
    while (b > a && s[b - 1] == ' ') --b;
    return s.substr(a, b - a);
}

// Крошечный исполнитель на два оператора: присваивание и PRINT. Нужен,
// чтобы выражению было чем задать переменные, и заодно проверяет приёмники.
// Возвращает значение последнего PRINT в печатном виде.
std::string ev(const char * utf8)
{
    std::string koi8;
    utf8_to_koi8(utf8, koi8);

    NameTable names;
    ProgramImage img;
    std::string error;
    if (!tokenize(koi8, img, names, error)) return "ТРАНСЛЯЦИЯ: " + error;

    VarStore store(img.vars());
    std::string result = "НЕТ PRINT";

    for (unsigned i = 0; i < img.line_count(); ++i) {
        const std::vector<uint8_t> & b = img.line(i).body;
        unsigned p = 0;
        while (p + 1 < b.size()) {
            const unsigned verb = b[p];
            const unsigned len = b[p + 1];
            if (p + 2 + len > b.size()) return "ОБРЫВ ОПЕРАТОРА";
            const uint8_t * ops = len ? &b[p + 2] : 0;
            p += 2 + len;

            if (verb == 0x46 || verb == 0x4E) continue;      // DIM и COM
            ByteSource src(ops, len, &img.vars());
            Evaluator ev(src, store);

            if (verb == 0x36) {                              // присваивание
                std::vector<Evaluator::Target> targets;
                for (;;) {
                    Evaluator::Target t;
                    if (!ev.target(t, true)) return "ПРИЁМНИК: " + ev.error();
                    targets.push_back(t);
                    Tok k;
                    if (!ev.parser().peek(k, true)) return "РАЗБОР: " + ev.error();
                    if (k.t == Tok::EQ) { ev.parser().consume(); break; }
                }
                Value v;
                if (!ev.expr(v)) return "ВЫЧИСЛЕНИЕ: " + ev.error();
                // Приёмник у символьной вырезки один: место запоминается в
                // самом вычислителе, поэтому несколько целей тут не нужны.
                if (!ev.store(targets[targets.size() - 1], v))
                    return "ЗАПИСЬ: " + ev.error();
                continue;
            }
            if (verb == 0x4C) {                              // PRINT
                Value v;
                if (!ev.expr(v)) return "ВЫЧИСЛЕНИЕ: " + ev.error();
                result = v.is_str ? v.str : trim(v.num.to_display());
                continue;
            }
            return "ОПЕРАТОР НЕ ПОДДЕРЖАН";
        }
    }
    std::string utf;
    return result.empty() ? result
                          : koi8_to_utf8(reinterpret_cast<const uint8_t *>(result.data()),
                                         static_cast<unsigned>(result.size()));
}

void test_arithmetic()
{
    CHECK_STR(ev("10 PRINT 2+2"), "4");
    CHECK_STR(ev("10 PRINT 7-9"), "-2");
    CHECK_STR(ev("10 PRINT 6*7"), "42");
    CHECK_STR(ev("10 PRINT 1/8"), ".125");
    CHECK_STR(ev("10 PRINT 2^10"), "1024");

    // Старшинство: в потоке лежит плоская инфиксная запись, и восстановить
    // его — работа вычислителя.
    CHECK_STR(ev("10 PRINT 2+3*4"), "14");
    CHECK_STR(ev("10 PRINT (2+3)*4"), "20");
    CHECK_STR(ev("10 PRINT 2*3+4"), "10");
    CHECK_STR(ev("10 PRINT -2^2"), "-4");
    CHECK_STR(ev("10 PRINT 2^3^2"), "512");     // возведение правоассоциативно
    CHECK_STR(ev("10 PRINT 100/10/2"), "5");    // деление левоассоциативно
    CHECK_STR(ev("10 PRINT -3+5"), "2");
}

void test_functions()
{
    CHECK_STR(ev("10 PRINT ABS(-5)"), "5");
    CHECK_STR(ev("10 PRINT INT(256.9)"), "256");
    CHECK_STR(ev("10 PRINT INT(-17.2)"), "-18");
    CHECK_STR(ev("10 PRINT SGN(-3)"), "-1");
    CHECK_STR(ev("10 PRINT SGN(0)"), "0");
    CHECK_STR(ev("10 PRINT SQR(144)"), "12");
    CHECK_STR(ev("10 PRINT INT(#PI*100)"), "314");
}

void test_variables()
{
    CHECK_STR(ev("10 A=6\n20 B=7\n30 PRINT A*B"), "42");
    // Неинициализированная переменная равна нулю.
    CHECK_STR(ev("10 PRINT Z+1"), "1");
    // Массив: индексы с единицы, дробная часть отбрасывается.
    CHECK_STR(ev("10 DIM A(5)\n20 A(2)=17\n30 PRINT A(2)"), "17");
    CHECK_STR(ev("10 DIM A(5)\n20 A(3)=8\n30 PRINT A(3.9)"), "8");
    // Двумерный массив.
    CHECK_STR(ev("10 DIM A(2,3)\n20 A(2,3)=5\n30 PRINT A(2,3)"), "5");
}

void test_comparisons()
{
    CHECK_STR(ev("10 PRINT 1=1"), "1");
    CHECK_STR(ev("10 PRINT 1=2"), "0");
    CHECK_STR(ev("10 PRINT 3>2"), "1");
    CHECK_STR(ev("10 PRINT 2+2=4"), "1");       // отношение ниже сложения

    // Связки равноправны и вычисляются слева направо (разд. 4.5):
    // `A OR B AND C` — это `(A OR B) AND C`.
    CHECK_STR(ev("10 PRINT 1=1 AND 1=1"), "1");
    CHECK_STR(ev("10 PRINT 1=1 AND 1=2"), "0");
    CHECK_STR(ev("10 PRINT 1=1 OR 1=2"), "1");
    CHECK_STR(ev("10 PRINT 1=1 OR 1=2 AND 1=2"), "0");
    CHECK_STR(ev("10 PRINT 1=2 AND 1=2 OR 1=1"), "1");
}

void test_strings()
{
    CHECK_STR(ev("10 PRINT \"ABC\""), "ABC");
    // Поле постоянной длины: значение дополняется пробелами.
    CHECK_STR(ev("10 DIM A$4\n20 A$=\"XY\"\n30 PRINT A$"), "XY  ");
    // STR( — вырезка; слева от знака равенства она тоже работает.
    CHECK_STR(ev("10 DIM A$4\n20 A$=\"ABCD\"\n30 PRINT STR(A$,2,2)"), "BC");
    CHECK_STR(ev("10 DIM A$4\n20 A$=\"ABCD\"\n30 STR(A$,2,2)=\"XY\"\n40 PRINT A$"),
              "AXYD");
    // LEN — до последнего непробельного байта, но не ноль.
    CHECK_STR(ev("10 DIM A$8\n20 A$=\"AB\"\n30 PRINT LEN(A$)"), "2");
    CHECK_STR(ev("10 DIM A$8\n20 A$=\" \"\n30 PRINT LEN(A$)"), "1");
    // VAL — двоичное значение первого байта.
    CHECK_STR(ev("10 DIM A$4\n20 A$=HEX(41)\n30 PRINT VAL(A$)"), "65");
    // Сравнение строк побайтовое, короткая дополняется пробелами.
    CHECK_STR(ev("10 DIM A$4\n20 A$=\"AB\"\n30 PRINT A$=\"AB  \""), "1");
    // POS — поиск байта по отношению, с единицы.
    CHECK_STR(ev("10 DIM A$4\n20 A$=\"ABCD\"\n30 PRINT POS(A$=43)"), "3");
    CHECK_STR(ev("10 DIM A$4\n20 A$=\"ABCD\"\n30 PRINT POS(A$=FF)"), "0");
}

void test_errors()
{
    // Ошибки вычислителя видны наружу, а не превращаются в ноль.
    CHECK_STR(ev("10 PRINT 1/0"), "ВЫЧИСЛЕНИЕ: деление на ноль");
    CHECK_STR(ev("10 PRINT SQR(-1)"), "ВЫЧИСЛЕНИЕ: корень из отрицательного числа");
    CHECK_STR(ev("10 DIM A(3)\n20 PRINT A(9)"),
              "ВЫЧИСЛЕНИЕ: индекс за границей массива");
    CHECK_STR(ev("10 DIM A(3)\n20 PRINT A(0)"),
              "ВЫЧИСЛЕНИЕ: индекс массива меньше единицы");
    // Числовой массив целиком значением не является: `E0 <индекс>` у него
    // встречается только в MAT, и разбирает его сам оператор.
    CHECK_STR(ev("10 DIM A(3)\n20 PRINT A()"),
              "ВЫЧИСЛЕНИЕ: числовой массив целиком в выражении не значение");
    // Вырезать из литерала можно, присваивать в него — нет.
    CHECK_STR(ev("10 PRINT STR(\"ABCD\",2,2)"), "BC");
    CHECK_STR(ev("10 STR(\"AB\",1)=\"X\""), "ПРИЁМНИК: присваивание в литерал");
}

} // namespace

int main()
{
    test_arithmetic();
    test_functions();
    test_variables();
    test_comparisons();
    test_strings();
    test_errors();
    return test::summary("вычисление выражений");
}
