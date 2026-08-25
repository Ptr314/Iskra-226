// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Mikhail Revzin <p3.141592653589793238462643@gmail.com>
// Part of the Iskra-226 project: https://github.com/Ptr314/Iskra-226
// Description: команды диалога внутри программы — CLEAR, RUN, LIST, RETURN CLEAR

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
    interp.set_max_steps(20000);            // страховка от зацикливания RUN
    const bool ok = interp.run(error);
    screen = host.dump();
    return ok;
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

// --- CLEAR (разд. 8.3) ------------------------------------------------------

// «После выполнения оператора CLEAR V стираются значения всех переменных, и
// в том числе общих переменных, заданных оператором COM. Оператор CLEAR N
// стирает значения только необщих переменных».
void test_clear_vars()
{
    std::string screen, error;
    const char * src =
        "10 COM C\n"
        "20 A=5:C=7\n"
        "30 CLEAR N\n"
        "40 PRINT A;C\n"
        "50 A=5\n"
        "60 CLEAR V\n"
        "70 PRINT A;C\n";
    if (!run_text(src, screen, error)) { std::printf("  %s\n", error.c_str()); CHECK(false); return; }
    // N оставляет общую C нетронутой.
    CHECK_STR(line_of(screen, 1), " 0  7");
    // V стирает и её.
    CHECK_STR(line_of(screen, 2), " 0  0");
}

// «При выполнении оператора CLEAR P из памяти машины стирается только текст
// программы, и никаких других изменений не происходит. Остаются неизменными
// значения переменных».
void test_clear_p()
{
    std::string screen, error;
    const char * src =
        "10 A=5\n"
        "20 CLEAR P 100,200\n"
        "30 PRINT A\n"
        "40 GOTO 300\n"
        "100 PRINT \"NE DOLZHNO BYT\"\n"
        "200 PRINT \"I ETOGO TOZE\"\n"
        "300 PRINT \"KONEC\"\n";
    if (!run_text(src, screen, error)) { std::printf("  %s\n", error.c_str()); CHECK(false); return; }
    // Переменная пережила стирание текста.
    CHECK_STR(line_of(screen, 1), " 5");
    CHECK_STR(line_of(screen, 2), "KONEC");
}

// «CLEAR P без параметров… из памяти стирается только текст программы».
// Стереть можно и строку, из которой стирают, — тогда исполнять больше
// нечего.
void test_clear_p_self()
{
    std::string screen, error;
    const char * src =
        "10 PRINT \"DO\"\n"
        "20 CLEAR P\n"
        "30 PRINT \"POSLE\"\n";
    if (!run_text(src, screen, error)) { std::printf("  %s\n", error.c_str()); CHECK(false); return; }
    CHECK_STR(line_of(screen, 1), "DO");
    CHECK_STR(line_of(screen, 2), "");
}

// Голый CLEAR очищает и экран, и память целиком (разд. 3.2).
void test_clear_all()
{
    std::string screen, error;
    const char * src =
        "10 PRINT \"DO\"\n"
        "20 CLEAR\n"
        "30 PRINT \"POSLE\"\n";
    if (!run_text(src, screen, error)) { std::printf("  %s\n", error.c_str()); CHECK(false); return; }
    CHECK_STR(line_of(screen, 1), "");
}

// --- RUN (разд. 4.1) --------------------------------------------------------

// «RUN с указанием номера строки: переменные сохраняют значения, присвоенные
// им ранее».
void test_run_line()
{
    std::string screen, error;
    const char * src =
        "10 A=A+1\n"
        "20 IF A>2 THEN 40\n"
        "30 RUN 10\n"
        "40 PRINT A\n";
    if (!run_text(src, screen, error)) { std::printf("  %s\n", error.c_str()); CHECK(false); return; }
    CHECK_STR(line_of(screen, 1), " 3");
}

