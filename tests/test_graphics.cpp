// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Mikhail Revzin <p3.141592653589793238462643@gmail.com>
// Part of the Iskra-226 project: https://github.com/Ptr314/Iskra-226
// Description: графический буфер и растр — заголовок, записи, вывод

// Буфер графики — обычный символьный массив Бейсика, и всё, что с ним
// делают операторы, видно самой программе. Поэтому байты здесь проверяются
// так же, как их видела бы машина: `HEXPRINT STR(B¤(),…)`.
//
// Разметка сверена с живой картинкой, снятой с образа `012   1.dsk`
// (`tools/probes/gbuffer.py`): у надписи «Y» там лежит ровно
// `86 09 00 07 00 00 01 02 00 59`, и столько же выходит у нас.
//
// Каждая программа исполняется дважды: как есть и после круга
// «токены → текст → токены». Совпасть обязаны и экран, и растр.

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

// Отпечаток растра: сколько точек горит и где они лежат. Сравнивать кадры
// целиком незачем, а число и края ловят любое расхождение.
std::string raster_sig(const Raster & r)
{
    unsigned lit = 0, x0 = RASTER_WIDTH, x1 = 0, y0 = RASTER_HEIGHT, y1 = 0;
    for (unsigned y = 0; y < RASTER_HEIGHT; ++y)
        for (unsigned x = 0; x < RASTER_WIDTH; ++x)
            if (r.at(x, y)) {
                ++lit;
                if (x < x0) x0 = x;
                if (x > x1) x1 = x;
                if (y < y0) y0 = y;
                if (y > y1) y1 = y;
            }
    char b[128];
    if (!lit) return "пусто";
    std::sprintf(b, "%u точек, x %u…%u, y %u…%u", lit, x0, x1, y0, y1);
    return b;
}

// Прогнать текст на своём хосте. Заодно круг «токены → текст → токены»:
// вторая копия обязана дать тот же экран и тот же растр.
bool run(HeadlessHost & host, const char * utf8, const char * what,
         std::string & screen, std::string & error)
{
    std::string koi8;
    utf8_to_koi8(utf8, koi8);

    NameTable names;
    ProgramImage img;
    if (!tokenize(koi8, img, names, error)) {
        std::printf("  %s: трансляция — %s\n", what, error.c_str());
        return false;
    }

    {
        ProgramImage copy = img;
        Interp interp(copy, host);
        interp.set_max_steps(200000);
        if (!interp.run(error)) return false;
        screen = host.dump();
    }

    // Круг: обратная трансляция и снова вперёд.
    std::string text;
    NameTable back;
    if (!detokenize(img, back, text, error)) {
        std::printf("  %s: обратная трансляция — %s\n", what, error.c_str());
        return false;
    }
    NameTable again;
    ProgramImage img2;
    if (!tokenize(text, img2, again, error)) {
        std::printf("  %s: обратно в токены — %s\n", what, error.c_str());
        return false;
    }

    HeadlessHost host2;
    Interp interp2(img2, host2);
    interp2.set_max_steps(200000);
    std::string e2;
    if (!interp2.run(e2)) {
        std::printf("  %s: после круга — %s\n", what, e2.c_str());
        return false;
    }
    if (host2.dump() != screen) {
        std::printf("  %s: после круга экран другой\n", what);
        return false;
    }
    if (raster_sig(host2.raster()) != raster_sig(host.raster())) {
        std::printf("  %s: после круга растр другой:\n    было %s\n    стало %s\n",
                    what, raster_sig(host.raster()).c_str(),
                    raster_sig(host2.raster()).c_str());
        return false;
    }
    return true;
}

// Пустой буфер — 43 байта, точка в нуле. `SLIDE` 5180 кладёт этот заголовок
// руками: `STR(A¤(),1,6)=HEX(2B0000000000)`.
void test_header()
{
    HeadlessHost host;
    std::string screen, error;
    if (!run(host,
             "10 DIM B\xC2\xA4(4)16\n"
             "20 \xC2\xA4OPEN B\xC2\xA4()\n"
             "30 HEXPRINT STR(B\xC2\xA4(),1,6)\n",
             "заголовок", screen, error)) {
        std::printf("  %s\n", error.c_str());
        CHECK(false);
        return;
    }
    CHECK_STR(line_of(screen, 1), "2B0000000000");
}

