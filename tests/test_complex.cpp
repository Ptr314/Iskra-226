// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Mikhail Revzin <p3.141592653589793238462643@gmail.com>
// Part of the Iskra-226 project: https://github.com/Ptr314/Iskra-226
// Description: сложные сочетания операторов и вложенность — исполнение

// Здесь проверяется не одна форма, а их взаимодействие: приоритеты в длинных
// выражениях, функции внутри функций и внутри индексов, вырезки поверх
// массива строк, переходы внутри циклов, перехват ошибок машины.
//
// Каждая программа исполняется дважды: как есть и после круга
// «токены → текст → токены». Экран обязан совпасть — значит обе стороны
// кодека понимают форму одинаково, и понимают её так же, как исполнитель.

#include <cstdio>
#include <string>
#include <vector>

#include "check.h"
#include "core/detokenize.h"
#include "core/interp.h"
#include "core/koi8.h"
#include "core/names.h"
#include "core/tokenize.h"
#include "host_headless/headless_host.h"

using namespace iskra;

namespace {

// Экран после прогона; строки без хвостовых пробелов, пустые в конце срезаны.
bool screen_of(const ProgramImage & src, std::string & out, std::string & error)
{
    ProgramImage img = src;
    HeadlessHost host;
    Interp interp(img, host);
    interp.set_max_steps(200000);
    if (!interp.run(error)) return false;
    out = host.dump();
    while (!out.empty() && (out[out.size() - 1] == '\n' || out[out.size() - 1] == ' '))
        out.resize(out.size() - 1);
    return true;
}

std::string line_of(const std::string & screen, unsigned n)
{
    std::size_t p = 0;
    for (unsigned i = 1; i < n; ++i) {
        const std::size_t e = screen.find('\n', p);
        if (e == std::string::npos) return std::string();
        p = e + 1;
    }
    const std::size_t e = screen.find('\n', p);
    return screen.substr(p, e - p);
}

// Прогнать текст и вернуть экран; заодно проверить, что программа переживает
// круг «токены → текст → токены» и после него печатает то же самое.
std::string run(const char * utf8, const char * what)
{
    std::string koi8;
    utf8_to_koi8(utf8, koi8);

    NameTable names;
    ProgramImage img;
    std::string error;
    if (!tokenize(koi8, img, names, error)) {
        std::printf("  %s: трансляция — %s\n", what, error.c_str());
        CHECK(false);
        return std::string();
    }

    std::string screen;
    if (!screen_of(img, screen, error)) {
        std::printf("  %s: исполнение — %s\n", what, error.c_str());
        CHECK(false);
        return std::string();
    }

    // Круг: обратная трансляция теми же именами и снова в токены.
    std::string text;
    for (unsigned i = 0; i < img.line_count(); ++i) {
        std::string one;
        if (!detokenize_line(img.line(i), names, one, error)) {
            std::printf("  %s: обратно строка %u — %s\n", what,
                        img.line(i).number, error.c_str());
            CHECK(false);
            return screen;
        }
        text += one;
        text += '\n';
    }
    NameTable again = names;
    ProgramImage back;
    if (!tokenize(text, back, again, error)) {
        std::printf("  %s: круг — %s\n", what, error.c_str());
        CHECK(false);
        return screen;
    }
    std::string screen2;
    if (!screen_of(back, screen2, error)) {
        std::printf("  %s: круг, исполнение — %s\n", what, error.c_str());
        CHECK(false);
        return screen;
    }
    if (screen2 != screen) {
        std::printf("  %s: после круга экран другой\n    было  |%s|\n    стало |%s|\n",
                    what, screen.c_str(), screen2.c_str());
        CHECK(false);
    }
    return screen;
}

// --- приоритеты и вложенность выражений -------------------------------------

void test_arithmetic()
{
    const char * src =
        "10 A=2:B=3:C=4\n"
        "20 PRINT A+B*C^2-1;(A+B)*(C-A)/2;-A^2\n"
        "30 PRINT A-B-C;2^3^2;A/B/C\n"
        "40 PRINT SQR(ABS(INT(-4.7)));SGN(A-B)*ABS(A-B)\n";
    const std::string s = run(src, "арифметика");
    // Степень раньше умножения, унарный минус позже степени: -2^2 = -4.
    CHECK_STR(line_of(s, 1), " 49  5 -4");
    // Вычитание и деление слева направо; `^` — справа налево: 2^(3^2)=512.
    CHECK_STR(line_of(s, 2), "-5  512  .1666666666667");
    CHECK_STR(line_of(s, 3), " 2.2360679775 -1");
}

// --- массивы, индексы-выражения, MAT REDIM ----------------------------------

void test_arrays()
{
    const char * src =
        "10 DIM D(3,2)\n"
        "20 FOR I=1TO3:FOR J=1TO2:D(I,J)=I*10+J:NEXT J:NEXT I\n"
        "30 PRINT D(1,1);D(3,2);D(2,1);D(SGN(2),INT(1.9))\n"
        "40 MAT REDIM D(6):PRINT D(1);D(6)\n";
    const std::string s = run(src, "массивы");
    // Индексом может быть любое выражение: D(SGN(2),INT(1.9)) = D(1,1).
    CHECK_STR(line_of(s, 1), " 11  32  21  11");
    // «MAT REDIM меняет размерности, сохраняя содержимое памяти»: 3x2 → 6.
    CHECK_STR(line_of(s, 2), " 11  32");
}

// --- символьные поля и вырезки ----------------------------------------------

void test_strings()
{
    const char * src =
        "10 DIM E\xC2\xA4""20,F\xC2\xA4(3)4\n"
        "20 E\xC2\xA4=\"ABCDEFGHIJ\":STR(E\xC2\xA4,1,2)=\"XY\"\n"
        "30 PRINT STR(E\xC2\xA4,3,4);\"/\";STR(E\xC2\xA4,1,5)\n"
        "40 PRINT LEN(E\xC2\xA4);POS(E\xC2\xA4=\"C\");NUM(\"12A\")\n"
        "50 F\xC2\xA4(1)=\"AAAA\":F\xC2\xA4(2)=\"BBBB\":F\xC2\xA4(3)=\"CCCC\"\n"
        "60 PRINT STR(F\xC2\xA4(),5,4);\"/\";STR(F\xC2\xA4(),1,12)\n";
    const std::string s = run(src, "строки");
    // STR( слева и справа от знака равенства в одной строке.
    CHECK_STR(line_of(s, 1), "CDEF/XYCDE");
    CHECK_STR(line_of(s, 2), " 10  3  2");
    // «Символьный массив рассматривается как одна непрерывная строка»:
    // вырезка с 5-го байта попадает во второй элемент.
    CHECK_STR(line_of(s, 3), "BBBB/AAAABBBBCCCC");
}

// --- функции, переходы, вложенные вызовы ------------------------------------

void test_control()
{
    const char * src =
        "10 DEFFN Q(X)=X*X+1\n"
        "20 DEFFN R(X)=FNQ(X)-1\n"
        "30 PRINT FNQ(3);FNR(3);FNQ(FNQ(1))\n"
        "40 G=0:FOR I=1TO5:IF I=3THEN60:G=G+I:GOTO 70\n"
        "60 G=G+100\n"
        "70 NEXT I:PRINT G\n"
        "80 H=0:GOSUB 200:GOSUB 200:PRINT H\n"
        "90 ON 2GOTO100,110:PRINT \"НЕ СЮДА\"\n"
        "100 PRINT \"ОДИН\":GOTO 120\n"
        "110 PRINT \"ДВА\"\n"
        "120 END\n"
        "200 H=H+1:IF H<2THEN210:RETURN\n"
        "210 GOSUB 200:RETURN\n";
    const std::string s = run(src, "управление");
    // Функция через функцию и функция от себя же: 10, 9, FNQ(2)=5.
    CHECK_STR(line_of(s, 1), " 10  9  5");
    // Переход из середины цикла и обратно: 1+2+100+4+5.
    CHECK_STR(line_of(s, 2), " 112");
    // Вложенный GOSUB из подпрограммы.
    CHECK_STR(line_of(s, 3), " 3");
    CHECK_STR(line_of(s, 4), "ДВА");
}

// --- байты, образы печати, зоны ---------------------------------------------

void test_bytes_and_print()
{
    const char * src =
        "10 K\xC2\xA4=HEX(F0):L\xC2\xA4=HEX(0F)\n"
        "20 AND(K\xC2\xA4,L\xC2\xA4):HEXPRINT STR(K\xC2\xA4,1,2)\n"
        "30 K\xC2\xA4=HEX(F0):OR(K\xC2\xA4,L\xC2\xA4):HEXPRINT STR(K\xC2\xA4,1,2)\n"
        "40 K\xC2\xA4=HEX(F0):XOR(K\xC2\xA4,FF):HEXPRINT STR(K\xC2\xA4,1,2)\n"
        "50 K\xC2\xA4=HEX(12):ROTATE(K\xC2\xA4,4):HEXPRINT STR(K\xC2\xA4,1,1)\n"
        "60 M=1234.5:CONVERT M TO N\xC2\xA4,(#####.##):PRINT N\xC2\xA4;\"/\"\n"
        "70 CONVERT N\xC2\xA4 TO P:PRINT P*2\n"
        "80 PRINT 1,2;3,,4\n"
        "90 PRINTUSING 100,7,8.25\n"
        "100 %## И ##.##\n";
    const std::string s = run(src, "байты и печать");
    // Хвост поля — пробелы, и операции над байтами их тоже трогают.
    CHECK_STR(line_of(s, 1), "0020");
    CHECK_STR(line_of(s, 2), "FF20");
    CHECK_STR(line_of(s, 3), "0FDF");
    CHECK_STR(line_of(s, 4), "21");
    // CONVERT с образом заполняет незанятые разряды нулями.
    CHECK_STR(line_of(s, 5), "01234.50        /");
    CHECK_STR(line_of(s, 6), " 2469");
    // Две запятые подряд пропускают зону; ширина зоны — 16 позиций.
    CHECK_STR(line_of(s, 7), " 1               2  3                            4");
    CHECK_STR(line_of(s, 8), " 7 И  8.25");
}

// --- ошибки машины ----------------------------------------------------------

// «Если ошибка математическая, то выполняется оператор возврата RETURN и
// происходит возврат в основную программу. При этом оператор, на котором
// произошла ошибка, пропускается» (руководство, пример 11.11).
void test_math_error_is_caught()
{
    const char * src =
        "10 DIM E\xC2\xA4""4,K\xC2\xA4""4\n"
        "20 ON ERROR E\xC2\xA4,K\xC2\xA4 THEN 100\n"
        "30 A=1/0\n"
        "40 PRINT \"ДАЛЬШЕ\"\n"
        "50 END\n"
        "100 PRINT \"КОД=\";E\xC2\xA4;\" СТРОКА=\";K\xC2\xA4:RETURN\n";
    const std::string s = run(src, "математическая ошибка");
    CHECK_STR(line_of(s, 1), "КОД=03   СТРОКА=0030");
    CHECK_STR(line_of(s, 2), "ДАЛЬШЕ");
}

// Ограничение эмулятора кода не имеет, и ON ERROR его не ловит: иначе дыра в
// эмуляторе выглядела бы поведением машины.
void test_limit_is_not_a_machine_error()
{
    std::string koi8;
    utf8_to_koi8("10 ON ERROR GOTO 100\n"
                 "20 $GIO /34,HEX(00),A\xC2\xA4\n"
                 "100 PRINT \"НЕ СЮДА\"\n", koi8);
    NameTable names;
    ProgramImage img;
    std::string error;
    if (!tokenize(koi8, img, names, error))
        { std::printf("  %s\n", error.c_str()); CHECK(false); return; }

    HeadlessHost host;
    Interp interp(img, host);
    CHECK(!interp.run(error));
    CHECK(error.find("машинозависим") != std::string::npos);
}

// --- граница общих переменных -----------------------------------------------

// «Если оператор COM CLEAR используется без указания переменной, все общие
// переменные становятся необщими… Если указать одну из общих переменных, то
// эта переменная и все переменные, объявленные вслед за указанной, становятся
// необщими» (руководство, разд. 19.3). Обратное тоже верно, и оба случая —
// одно правило: граница встаёт на названную переменную. Проверяется тем, что
// после этого стирает `CLEAR N`.
void test_com_clear()
{
    const char * src =
        "10 COM A,B\n"
        "20 DIM C(2)\n"
        "30 A=1:B=2:C(1)=3\n"
        "40 COM CLEAR B\n"
        "50 CLEAR N\n"
        "60 PRINT A;B;C(1)\n"
        "70 A=1:B=2:C(1)=3:COM CLEAR C()\n"
        "80 CLEAR N:PRINT A;B;C(1)\n"
        "90 A=1:B=2:COM CLEAR\n"
        "100 CLEAR N:PRINT A;B\n";
    const std::string s = run(src, "COM CLEAR");
    // Граница на B: общей осталась только A.
    CHECK_STR(line_of(s, 1), " 1  0  0");
    // Граница на C: A и B стали общими, хотя C объявлена в DIM.
    CHECK_STR(line_of(s, 2), " 1  2  0");
    // Без операнда общих не остаётся вовсе.
    CHECK_STR(line_of(s, 3), " 0  0");
}

// --- вложенность до предела -------------------------------------------------

void test_deep_nesting()
{
    const char * src =
        "10 DIM A\xC2\xA4""8,B(4)\n"
        "20 B(1)=1:B(2)=2:B(3)=3:B(4)=4\n"
        "30 A\xC2\xA4=\"12345678\"\n"
        "40 PRINT VAL(STR(A\xC2\xA4,INT(B(2)),1))\n"
        "50 PRINT B(INT(SGN(B(3))*ABS(-2)))\n"
        "60 PRINT LEN(STR(A\xC2\xA4,1,LEN(A\xC2\xA4)-2))\n"
        "70 PRINT SQR(SQR(SQR(256)))\n";
    const std::string s = run(src, "вложенность");
    // VAL( от вырезки, чей индекс сам вычисляется: байт «2» = код 50.
    CHECK_STR(line_of(s, 1), " 50");
    // Индекс массива через две функции.
    CHECK_STR(line_of(s, 2), " 2");
    CHECK_STR(line_of(s, 3), " 6");
    CHECK_STR(line_of(s, 4), " 2");
}

} // namespace

int main()
{
    test_arithmetic();
    test_arrays();
    test_strings();
    test_control();
    test_bytes_and_print();
    test_math_error_is_caught();
    test_limit_is_not_a_machine_error();
    test_com_clear();
    test_deep_nesting();
    return test::summary("сложные сочетания");
}