// «RUN без указания номера строки: числовым переменным автоматически
// присваивается значение 0».
void test_run_bare()
{
    std::string screen, error;
    const char * src =
        "10 B=B+1\n"
        "20 IF B=2 THEN 40\n"
        "30 A=1:RUN\n"
        "40 PRINT A;B\n";
    // Первый проход: B=1, дальше RUN обнуляет всё и начинает сначала;
    // второй проход даёт B=1 снова — программа зациклилась бы, если бы RUN
    // переменные не стирал. Ограничение по шагам её и обрывает.
    CHECK(!run_text(src, screen, error));
    CHECK(error.find("шаг") != std::string::npos ||
          error.find("операторов") != std::string::npos);
}

// --- LIST (разд. 3.4) -------------------------------------------------------

// «LIST — устройство вывода для операторов LIST» (разд. 11.5); по умолчанию
// это экран.
void test_list_range()
{
    std::string screen, error;
    const char * src =
        "10 LIST 30,40\n"
        "20 END\n"
        "30 A=1\n"
        "40 B=2\n"
        "50 C=3\n";
    if (!run_text(src, screen, error)) { std::printf("  %s\n", error.c_str()); CHECK(false); return; }
    // Имена в листинге придуманы детокенизатором: в потоке их нет вовсе,
    // и переменные получают A, A0, A1… по порядку индексов.
    CHECK_STR(line_of(screen, 1), "30 A=1");
    CHECK_STR(line_of(screen, 2), "40 A0=2");
    CHECK_STR(line_of(screen, 3), "");
}

// Один номер — одна строка.
void test_list_one()
{
    std::string screen, error;
    const char * src =
        "10 LIST 40\n"
        "20 END\n"
        "30 A=1\n"
        "40 B=2\n";
    if (!run_text(src, screen, error)) { std::printf("  %s\n", error.c_str()); CHECK(false); return; }
    CHECK_STR(line_of(screen, 1), "40 A0=2");
    CHECK_STR(line_of(screen, 2), "");
}

// --- RETURN CLEAR (разд. 10.3) ----------------------------------------------

// «Стирание адреса возврата… переход к оператору, следующему за оператором
// GOSUB, не производится, а выполняется следующий за оператором
// RETURN CLEAR оператор».
void test_return_clear()
{
    std::string screen, error;
    const char * src =
        "10 GOSUB 100\n"
        "20 PRINT \"NE DOLZHNO BYT\":STOP\n"
        "100 PRINT \"V PODPROGRAMME\"\n"
        "110 RETURN CLEAR\n"
        "120 PRINT \"POSLE\"\n";
    if (!run_text(src, screen, error)) { std::printf("  %s\n", error.c_str()); CHECK(false); return; }
    CHECK_STR(line_of(screen, 1), "V PODPROGRAMME");
    CHECK_STR(line_of(screen, 2), "POSLE");
}

// «Список адресов возврата уменьшается на один адрес»: после
// RETURN CLEAR во вложенной подпрограмме очередной RETURN возвращает на
// уровень выше.
void test_return_clear_nested()
{
    std::string screen, error;
    const char * src =
        "10 GOSUB 100\n"
        "20 PRINT \"KONEC\":STOP\n"
        "100 GOSUB 200\n"
        "110 PRINT \"NE DOLZHNO BYT\":STOP\n"
        "200 RETURN CLEAR\n"
        "210 RETURN\n";
    if (!run_text(src, screen, error)) { std::printf("  %s\n", error.c_str()); CHECK(false); return; }
    CHECK_STR(line_of(screen, 1), "KONEC");
}

// `ALL` стирает список целиком.
void test_return_clear_all()
{
    std::string screen, error;
    const char * src =
        "10 GOSUB 100\n"
        "20 PRINT \"NE DOLZHNO BYT\":STOP\n"
        "100 GOSUB 200\n"
        "110 PRINT \"I ETOGO TOZE\":STOP\n"
        "200 RETURN CLEAR ALL\n"
        "210 PRINT \"KONEC\"\n";
    if (!run_text(src, screen, error)) { std::printf("  %s\n", error.c_str()); CHECK(false); return; }
    CHECK_STR(line_of(screen, 1), "KONEC");
}

