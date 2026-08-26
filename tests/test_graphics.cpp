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

// --- относительное рисование -------------------------------------------------

// `DDRAW` кладёт ту же запись `85`, что и `DRAW`, но точку считает
// приращением от текущей. Что это именно приращение, видно по `SIG` 1560:
// `DDRAW A¤(),0,4*Y2`, `4*X2,0`, `0,-4*Y2`, `-4*X2,0` — прямоугольник,
// который сходится в начале, и отрицательные значения там не координаты.
// То же и в токенах: `M4` даёт `06 14 09 E0 10 DE E8 00 DE E9 E8 03`, то
// есть `DDRAW <буфер>,0,-3`.
void test_ddraw()
{
    HeadlessHost host;
    std::string screen, error;
    if (!run(host,
             "10 DIM B\xC2\xA4(8)16\n"
             "20 \xC2\xA4OPEN B\xC2\xA4()\n"
             "30 NPLOT B\xC2\xA4(),100,50\n"
             "40 DDRAW B\xC2\xA4(),20,0\n"
             "50 DDRAW B\xC2\xA4(),0,-30\n"
             "60 HEXPRINT STR(B\xC2\xA4(),44,15)\n"
             "70 HEXPRINT STR(B\xC2\xA4(),1,6)\n",
             "DDRAW", screen, error)) {
        std::printf("  %s\n", error.c_str());
        CHECK(false);
        return;
    }
    CHECK_STR(line_of(screen, 1), "800064003285007800328500780014");
    CHECK_STR(line_of(screen, 2), "3A0078001400");     // 58 байт, точка 120,20
}

// --- преобразования уже лежащей картинки ------------------------------------

// `¤MOVE <буфер>,<dx>,<dy>` — сдвиг всей картинки. Подсказка `SLIDE` 5880:
// «¤MOVE, ВВЕДИТЕ СДВИГ ПО X И ПО Y».
void test_move()
{
    HeadlessHost host;
    std::string screen, error;
    if (!run(host,
             "10 DIM B\xC2\xA4(8)16\n"
             "20 \xC2\xA4OPEN B\xC2\xA4()\n"
             "30 NPLOT B\xC2\xA4(),100,50\n"
             "40 DRAW B\xC2\xA4(),120,80\n"
             "50 \xC2\xA4MOVE B\xC2\xA4(),10,-20\n"
             "60 HEXPRINT STR(B\xC2\xA4(),44,10)\n",
             "¤MOVE", screen, error)) {
        std::printf("  %s\n", error.c_str());
        CHECK(false);
        return;
    }
    CHECK_STR(line_of(screen, 1), "80006E001E850082003C");   // 110,30 и 130,60
}

// `STRETCH <буфер>,<x>,<y>,<kx>,<ky>` — растяжение относительно точки.
// Точка неподвижна: `SLIDE` 5950 растягивает картинку, поворачивает и
// растягивает обратно теми же множителями наоборот — с «поплавком» такая
// пара не сошлась бы.
void test_stretch()
{
    HeadlessHost host;
    std::string screen, error;
    if (!run(host,
             "10 DIM B\xC2\xA4(8)16\n"
             "20 \xC2\xA4OPEN B\xC2\xA4()\n"
             "30 NPLOT B\xC2\xA4(),100,50\n"
             "40 DRAW B\xC2\xA4(),200,100\n"
             "50 STRETCH B\xC2\xA4(),100,50,2,3\n"
             "60 HEXPRINT STR(B\xC2\xA4(),44,10)\n",
             "STRETCH", screen, error)) {
        std::printf("  %s\n", error.c_str());
        CHECK(false);
        return;
    }
    // Точка (100,50) на месте, а (200,100) уехала в (300,200).
    CHECK_STR(line_of(screen, 1), "800064003285012C00C8");
}

// `TURN <буфер>,<x>,<y>,<угол>` — поворот вокруг точки. Единица угла — из
// таблицы устройств, как у тригонометрии (руководство, разд. 13.3); по
// умолчанию радиан, и `SLIDE` 260 не зря ставит `SELECT D`.
void test_turn()
{
    HeadlessHost host;
    std::string screen, error;
    if (!run(host,
             "10 DIM B\xC2\xA4(8)16\n"
             "20 \xC2\xA4OPEN B\xC2\xA4()\n"
             "30 NPLOT B\xC2\xA4(),100,100\n"
             "40 DRAW B\xC2\xA4(),150,100\n"
             "50 TURN B\xC2\xA4(),100,100,#PI/2\n"
             "60 HEXPRINT STR(B\xC2\xA4(),44,10)\n",
             "TURN", screen, error)) {
        std::printf("  %s\n", error.c_str());
        CHECK(false);
        return;
    }
    // Отрезок вправо стал отрезком вверх: (150,100) → (100,150).
    CHECK_STR(line_of(screen, 1), "80006400648500640096");
}

