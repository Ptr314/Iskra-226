// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Mikhail Revzin <p3.141592653589793238462643@gmail.com>
// Part of the Iskra-226 project: https://github.com/Ptr314/Iskra-226
// Description: PRINTUSING и оператор задания формата % (руководство, гл. 16)

#include <cstdio>
#include <string>
#include <vector>

#include "check.h"
#include "core/image.h"
#include "core/interp.h"
#include "core/koi8.h"
#include "core/names.h"
#include "core/tokenize.h"
#include "host_headless/headless_host.h"

using namespace iskra;

namespace {

// Прогон возвращает и экран, и ленту АЦПУ: устройством вывода PRINTUSING
// управляет SELECT PRINT, и проверять надо оба.
bool run_program(ProgramImage & img, std::string & screen, std::string & printer,
                 std::string & error)
{
    HeadlessHost host;
    Interp interp(img, host);
    if (!interp.run(error)) return false;
    screen = host.dump();
    printer = host.printer();
    return true;
}

bool run_text(const char * utf8_source, std::string & screen, std::string & error,
              std::string * printer = 0)
{
    std::string koi8;
    utf8_to_koi8(utf8_source, koi8);

    NameTable names;
    ProgramImage img;
    if (!tokenize(koi8, img, names, error)) return false;

    std::string tape;
    if (!run_program(img, screen, tape, error)) return false;
    if (printer) *printer = tape;
    return true;
}

std::string line_of(const std::string & text, unsigned n)
{
    std::size_t p = 0;
    for (unsigned i = 1; i < n; ++i) {
        const std::size_t e = text.find('\n', p);
        if (e == std::string::npos) return std::string();
        p = e + 1;
    }
    const std::size_t e = text.find('\n', p);
    return text.substr(p, e - p);
}

// --- разбор образа ----------------------------------------------------------

void test_fields()
{
    // Знак — часть описания только вплотную к разрядам. В образе STAT09 480
    // первый дефис принадлежит слову, второй — числу.
    ImageField f;
    CHECK(image_next_field("U-KRIT.=-######.####", 0, f));
    CHECK_EQ(f.at, 8u);
    CHECK_EQ(f.sign, 2u);
    CHECK_EQ(f.ip, 6u);
    CHECK_EQ(f.fp, 4u);
    CHECK(f.dot);
    CHECK_EQ(f.width(), 12u);

    // Образ STAT09 470 — три описания подряд, между ними пробелы.
    const std::string img = "##     ###   ######.###";
    unsigned pos = 0, n = 0;
    while (image_next_field(img, pos, f)) { ++n; pos = f.at + f.len; }
    CHECK_EQ(n, 3u);

    // Образ без единого знака # описаний не содержит вовсе (пример 16.4).
    CHECK(!image_next_field("  PLAN    OTCHET", 0, f));

    // Показатель степени пишут и как ^^^^, и как /\/\/\/\ — в поле это
    // всё равно четыре знака печати.
    CHECK(image_next_field("#.#^^^^", 0, f));
    CHECK(f.exponential);
    CHECK_EQ(f.len, 7u);
    CHECK_EQ(f.width(), 7u);
}

// --- примеры из книги -------------------------------------------------------

// Пример 16.1: тот же столбец чисел оператором PRINT и оператором PRINTUSING.
void test_book_16_1()
{
    std::string screen, error;
    const char * src =
        "10 A=23400:GOSUB 500\n"
        "20 A=5:GOSUB 500\n"
        "30 A=7.3:GOSUB 500\n"
        "40 A=.6:GOSUB 500\n"
        "50 A=13.06:GOSUB 500\n"
        "60 A=140.767685341:GOSUB 500\n"
        "70 A=0:GOSUB 500\n"
        "80 A=.01:GOSUB 500\n"
        "90 END\n"
        "500 PRINTUSING 510,A:RETURN\n"
        "510 %#####.##\n";
    if (!run_text(src, screen, error)) { std::printf("  %s\n", error.c_str()); CHECK(false); return; }

    CHECK_STR(line_of(screen, 1), "23400.00");
    CHECK_STR(line_of(screen, 2), "    5.00");
    CHECK_STR(line_of(screen, 3), "    7.30");
    // «При печати чисел, являющихся дробями, вместо первой цифры целой
    // части чисел подставляется нуль».
    CHECK_STR(line_of(screen, 4), "    0.60");
    CHECK_STR(line_of(screen, 5), "   13.06");
    // Округления нет: «десятичные цифры, для которых в описании формата
    // не хватило знаков #, просто отбрасываются».
    CHECK_STR(line_of(screen, 6), "  140.76");
    CHECK_STR(line_of(screen, 7), "    0.00");
    CHECK_STR(line_of(screen, 8), "    0.01");
}

// Пример 16.2: одно число по четырём описаниям разной точности.
void test_book_16_2()
{
    std::string screen, error;
    const char * src =
        "100 PRINTUSING 110,145.76,145.76,145.76,145.76\n"
        "110 %### ###.# ###.## ###.###\n";
    if (!run_text(src, screen, error)) { std::printf("  %s\n", error.c_str()); CHECK(false); return; }
    // «В последнем формате есть лишний символ справа. Этот символ
    // замещается нулем».
    CHECK_STR(line_of(screen, 1), "145 145.7 145.76 145.760");
}

// Пример 16.3: пояснительный текст в образе печатается как есть.
void test_book_16_3()
{
    std::string screen, error;
    const char * src =
        "130 PRINTUSING 140,45,54,54*100/45\n"
        "140 %PLAN = ###.# OTCHET = ###.# PROCENT= ###.##\n";
    if (!run_text(src, screen, error)) { std::printf("  %s\n", error.c_str()); CHECK(false); return; }
    CHECK_STR(line_of(screen, 1),
              "PLAN =  45.0 OTCHET =  54.0 PROCENT= 120.00");
}

// Пример 16.4: образ без описаний формата печатается целиком, и элементов
// у такого PRINTUSING нет вовсе.
void test_book_16_4()
{
    std::string screen, error;
    const char * src =
        "200 PRINTUSING 100\n"
        "210 PRINTUSING 110,45,54\n"
        "220 PRINTUSING 110,20,25:END\n"
        "100 %  PLAN  OTCHET\n"
        "110 %  ####   ####\n";
    if (!run_text(src, screen, error)) { std::printf("  %s\n", error.c_str()); CHECK(false); return; }
    CHECK_STR(line_of(screen, 1), "  PLAN  OTCHET");
    CHECK_STR(line_of(screen, 2), "    45     54");
    CHECK_STR(line_of(screen, 3), "    20     25");
}

// Пример 16.5: знак в образе.
void test_book_16_5()
{
    std::string screen, error;
    const char * src =
        "100 PRINTUSING 130,15.35,-6.27\n"
        "110 PRINTUSING 140,15.35,-6.27\n"
        "120 PRINTUSING 150,15.35,-6.27:END\n"
        "130 %###.## ###.##\n"
        "140 %-###.## -###.##\n"
        "150 %+###.## +###.##\n";
    if (!run_text(src, screen, error)) { std::printf("  %s\n", error.c_str()); CHECK(false); return; }
    // Без знака в образе минус «теряется».
    CHECK_STR(line_of(screen, 1), " 15.35   6.27");
    // «−» — минус у отрицательных, пробел у остальных.
    CHECK_STR(line_of(screen, 2), "  15.35 -  6.27");
    // «+» — знак печатается всегда.
    CHECK_STR(line_of(screen, 3), "+ 15.35 -  6.27");
}

// Пример 16.7: целая часть не влезла — печатается само описание формата.
void test_book_16_7()
{
    std::string screen, error;
    const char * src =
        "100 PRINTUSING 110,5555,375,147.52\n"
        "110 %### ### ###\n";
    if (!run_text(src, screen, error)) { std::printf("  %s\n", error.c_str()); CHECK(false); return; }
    CHECK_STR(line_of(screen, 1), "### 375 147");
}

// Пример 16.8: экспоненциальная форма — четыре знака за мантиссой.
void test_book_16_8()
{
    std::string screen, error;
    // 2.3E−9 записано делением: отрицательный порядок в константе
    // транслятор пока не кодирует.
    const char * src =
        "100 A=6.57:C=2.3/1000000000\n"
        "110 PRINTUSING 120,A,C\n"
        "120 %#.#^^^^ ##^^^^\n";
    if (!run_text(src, screen, error)) { std::printf("  %s\n", error.c_str()); CHECK(false); return; }
    CHECK_STR(line_of(screen, 1), "6.5E+00 23E-10");
}

// Пример 16.9: символьное значение прижимается влево, лишнее отбрасывается.
void test_book_16_9()
{
    std::string screen, error;
    const char * src =
        "100 A\xC2\xA4=\"PROEKT PLANA\"\n"
        "110 PRINTUSING 130,A\xC2\xA4\n"
        "120 PRINTUSING 140,A\xC2\xA4:END\n"
        "130 %############\n"
        "140 %########\n";
    if (!run_text(src, screen, error)) { std::printf("  %s\n", error.c_str()); CHECK(false); return; }
    CHECK_STR(line_of(screen, 1), "PROEKT PLANA");
    CHECK_STR(line_of(screen, 2), "PROEKT P");
}

// Пример 16.11: элементов больше, чем описаний, — образ идёт по второму разу
// с новой строки.
void test_book_16_11()
{
    std::string screen, error;
    const char * src =
        "160 PRINTUSING 120,13,1320.2,21,725.7,32,5526.5\n"
        "170 END\n"
        "120 % ###  #####.#\n";
    if (!run_text(src, screen, error)) { std::printf("  %s\n", error.c_str()); CHECK(false); return; }
    CHECK_STR(line_of(screen, 1), "  13   1320.2");
    CHECK_STR(line_of(screen, 2), "  21    725.7");
    CHECK_STR(line_of(screen, 3), "  32   5526.5");
}

// Пример 16.12: точка с запятой подавляет перевод строки при повторном
// использовании образа.
void test_book_16_12()
{
    std::string screen, error;
    const char * src =
        "110 PRINTUSING 140,150,156;372,381\n"
        "120 END\n"
        "140 %### ###\n";
    if (!run_text(src, screen, error)) { std::printf("  %s\n", error.c_str()); CHECK(false); return; }
    // В книге напечатано «150 156 372 381» — с пробелом между заходами.
    // Взяться ему неоткуда, кроме как из самого образа: в примере 16.13
    // книга отдельно оговаривает пробел справа от последнего знака #.
    // Разбивку в скане распознавание потеряло, и здесь проверяется
    // механика — образ идёт по второму разу с той же позиции строки.
    CHECK_STR(line_of(screen, 1), "150 156372 381");
    CHECK_STR(line_of(screen, 2), "");
}

// Пример 16.13: точка с запятой в конце оператора подавляет перевод строки,
// и значения печатаются одно за другим на одной строке.
void test_book_16_13()
{
    std::string screen, error;
    const char * src =
        "200 FOR K=1 TO 3\n"
        "210 PRINTUSING 250,K*10;\n"
        "220 NEXT K\n"
        "230 PRINT\n"
        "240 PRINT \"KONEC\":END\n"
        "250 %###.## \n";
    if (!run_text(src, screen, error)) { std::printf("  %s\n", error.c_str()); CHECK(false); return; }
    CHECK_STR(line_of(screen, 1), " 10.00  20.00  30.00");
    CHECK_STR(line_of(screen, 2), "KONEC");
}

// Разд. 16.2: образ можно задать и прямо в операторе — символьной
// константой или символьной переменной.
void test_image_as_string()
{
    std::string screen, error;
    const char * src =
        "10 DIM T\xC2\xA4 30\n"
        "20 T\xC2\xA4=\"ITOGO= ###   PRIBYL = ###.#\"\n"
        "30 PRINTUSING \"###  ##.#\",45,6.5\n"
        "40 PRINTUSING T\xC2\xA4,12,3.5\n";
    if (!run_text(src, screen, error)) { std::printf("  %s\n", error.c_str()); CHECK(false); return; }
    CHECK_STR(line_of(screen, 1), " 45   6.5");
    CHECK_STR(line_of(screen, 2), "ITOGO=  12   PRIBYL =   3.5");
}

// --- устройство вывода ------------------------------------------------------

// «PRINT — устройство вывода для операторов PRINTUSING, HEXPRINT и MATPRINT»
// (руководство, разд. 11.5). По умолчанию это экран, SELECT PRINT 0C уводит
// вывод на АЦПУ — так и делает STAT09 430.
void test_print_device()
{
    std::string screen, printer, error;
    const char * src =
        "10 PRINTUSING 100,1\n"
        "20 SELECT PRINT 0C(130)\n"
        "30 PRINTUSING 100,2\n"
        "40 SELECT PRINT 05(80)\n"
        "50 PRINTUSING 100,3:END\n"
        "100 %###\n";
    if (!run_text(src, screen, error, &printer)) { std::printf("  %s\n", error.c_str()); CHECK(false); return; }
    CHECK_STR(line_of(screen, 1), "  1");
    CHECK_STR(line_of(screen, 2), "  3");
    // На ленте строка кончается парой ВК ПС — экранного курсора у АЦПУ нет.
    CHECK_STR(printer, "  2\r\n");
}

// --- ошибки -----------------------------------------------------------------

void test_missing_image()
{
    // Ссылка в никуда — ошибка машины, а не молчаливая пустая строка.
    std::string screen, error;
    CHECK(!run_text("10 PRINTUSING 900,1\n", screen, error));
    CHECK(error.find("%") != std::string::npos);

    // Строка есть, а оператора % в ней нет.
    CHECK(!run_text("10 PRINTUSING 20,1\n20 REM OBRAZ\n", screen, error));
}

// --- оттранслированная форма ------------------------------------------------

// У токенов своя кодировка, и текстовыми примерами её не проверить: номер
// строки образа — константа E7, элементы разделяются DE, а операнд % —
// сырой текст без длины и без префикса.
class TokenBuilder
{
public:
    void add_numeric_var() { flags_.push_back(0x10); }