// --- оттранслированная форма ------------------------------------------------

// Коды видов `CLEAR` взяты из корпуса: `14` это `P` (за ним диапазон строк,
// `DASB2` 790), `11` и `12` — `V` и `N` (`M3` 5150), без операндов — голый
// `CLEAR` (`UDAW` 363). Номера строк — сырые пары BCD, как у GOTO.
void test_tokenized()
{
    std::string koi8, error;
    utf8_to_koi8(
        "10 CLEAR V\n"
        "20 CLEAR N\n"
        "30 CLEAR P 9500,9920\n"
        "40 CLEAR\n"
        "50 LIST /05,9502\n"
        "60 LIST 8103,8104\n"
        "70 RUN 100\n"
        "80 RETURN CLEAR ALL\n", koi8);

    NameTable names;
    ProgramImage img;
    if (!tokenize(koi8, img, names, error)) { std::printf("  %s\n", error.c_str()); CHECK(false); return; }
    CHECK_EQ(img.line_count(), 8u);

    const std::vector<uint8_t> & l10 = img.line(0).body;
    CHECK_EQ(l10[0], 0x2Cu); CHECK_EQ(l10[2], 0x11u);
    const std::vector<uint8_t> & l20 = img.line(1).body;
    CHECK_EQ(l20[2], 0x12u);

    // 2C 06 | 14 95 00 DE 99 20 — ровно то, что в DASB2 790.
    const std::vector<uint8_t> & l30 = img.line(2).body;
    CHECK_EQ(l30.size(), 8u);
    CHECK_EQ(l30[1], 0x06u);
    CHECK_EQ(l30[2], 0x14u);
    CHECK_EQ(l30[3], 0x95u); CHECK_EQ(l30[4], 0x00u);
    CHECK_EQ(l30[5], 0xDEu);
    CHECK_EQ(l30[6], 0x99u); CHECK_EQ(l30[7], 0x20u);

    // Голый CLEAR — без операндов.
    CHECK_EQ(img.line(3).body.size(), 2u);

    // 2E 06 | DC DE 05 DE 95 02 — ровно то, что в DASB2 448.
    const std::vector<uint8_t> & l50 = img.line(4).body;
    CHECK_EQ(l50.size(), 8u);
    CHECK_EQ(l50[0], 0x2Eu);
    CHECK_EQ(l50[2], 0xDCu); CHECK_EQ(l50[3], 0xDEu); CHECK_EQ(l50[4], 0x05u);
    CHECK_EQ(l50[5], 0xDEu);
    CHECK_EQ(l50[6], 0x95u); CHECK_EQ(l50[7], 0x02u);

    // 2E 05 | 81 03 DE 81 04 — как в GRAFISN 1016.
    const std::vector<uint8_t> & l60 = img.line(5).body;
    CHECK_EQ(l60.size(), 7u);
    CHECK_EQ(l60[2], 0x81u); CHECK_EQ(l60[3], 0x03u);
    CHECK_EQ(l60[4], 0xDEu);

    const std::vector<uint8_t> & l70 = img.line(6).body;
    CHECK_EQ(l70[0], 0x2Fu);
    CHECK_EQ(l70[2], 0x01u); CHECK_EQ(l70[3], 0x00u);

    const std::vector<uint8_t> & l80 = img.line(7).body;
    CHECK_EQ(l80[0], 0x30u);
    CHECK_EQ(l80[2], 0xCBu);
}

} // namespace

int main()
{
    test_clear_vars();
    test_clear_p();
    test_clear_p_self();
    test_clear_all();
    test_run_line();
    test_run_bare();
    test_list_range();
    test_list_one();
    test_return_clear();
    test_return_clear_nested();
    test_return_clear_all();
    test_tokenized();
    return test::summary("команды диалога внутри программы");
}