// **Приращение надписи — со знаком**, в отличие от координаты точки: иначе
// повёрнутая надпись была бы непредставима, а `TURN` крутит картинку
// целиком. Так его читает и сам `SLIDE` — `ADD C (N¤,STR(A¤(),J+2,4))`
// (1430), двоичное сложение с переносом, а оно переносит и заём.
void test_turn_label()
{
    HeadlessHost host;
    std::string screen, error;
    if (!run(host,
             "10 DIM B\xC2\xA4(8)16\n"
             "20 \xC2\xA4OPEN B\xC2\xA4()\n"
             "30 NPLOT B\xC2\xA4(),100,100\n"
             "40 LABEL B\xC2\xA4(),,,\"AB\"\n"
             "50 TURN B\xC2\xA4(),100,100,#PI\n"
             "60 HEXPRINT STR(B\xC2\xA4(),44,5)\n"
             "70 HEXPRINT STR(B\xC2\xA4(),49,11)\n",
             "TURN надписи", screen, error)) {
        std::printf("  %s\n", error.c_str());
        CHECK(false);
        return;
    }
    // Точка вращения — она же и точка надписи, поэтому запись `80` на месте.
    CHECK_STR(line_of(screen, 1), "8000640064");
    // А приращение развернулось: было 14 (два знака по семь), стало -14.
    CHECK_STR(line_of(screen, 2), "860AFFF200000102004142");
}

// Картинка, уехавшая за левый край, — отказ машины, а не наше ограничение:
// `SLIDE` 5070 ставит вокруг `¤MOVE` свой `ON ERROR` и печатает «СДВИГ
// НЕВОЗМОЖЕН (ВЫХОД ЗА ЭКРАН)». Кода у отказа мы не знаем, и `ON ERROR`
// получает `??` — как у отказов `COPY`.
void test_move_off_raster()
{
    HeadlessHost host;
    std::string screen, error;
    if (!run(host,
             "10 DIM B\xC2\xA4(8)16,E\xC2\xA4""4,N\xC2\xA4""4\n"
             "20 \xC2\xA4OPEN B\xC2\xA4()\n"
             "30 NPLOT B\xC2\xA4(),10,10\n"
             "40 ON ERROR E\xC2\xA4,N\xC2\xA4GOTO70\n"
             "50 \xC2\xA4MOVE B\xC2\xA4(),-50,0\n"
             "60 PRINT \"\xD0\x9D\xD0\x95\xD0\xA2 \xD0\x9E\xD0\xA2\xD0\x9A\xD0\x90\xD0\x97\xD0\x90\":STOP\n"
             "70 PRINT \"\xD0\x9E\xD0\xA2\xD0\x9A\xD0\x90\xD0\x97 \";E\xC2\xA4\n",
             "¤MOVE за край", screen, error)) {
        std::printf("  %s\n", error.c_str());
        CHECK(false);
        return;
    }
    CHECK_STR(line_of(screen, 1), "ОТКАЗ ??");
}

// `¤LET` — присваивание картинки. В корпусе форм две и обе вырожденные:
// буфер сам себе (`EDITOR` 1236, 1335; `SIG` 645, 7590) и обнуление
// (`SIG` 7470). Присваивание самому себе ничего не меняет — на этом стоит
// разветвление `EDITOR` 1235/1236, где вторая ветка значит «оставить как
// есть». Заодно здесь проверяется `¤OPEN` с двумя буферами (`SIG` 7580).
void test_let()
{
    HeadlessHost host;
    std::string screen, error;
    if (!run(host,
             "10 DIM B\xC2\xA4(8)16,C\xC2\xA4(8)16\n"
             "20 \xC2\xA4OPEN B\xC2\xA4(),C\xC2\xA4()\n"
             "30 NPLOT B\xC2\xA4(),100,50\n"
             "40 DRAW B\xC2\xA4(),120,80\n"
             "50 \xC2\xA4LET B\xC2\xA4()=B\xC2\xA4()\n"
             "60 HEXPRINT STR(B\xC2\xA4(),44,10)\n"
             "70 \xC2\xA4LET C\xC2\xA4()=B\xC2\xA4()\n"
             "80 HEXPRINT STR(C\xC2\xA4(),1,6)\n"
             "90 HEXPRINT STR(C\xC2\xA4(),44,10)\n"
             "100 \xC2\xA4LET B\xC2\xA4()=0\n"
             "110 HEXPRINT STR(B\xC2\xA4(),1,6)\n",
             "¤LET", screen, error)) {
        std::printf("  %s\n", error.c_str());
        CHECK(false);
        return;
    }
    // Себе самому — без изменений.
    CHECK_STR(line_of(screen, 1), "80006400328500780050");
    // Копия: заголовок 53 байта, точка 120,80, и те же две записи.
    CHECK_STR(line_of(screen, 2), "350078005000");
    CHECK_STR(line_of(screen, 3), "80006400328500780050");
    // Ноль справа опустошает буфер.
    CHECK_STR(line_of(screen, 4), "2B0000000000");
}