// Записи точки — пять байт: код и две координаты старшим байтом вперёд.
// Длина в заголовке растёт на них, а числа там лежат наоборот, младшим
// байтом вперёд: 43 + 10 = 53 = `35 00`.
void test_records()
{
    HeadlessHost host;
    std::string screen, error;
    if (!run(host,
             "10 DIM B\xC2\xA4(4)16\n"
             "20 \xC2\xA4OPEN B\xC2\xA4()\n"
             "30 NPLOT B\xC2\xA4(),38,243:DRAW B\xC2\xA4(),100,50\n"
             "40 HEXPRINT STR(B\xC2\xA4(),1,2)\n"
             "50 HEXPRINT STR(B\xC2\xA4(),44,10)\n"
             "60 DOT B\xC2\xA4(),7,9\n"
             "70 HEXPRINT STR(B\xC2\xA4(),54,5)\n",
             "записи", screen, error)) {
        std::printf("  %s\n", error.c_str());
        CHECK(false);
        return;
    }
    CHECK_STR(line_of(screen, 1), "3500");
    CHECK_STR(line_of(screen, 2), "80002600F38500640032");
    CHECK_STR(line_of(screen, 3), "8400070009");
}

// Надпись: код, длина считая себя, четыре байта приращения, три признака и
// текст. Приращение — семь дискрет на знак. Байт в байт с картинкой,
// снятой с образа.
void test_label()
{
    HeadlessHost host;
    std::string screen, error;
    if (!run(host,
             "10 DIM B\xC2\xA4(4)16\n"
             "20 \xC2\xA4OPEN B\xC2\xA4()\n"
             "30 LABEL B\xC2\xA4(),,,\"Y\"\n"
             "40 HEXPRINT STR(B\xC2\xA4(),44,10)\n"
             "50 LABEL B\xC2\xA4(),,,\"-1\"\n"
             "60 HEXPRINT STR(B\xC2\xA4(),54,11)\n"
             "70 HEXPRINT STR(B\xC2\xA4(),3,4)\n",
             "надпись", screen, error)) {
        std::printf("  %s\n", error.c_str());
        CHECK(false);
        return;
    }
    CHECK_STR(line_of(screen, 1), "86090007000001020059");
    CHECK_STR(line_of(screen, 2), "860A000E00000102002D31");
    // Точка ушла вправо на 7 и ещё на 14 — приращения обеих надписей. В
    // заголовке числа лежат младшим байтом вперёд, поэтому 21 это `15 00`.
    CHECK_STR(line_of(screen, 3), "15000000");
}

// `WINDOW` — область на устройстве, `FRAME` — та же область в
// пользовательских единицах. Точка 5,5 при рамке 0…10 ложится ровно на
// середину экрана: 5*559/10 = 279 (`01 17`), 5*255/10 = 127 (`7F`).
void test_window_frame()
{
    HeadlessHost host;
    std::string screen, error;
    if (!run(host,
             "10 DIM B\xC2\xA4(4)16\n"
             "20 \xC2\xA4OPEN B\xC2\xA4()\n"
             "30 WINDOW 0,559,0,255:FRAME B\xC2\xA4(),0,10,0,10\n"
             "40 NPLOT B\xC2\xA4(),5,5\n"
             "50 HEXPRINT STR(B\xC2\xA4(),44,5)\n"
             "60 FRAME B\xC2\xA4(),0,559,0,255\n"
             "70 NPLOT B\xC2\xA4(),38,243\n"
             "80 HEXPRINT STR(B\xC2\xA4(),49,5)\n",
             "окно и рамка", screen, error)) {
        std::printf("  %s\n", error.c_str());
        CHECK(false);
        return;
    }
    CHECK_STR(line_of(screen, 1), "800117007F");
    CHECK_STR(line_of(screen, 2), "80002600F3");
}

// Пока буфер не выложен на устройство, растр пуст: `NPLOT` и `DRAW` ничего
// не рисуют, они дописывают записи.
void test_copy_draws()
{
    HeadlessHost host;
    std::string screen, error;
    CHECK(host.raster().empty());
    if (!run(host,
             "10 DIM B\xC2\xA4(40)253\n"
             "20 \xC2\xA4OPEN B\xC2\xA4()\n"
             "30 NPLOT B\xC2\xA4(),10,20:DRAW B\xC2\xA4(),30,20\n"
             "40 \xC2\xA4" "COPY /10,B\xC2\xA4()\n",
             "вывод", screen, error)) {
        std::printf("  %s\n", error.c_str());
        CHECK(false);
        return;
    }
    // Отрезок по горизонтали: двадцать одна точка от 10 до 30.
    CHECK_STR(raster_sig(host.raster()), "21 точек, x 10…30, y 20…20");
    CHECK(host.raster().at(10, 20));
    CHECK(host.raster().at(30, 20));
    CHECK(!host.raster().at(9, 20));
    CHECK(!host.raster().at(31, 20));
    CHECK(!host.raster().at(20, 21));
    // Лист графопостроителя при этом чист: буфер уходил на трубку.
    CHECK(host.plotter().empty());
}

