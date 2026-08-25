// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Mikhail Revzin <p3.141592653589793238462643@gmail.com>
// Part of the Iskra-226 project: https://github.com/Ptr314/Iskra-226
// Description: функции пользователя — DEFFN и обращение FN

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

bool run_text(const char * utf8_source, std::string & screen, std::string & error)
{
    std::string koi8;
    utf8_to_koi8(utf8_source, koi8);
    NameTable names;
    ProgramImage img;
    if (!tokenize(koi8, img, names, error)) return false;
    HeadlessHost host;
    Interp interp(img, host);
    if (!interp.run(error)) return false;
    screen = host.dump();
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

// Оттранслировать один оператор и вернуть его байты.
bool encode(const char * utf8_source, std::vector<uint8_t> & out,
            std::string & error)
{
    std::string koi8;
    utf8_to_koi8(utf8_source, koi8);
    NameTable names;
    ProgramImage img;
    if (!tokenize(koi8, img, names, error)) return false;
    if (img.line_count() != 1) { error = "ждали одну строку"; return false; }
    out = img.line(0).body;
    return true;
}

// --- исполнение -------------------------------------------------------------

// Пример 4.20 книги целиком. Заодно он показывает, что имя функции и имя
// переменной — разные вещи: функция называется A, и переменная A тоже есть.
void test_book_example()
{
    std::string screen, error;
    const char * src =
        "10 REM\n"
        "20 DEFFN A(H)=(H^2-A)^(1/3)\n"
        "30 A=56\n"
        "40 PRINT FN A(8),FN A(9),FN A(10)\n";
    if (!run_text(src, screen, error)) { std::printf("  %s\n", error.c_str()); CHECK(false); return; }
    // Первое значение сходится с книгой точно. Два других расходятся с ней
    // в последнем разряде: `^` считается через логарифм и показательную
    // функцию, и накопленная ошибка у машины своя, а у нас своя. Точные
    // значения — 2.92401773821286… и 3.53034833532606…, так что книжные
    // 2.924017738212 и 3.530348335325 не выходят ни округлением, ни
    // усечением.
    CHECK_STR(line_of(screen, 1),
              " 2               2.924017738213                  3.530348335326");
}

// «Функция может быть объявлена в любом месте программы, независимо от того,
// где она будет использоваться» (разд. 4.8) — в том числе ниже обращения.
void test_declared_below()
{
    std::string screen, error;
    const char * src =
        "10 PRINT FN B(4)\n"
        "20 STOP\n"
        "30 DEFFN B(R)=R*R\n";
    if (!run_text(src, screen, error)) { std::printf("  %s\n", error.c_str()); CHECK(false); return; }
    CHECK_STR(line_of(screen, 1), " 16");
}

// «Именем функции может использоваться любая латинская буква от A до Z или
// любая цифра от 0 до 9»; книга приводит обращение FN 2(4852).
void test_digit_name()
{
    std::string screen, error;
    const char * src =
        "10 DEFFN 2(X)=X/2\n"
        "20 PRINT FN 2(4852)\n";
    if (!run_text(src, screen, error)) { std::printf("  %s\n", error.c_str()); CHECK(false); return; }
    CHECK_STR(line_of(screen, 1), " 2426");
}

// «В качестве новых функций могут использоваться любые арифметические
// выражения, включающие … а также другие функции, определенные в данной
// программе». Так и в корпусе: GC121 2310 = `DEFFN T(E4)=FNH(E4)*E+E3`.
void test_function_calls_function()
{
    std::string screen, error;
    const char * src =
        "10 DEFFN H(X)=(X+ABS(X))/2\n"
        "20 DEFFN T(X)=FNH(X)*10\n"
        "30 PRINT FN T(3),FN T(-3)\n";
    if (!run_text(src, screen, error)) { std::printf("  %s\n", error.c_str()); CHECK(false); return; }
    CHECK_STR(line_of(screen, 1), " 30              0");
}

// Три определения из LL 9 — ровно те, что лежат в корпусе оттранслированными:
// округление вверх, чётность и положительная часть.
void test_corpus_definitions()
{
    std::string screen, error;
    const char * src =
        "10 DEFFN R(D8)=INT(D8)+SGN(D8-INT(D8))\n"
        "20 DEFFN K(D8)=2-SGN(D8/2-INT(D8/2))\n"
        "30 DEFFN H(D8)=(D8+ABS(D8))/2\n"
        "40 PRINT FN R(7.2),FN K(6),FN K(7),FN H(-5)\n";
    if (!run_text(src, screen, error)) { std::printf("  %s\n", error.c_str()); CHECK(false); return; }
    CHECK_STR(line_of(screen, 1), " 8               2               1               0");
}

// Аргумент вычисляется до присваивания формальной переменной, и сама она —
// обычная глобальная переменная программы: L2 1250 зовёт FNH(I0%-I2%), где
// формальная переменная участвует и в выражении аргумента.
void test_formal_is_a_global()
{
    std::string screen, error;
    const char * src =
        "10 DEFFN P(X)=X*2\n"
        "20 X=5\n"
        "30 PRINT FN P(X+1)\n"
        "40 PRINT X\n";
    if (!run_text(src, screen, error)) { std::printf("  %s\n", error.c_str()); CHECK(false); return; }
    CHECK_STR(line_of(screen, 1), " 12");
    // Прежнее значение не восстанавливается — своего места для формальной
    // переменной в машине нет.
    CHECK_STR(line_of(screen, 2), " 6");
}

// Определение при исполнении не делает ничего: программа проходит его
// насквозь.
void test_definition_is_skipped()
{
    std::string screen, error;
    const char * src =
        "10 DEFFN C(X)=X\n"
        "20 PRINT \"DALSHE\"\n";
    if (!run_text(src, screen, error)) { std::printf("  %s\n", error.c_str()); CHECK(false); return; }
    CHECK_STR(line_of(screen, 1), "DALSHE");
}

// Обращение к необъявленной функции — ограничение не эмулятора, а программы,
// но кода машины у него мы не знаем, поэтому это простая остановка.
void test_unknown_function()
{
    std::string screen, error;
    CHECK(!run_text("10 PRINT FN Z(1)\n", screen, error));
    CHECK(error.find("FN") != std::string::npos);
}

// «Именем формальной переменной может быть имя любой числовой переменной
// Бейсика» — символьная не годится ни в определении, ни в аргументе.
void test_string_argument_refused()
{
    std::string screen, error;
    CHECK(!run_text("10 DEFFN S(X)=X\n20 A\xC2\xA4=\"AB\"\n30 PRINT FN S(A\xC2\xA4)\n",
                    screen, error));
    CHECK(error.find("символьн") != std::string::npos);
}

// Функция, зовущая сама себя, программу бы зациклила; стек при этом
// обвалился бы молча, поэтому глубина ограничена.
void test_recursion_is_bounded()
{
    std::string screen, error;
    CHECK(!run_text("10 DEFFN Q(X)=FNQ(X)\n20 PRINT FN Q(1)\n", screen, error));
    CHECK(error.find("вложенност") != std::string::npos);
}

// --- оттранслированная форма ------------------------------------------------

// `DEFFN H(X)=(X+ABS(X))/2` = `5A 48 00 00 <X> EB <X> EA F2 <X> D0 D0 DC E8 02`
// (LL 9). Рабочее поле — два байта, машина заполняет их при исполнении.
void test_tokenized_deffn()
{
    std::vector<uint8_t> b;
    std::string error;
    if (!encode("10 DEFFN H(X)=(X+ABS(X))/2\n", b, error))
        { std::printf("  %s\n", error.c_str()); CHECK(false); return; }

    static const uint8_t want[] = {
        0x5A, 0x0E, 0x48, 0x00, 0x00, 0x00,
        0xEB, 0x00, 0xEA, 0xF2, 0x00, 0xD0, 0xD0, 0xDC, 0xE8, 0x02
    };
    CHECK_EQ(b.size(), sizeof(want));
    for (unsigned i = 0; i < sizeof(want) && i < b.size(); ++i)
        CHECK_EQ(b[i], want[i]);
}

// Обращение: `F0`, имя сырым кодом символа, выражение, `D0`.
// `V58%=FNH(V58%-V55%)` = `58 D9 F0 48 58 E9 55 D0` (L2 1080).
void test_tokenized_call()
{
    std::vector<uint8_t> b;
    std::string error;
    if (!encode("10 A=FN H(A-B)\n", b, error))
        { std::printf("  %s\n", error.c_str()); CHECK(false); return; }

    static const uint8_t want[] = {
        0x36, 0x08, 0x00, 0xD9, 0xF0, 0x48, 0x00, 0xE9, 0x01, 0xD0
    };
    CHECK_EQ(b.size(), sizeof(want));
    for (unsigned i = 0; i < sizeof(want) && i < b.size(); ++i)
        CHECK_EQ(b[i], want[i]);
}

// Имя функции — не переменная: в таблицу имён оно не попадает, и индексы
// переменных из-за него не смещаются.
void test_name_is_not_a_variable()
{
    std::string koi8, error;
    utf8_to_koi8("10 DEFFN H(X)=X\n20 B=1\n", koi8);
    NameTable names;
    ProgramImage img;
    if (!tokenize(koi8, img, names, error))
        { std::printf("  %s\n", error.c_str()); CHECK(false); return; }
    // Имён ровно два: X и B.
    CHECK_EQ(names.count(), 2u);
    CHECK_STR(names.name(0), "X");
    CHECK_STR(names.name(1), "B");
}

// Круговая проверка: текст → токены → текст.
void test_round_trip()
{
    std::string koi8, error;
    utf8_to_koi8("10 DEFFN R(D8)=INT(D8)+SGN(D8-INT(D8))\n"
                 "20 A=FNR(B/2)\n", koi8);
    NameTable names;
    ProgramImage img;
    if (!tokenize(koi8, img, names, error))
        { std::printf("  %s\n", error.c_str()); CHECK(false); return; }

    // Построчно: detokenize() целиком раздаёт имена заново, а здесь они уже
    // настоящие, из текста.
    std::string text;
    for (unsigned i = 0; i < img.line_count(); ++i) {
        std::string one;
        if (!detokenize_line(img.line(i), names, one, error))
            { std::printf("  %s\n", error.c_str()); CHECK(false); return; }
        text += one;
        text += "\n";
    }
    CHECK_STR(koi8_to_utf8(reinterpret_cast<const uint8_t *>(text.data()),
                           static_cast<unsigned>(text.size())),
              "10 DEFFN R(D8)=INT(D8)+SGN(D8-INT(D8))\n"
              "20 A=FNR(B/2)\n");
}

} // namespace

int main()
{
    test_book_example();
    test_declared_below();
    test_digit_name();
    test_function_calls_function();
    test_corpus_definitions();
    test_formal_is_a_global();
    test_definition_is_skipped();
    test_unknown_function();
    test_string_argument_refused();
    test_recursion_is_bounded();
    test_tokenized_deffn();
    test_tokenized_call();
    test_name_is_not_a_variable();
    test_round_trip();
    return test::summary("функции пользователя");
}
