// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Mikhail Revzin <p3.141592653589793238462643@gmail.com>
// Part of the Iskra-226 project: https://github.com/Ptr314/Iskra-226
// Description: символьные переменные, STR( и функции над строками

#include <cstdio>
#include <string>
#include <vector>

#include "check.h"
#include "core/front_text.h"
#include "core/front_tokens.h"
#include "core/interp.h"
#include "core/koi8.h"
#include "host_headless/headless_host.h"

using namespace iskra;

namespace {

bool run_program(const Program & prog, const char * input, std::string & screen,
                 std::string & error)
{
    HeadlessHost host;
    if (input) {
        std::string keys;
        utf8_to_koi8(input, keys);
        host.feed_keys(reinterpret_cast<const uint8_t *>(keys.data()),
                       static_cast<unsigned>(keys.size()));
    }
    Interp interp(prog, host);
    if (!interp.run(error)) return false;
    screen = host.dump();
    return true;
}

bool run_text(const char * utf8_source, std::string & screen, std::string & error,
              const char * input = 0)
{
    std::string koi8;
    utf8_to_koi8(utf8_source, koi8);

    Program prog;
    NameTable names;
    if (!parse_text(koi8, prog, names, error)) return false;
    return run_program(prog, input, screen, error);
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
        stream.insert(stream.end(), t1_.begin(), t1_.end());
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

    std::vector<VarInfo> vars;
    std::string error;
    if (!parse_tokenized_vars(file, vars, error)) {
        std::printf("  таблицы: %s\n", error.c_str());
        CHECK(false);
        return;
    }
    CHECK_EQ(vars.size(), 1u);
    if (!vars.empty()) {
        CHECK(vars[0].is_string);
        CHECK(vars[0].is_array);
        CHECK_EQ(vars[0].dim1, 3u);
        CHECK_EQ(vars[0].str_len, 4u);
    }

    Program prog;
    if (!parse_tokenized(file, prog, error)) {
        std::printf("  разбор: %s\n", error.c_str());
        CHECK(false);
        return;
    }
    CHECK_EQ(prog.lines.size(), 3u);

    std::string screen;
    if (!run_program(prog, 0, screen, error)) {
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

    Program prog;
    std::string error;
    if (!parse_tokenized(b.file(), prog, error)) {
        std::printf("  разбор: %s\n", error.c_str());
        CHECK(false);
        return;
    }
    CHECK_EQ(prog.lines.size(), 2u);

    std::string screen;
    if (!run_program(prog, 0, screen, error)) {
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

} // namespace

int main()
{
    test_length_and_truncation();
    test_substring();
    test_substring_assignment();
    test_len();
    test_num();
    test_val();
    test_pos();
    test_comparison();
    test_string_array();
    test_input();
    test_tokenized_strings();
    test_tokenized_val();
    return test::summary("символьные переменные и STR(");
}