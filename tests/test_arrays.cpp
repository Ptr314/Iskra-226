// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Mikhail Revzin <p3.141592653589793238462643@gmail.com>
// Part of the Iskra-226 project: https://github.com/Ptr314/Iskra-226
// Description: массивы, DIM и таблицы переменных

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

bool read_bytes(const std::string & path, std::string & out)
{
    std::FILE * f = std::fopen(path.c_str(), "rb");
    if (!f) return false;
    char buf[4096];
    std::size_t n;
    while ((n = std::fread(buf, 1, sizeof(buf), f)) > 0) out.append(buf, n);
    std::fclose(f);
    return true;
}

bool load_hex_dump(const std::string & name, std::vector<uint8_t> & out)
{
    std::string text;
    if (!read_bytes(std::string(ISKRA_CORPUS_DIR) + "/" + name, text)) return false;

    std::size_t p = 0;
    while (p < text.size()) {
        std::size_t e = text.find('\n', p);
        if (e == std::string::npos) e = text.size();

        const std::size_t bar = text.find('|', p);
        if (bar != std::string::npos && bar < e) {
            const std::size_t bar2 = text.find('|', bar + 1);
            const std::size_t stop = (bar2 != std::string::npos && bar2 < e) ? bar2 : e;
            unsigned nibbles = 0, value = 0;
            for (std::size_t i = bar + 1; i < stop; ++i) {
                const char c = text[i];
                int v;
                if (c >= '0' && c <= '9') v = c - '0';
                else if (c >= 'A' && c <= 'F') v = c - 'A' + 10;
                else if (c >= 'a' && c <= 'f') v = c - 'a' + 10;
                else { nibbles = 0; value = 0; continue; }
                value = (value << 4) | static_cast<unsigned>(v);
                if (++nibbles == 2) { out.push_back(static_cast<uint8_t>(value));
                                      nibbles = 0; value = 0; }
            }
        }
        p = (e < text.size()) ? e + 1 : e;
    }
    return !out.empty();
}

