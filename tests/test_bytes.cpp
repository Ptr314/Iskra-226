// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Mikhail Revzin <p3.141592653589793238462643@gmail.com>
// Part of the Iskra-226 project: https://github.com/Ptr314/Iskra-226
// Description: операции над байтами — AND, OR, XOR, BOOL, ADD, ROTATE (гл. 14)

#include <cstdio>
#include <string>
#include <vector>

#include "check.h"
#include "core/interp.h"
#include "core/koi8.h"
#include "core/names.h"
#include "core/tokenize.h"
#include "host_headless/headless_host.h"

using namespace iskra;

namespace {

bool run_program(ProgramImage & img, std::string & screen, std::string & error)
{
    HeadlessHost host;
    Interp interp(img, host);
    if (!interp.run(error)) return false;
    screen = host.dump();
    return true;
}

bool run_text(const char * utf8, std::string & screen, std::string & error)
{
    std::string koi8;
    utf8_to_koi8(utf8, koi8);

    NameTable names;
    ProgramImage img;
    if (!tokenize(koi8, img, names, error)) return false;
    return run_program(img, screen, error);
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

// --- логические операции (разд. 14.3) ---------------------------------------

// Пример из книги: `C¤=HEX(4145)`, `B¤=HEX(2185)`, результат в `A¤`.
//
// **Печатная выдача этого раздела повреждена распознаванием**, и проверять
// её приходится арифметикой самой книги. У `AND` и `XOR` она сходится
// целиком; у `OR` второй байт напечатан как `65`, хотя `45 OR 85` это `C5`
// (первый байт `61` при этом верен). У `OR(A¤,FF)` напечатано `0000` при
// том, что тут же сказано: «значения всех разрядов результата равны
// единице», то есть `FFFF`.
void test_logic_book()
{
    std::string screen, error;
    const char * src =
        "10 DIM A\xC2\xA4""2,B\xC2\xA4""2,C\xC2\xA4""2\n"
        "20 C\xC2\xA4=HEX(4145)\n"
        "30 B\xC2\xA4=HEX(2185)\n"
        "40 A\xC2\xA4=C\xC2\xA4:AND(A\xC2\xA4,B\xC2\xA4):HEXPRINT A\xC2\xA4\n"
        "50 A\xC2\xA4=C\xC2\xA4:OR(A\xC2\xA4,B\xC2\xA4):HEXPRINT A\xC2\xA4\n"
        "60 A\xC2\xA4=C\xC2\xA4:XOR(A\xC2\xA4,B\xC2\xA4):HEXPRINT A\xC2\xA4\n"
        "70 A\xC2\xA4=C\xC2\xA4:AND(A\xC2\xA4,00):HEXPRINT A\xC2\xA4\n"
        "80 A\xC2\xA4=C\xC2\xA4:OR(A\xC2\xA4,FF):HEXPRINT A\xC2\xA4\n"
        "90 A\xC2\xA4=C\xC2\xA4:XOR(A\xC2\xA4,A\xC2\xA4):HEXPRINT A\xC2\xA4\n"
        "100 A\xC2\xA4=HEX(970F):XOR(A\xC2\xA4,FF):HEXPRINT A\xC2\xA4\n";
    if (!run_text(src, screen, error)) { std::printf("  %s\n", error.c_str()); CHECK(false); return; }
    CHECK_STR(line_of(screen, 1), "0105");     // книга: 0105
    CHECK_STR(line_of(screen, 2), "61C5");     // книга: 6165 — второй байт потерян
    CHECK_STR(line_of(screen, 3), "60C0");     // книга: 60C0
    CHECK_STR(line_of(screen, 4), "0000");     // книга: 0000
    CHECK_STR(line_of(screen, 5), "FFFF");     // книга: 0000, но её же пояснение — «все единицы»
    CHECK_STR(line_of(screen, 6), "0000");     // книга: 0000
    CHECK_STR(line_of(screen, 7), "68F0");     // книга: 68F0
}

// Пример 14.5: «Связь Шеффера» — это `BOOL 7`, то есть НЕ-И.
//
// В книге начальное значение напечатано как `HEX(4541)`, но выдача `FEFA`
// получается только при `HEX(4145)`: `~(41 AND 21)` = `FE`,
// `~(45 AND 85)` = `FA`. Цифры в скане переставлены.
void test_bool_book()
{
    std::string screen, error;
    const char * src =
        "10 DIM A\xC2\xA4""2,B\xC2\xA4""2\n"
        "20 A\xC2\xA4=HEX(4145):B\xC2\xA4=HEX(2185)\n"
        "30 BOOL 7(A\xC2\xA4,B\xC2\xA4)\n"
        "40 HEXPRINT A\xC2\xA4\n";
    if (!run_text(src, screen, error)) { std::printf("  %s\n", error.c_str()); CHECK(false); return; }
    CHECK_STR(line_of(screen, 1), "FEFA");
}

// «Операторы BOOL 8 и AND, BOOL Е и OR, BOOL 6 и XOR соответственно
// эквивалентны друг другу» (разд. 14.3).
void test_bool_equivalents()
{
    std::string screen, error;
    const char * src =
        "10 DIM A\xC2\xA4""2,B\xC2\xA4""2,C\xC2\xA4""2\n"
        "20 C\xC2\xA4=HEX(4145):B\xC2\xA4=HEX(2185)\n"
        "30 A\xC2\xA4=C\xC2\xA4:BOOL 8(A\xC2\xA4,B\xC2\xA4):HEXPRINT A\xC2\xA4\n"
        "40 A\xC2\xA4=C\xC2\xA4:BOOL E(A\xC2\xA4,B\xC2\xA4):HEXPRINT A\xC2\xA4\n"
        "50 A\xC2\xA4=C\xC2\xA4:BOOL 6(A\xC2\xA4,B\xC2\xA4):HEXPRINT A\xC2\xA4\n"
        // 0 — «всегда ложно», F — «всегда истинно» (таблица 14.5).
        "60 A\xC2\xA4=C\xC2\xA4:BOOL 0(A\xC2\xA4,B\xC2\xA4):HEXPRINT A\xC2\xA4\n"
        "70 A\xC2\xA4=C\xC2\xA4:BOOL F(A\xC2\xA4,B\xC2\xA4):HEXPRINT A\xC2\xA4\n";
    if (!run_text(src, screen, error)) { std::printf("  %s\n", error.c_str()); CHECK(false); return; }
    CHECK_STR(line_of(screen, 1), "0105");     // как AND
    CHECK_STR(line_of(screen, 2), "61C5");     // как OR
    CHECK_STR(line_of(screen, 3), "60C0");     // как XOR
    CHECK_STR(line_of(screen, 4), "0000");
    CHECK_STR(line_of(screen, 5), "FFFF");
}

// --- двоичное сложение (разд. 14.1) -----------------------------------------

// Пример 14.1: 60 + 100 = 160, то есть HEX(3C) + HEX(64) = HEX(A0).
void test_add_book_1()
{
    std::string screen, error;
    const char * src =
        "10 DIM B0\xC2\xA4""1\n"
        "20 B0\xC2\xA4=HEX(3C)\n"
        "30 ADD(B0\xC2\xA4,64)\n"
        "40 HEXPRINT B0\xC2\xA4\n";
    if (!run_text(src, screen, error)) { std::printf("  %s\n", error.c_str()); CHECK(false); return; }
    CHECK_STR(line_of(screen, 1), "A0");
}

// Пример 14.2: `A¤=HEX(0123)`, четыре формы сложения с кодом байта.
// Все четыре значения книги сходятся целиком.
void test_add_book_2()
{
    std::string screen, error;
    const char * src =
        "10 DIM A\xC2\xA4""2\n"
        "20 A\xC2\xA4=HEX(0123):ADD(A\xC2\xA4,02):HEXPRINT A\xC2\xA4\n"
        "30 A\xC2\xA4=HEX(0123):ADDC(A\xC2\xA4,02):HEXPRINT A\xC2\xA4\n"
        "40 A\xC2\xA4=HEX(0123):ADD(A\xC2\xA4,FF):HEXPRINT A\xC2\xA4\n"
        "50 A\xC2\xA4=HEX(0123):ADDC(A\xC2\xA4,FF):HEXPRINT A\xC2\xA4\n";
    if (!run_text(src, screen, error)) { std::printf("  %s\n", error.c_str()); CHECK(false); return; }
    // «hh складывается с содержимым каждого байта» — переноса нет.
    CHECK_STR(line_of(screen, 1), "0325");
    // «Если параметр С присутствует, hh складывается только с содержимым
    // последнего байта».
    CHECK_STR(line_of(screen, 2), "0125");
    CHECK_STR(line_of(screen, 3), "0022");
    CHECK_STR(line_of(screen, 4), "0222");
}

// Пример 14.3: сложение двух переменных разной длины. «Складываются сначала
// последние байты переменных, потом предпоследние».
//
// Книга печатает `00020F` и `00030F`, но `20 + FF` это `11F`, то есть
// младший байт `1F`. Верхняя половина последнего байта в скане потеряна в
// обеих строках одинаково; остальные пять цифр сходятся.
void test_add_book_3()
{
    std::string screen, error;
    const char * src =
        "10 DIM A\xC2\xA4""3,B\xC2\xA4""2,C\xC2\xA4""3\n"
        "20 B\xC2\xA4=HEX(01FF)\n"
        "30 STR(A\xC2\xA4,1,2)=HEX(0001)\n"
        "40 C\xC2\xA4=A\xC2\xA4:ADD(C\xC2\xA4,B\xC2\xA4):HEXPRINT C\xC2\xA4\n"
        "50 C\xC2\xA4=A\xC2\xA4:ADDC(C\xC2\xA4,B\xC2\xA4):HEXPRINT C\xC2\xA4\n";
    if (!run_text(src, screen, error)) { std::printf("  %s\n", error.c_str()); CHECK(false); return; }
    CHECK_STR(line_of(screen, 1), "00021F");
    CHECK_STR(line_of(screen, 2), "00031F");
}

// --- циклический сдвиг (разд. 14.4) -----------------------------------------

// Пример 14.6 целиком: восемь сдвигов `HEX(C33C)`. Книга печатает семь
// значений — одно `3CC3` распознавание потеряло, у двухбайтовой переменной
// сдвиг на 8 влево и на 8 вправо дают одно и то же.
void test_rotate_book()
{
    std::string screen, error;
    const char * src =
        "10 DIM A\xC2\xA4""2\n"
        "20 A\xC2\xA4=HEX(C33C):ROTATE(A\xC2\xA4,1):HEXPRINT A\xC2\xA4\n"
        "30 A\xC2\xA4=HEX(C33C):ROTATE(A\xC2\xA4,-2):HEXPRINT A\xC2\xA4\n"
        "40 A\xC2\xA4=HEX(C33C):ROTATE C(A\xC2\xA4,1):HEXPRINT A\xC2\xA4\n"
        "50 A\xC2\xA4=HEX(C33C):ROTATE C(A\xC2\xA4,4):HEXPRINT A\xC2\xA4\n"
        "60 A\xC2\xA4=HEX(C33C):ROTATE C(A\xC2\xA4,-4):HEXPRINT A\xC2\xA4\n"
        "70 A\xC2\xA4=HEX(C33C):ROTATE C(A\xC2\xA4,8):HEXPRINT A\xC2\xA4\n"
        "80 A\xC2\xA4=HEX(C33C):ROTATE C(A\xC2\xA4,-8):HEXPRINT A\xC2\xA4\n"
        "90 A\xC2\xA4=HEX(C33C):ROTATE C(A\xC2\xA4,0):HEXPRINT A\xC2\xA4\n";
    if (!run_text(src, screen, error)) { std::printf("  %s\n", error.c_str()); CHECK(false); return; }
    // Без C сдвигается содержимое каждого байта порознь.
    CHECK_STR(line_of(screen, 1), "8778");
    CHECK_STR(line_of(screen, 2), "F00F");
    // С C границы между байтами игнорируются.
    CHECK_STR(line_of(screen, 3), "8679");
    CHECK_STR(line_of(screen, 4), "33CC");
    CHECK_STR(line_of(screen, 5), "CC33");
    CHECK_STR(line_of(screen, 6), "3CC3");
    CHECK_STR(line_of(screen, 7), "3CC3");
    CHECK_STR(line_of(screen, 8), "C33C");
}

// Пример 14.7: деление двоичного содержимого на два — сдвиг вправо и
// обнуление старшего разряда.
void test_rotate_divide()
{
    std::string screen, error;
    const char * src =
        "10 DIM B\xC2\xA4""2\n"
        "20 B\xC2\xA4=HEX(0100)\n"                   // 256
        "30 ROTATE C(B\xC2\xA4,-1)\n"
        "40 AND(STR(B\xC2\xA4,1,1),7F)\n"
        "50 PRINT VAL(B\xC2\xA4,2)\n";
    if (!run_text(src, screen, error)) { std::printf("  %s\n", error.c_str()); CHECK(false); return; }
    CHECK_STR(line_of(screen, 1), " 128");
}

// Пример 14.9: сдвиг всей переменной на байт освобождает позицию под
// вставку символа.
void test_rotate_insert()
{
    std::string screen, error;
    const char * src =
        "10 DIM T\xC2\xA4""8,A\xC2\xA4""1\n"
        "20 T\xC2\xA4=\"ABCDEFG\"\n"
        "30 A\xC2\xA4=\"*\":X=3\n"
        "40 ROTATE C(STR(T\xC2\xA4,X),-8)\n"
        "50 STR(T\xC2\xA4,X,1)=A\xC2\xA4\n"
        "60 PRINT T\xC2\xA4\n";
    if (!run_text(src, screen, error)) { std::printf("  %s\n", error.c_str()); CHECK(false); return; }
    // Хвост «G » уехал вправо, на его место встала звёздочка.
    CHECK_STR(line_of(screen, 1), "AB*CDEFG");
}

// --- второй аргумент --------------------------------------------------------

// «Если параметр С отсутствует, hh складывается с содержимым каждого байта»,
// а вторая переменная берётся «начиная с первого байта». Разделителя между
// аргументами в потоке нет вовсе: `DE hh` — это однобайтовый литерал
// (`AND(B¤,DF)` = `43 03 23 DE DF`, EDITOR 3469), а вторая переменная стоит
// сразу за приёмником (`AND(A¤,B¤)` = `43 02 1E 15`, DISSM 23571).
void test_shorter_second()
{
    std::string screen, error;
    const char * src =
        "10 DIM A\xC2\xA4""4,B\xC2\xA4""2\n"
        "20 A\xC2\xA4=HEX(FFFFFFFF):B\xC2\xA4=HEX(0F0F)\n"
        "30 AND(A\xC2\xA4,B\xC2\xA4):HEXPRINT A\xC2\xA4\n";
    if (!run_text(src, screen, error)) { std::printf("  %s\n", error.c_str()); CHECK(false); return; }
    // Хвост приёмника, которому не хватило пары, остаётся как был.
    CHECK_STR(line_of(screen, 1), "0F0FFFFF");
}

// --- оттранслированная форма ------------------------------------------------

class TokenBuilder
{
public:
    void add_string_var(unsigned elements, unsigned elem_len)
    {
        flags_.push_back(0x21);                 // символьная + дескриптор
        t1_.push_back(0x00); t1_.push_back(0x00);
        t1_.push_back(0x00); t1_.push_back(0x08);
        t1_.push_back(elements & 0xFF); t1_.push_back(elements >> 8);
        const unsigned code = elem_len * 2 + 1;
        t1_.push_back(code & 0xFF); t1_.push_back(code >> 8);
    }

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
        const unsigned L1 = static_cast<unsigned>(t1_.size());
        const unsigned L2 = static_cast<unsigned>(flags_.size()) * 4;
        stream.push_back(L1 >> 8); stream.push_back(L1 & 0xFF);
        stream.push_back(L2 >> 8); stream.push_back(L2 & 0xFF);
        stream.push_back(0); stream.push_back(0);
        stream.insert(stream.end(), t1_.begin(), t1_.end());
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
    std::vector<uint8_t> t1_;
    std::vector<uint8_t> flags_;
    std::vector<uint8_t> lines_;
};

void test_tokenized()
{
    TokenBuilder b;
    b.add_string_var(1, 2);                  // 0: A¤ длиной 2
    b.add_string_var(1, 2);                  // 1: B¤ длиной 2

    // A¤=HEX(4145) : B¤=HEX(2185)
    static const int l10[] = { 0x36, 0x06, 0x00, 0xD9, 0xE2, 0x02, 0x41, 0x45,
                               0x36, 0x06, 0x01, 0xD9, 0xE2, 0x02, 0x21, 0x85 };
    b.add_line(10, l10, 16);

    // 43 02 | 00 01 — AND(A¤,B¤): разделителя между аргументами нет.
    static const int l20[] = { 0x43, 0x02, 0x00, 0x01 };
    b.add_line(20, l20, 4);

    static const int l30[] = { 0x50, 0x01, 0x00 };                 // HEXPRINT A¤
    b.add_line(30, l30, 3);

    // 43 03 | 00 DE 0F — AND(A¤,0F): `DE hh` это однобайтовый литерал.
    static const int l40[] = { 0x43, 0x03, 0x00, 0xDE, 0x0F };
    b.add_line(40, l40, 5);

    static const int l50[] = { 0x50, 0x01, 0x00 };
    b.add_line(50, l50, 3);

    ProgramImage img;
    std::string error;
    if (!img.load_file(b.file(), error)) {
        std::printf("  разбор: %s\n", error.c_str());
        CHECK(false);
        return;
    }
    CHECK_EQ(img.line_count(), 5u);

    std::string screen;
    if (!run_program(img, screen, error)) {
        std::printf("  исполнение: %s\n", error.c_str());
        CHECK(false);
        return;
    }
    CHECK_STR(line_of(screen, 1), "0105");
    CHECK_STR(line_of(screen, 2), "0105");
}

} // namespace

int main()
{
    test_logic_book();
    test_bool_book();
    test_bool_equivalents();
    test_add_book_1();
    test_add_book_2();
    test_add_book_3();
    test_rotate_book();
    test_rotate_divide();
    test_rotate_insert();
    test_shorter_second();
    test_tokenized();
    return test::summary("операции над байтами");
}
