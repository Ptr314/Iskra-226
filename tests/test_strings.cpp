// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Mikhail Revzin <p3.141592653589793238462643@gmail.com>
// Part of the Iskra-226 project: https://github.com/Ptr314/Iskra-226
// Description: символьные переменные, STR( и функции над строками

#include <cstdio>
#include <string>
#include <vector>

#include "check.h"
#include "core/names.h"
#include "core/tokenize.h"
#include "core/interp.h"
#include "core/keys.h"
#include "core/koi8.h"
#include "host_headless/headless_host.h"

using namespace iskra;

namespace {

bool run_program(ProgramImage & img, const char * input, std::string & screen,
                 std::string & error)
{
    HeadlessHost host;
    if (input) {
        std::string keys;
        utf8_to_koi8(input, keys);
        // Конец набора в сценариях пишется как `\\r`, а у клавиши
        // CR/LF свой код (`core/keys.h`).
        for (std::size_t i = 0; i < keys.size(); ++i)
            if (keys[i] == '\r') keys[i] = static_cast<char>(KEY_CR);
        host.feed_keys(reinterpret_cast<const uint8_t *>(keys.data()),
                       static_cast<unsigned>(keys.size()));
    }
    Interp interp(img, host);
    if (!interp.run(error)) return false;
    screen = host.dump();
    return true;
}

bool run_text(const char * utf8_source, std::string & screen, std::string & error,
              const char * input = 0)
{
    std::string koi8;
    utf8_to_koi8(utf8_source, koi8);

    NameTable names;
    // Текст исполняется не сам по себе: он сначала транслируется в токены,
    // как и в машине (docs/DECISIONS.md, разд. 12).
    ProgramImage img;
    if (!tokenize(koi8, img, names, error)) return false;
    return run_program(img, input, screen, error);
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

// --- Примеры из руководства -------------------------------------------------

// Разд. 4.3: «если длина присваиваемого значения больше размерности
// символьной переменной, то крайние справа символы игнорируются».
void test_length_and_truncation()
{
    std::string screen, error;
    const char * src =
        "10 A¤=\"РАСЧЕТ СЕБЕСТОИМОСТИ\"\n"
        "20 PRINT A¤\n";
    if (!run_text(src, screen, error)) { std::printf("  %s\n", error.c_str()); CHECK(false); return; }
    CHECK_STR(line_of(screen, 1), "РАСЧЕТ СЕБЕСТОИМ");     // ровно 16 символов

    const char * src2 =
        "5 DIM A¤20\n"
        "10 A¤=\"РАСЧЕТ СЕБЕСТОИМОСТИ\"\n"
        "20 PRINT A¤\n";
    if (!run_text(src2, screen, error)) { std::printf("  %s\n", error.c_str()); CHECK(false); return; }
    CHECK_STR(line_of(screen, 1), "РАСЧЕТ СЕБЕСТОИМОСТИ");
}

// Разд. 13.2: STR(A¤,6,3) от «МИНИ-ЭВМ» это «ЭВМ»; STR(B¤,10) — до конца поля.
void test_substring()
{
    std::string screen, error;
    const char * src =
        "10 A¤=\"МИНИ-ЭВМ\"\n"
        "20 PRINT STR(A¤,6,3)\n"
        "30 B¤=\"МАТЕРИАЛ N 50\"\n"
        "40 PRINT STR(B¤,10)\n";
    if (!run_text(src, screen, error)) { std::printf("  %s\n", error.c_str()); CHECK(false); return; }
    CHECK_STR(line_of(screen, 1), "ЭВМ");
    CHECK_STR(line_of(screen, 2), "N 50");                 // хвостовые пробелы срезает дамп
}

// Разд. 13.2: STR( слева от знака равенства присваивает части переменной.
void test_substring_assignment()
{
    std::string screen, error;
    const char * src =
        "10 K¤=\"МАТЕРИАЛ:\"\n"
        "20 M¤=\"КИРПИЧ\"\n"
        "30 STR(K¤,11)=M¤\n"
        "40 PRINT K¤\n";
    if (!run_text(src, screen, error)) { std::printf("  %s\n", error.c_str()); CHECK(false); return; }
    CHECK_STR(line_of(screen, 1), "МАТЕРИАЛ: КИРПИЧ");
}

// Разд. 13.3: LEN — до последнего непробельного байта, у строки из пробелов 1.
void test_len()
{
    std::string screen, error;
    const char * src =
        "10 A¤=\"МИНИ-ЭВМ\"\n"
        "20 PRINT LEN(A¤);LEN(HEX(202122818283));LEN(\"   \")\n";
    if (!run_text(src, screen, error)) { std::printf("  %s\n", error.c_str()); CHECK(false); return; }
    CHECK_STR(line_of(screen, 1), " 8  6  1");
}

// **У неявных функций нет своей закрывающей скобки.** В тексте `LEN(A¤)`
// она есть, а в потоке её нет вовсе: `B=LEN(A¤)` = `36 04 00 D9 ED 01`.
// Значит всякий `D0` после аргумента принадлежит кому-то снаружи, и трогать
// его нельзя. Ровно на этом вставал `EDITOR` 5705 — `STR(B9¤,1,LEN B9¤)`:
// скобку `STR(` съедала `LEN`, а `STR(` потом сообщала, что не закрыта.
void test_implicit_inside()
{
    std::string screen, error;
    const char * src =
        "10 DIM B¤8,C¤4\n"
        "20 B¤=\"1CF\":C¤=HEX(41424344)\n"
        "30 PRINT STR(B¤,1,LEN(B¤));\".\"\n"
        "40 PRINT STR(B¤,1,NUM(B¤));\".\"\n"
        "50 PRINT STR(C¤,1,VAL(C¤)-64);\".\"\n"
        "60 PRINT STR(C¤,1,VAL(C¤,2)/4200);\".\"\n"
        "70 PRINT STR(B¤,1,POS(B¤=\"C\"));\".\"\n"
        "80 PRINT (LEN(B¤)+1)*2\n";
    if (!run_text(src, screen, error)) { std::printf("  %s\n", error.c_str()); CHECK(false); return; }
    CHECK_STR(line_of(screen, 1), "1CF.");
    CHECK_STR(line_of(screen, 2), "1.");
    CHECK_STR(line_of(screen, 3), "A.");
    CHECK_STR(line_of(screen, 4), "ABC.");
    CHECK_STR(line_of(screen, 5), "1C.");
    CHECK_STR(line_of(screen, 6), " 8");
}

// Разд. 13.6: три примера NUM из руководства.
void test_num()
{
    std::string screen, error;
    const char * src =
        "10 A¤=\"98.1E+10\"\n"
        "20 B¤=\"+ 1.2   -14   +1.2587\"\n"
        "30 C¤=\"0..\"\n"
        "40 PRINT NUM(A¤);NUM(B¤);NUM(C¤)\n";
    if (!run_text(src, screen, error)) { std::printf("  %s\n", error.c_str()); CHECK(false); return; }
    // A¤ — всё поле из шестнадцати байт: хвост из одних пробелов входит в счёт.
    // B¤ — только «+ 1.2», потому что дальше не пробелы.
    CHECK_STR(line_of(screen, 1), " 16  5  2");
}

// Разд. 14.2: VAL(X¤,2) = VAL(X¤)*256 + VAL(STR(X¤,2)).
void test_val()
{
    std::string screen, error;
    const char * src =
        "10 X¤=HEX(0200)\n"
        "20 PRINT VAL(X¤);VAL(X¤,2);VAL(STR(X¤,2))\n";
    if (!run_text(src, screen, error)) { std::printf("  %s\n", error.c_str()); CHECK(false); return; }
    CHECK_STR(line_of(screen, 1), " 2  512  0");

    // Оба примера книги целиком.
    error.clear();
    const char * src2 =
        "10 A¤=HEX(00):PRINT VAL(A¤)\n"
        "20 A¤=HEX(FF):PRINT VAL(A¤)\n"
        "30 A¤=HEX(0001):PRINT VAL(A¤,2)\n";
    if (!run_text(src2, screen, error)) { std::printf("  %s\n", error.c_str()); CHECK(false); return; }
    CHECK_STR(line_of(screen, 1), " 0");
    CHECK_STR(line_of(screen, 2), " 255");
    CHECK_STR(line_of(screen, 3), " 1");

    // «Первого байта или первых двух байтов» — других длин грамматика книги
    // не даёт, и в корпусе других не встречается.
    error.clear();
    CHECK(!run_text("10 A¤=HEX(0102)\n20 PRINT VAL(A¤,3)\n", screen, error));
    CHECK(error.find("только 2") != std::string::npos);
}

// Разд. 14.2: BIN — операция, обратная VAL. Пример 14.4 книги целиком.
// В книге состояние A¤ печатается через HEXPRINT, которого ещё нет, поэтому
// оба байта читаются обратно через VAL(A¤,2): 0100 = 256, 2000 = 8192 и т. д.
void test_bin()
{
    std::string screen, error;
    const char * src =
        "10 DIM A¤2\n"
        "20 INIT(00)A¤\n"
        "30 BIN(A¤)=1:PRINT VAL(A¤,2)\n"
        "40 BIN(A¤)=32:PRINT VAL(A¤,2)\n"
        "50 BIN(A¤)=47:PRINT VAL(A¤,2)\n"
        "60 BIN(STR(A¤,2,1))=255:PRINT VAL(A¤,2)\n"
        "70 BIN(A¤,2)=1:PRINT VAL(A¤,2)\n"
        "80 BIN(A¤,2)=33:PRINT VAL(A¤,2)\n"
        "90 BIN(A¤,2)=256:PRINT VAL(A¤,2)\n"
        "100 BIN(A¤,2)=65534:PRINT VAL(A¤,2)\n";
    if (!run_text(src, screen, error)) { std::printf("  %s\n", error.c_str()); CHECK(false); return; }

    static const char * WANT[] = {
        " 256",      // 0100 — записан только первый байт, второй не тронут
        " 8192",     // 2000
        " 12032",    // 2F00
        " 12287",    // 2FFF — BIN( во второй байт через STR(
        " 1",        // 0001
        " 33",       // 0021
        " 256",      // 0100
        " 65534"     // FFFE
    };
    for (unsigned i = 0; i < sizeof(WANT) / sizeof(WANT[0]); ++i)
        CHECK_STR(line_of(screen, i + 1), WANT[i]);
}

void test_bin_limits()
{
    std::string screen, error;

    // «Преобразует целую часть арифметического выражения».
    if (!run_text("10 DIM A¤2\n20 A¤=HEX(0000)\n30 BIN(A¤)=47.9\n40 PRINT VAL(A¤)\n",
                  screen, error)) {
        std::printf("  %s\n", error.c_str());
        CHECK(false);
        return;
    }
    CHECK_STR(line_of(screen, 1), " 47");

    // «Значение арифметического выражения должно быть в пределах 0 ≤ A ≤ 255»
    // без параметра и 0 ≤ A ≤ 65535 с параметром 2.
    error.clear();
    CHECK(!run_text("10 DIM A¤2\n20 BIN(A¤)=256\n", screen, error));
    CHECK(error.find("0…255") != std::string::npos);

    error.clear();
    CHECK(!run_text("10 DIM A¤2\n20 BIN(A¤,2)=65536\n", screen, error));
    CHECK(error.find("0…65535") != std::string::npos);

    error.clear();
    CHECK(!run_text("10 DIM A¤2\n20 BIN(A¤)=-1\n", screen, error));
    CHECK(error.find("0…255") != std::string::npos);

    // Двум байтам нужен приёмник хотя бы в два байта.
    error.clear();
    CHECK(!run_text("10 DIM A¤2\n20 BIN(STR(A¤,2,1),2)=1\n", screen, error));
    CHECK(error.find("короче") != std::string::npos);

    // Других значений параметра книга не даёт.
    error.clear();
    CHECK(!run_text("10 DIM A¤4\n20 BIN(A¤,3)=1\n", screen, error));
    CHECK(error.find("только 2") != std::string::npos);

    // «BIN(<символьная переменная>)» — числовая приёмником не бывает.
    error.clear();
    CHECK(!run_text("10 BIN(X)=1\n", screen, error));
    CHECK(error.find("символьную") != std::string::npos);
}

// Разд. 13.3: INIT заполняет одним значением все байты приёмников. Значение —
// код из двух шестнадцатеричных цифр, символ в кавычках или символьная
// переменная (берётся её первый байт).
void test_init()
{
    std::string screen, error;
    const char * src =
        "10 DIM A¤2,T¤5,C¤(3)4,M¤2\n"
        "20 INIT(00)A¤\n"
        "30 PRINT VAL(A¤,2)\n"
        "40 INIT(2E)T¤\n"
        "50 PRINT \"[\";T¤;\"]\"\n"
        "60 INIT(\".\")C¤()\n"
        "70 PRINT STR(C¤(),1,12)\n"
        "80 INIT(20)STR(T¤,2,3)\n"
        "90 PRINT \"[\";T¤;\"]\"\n"
        "100 M¤=\"*\"\n"
        "110 INIT(M¤)A¤\n"
        "120 PRINT A¤\n";
    if (!run_text(src, screen, error)) { std::printf("  %s\n", error.c_str()); CHECK(false); return; }

    CHECK_STR(line_of(screen, 1), " 0");
    CHECK_STR(line_of(screen, 2), "[.....]");
    // «Значение присваивается всем байтам символьного массива»: три элемента
    // по четыре байта — одно непрерывное поле в двенадцать точек.
    CHECK_STR(line_of(screen, 3), "............");
    // Через STR( — только указанные байты, остальные не тронуты.
    CHECK_STR(line_of(screen, 4), "[.   .]");
    // «Используется первый байт символьной переменной».
    CHECK_STR(line_of(screen, 5), "**");
}

void test_init_errors()
{
    std::string screen, error;

    CHECK(!run_text("10 INIT(00)X\n", screen, error));
    CHECK(error.find("символьные") != std::string::npos);

    error.clear();
    CHECK(!run_text("10 DIM A¤2\n20 X=300:INIT(X)A¤\n", screen, error));
    CHECK(error.find("не байт") != std::string::npos);
}

// Разд. 15.1: POS ищет один байт по заданному отношению, 0 если не найден.
void test_pos()
{
    std::string screen, error;
    const char * src =
        "10 A¤=\"МИНИ-ЭВМ\"\n"
        "20 PRINT POS(A¤=\"-\");POS(A¤=\"Ж\")\n";
    if (!run_text(src, screen, error)) { std::printf("  %s\n", error.c_str()); CHECK(false); return; }
    CHECK_STR(line_of(screen, 1), " 5  0");
}

void test_comparison()
{
    std::string screen, error;
    const char * src =
        "10 A¤=\"ABC\"\n"
        "20 B¤=\"ABD\"\n"
        "30 IF A¤<B¤ THEN 50\n"
        "40 PRINT \"NET\":STOP\n"
        "50 IF A¤=\"ABC\" THEN 70\n"
        "60 PRINT \"NET\":STOP\n"
        "70 PRINT \"DA\"\n";
    if (!run_text(src, screen, error)) { std::printf("  %s\n", error.c_str()); CHECK(false); return; }
    CHECK_STR(line_of(screen, 1), "DA");
}

// Разд. 13.2: массив рассматривается как одна непрерывная строка, поэтому
// STR( по имени массива видит все его элементы подряд.
void test_string_array()
{
    std::string screen, error;
    const char * src =
        "10 DIM A¤(3)4\n"
        "20 A¤(1)=\"АА\":A¤(2)=\"ББ\":A¤(3)=\"ВВ\"\n"
        "30 PRINT A¤(2)\n"
        "40 PRINT STR(A¤(),1,12)\n";
    if (!run_text(src, screen, error)) { std::printf("  %s\n", error.c_str()); CHECK(false); return; }
    CHECK_STR(line_of(screen, 1), "ББ");
    CHECK_STR(line_of(screen, 2), "АА  ББ  ВВ");
}

void test_input()
{
    std::string screen, error;
    const char * src =
        "10 INPUT \"ИМЯ\",A¤\n"
        "20 PRINT LEN(A¤);A¤\n";
    if (!run_text(src, screen, error, "ПЕТРОВ, И.\r")) {
        std::printf("  %s\n", error.c_str());
        CHECK(false);
        return;
    }
    // Единственный символьный приёмник получает строку целиком, запятая в
    // ней — обычный символ.
    CHECK(screen.find(" 10 ПЕТРОВ, И.") != std::string::npos);
}

// --- Оттранслированная форма ------------------------------------------------

// Собрать файл в оттранслированном виде. Проверять символьные операции
// только на текстовой записи нельзя: у токенов своя кодировка — первая
// запятая STR( не пишется, а «массив или скаляр» по таблицам неразрешимо.
class TokenBuilder
{
public:
    TokenBuilder() {}

    // Дескриптор переменной: флаг таблиц 2/3 и, если нужен, запись таблицы 1.
    void add_string_var(unsigned elements, unsigned elem_len)
    {
        flags_.push_back(0x21);                 // символьная + есть дескриптор
        t1_.push_back(0x00); t1_.push_back(0x00);          // адрес
        t1_.push_back(0x00); t1_.push_back(0x08);          // одномерная
        t1_.push_back(elements & 0xFF); t1_.push_back(elements >> 8);
        const unsigned code = elem_len * 2 + 1;
        t1_.push_back(code & 0xFF); t1_.push_back(code >> 8);
    }

    void add_numeric_var() { flags_.push_back(0x10); }      // действительная, без дескриптора

    void add_line(unsigned number, const std::vector<uint8_t> & body)
    {
        if (!lines_.empty()) lines_.push_back(0xFE);
        lines_.push_back(static_cast<uint8_t>(((number / 1000) % 10) * 16 + (number / 100) % 10));
        lines_.push_back(static_cast<uint8_t>(((number / 10) % 10) * 16 + number % 10));
        lines_.push_back(static_cast<uint8_t>(body.size() + 1));
        lines_.insert(lines_.end(), body.begin(), body.end());
    }

    std::vector<uint8_t> file() const
    {
        // Записи таблиц 2/3 идут в порядке убывания индекса переменной.
        std::vector<uint8_t> stream;
        const unsigned L1 = static_cast<unsigned>(t1_.size());
        const unsigned L2 = static_cast<unsigned>(flags_.size()) * 4;

        stream.push_back(L1 >> 8); stream.push_back(L1 & 0xFF);
        stream.push_back(L2 >> 8); stream.push_back(L2 & 0xFF);
        stream.push_back(0); stream.push_back(0);
        // Записи таблицы 1 идут в том же порядке убывания индекса переменной,
        // что и записи таблиц 2/3, то есть первой — запись последней
        // объявленной переменной. При одной переменной это незаметно, при
        // нескольких — разъезжается.
        for (std::size_t r = t1_.size(); r >= 8; r -= 8)
            stream.insert(stream.end(), t1_.begin() + (r - 8), t1_.begin() + r);
        for (unsigned i = flags_.size(); i-- > 0; ) {
            stream.push_back(0); stream.push_back(0);
            stream.push_back(flags_[i]);
            stream.push_back(0);
        }
        stream.insert(stream.end(), lines_.begin(), lines_.end());

        // Заголовочный сектор, затем секторы по 254 байта данных.
        std::vector<uint8_t> file(256, 0);
        file[0] = 1;
        file[9] = 0x21;                          // оттранслированная программа
        for (std::size_t p = 0; p < stream.size(); p += 254) {
            file.push_back(0x00);
            file.push_back(0x80);
            for (unsigned i = 0; i < 254; ++i)
                file.push_back(p + i < stream.size() ? stream[p + i] : 0);
        }
        return file;
    }

private:
    std::vector<uint8_t> t1_;
    std::vector<uint8_t> flags_;
    std::vector<uint8_t> lines_;
};

std::vector<uint8_t> bytes(const int * v, unsigned n)
{
    std::vector<uint8_t> r;
    for (unsigned i = 0; i < n; ++i) r.push_back(static_cast<uint8_t>(v[i]));
    return r;
}

// A¤(3)4 : A¤(2)="БВ" : PRINT STR(A¤(2),1,2)
//
// Ключевое здесь — то, чего нет в текстовой записи: у индекса массива нет
// открывающей скобки, а первая запятая STR( не кодируется вовсе.
void test_tokenized_strings()
{
    TokenBuilder b;
    b.add_string_var(3, 4);                      // переменная 0: A¤(3)4

    // 46 01 00                     DIM A¤
    static const int dim[] = { 0x46, 0x01, 0x00 };
    b.add_line(10, bytes(dim, 3));

    // 36 09 00 E8 02 D0 D9 E3 02 E2 F7
    //         └ A¤ (2)      =   "БВ" (Б и В в КОИ-8)
    static const int let[] = { 0x36, 0x09, 0x00, 0xE8, 0x02, 0xD0, 0xD9,
                               0xE3, 0x02, 0xE2, 0xF7 };
    b.add_line(20, bytes(let, 11));

    // 4C 09 E1 00 E8 02 D0 E8 01 DE E8 02 D0
    //       └ STR( A¤ (2)   ,нет 1   ,  2   )
    static const int pr[] = { 0x4C, 0x0B, 0xE1, 0x00, 0xE8, 0x02, 0xD0,
                              0xE8, 0x01, 0xDE, 0xE8, 0x02, 0xD0 };
    b.add_line(30, bytes(pr, 13));

    const std::vector<uint8_t> file = b.file();

    ProgramImage img;
    std::string error;
    if (!img.load_file(file, error)) {
        std::printf("  разбор: %s\n", error.c_str());
        CHECK(false);
        return;
    }
    const std::vector<VarInfo> & vars = img.vars();
    CHECK_EQ(vars.size(), 1u);
    if (!vars.empty()) {
        CHECK(vars[0].is_string);
        CHECK(vars[0].is_array);
        CHECK_EQ(vars[0].dim1, 3u);
        CHECK_EQ(vars[0].str_len, 4u);
    }
    CHECK_EQ(img.line_count(), 3u);

    std::string screen;
    if (!run_program(img, 0, screen, error)) {
        std::printf("  исполнение: %s\n", error.c_str());
        CHECK(false);
        return;
    }
    CHECK_STR(line_of(screen, 1), "БВ");
}

// PRINT VAL(X¤);VAL(X¤,2) при X¤=HEX(0200)
//
// Проверяется то, чего в текстовой записи нет: второй аргумент VAL( — это
// пара DE DB, где сам DB и означает двойку. Форма взята из VICT 2250
// (EF 11 DE DB = VAL(Y2¤,2)); в EDITOR 1315 она же с вложенным STR(.
void test_tokenized_val()
{
    TokenBuilder b;
    b.add_string_var(1, 2);                      // переменная 0: X¤ в два байта

    // 36 06 00 D9 E2 02 02 00      X¤=HEX(0200)
    static const int let[] = { 0x36, 0x06, 0x00, 0xD9, 0xE2, 0x02, 0x02, 0x00 };
    b.add_line(10, bytes(let, 8));

    // 4C 07 EF 00 DD EF 00 DE DB   PRINT VAL(X¤);VAL(X¤,2)
    //       └ VAL( X¤  ;  VAL( X¤  ,  2      — закрывающих скобок нет
    static const int pr[] = { 0x4C, 0x07, 0xEF, 0x00, 0xDD, 0xEF, 0x00,
                              0xDE, 0xDB };
    b.add_line(20, bytes(pr, 9));

    ProgramImage img;
    std::string error;
    if (!img.load_file(b.file(), error)) {
        std::printf("  разбор: %s\n", error.c_str());
        CHECK(false);
        return;
    }
    CHECK_EQ(img.line_count(), 2u);

    std::string screen;
    if (!run_program(img, 0, screen, error)) {
        std::printf("  исполнение: %s\n", error.c_str());
        CHECK(false);
        return;
    }
    CHECK_STR(line_of(screen, 1), " 2  512");

    // Та же программа текстом должна дать тот же экран.
    std::string text_screen;
    if (!run_text("10 X¤=HEX(0200)\n20 PRINT VAL(X¤);VAL(X¤,2)\n",
                  text_screen, error)) {
        std::printf("  %s\n", error.c_str());
        CHECK(false);
        return;
    }
    CHECK(screen == text_screen);
}

// BIN(A¤)=J : BIN(STR(B¤,1),2)=J
//
// Главное здесь — приёмник без «,2»: за ним сразу идёт индекс переменной
// (EDITOR 3650 = 4B 02 22 3B), и заглядывание вперёд приняло бы значение за
// список индексов. Массив это или скаляр, решается только по таблицам.
void test_tokenized_bin()
{
    TokenBuilder b;
    b.add_string_var(1, 2);                      // переменная 0: A¤ — скаляр в 2 байта
    b.add_numeric_var();                         // переменная 1: J
    b.add_string_var(1, 4);                      // переменная 2: B¤ — скаляр в 4 байта

    // 36 04 01 D9 E8 47            J=47
    static const int let[] = { 0x36, 0x04, 0x01, 0xD9, 0xE8, 0x47 };
    b.add_line(10, bytes(let, 6));

    // 36 06 00 D9 E2 02 00 00      A¤=HEX(0000)
    static const int zero[] = { 0x36, 0x06, 0x00, 0xD9, 0xE2, 0x02, 0x00, 0x00 };
    b.add_line(20, bytes(zero, 8));

    // 4B 02 00 01                  BIN(A¤)=J     — ни скобки, ни знака равенства
    static const int bin1[] = { 0x4B, 0x02, 0x00, 0x01 };
    b.add_line(30, bytes(bin1, 4));

    // 4C 04 EF 00 DE DB            PRINT VAL(A¤,2)
    static const int pr1[] = { 0x4C, 0x04, 0xEF, 0x00, 0xDE, 0xDB };
    b.add_line(40, bytes(pr1, 6));

    // 36 06 02 D9 E2 02 00 00      B¤=HEX(0000)  — пишем только первые два байта
    static const int zero2[] = { 0x36, 0x06, 0x02, 0xD9, 0xE2, 0x02, 0x00, 0x00 };
    b.add_line(50, bytes(zero2, 8));

    // 4B 08 E1 02 E8 01 D0 DE DB 01   BIN(STR(B¤,1),2)=J   — форма EDITOR 1222
    static const int bin2[] = { 0x4B, 0x08, 0xE1, 0x02, 0xE8, 0x01, 0xD0,
                                0xDE, 0xDB, 0x01 };
    b.add_line(60, bytes(bin2, 10));

    // 4C 04 EF 02 DE DB            PRINT VAL(B¤,2)
    static const int pr2[] = { 0x4C, 0x04, 0xEF, 0x02, 0xDE, 0xDB };
    b.add_line(70, bytes(pr2, 6));

    ProgramImage img;
    std::string error;
    if (!img.load_file(b.file(), error)) {
        std::printf("  разбор: %s\n", error.c_str());
        CHECK(false);
        return;
    }
    CHECK_EQ(img.line_count(), 7u);

    std::string screen;
    if (!run_program(img, 0, screen, error)) {
        std::printf("  исполнение: %s\n", error.c_str());
        CHECK(false);
        return;
    }
    CHECK_STR(line_of(screen, 1), " 12032");     // 2F00 — один байт
    CHECK_STR(line_of(screen, 2), " 47");        // 002F — два байта

    // Та же программа текстом должна дать тот же экран.
    const char * src =
        "10 J=47\n"
        "20 DIM A¤2,B¤4\n"
        "25 A¤=HEX(0000)\n"
        "30 BIN(A¤)=J\n"
        "40 PRINT VAL(A¤,2)\n"
        "50 B¤=HEX(0000)\n"
        "60 BIN(STR(B¤,1),2)=J\n"
        "70 PRINT VAL(B¤,2)\n";
    std::string text_screen;
    if (!run_text(src, text_screen, error)) { std::printf("  %s\n", error.c_str()); CHECK(false); return; }
    CHECK(screen == text_screen);
}

// INIT(2E)A¤,B¤ : INIT(".")C¤()
//
// В потоке запятых между приёмниками нет вовсе, поэтому два скалярных
// приёмника подряд — это снова случай, где заглядывание вперёд не работает.
// Формы взяты из EDITOR: 64 03 DE 2E 0E = INIT(2E)H¤, 64 05 DE 00 E0 09 E0 0A
// = INIT(00)T¤(),L¤().
void test_tokenized_init()
{
    TokenBuilder b;
    b.add_string_var(1, 2);                      // переменная 0: A¤ — скаляр
    b.add_string_var(1, 2);                      // переменная 1: B¤ — скаляр
    b.add_string_var(3, 2);                      // переменная 2: C¤(3)2 — массив

    // 64 04 DE 2E 00 01        INIT(2E)A¤,B¤   — значение сырым байтом
    static const int i1[] = { 0x64, 0x04, 0xDE, 0x2E, 0x00, 0x01 };
    b.add_line(10, bytes(i1, 6));

    // 4C 03 00 DD 01           PRINT A¤;B¤
    static const int p1[] = { 0x4C, 0x03, 0x00, 0xDD, 0x01 };
    b.add_line(20, bytes(p1, 5));

    // 64 05 E3 01 2E E0 02     INIT(".")C¤()   — значение литералом
    static const int i2[] = { 0x64, 0x05, 0xE3, 0x01, 0x2E, 0xE0, 0x02 };
    b.add_line(30, bytes(i2, 7));

    // 4C 09 E1 E0 02 E8 01 DE E8 06 D0   PRINT STR(C¤(),1,6)
    static const int p2[] = { 0x4C, 0x09, 0xE1, 0xE0, 0x02, 0xE8, 0x01,
                              0xDE, 0xE8, 0x06, 0xD0 };
    b.add_line(40, bytes(p2, 11));

    ProgramImage img;
    std::string error;
    if (!img.load_file(b.file(), error)) {
        std::printf("  разбор: %s\n", error.c_str());
        CHECK(false);
        return;
    }
    CHECK_EQ(img.line_count(), 4u);

    std::string screen;
    if (!run_program(img, 0, screen, error)) {
        std::printf("  исполнение: %s\n", error.c_str());
        CHECK(false);
        return;
    }
    CHECK_STR(line_of(screen, 1), "....");
    CHECK_STR(line_of(screen, 2), "......");

    // Та же программа текстом должна дать тот же экран.
    const char * src =
        "10 DIM A¤2,B¤2,C¤(3)2\n"
        "20 INIT(2E)A¤,B¤\n"
        "30 PRINT A¤;B¤\n"
        "40 INIT(\".\")C¤()\n"
        "50 PRINT STR(C¤(),1,6)\n";
    std::string text_screen;
    if (!run_text(src, text_screen, error)) { std::printf("  %s\n", error.c_str()); CHECK(false); return; }
    CHECK(screen == text_screen);
}

} // namespace

int main()
{
    test_length_and_truncation();
    test_substring();
    test_substring_assignment();
    test_len();
    test_implicit_inside();
    test_num();
    test_val();
    test_pos();
    test_comparison();
    test_string_array();
    test_input();
    test_bin();
    test_bin_limits();
    test_init();
    test_init_errors();
    test_tokenized_strings();
    test_tokenized_val();
    test_tokenized_bin();
    test_tokenized_init();
    return test::summary("символьные переменные и STR(");
}