// `¤COPY /14` — тот же буфер на графопостроитель. Бумаги у нас нет, и лист
// берётся в тех же координатах, что и буфер (docs/DECISIONS.md, разд. 15).
void test_copy_plotter()
{
    HeadlessHost host;
    std::string screen, error;
    if (!run(host,
             "10 DIM B\xC2\xA4(40)253\n"
             "20 \xC2\xA4OPEN B\xC2\xA4()\n"
             "30 NPLOT B\xC2\xA4(),0,0:DRAW B\xC2\xA4(),0,10\n"
             "40 \xC2\xA4" "COPY /14,B\xC2\xA4()\n",
             "графопостроитель", screen, error)) {
        std::printf("  %s\n", error.c_str());
        CHECK(false);
        return;
    }
    CHECK(host.raster().empty());
    CHECK_STR(raster_sig(host.plotter()), "11 точек, x 0…0, y 0…10");
}

// `¤COPY` чистит растр перед выводом: иначе не работал бы `SLIDE`, у
// которого стирание элемента — это сброс бита `04` в коде записи (5240), а
// следом одна только «ВОССТ.ИНФ.» = `¤COPY /10` (1480).
void test_copy_clears()
{
    HeadlessHost host;
    std::string screen, error;
    if (!run(host,
             "10 DIM B\xC2\xA4(40)253\n"
             "20 \xC2\xA4OPEN B\xC2\xA4()\n"
             "30 NPLOT B\xC2\xA4(),10,20:DRAW B\xC2\xA4(),30,20\n"
             "40 \xC2\xA4" "COPY /10,B\xC2\xA4()\n"
             "50 \xC2\xA4OPEN B\xC2\xA4()\n"
             "60 NPLOT B\xC2\xA4(),100,50:DRAW B\xC2\xA4(),110,50\n"
             "70 \xC2\xA4" "COPY /10,B\xC2\xA4()\n",
             "очистка", screen, error)) {
        std::printf("  %s\n", error.c_str());
        CHECK(false);
        return;
    }
    // От первой картинки не осталось ничего.
    CHECK_STR(raster_sig(host.raster()), "11 точек, x 100…110, y 50…50");
}

// Стирание элемента по-настоящему, как это делает `SLIDE`: сброс бита `04`
// превращает `85 DRAW` в `81 NDRAW`, и линия перестаёт рисоваться, оставаясь
// в буфере. Запись `DRAW` лежит с 49-го байта: заголовок 43 плюс пятибайтовый
// `NPLOT`.
void test_erased_record()
{
    HeadlessHost host;
    std::string screen, error;
    if (!run(host,
             "10 DIM B\xC2\xA4(40)253\n"
             "20 \xC2\xA4OPEN B\xC2\xA4()\n"
             "30 NPLOT B\xC2\xA4(),10,20:DRAW B\xC2\xA4(),30,20\n"
             "40 \xC2\xA4" "COPY /10,B\xC2\xA4()\n"
             "50 HEXPRINT STR(B\xC2\xA4(),49,1)\n"
             "60 AND(STR(B\xC2\xA4(),49,1),FB)\n"
             "70 HEXPRINT STR(B\xC2\xA4(),49,1)\n"
             "80 \xC2\xA4" "COPY /10,B\xC2\xA4()\n",
             "стёртая запись", screen, error)) {
        std::printf("  %s\n", error.c_str());
        CHECK(false);
        return;
    }
    CHECK_STR(line_of(screen, 1), "85");
    CHECK_STR(line_of(screen, 2), "81");
    CHECK_STR(raster_sig(host.raster()), "пусто");
}