    void add_line(unsigned number, const int * body, unsigned n)
    {
        if (!lines_.empty()) lines_.push_back(0xFE);
        lines_.push_back(static_cast<uint8_t>(((number / 1000) % 10) * 16 + (number / 100) % 10));
        lines_.push_back(static_cast<uint8_t>(((number / 10) % 10) * 16 + number % 10));
        lines_.push_back(static_cast<uint8_t>(n + 1));
        for (unsigned i = 0; i < n; ++i) lines_.push_back(static_cast<uint8_t>(body[i]));
    }

    std::vector<uint8_t> file() const
    {
        std::vector<uint8_t> stream;
        const unsigned L2 = static_cast<unsigned>(flags_.size()) * 4;
        stream.push_back(0); stream.push_back(0);
        stream.push_back(L2 >> 8); stream.push_back(L2 & 0xFF);
        stream.push_back(0); stream.push_back(0);
        // Записи таблиц 2/3 идут в порядке убывания индекса переменной.
        for (unsigned i = flags_.size(); i-- > 0; ) {
            stream.push_back(0); stream.push_back(0);
            stream.push_back(flags_[i]);
            stream.push_back(0);
        }
        stream.insert(stream.end(), lines_.begin(), lines_.end());

        std::vector<uint8_t> file(256, 0);
        file[0] = 1;
        file[9] = 0x21;
        for (std::size_t p = 0; p < stream.size(); p += 254) {
            file.push_back(0x00);
            file.push_back(0x80);
            for (unsigned i = 0; i < 254; ++i)
                file.push_back(p + i < stream.size() ? stream[p + i] : 0);
        }
        return file;
    }

private:
    std::vector<uint8_t> flags_;
    std::vector<uint8_t> lines_;
};

void test_tokenized()
{
    TokenBuilder b;
    b.add_numeric_var();                     // 0
    b.add_numeric_var();                     // 1
    b.add_numeric_var();                     // 2

    static const int l10[] = { 0x36, 0x04, 0x00, 0xD9, 0xE8, 0x03,
                               0x36, 0x04, 0x01, 0xD9, 0xE8, 0x07,
                               0x36, 0x04, 0x02, 0xD9, 0xE8, 0x45 };
    b.add_line(10, l10, 18);

    // 28 09 | E7 04 70 DE 00 DE 01 DE 02 — PRINTUSING 470, A, B, C.
    // Номер строки образа — двоично-десятичная константа E7, элементы
    // разделяет DE: ровно как в STAT09 250.
    static const int l20[] = { 0x28, 0x09, 0xE7, 0x04, 0x70,
                               0xDE, 0x00, 0xDE, 0x01, 0xDE, 0x02 };
    b.add_line(20, l20, 11);

    static const int l30[] = { 0x59, 0x00 };                   // END
    b.add_line(30, l30, 2);

    // Байты образа взяты из STAT09 470 как есть: 3F <длина> <сырой текст>.
    static const int l470[] = { 0x3F, 0x17,
                                0x23, 0x23, 0x20, 0x20, 0x20, 0x20, 0x20,
                                0x23, 0x23, 0x23, 0x20, 0x20, 0x20,
                                0x23, 0x23, 0x23, 0x23, 0x23, 0x23, 0x2E,
                                0x23, 0x23, 0x23 };
    b.add_line(470, l470, 25);

    ProgramImage img;
    std::string error;
    if (!img.load_file(b.file(), error)) {
        std::printf("  разбор: %s\n", error.c_str());
        CHECK(false);
        return;
    }
    CHECK_EQ(img.line_count(), 4u);

    std::string screen, printer;
    if (!run_program(img, screen, printer, error)) {
        std::printf("  исполнение: %s\n", error.c_str());
        CHECK(false);
        return;
    }
    CHECK_STR(line_of(screen, 1), " 3       7       45.000");
}

} // namespace

int main()
{
    test_fields();
    test_book_16_1();
    test_book_16_2();
    test_book_16_3();
    test_book_16_4();
    test_book_16_5();
    test_book_16_7();
    test_book_16_8();
    test_book_16_9();
    test_book_16_11();
    test_book_16_12();
    test_book_16_13();
    test_image_as_string();
    test_print_device();
    test_missing_image();
    test_tokenized();
    return test::summary("PRINTUSING и оператор %");
}