// --- PLOT -------------------------------------------------------------------

// `PLOT` — единственный графический оператор без буфера: он рисует прямо на
// устройстве группы `PLOT`. Координаты приращениями: `SLIDE` 6170 считает
// разность соседних точек буфера и подаёт её `PLOT <X2%,Y2%,U>`.
void test_plot_pens()
{
    HeadlessHost host;
    std::string screen, error;
    if (!run(host,
             "10 PLOT <,,R>\n"
             "20 PLOT <10,20,U>,<30,0,D>\n",
             "PLOT перьями", screen, error)) {
        std::printf("  %s\n", error.c_str());
        CHECK(false);
        return;
    }
    CHECK(host.raster().at(10, 20));        // `R` поставил перо в нуль,
    CHECK(host.raster().at(40, 20));        // `U` увёл на (10,20), `D` провёл
    CHECK(!host.raster().at(41, 20));       // отрезок ровно до (40,20)
    CHECK(!host.raster().at(10, 21));
}

// Третьим элементом группы бывает не перо, а надпись: `SLIDE` 5690 подаёт
// туда вырезку из записи буфера. Шаг знака берётся из пера `S`, а пока его
// не задали — семь дискрет, как у `LABEL`.
void test_plot_text()
{
    HeadlessHost host;
    std::string screen, error;
    if (!run(host,
             "10 PLOT <,,R>,<100,100,U>,<,,\"AB\">\n",
             "PLOT надписью", screen, error)) {
        std::printf("  %s\n", error.c_str());
        CHECK(false);
        return;
    }
    CHECK(host.raster().at(100, 100));      // левый нижний угол «A»
    CHECK(host.raster().at(107, 100));      // и «B» через знакоместо
    CHECK(!host.raster().at(105, 100));
}

// Устройство берётся из таблицы: `SELECT PLOT` его меняет, и на том,
// которого у хоста нет, программа останавливается — как при блочном обмене.
void test_plot_missing_device()
{
    std::string koi8, error;
    utf8_to_koi8("10 SELECT PLOT 34\n20 PLOT <10,10,U>\n", koi8);
    NameTable names;
    ProgramImage img;
    if (!tokenize(koi8, img, names, error)) {
        std::printf("  %s\n", error.c_str());
        CHECK(false);
        return;
    }
    HeadlessHost host;
    Interp interp(img, host);
    CHECK(!interp.run(error));
    CHECK(error.find("/34") != std::string::npos);
}

// --- оттранслированная форма ------------------------------------------------

// Байты сверены с парой `SLIDE`/`SL2` (`docs/format.md`, разд. 5):
// `06 1A E0 00 DE 4E DE 44`, `06 1B E0 00 DE 39 DE 45 DE 66`,
// `06 1C E0 00 DE E8 00 DE E8 00 DE 4F DE E8 01 DC 50`.
void test_tokenized_transforms()
{
    std::string koi8, error;
    utf8_to_koi8("10 \xC2\xA4MOVE B\xC2\xA4(),I,J\n"
                 "20 TURN B\xC2\xA4(),R,N,F\n"
                 "30 DDRAW B\xC2\xA4(),0,-3\n", koi8);
    NameTable names;
    ProgramImage img;
    if (!tokenize(koi8, img, names, error)) {
        std::printf("  %s\n", error.c_str());
        CHECK(false);
        return;
    }
    CHECK_EQ(img.line_count(), 3u);

    const std::vector<uint8_t> & a = img.line(0).body;
    CHECK_EQ(a.size(), 9u);
    CHECK_EQ(a[0], 0x06u); CHECK_EQ(a[1], 0x1Au); CHECK_EQ(a[2], 0x06u);
    CHECK_EQ(a[3], 0xE0u); CHECK_EQ(a[5], 0xDEu);

    const std::vector<uint8_t> & b = img.line(1).body;
    CHECK_EQ(b[1], 0x1Bu);
    CHECK_EQ(b[2], 0x08u);                  // буфер и три операнда через DE

    // `DDRAW B¤(),0,-3` = `06 14 09 E0 xx DE E8 00 DE E9 E8 03` (M4).
    const std::vector<uint8_t> & c = img.line(2).body;
    CHECK_EQ(c[1], 0x14u);
    CHECK_EQ(c[2], 0x09u);
    CHECK_EQ(c[5], 0xDEu); CHECK_EQ(c[6], 0xE8u); CHECK_EQ(c[7], 0x00u);
    CHECK_EQ(c[8], 0xDEu); CHECK_EQ(c[9], 0xE9u);
    CHECK_EQ(c[10], 0xE8u); CHECK_EQ(c[11], 0x03u);
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
    test_ddraw();
    test_move();
    test_stretch();
    test_turn();
    test_turn_label();
    test_move_off_raster();
    test_let();
    test_plot_pens();
    test_plot_text();
    test_plot_missing_device();
    test_tokenized_transforms();
    return test::summary("графический буфер и растр");
}
