// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Mikhail Revzin <p3.141592653589793238462643@gmail.com>
// Part of the Iskra-226 project: https://github.com/Ptr314/Iskra-226
// Description: операторы задания констант READ, DATA и RESTORE (разд. 4.9)

#include <cstdio>
#include <string>
#include <vector>

#include "check.h"
#include "core/interp.h"
#include "core/keys.h"
#include "core/koi8.h"
#include "core/names.h"
#include "core/tokenize.h"
#include "host_headless/headless_host.h"

using namespace iskra;

namespace {

bool run_program(ProgramImage & img, const char * input, std::string & screen,
                 std::string & error, std::string * code = 0)
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
    const bool ok = interp.run(error);
    screen = host.dump();
    if (code) *code = interp.error_code();
    return ok;
}

bool run_text(const char * utf8_source, std::string & screen, std::string & error,
              const char * input = 0, std::string * code = 0)
{
    std::string koi8;
    utf8_to_koi8(utf8_source, koi8);

    NameTable names;
    ProgramImage img;
    if (!tokenize(koi8, img, names, error)) return false;
    return run_program(img, input, screen, error, code);
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

// --- READ и DATA ------------------------------------------------------------

// «Пара операторов DATA … READ A,B,X¤,Y¤ равносильна последовательности
// A=1986 : B=1990 : X¤="ПЛАН" : Y¤="ОТЧЕТ"» (руководство, разд. 4.9).
void test_read_basic()
{
    std::string screen, error;
    const char * src =
        "10 DATA 1986,1990,\"PLAN\",\"OTCHET\"\n"
        "20 READ A,B,X\xC2\xA4,Y\xC2\xA4\n"
        "30 PRINT A;B;X\xC2\xA4;Y\xC2\xA4\n";
    if (!run_text(src, screen, error)) { std::printf("  %s\n", error.c_str()); CHECK(false); return; }
    // Символьная переменная — поле постоянной длины, шестнадцать байт по
    // умолчанию, поэтому «PLAN» печатается с дополнением до неё.
    CHECK_STR(line_of(screen, 1), " 1986  1990 PLAN            OTCHET");
}

// «Константы могут перечисляться в нескольких операторах DATA, расположенных
// в произвольных местах программы», а «переменные могут перечисляться в
// нескольких операторах READ» — важен только порядок.
void test_split_statements()
{
    std::string screen, error;
    const char * src =
        "10 READ A\n"
        "20 READ B,C\n"
        "30 PRINT A;B;C\n"
        "40 END\n"
        "50 DATA 1\n"
        "60 DATA 2,3\n";
    if (!run_text(src, screen, error)) { std::printf("  %s\n", error.c_str()); CHECK(false); return; }
    CHECK_STR(line_of(screen, 1), " 1  2  3");
}

// Считывание переходит из одного оператора DATA в следующий само:
// в `EDITOR` 4865 `RESTORE 1,4850` и семнадцать чтений подряд разбирают
// девять значений строки 4850 и восемь значений строки 4855.
void test_spill_across_statements()
{
    std::string screen, error;
    const char * src =
        "10 RESTORE 1,100\n"
        "20 FOR I=1 TO 5\n"
        "30 READ V:PRINT V;\n"
        "40 NEXT I\n"
        "50 END\n"
        "100 DATA 11,12\n"
        "110 DATA 13,14,15\n";
    if (!run_text(src, screen, error)) { std::printf("  %s\n", error.c_str()); CHECK(false); return; }
    CHECK_STR(line_of(screen, 1), " 11  12  13  14  15");
}

// Значения в потоке идут вплотную, без разделителей, поэтому берётся ровно
// один операнд: полное выражение прочитало бы `E7` следующей константы как
// `AND` (docs/format.md, «Хвост оператора DATA»).
void test_adjacent_constants()
{
    std::string screen, error;
    const char * src =
        "10 DATA 31,334,0,28,-7,.5\n"
        "20 READ A,B,C,D,E,F\n"
        "30 PRINT A;B;C;D;E;F\n";
    if (!run_text(src, screen, error)) { std::printf("  %s\n", error.c_str()); CHECK(false); return; }
    CHECK_STR(line_of(screen, 1), " 31  334  0  28 -7  .5");
}

// «В операторе DATA допускается также указывать имена переменных, значения
// которых играют роль соответствующих констант».
void test_variables_in_data()
{
    std::string screen, error;
    const char * src =
        "10 N=99:E=7\n"
        "20 RESTORE 1,100\n"
        "30 READ Q,R\n"
        "40 PRINT Q;R\n"
        "50 END\n"
        "100 DATA E,N\n";
    if (!run_text(src, screen, error)) { std::printf("  %s\n", error.c_str()); CHECK(false); return; }
    CHECK_STR(line_of(screen, 1), " 7  99");
}

// --- RESTORE ----------------------------------------------------------------

// Все четыре формы разд. 4.9 на одном наборе данных.
void test_restore_forms()
{
    std::string screen, error;
    const char * src =
        "10 DATA 1,2,3\n"
        "20 DATA 4,5,6\n"
        "30 READ A,B,C,D,E,F:PRINT A;B;C;D;E;F\n"
        "40 RESTORE ,20:READ G:PRINT G\n"
        "50 RESTORE 2,10:READ H:PRINT H\n"
        "60 RESTORE 5:READ I:PRINT I\n"
        "70 RESTORE :READ J:PRINT J\n";
    if (!run_text(src, screen, error)) { std::printf("  %s\n", error.c_str()); CHECK(false); return; }
    CHECK_STR(line_of(screen, 1), " 1  2  3  4  5  6");
    // «отсчёт начинается с первой константы DATA указанной строки»
    CHECK_STR(line_of(screen, 2), " 4");
    // «RESTORE 4,120 — указатель установится на четвёртой константе»
    CHECK_STR(line_of(screen, 3), " 2");
    // Без номера строки отсчёт идёт по всем операторам DATA программы.
    CHECK_STR(line_of(screen, 4), " 5");
    // «RESTORE без параметров устанавливает указатель на первую константу
    // первого оператора DATA в программе».
    CHECK_STR(line_of(screen, 5), " 1");
}

// Пример 4.22: перевод номера месяца в наименование.
//
// **Книга сама себе противоречит.** Текст разд. 4.9 говорит, что указатель
// встаёт на N-ю константу («RESTORE 4,120 — на четвёртой»), а печатная
// выдача примера 4.22 на ввод 11 показывает ДЕКАБРЬ, то есть двенадцатую.
// Корпус решает спор в пользу текста: `EDITOR` 4865 делает `RESTORE 1,4850`
// и читает семнадцать значений, а в строках 4850 и 4855 их ровно 9 + 8 —
// значит, `RESTORE 1` встаёт на первое значение, а не пропускает его.
void test_book_4_22()
{
    std::string screen, error;
    const char * src =
        "20 INPUT \"NOMER\",M\n"
        "30 RESTORE M,60\n"
        "40 READ M\xC2\xA4\n"
        "50 PRINT M\xC2\xA4\n"
        "55 END\n"
        "60 DATA \"JAN\",\"FEB\",\"MAR\",\"APR\",\"MAJ\",\"IJUN\"\n";
    if (!run_text(src, screen, error, "1\r")) { std::printf("  %s\n", error.c_str()); CHECK(false); return; }
    CHECK_STR(line_of(screen, 2), "JAN");

    if (!run_text(src, screen, error, "6\r")) { std::printf("  %s\n", error.c_str()); CHECK(false); return; }
    CHECK_STR(line_of(screen, 2), "IJUN");
}

void test_restore_bad_line()
{
    // Строка есть, а оператора DATA в ней нет.
    std::string screen, error;
    CHECK(!run_text("10 RESTORE ,20\n20 REM NET DATA\n", screen, error));
    CHECK(error.find("DATA") != std::string::npos);
}

// --- конец данных -----------------------------------------------------------

// «При попытке считывания 13-й пары данных система выдаст сообщение об
// ошибке (ERR 27), поскольку в операторах DATA нет больше констант»
// (пример 4.21).
void test_out_of_data()
{
    std::string screen, error, code;
    const char * src =
        "10 DATA 1,2\n"
        "20 READ A,B,C\n";
    CHECK(!run_text(src, screen, error, 0, &code));
    CHECK_STR(code, "27");

    // Это ошибка машины, и ON ERROR её ловит.
    const char * src2 =
        "10 DIM E$4,N$4\n"
        "20 ON ERROR E$,N$GOTO 100\n"
        "30 DATA 1\n"
        "40 READ A,B\n"
        "50 STOP\n"
        "100 PRINT E$;\"/\";N$\n";
    if (!run_text(src2, screen, error)) { std::printf("  %s\n", error.c_str()); CHECK(false); return; }
    CHECK_STR(line_of(screen, 1), "27  /0040");
}

// Тип значения и тип приёмника обязаны совпадать.
void test_type_mismatch()
{
    std::string screen, error;
    CHECK(!run_text("10 DATA \"TEKST\"\n20 READ A\n", screen, error));
    CHECK(!run_text("10 DATA 5\n20 READ A\xC2\xA4\n", screen, error));
}

// RUN без номера строки возвращает указатель на начало — как и CLEAR.
void test_run_resets_pointer()
{
    std::string koi8, error, screen;
    utf8_to_koi8("10 DATA 1,2\n20 READ A:PRINT A\n", koi8);

    NameTable names;
    ProgramImage img;
    if (!tokenize(koi8, img, names, error)) { std::printf("  %s\n", error.c_str()); CHECK(false); return; }

    HeadlessHost host;
    Interp interp(img, host);
    CHECK(interp.run(error));
    CHECK(interp.run(error));
    CHECK_STR(line_of(host.dump(), 1), " 1");
    CHECK_STR(line_of(host.dump(), 2), " 1");
}

// --- оттранслированная форма ------------------------------------------------

// У токенов своя кодировка: значения DATA идут вплотную, два последних байта
// операндов — указатель цепочки, приёмники READ тоже без разделителей, а
// запятая RESTORE — байт DE перед двухбайтовым BCD номером строки.
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
    b.add_numeric_var();                     // 0: A
    b.add_numeric_var();                     // 1: B

    // 51 03 | DE 00 60 — RESTORE ,60. Запятая в начале операндов — сырой DE:
    // в позиции операнда он значил бы однобайтовый литерал (VICT 2190).
    static const int l10[] = { 0x51, 0x03, 0xDE, 0x00, 0x60 };
    b.add_line(10, l10, 5);

    // 44 02 | 00 01 — READ A,B: приёмники вплотную, без разделителей.
    static const int l20[] = { 0x44, 0x02, 0x00, 0x01 };
    b.add_line(20, l20, 4);

    // PRINT A;B
    static const int l30[] = { 0x4C, 0x03, 0x00, 0xDD, 0x01 };
    b.add_line(30, l30, 5);

    static const int l40[] = { 0x59, 0x00 };                       // END
    b.add_line(40, l40, 2);

    // 29 06 | E8 31 E7 03 34 | 00 00 — DATA 31,334 и пустой хвост цепочки.
    static const int l60[] = { 0x29, 0x07, 0xE8, 0x31, 0xE7, 0x03, 0x34, 0x00, 0x00 };
    b.add_line(60, l60, 9);

    ProgramImage img;
    std::string error;
    if (!img.load_file(b.file(), error)) {
        std::printf("  разбор: %s\n", error.c_str());
        CHECK(false);
        return;
    }
    CHECK_EQ(img.line_count(), 5u);

    std::string screen;
    if (!run_program(img, 0, screen, error)) {
        std::printf("  исполнение: %s\n", error.c_str());
        CHECK(false);
        return;
    }
    CHECK_STR(line_of(screen, 1), " 31  334");
}

} // namespace

int main()
{
    test_read_basic();
    test_split_statements();
    test_spill_across_statements();
    test_adjacent_constants();
    test_variables_in_data();
    test_restore_forms();
    test_book_4_22();
    test_restore_bad_line();
    test_out_of_data();
    test_type_mismatch();
    test_run_resets_pointer();
    test_tokenized();
    return test::summary("READ, DATA и RESTORE");
}