// Прогон текстовой программы; возвращает экран.
bool run_text(const char * utf8_source, std::string & screen, std::string & error,
              const char * input = 0)
{
    std::string koi8;
    utf8_to_koi8(utf8_source, koi8);

    Program prog;
    NameTable names;
    if (!parse_text(koi8, prog, names, error)) return false;

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

std::string first_line(const std::string & screen)
{
    const std::size_t e = screen.find('\n');
    return screen.substr(0, e);
}

// --- Таблицы переменных настоящих программ --------------------------------

// Размеры массивов в оттранслированной форме нигде, кроме таблиц, не
// записаны: оператор DIM несёт одни индексы переменных. Проверяем чтение
// таблиц по текстовым листингам тех же программ.
void test_var_tables()
{
    struct Expect { unsigned var; unsigned dim1; unsigned dim2; };

    // STAT02, строка 20: DIM R(6),Q(6),K(12) — байты 46 03 00 01 02.
    {
        std::vector<uint8_t> file;
        if (!load_hex_dump("STAT02_bin.txt", file)) {
            std::printf("  не прочитался STAT02_bin.txt\n");
            CHECK(false);
        } else {
            std::vector<VarInfo> vars;
            std::string error;
            CHECK(parse_tokenized_vars(file, vars, error));

            static const Expect E[] = { {0, 6, 0}, {1, 6, 0}, {2, 12, 0} };
            for (unsigned i = 0; i < 3; ++i) {
                if (E[i].var >= vars.size()) { CHECK(false); continue; }
                const VarInfo & v = vars[E[i].var];
                CHECK(v.is_array);
                CHECK_EQ(v.dim1, E[i].dim1);
                CHECK_EQ(v.dim2, E[i].dim2);
            }
        }
    }

    // STAT03, строка 20: DIM X(100),G¤(100)5,Q(5,100),D(6),E(5),A(5,6),
    // закодировано как 46 06 19 1A 1B 03 04 05.
    //
    // Первые три индекса — «теневые»: программа выполнялась, MAT REDIM в
    // строке 200 увеличил X, G¤ и Q со 100 до 300, каждый получил новый
    // дескриптор и новый индекс, и интерпретатор подставил новые индексы
    // прямо в оператор DIM. Код и сам MAT REDIM продолжают работать со
    // старыми — 00, 01, 02 (docs/format.md, разд. 6).
    //
    // Поэтому здесь проверяются обе стороны: у старых индексов размеры до
    // переопределения, у теневых — после. Двумерность при этом сохраняется.
    {
        std::vector<uint8_t> file;
        if (!load_hex_dump("STAT03_bin.txt", file)) {
            std::printf("  не прочитался STAT03_bin.txt\n");
            CHECK(false);
        } else {
            std::vector<VarInfo> vars;
            std::string error;
            CHECK(parse_tokenized_vars(file, vars, error));

            static const Expect E[] = {
                { 0x00, 100, 0 },      // X(100)   — исходный дескриптор
                { 0x19, 300, 0 },      // X        — после MAT REDIM
                { 0x02,   5, 100 },    // Q(5,100) — исходный
                { 0x1B,   5, 300 },    // Q        — после MAT REDIM
                { 0x03,   6, 0 },      // D(6)     — не переопределялся
                { 0x04,   5, 0 },      // E(5)
                { 0x05,   5, 6 }       // A(5,6)
            };
            for (unsigned i = 0; i < sizeof(E) / sizeof(E[0]); ++i) {
                if (E[i].var >= vars.size()) { CHECK(false); continue; }
                const VarInfo & v = vars[E[i].var];
                if (!v.is_array || v.dim1 != E[i].dim1 || v.dim2 != E[i].dim2)
                    std::printf("  переменная %02X: получено (%u,%u), массив=%d\n",
                                E[i].var, v.dim1, v.dim2, v.is_array ? 1 : 0);
                CHECK(v.is_array);
                CHECK_EQ(v.dim1, E[i].dim1);
                CHECK_EQ(v.dim2, E[i].dim2);
            }

            // G¤(100)5 — массив из ста строк по пять байт. Длина элемента
            // берётся из размерного кода: он равен удвоенному размеру
            // элемента, младший бит означает «длина задана явно».
            if (0x1A < vars.size()) {
                CHECK(vars[0x1A].is_string);
                CHECK(vars[0x1A].is_array);
                CHECK_EQ(vars[0x1A].str_len, 5u);
            }
        }
    }
}

// --- Исполнение ------------------------------------------------------------

void test_one_dimensional()
{
    // Индексы с единицы: «А(5) означает 5-й элемент массива А()».
    const char * src =
        "10 DIM A(5)\n"
        "20 FOR I=1 TO 5:A(I)=I*I:NEXT I\n"
        "30 PRINT A(1);A(3);A(5)\n";

    std::string screen, error;
    if (!run_text(src, screen, error)) { std::printf("  %s\n", error.c_str()); CHECK(false); return; }
    CHECK_STR(first_line(screen), " 1  9  25");
}

void test_two_dimensional()
{
    const char * src =
        "10 DIM M(2,3)\n"
        "20 FOR I=1 TO 2:FOR J=1 TO 3:M(I,J)=I*10+J:NEXT J:NEXT I\n"
        "30 PRINT M(1,1);M(1,3);M(2,1);M(2,3)\n";

    std::string screen, error;
    if (!run_text(src, screen, error)) { std::printf("  %s\n", error.c_str()); CHECK(false); return; }
    CHECK_STR(first_line(screen), " 11  13  21  23");
}

void test_expression_index()
{
    // «Если арифметические выражения состоят из целой и дробной частей,
    // используется только их целая часть» (руководство, разд. 7.1).
    const char * src =
        "10 DIM A(10)\n"
        "20 A(4)=42\n"
        "30 X=2\n"
        "40 PRINT A(X*2);A(4.7)\n";

    std::string screen, error;
    if (!run_text(src, screen, error)) { std::printf("  %s\n", error.c_str()); CHECK(false); return; }
    CHECK_STR(first_line(screen), " 42  42");
}

void test_implicit_array()
{
    // Массив без DIM получает размерность по умолчанию — десять элементов.
    const char * src =
        "10 B(10)=7\n"
        "20 PRINT B(10)\n";

    std::string screen, error;
    if (!run_text(src, screen, error)) { std::printf("  %s\n", error.c_str()); CHECK(false); return; }
    CHECK_STR(first_line(screen), " 7");
}

void test_bounds()
{
    std::string screen, error;

    CHECK(!run_text("10 DIM A(5)\n20 A(6)=1\n", screen, error));
    CHECK(error.find("границей") != std::string::npos);

    error.clear();
    CHECK(!run_text("10 DIM A(5)\n20 A(0)=1\n", screen, error));
    CHECK(error.find("меньше единицы") != std::string::npos);
}

void test_multiple_targets()
{
    // Присваивание в несколько целей сразу, в том числе в элементы массива:
    // V01(V0A),V02(V0A)=0 — так пишет STAT00.
    const char * src =
        "10 DIM A(3),B(3)\n"
        "20 I=2\n"
        "30 A(I),B(I),C=5\n"
        "40 PRINT A(2);B(2);C\n";

    std::string screen, error;
    if (!run_text(src, screen, error)) { std::printf("  %s\n", error.c_str()); CHECK(false); return; }
    CHECK_STR(first_line(screen), " 5  5  5");
}

void test_input_into_array()
{
    const char * src =
        "10 DIM A(3)\n"
        "20 INPUT A(2)\n"
        "30 PRINT A(2)\n";

    std::string screen, error;
    if (!run_text(src, screen, error, "17\r")) {
        std::printf("  %s\n", error.c_str());
        CHECK(false);
        return;
    }
    CHECK(screen.find(" 17") != std::string::npos);
}

void test_gosub_and_on()
{
    const char * src =
        "10 DIM A(3)\n"
        "20 FOR I=1 TO 3:GOSUB 100:NEXT I\n"
        "30 ON 2 GOTO 50,60\n"
        "40 PRINT \"NE TUDA\":STOP\n"
        "50 PRINT \"TOZHE NE TUDA\":STOP\n"
        "60 PRINT A(1);A(2);A(3)\n"
        "70 ON 9 GOTO 40,50\n"
        "80 PRINT \"PROSKOCHILO\":STOP\n"
        "100 A(I)=I*3:RETURN\n";

    std::string screen, error;
    if (!run_text(src, screen, error)) { std::printf("  %s\n", error.c_str()); CHECK(false); return; }
    CHECK_STR(first_line(screen), " 3  6  9");
    // Значение вне списка ON передаёт управление следующему оператору.
    CHECK(screen.find("PROSKOCHILO") != std::string::npos);
}

void test_tab()
{
    // «Позиции строки нумеруются с нуля» (разд. 4.4): TAB(10) — одиннадцатая
    // позиция экрана.
    const char * src = "10 PRINT TAB(10);\"X\"\n";

    std::string screen, error;
    if (!run_text(src, screen, error)) { std::printf("  %s\n", error.c_str()); CHECK(false); return; }
    CHECK_STR(first_line(screen), "          X");
}

} // namespace

int main()
{
    test_var_tables();
    test_one_dimensional();
    test_two_dimensional();
    test_expression_index();
    test_implicit_array();
    test_bounds();
    test_multiple_targets();
    test_input_into_array();
    test_gosub_and_on();
    test_tab();
    return test::summary("массивы и DIM");
}