// `PRINT /10,HEX(03)` гасит графическое поле. Сходится втроём: руководство
// по 2282 Graphic CRT, своя таблица кодов дисплея (`03` — КТ) и `GRAFISN`
// 110, где `PRINT HEX(03)` и `PRINT /10,HEX(03)` стоят подряд — гасится
// текст, следом графика. Прочие коды устройства не разобраны, и пакет с
// ними отвергается целиком.
void test_clear_by_code()
{
    HeadlessHost host;
    std::string screen, error;
    if (!run(host,
             "10 DIM B\xC2\xA4(40)253\n"
             "20 \xC2\xA4OPEN B\xC2\xA4()\n"
             "30 NPLOT B\xC2\xA4(),10,20:DRAW B\xC2\xA4(),30,20\n"
             "40 \xC2\xA4" "COPY /10,B\xC2\xA4()\n"
             "50 PRINT /10,HEX(03)\n",
             "гашение", screen, error)) {
        std::printf("  %s\n", error.c_str());
        CHECK(false);
        return;
    }
    CHECK(host.raster().empty());

    // `0D`, `0E` и `0F` — размер знака и состояние пера: сами по себе не
    // рисуют, точку кладёт только двоичный вектор, а его мы не принимаем.
    HeadlessHost h1;
    std::string s1;
    if (!run(h1,
             "10 PRINT /10,HEX(0E);\n"
             "20 PRINT /10,HEX(0D0F);\n"
             "30 PRINT \"ДОШЛИ\"\n",
             "перо и размер", s1, error)) {
        std::printf("  %s\n", error.c_str());
        CHECK(false);
    } else {
        CHECK_STR(line_of(s1, 1), "ДОШЛИ");
        CHECK(h1.raster().empty());
    }

    // Неразобранный код останавливает программу: принять и промолчать
    // значило бы соврать. Ключ `-i` велит такое пропускать.
    std::string koi8;
    utf8_to_koi8("10 PRINT /10,HEX(01)\n", koi8);
    NameTable names;
    ProgramImage img;
    if (!tokenize(koi8, img, names, error)) { CHECK(false); return; }
    HeadlessHost h2;
    Interp interp(img, h2);
    CHECK(!interp.run(error));
    CHECK(error.find("/10") != std::string::npos);
}

// Устройства, которого у хоста нет, программа не переживает: молча терять
// картинку нельзя — вывод выглядел бы работающим.
void test_copy_unknown_device()
{
    std::string koi8, error;
    utf8_to_koi8("10 DIM B\xC2\xA4(4)16\n"
                 "20 \xC2\xA4OPEN B\xC2\xA4()\n"
                 "30 \xC2\xA4" "COPY /0C,B\xC2\xA4()\n", koi8);
    NameTable names;
    ProgramImage img;
    if (!tokenize(koi8, img, names, error)) { CHECK(false); return; }

    HeadlessHost host;
    Interp interp(img, host);
    CHECK(!interp.run(error));
    CHECK(error.find("/0C") != std::string::npos);
}

// Невскрытая переменная буфером не является: в ней пробелы, и длина оттуда
// вышла бы бессмысленная.
void test_copy_unopened()
{
    std::string koi8, error;
    utf8_to_koi8("10 DIM B\xC2\xA4(4)16\n"
                 "20 \xC2\xA4" "COPY /10,B\xC2\xA4()\n", koi8);
    NameTable names;
    ProgramImage img;
    if (!tokenize(koi8, img, names, error)) { CHECK(false); return; }

    HeadlessHost host;
    Interp interp(img, host);
    CHECK(!interp.run(error));
    CHECK(error.find("\xC2\xA4OPEN") != std::string::npos ||
          error.find("OPEN") != std::string::npos);
}

// Поле короче заголовка буфером быть не может.
void test_too_small()
{
    std::string koi8, error;
    utf8_to_koi8("10 DIM B\xC2\xA4(2)16\n"
                 "20 \xC2\xA4OPEN B\xC2\xA4()\n", koi8);
    NameTable names;
    ProgramImage img;
    if (!tokenize(koi8, img, names, error)) { CHECK(false); return; }

    HeadlessHost host;
    Interp interp(img, host);
    CHECK(!interp.run(error));
    CHECK(error.find("43") != std::string::npos);
}

// Надпись рисуется тем же знакогенератором: ширина знакоместа семь дискрет
// сходится с приращением из записи.
void test_label_draws()
{
    HeadlessHost host;
    std::string screen, error;
    if (!run(host,
             "10 DIM B\xC2\xA4(40)253\n"
             "20 \xC2\xA4OPEN B\xC2\xA4()\n"
             "30 NPLOT B\xC2\xA4(),100,100\n"
             "40 LABEL B\xC2\xA4(),,,\"AB\"\n"
             "50 \xC2\xA4" "COPY /10,B\xC2\xA4()\n",
             "надпись растром", screen, error)) {
        std::printf("  %s\n", error.c_str());
        CHECK(false);
        return;
    }
    // Два знака по пять точек в ширину: первый с 100, второй с 107.
    CHECK(!host.raster().empty());
    CHECK(host.raster().at(100, 100));      // левый нижний угол «А»
    CHECK(host.raster().at(107, 100));      // и «В» через знакоместо
    CHECK(!host.raster().at(105, 100));     // просвет между ними
}

} // namespace

int main()
{
    test_header();
    test_records();
    test_label();
    test_window_frame();
    test_copy_draws();
    test_copy_clears();
    test_clear_by_code();
    test_erased_record();
    test_copy_plotter();
    test_copy_unknown_device();
    test_copy_unopened();
    test_too_small();
    test_label_draws();
    return test::summary("графический буфер и растр");
}
