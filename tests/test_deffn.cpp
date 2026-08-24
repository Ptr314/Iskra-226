// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Mikhail Revzin <p3.141592653589793238462643@gmail.com>
// Part of the Iskra-226 project: https://github.com/Ptr314/Iskra-226
// Description: помеченные подпрограммы DEFFN' и GOSUB'

#include <cstdio>
#include <string>
#include <vector>

#include "check.h"
#include "core/names.h"
#include "core/tokenize.h"
#include "core/interp.h"
#include "core/koi8.h"
#include "host_headless/headless_host.h"

using namespace iskra;

namespace {

bool run_program(const ProgramImage & img, std::string & screen, std::string & error)
{
    HeadlessHost host;
    Interp interp(img, host);
    if (!interp.run(error)) return false;
    screen = host.dump();
    return true;
}

bool run_text(const char * utf8_source, std::string & screen, std::string & error)
{
    std::string koi8;
    utf8_to_koi8(utf8_source, koi8);

    NameTable names;
    // Текст исполняется не сам по себе: он сначала транслируется в токены,
    // как и в машине (docs/DECISIONS.md, разд. 12).
    ProgramImage img;
    if (!tokenize(koi8, img, names, error)) return false;
    return run_program(img, screen, error);
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

// --- Текстовая форма --------------------------------------------------------

void test_call_without_parameters()
{
    // «Возврат из помеченной подпрограммы осуществляется так же, как и из
    // непомеченных» (руководство, разд. 10.4).
    const char * src =
        "10 GOSUB '5\n"
        "20 PRINT \"POSLE\"\n"
        "30 STOP\n"
        "100 DEFFN '5\n"
        "110 PRINT \"VNUTRI\"\n"
        "120 RETURN\n";

    std::string screen, error;
    if (!run_text(src, screen, error)) { std::printf("  %s\n", error.c_str()); CHECK(false); return; }
    CHECK_STR(line_of(screen, 1), "VNUTRI");
    CHECK_STR(line_of(screen, 2), "POSLE");
}

void test_numeric_parameters()
{
    // Пример из разд. 10.4: остаток от деления, параметры передаются
    // в самом операторе перехода.
    const char * src =
        "10 GOSUB '50(110,33)\n"
        "20 PRINT M\n"
        "30 STOP\n"
        "300 DEFFN '50(A,B)\n"
        "320 M=A-INT(A/B)*B\n"
        "330 RETURN\n";

    std::string screen, error;
    if (!run_text(src, screen, error)) { std::printf("  %s\n", error.c_str()); CHECK(false); return; }
    CHECK_STR(line_of(screen, 1), " 11");
}

void test_expression_parameters()
{
    // «В качестве фактических параметров могут использоваться
    // арифметические выражения».
    const char * src =
        "10 E=100:C=11\n"
        "20 GOSUB '50(E+10,C*3)\n"
        "30 PRINT M\n"
        "40 STOP\n"
        "300 DEFFN '50(A,B)\n"
        "320 M=A-INT(A/B)*B\n"
        "330 RETURN\n";

    std::string screen, error;
    if (!run_text(src, screen, error)) { std::printf("  %s\n", error.c_str()); CHECK(false); return; }
    CHECK_STR(line_of(screen, 1), " 11");
}

void test_two_entry_points()
{
    // Пример 10.6: два помеченных входа в одну подпрограмму и один RETURN.
    // Встреченный по ходу исполнения DEFFN' «не влияет на ход выполнения».
    const char * src =
        "10 GOSUB '100\n"
        "20 GOSUB '101\n"
        "30 PRINT N;S1\n"
        "40 STOP\n"
        "1010 DEFFN '100\n"
        "1030 S1,N=0\n"
        "1050 DEFFN '101:S=5\n"
        "1070 N=N+1\n"
        "1080 S1=S1+S:RETURN\n";

    std::string screen, error;
    if (!run_text(src, screen, error)) { std::printf("  %s\n", error.c_str()); CHECK(false); return; }
    CHECK_STR(line_of(screen, 1), " 2  10");
}

void test_string_parameter()
{
    // Пример 10.7: первый параметр символьный, второй числовой.
    const char * src =
        "10 GOSUB '161(\"728\",3)\n"
        "20 PRINT STR(A¤,1,3);K\n"
        "30 STOP\n"
        "1010 DEFFN '161(A¤,K)\n"
        "1020 RETURN\n";

    std::string screen, error;
    if (!run_text(src, screen, error)) { std::printf("  %s\n", error.c_str()); CHECK(false); return; }
    CHECK_STR(line_of(screen, 1), "728 3");
}

void test_parameters_evaluated_before_assignment()
{
    // Формальный параметр подпрограммы — обычная глобальная переменная, и
    // корпус зовёт подпрограммы через их же параметры: EDITOR 6855 —
    // GOSUB '100(L3,A%,1) при DEFFN '100(L1,L4,L3). Там порядок ничего не
    // меняет, но если формальный стоит не первым фактическим — меняет.
    //
    // ДОПУЩЕНИЕ: принято, что все фактические вычисляются до первого
    // присваивания. Ни в книге, ни в корпусе различающего примера нет.
    const char * src =
        "10 L1=9\n"
        "20 GOSUB '100(5,L1)\n"
        "30 PRINT L1;L4\n"
        "40 STOP\n"
        "100 DEFFN '100(L1,L4)\n"
        "110 RETURN\n";

    std::string screen, error;
    if (!run_text(src, screen, error)) { std::printf("  %s\n", error.c_str()); CHECK(false); return; }
    // При присваивании по одному вышло бы « 5  5»: L4 получил бы уже
    // затёртое значение L1.
    CHECK_STR(line_of(screen, 1), " 5  9");
}

void test_nested_calls()
{
    const char * src =
        "10 GOSUB '1\n"
        "20 PRINT A;B\n"
        "30 STOP\n"
        "100 DEFFN '1\n"
        "110 A=1:GOSUB '2\n"
        "120 A=A+B:RETURN\n"
        "200 DEFFN '2\n"
        "210 B=10:RETURN\n";

    std::string screen, error;
    if (!run_text(src, screen, error)) { std::printf("  %s\n", error.c_str()); CHECK(false); return; }
    CHECK_STR(line_of(screen, 1), " 11  10");
}

void test_first_definition_wins()
{
    // Машина ищет DEFFN' просмотром текста программы сверху вниз.
    const char * src =
        "10 GOSUB '7\n"
        "20 STOP\n"
        "100 DEFFN '7\n"
        "110 PRINT \"PERVYJ\":RETURN\n"
        "200 DEFFN '7\n"
        "210 PRINT \"VTOROJ\":RETURN\n";

    std::string screen, error;
    if (!run_text(src, screen, error)) { std::printf("  %s\n", error.c_str()); CHECK(false); return; }
    CHECK_STR(line_of(screen, 1), "PERVYJ");
}

void test_errors()
{
    std::string screen, error;

    CHECK(!run_text("10 GOSUB '9\n", screen, error));
    CHECK(error.find("нет подпрограммы") != std::string::npos);

    // «Количество фактических параметров должно соответствовать количеству
    // формальных».
    error.clear();
    CHECK(!run_text("10 GOSUB '50\n100 DEFFN '50(A,B)\n110 RETURN\n", screen, error));
    CHECK(error.find("описано") != std::string::npos);

    // «Типы соответствующих фактических и формальных параметров должны быть
    // одинаковыми»: GOSUB '161(100,3) при DEFFN '161(A¤,K) — ошибка.
    error.clear();
    CHECK(!run_text("10 GOSUB '161(100,3)\n100 DEFFN '161(A¤,K)\n110 RETURN\n",
                    screen, error));
    CHECK(error.find("типы") != std::string::npos);

    // DEFFN без апострофа — функция пользователя, её пока нет.
    error.clear();
    CHECK(!run_text("10 DEFFN A(X)=X+1\n", screen, error));
}

void test_key_text_is_not_a_subroutine()
{
    // DEFFN' с текстом определяет клавишу специальных функций: на такую
    // метку GOSUB' не переходит.
    std::string screen, error;
    const char * src =
        "10 DEFFN '15 \"LIST\"\n"
        "20 PRINT \"DALSHE\"\n";
    if (!run_text(src, screen, error)) { std::printf("  %s\n", error.c_str()); CHECK(false); return; }
    CHECK_STR(line_of(screen, 1), "DALSHE");

    error.clear();
    CHECK(!run_text("10 DEFFN '15 \"LIST\"\n20 GOSUB '15\n", screen, error));
    CHECK(error.find("нет подпрограммы") != std::string::npos);

    // Текст клавиши бывает и шестнадцатеричным: SCOPE 1 — три подряд
    // определения вида DEFFN '31 HEX(0D).
    error.clear();
    if (!run_text("10 DEFFN '31 HEX(0D)\n20 PRINT \"DALSHE\"\n", screen, error)) {
        std::printf("  %s\n", error.c_str());
        CHECK(false);
        return;
    }
    CHECK_STR(line_of(screen, 1), "DALSHE");
}

// --- Оттранслированная форма -----------------------------------------------

// У токенов своя кодировка, текстовыми примерами её не проверить: метка —
// двоичный байт, скобка списка параметров не пишется вовсе, а у DEFFN' за
// меткой идут четыре байта адреса возврата.
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

        stream.push_back(0); stream.push_back(0);              // таблица 1 пуста
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
    b.add_numeric_var();                         // переменная 00: S
    b.add_numeric_var();                         // переменная 01: N

    // 23 06 | 64 E8 03 DE E8 04 — GOSUB '100(3,4). Метка 64 — двоичная,
    // а не BCD; скобки списка нет, параметры разделены DE.
    static const int l10[] = { 0x23, 0x06, 0x64, 0xE8, 0x03, 0xDE, 0xE8, 0x04 };
    b.add_line(10, l10, 8);

    static const int l20[] = { 0x4C, 0x01, 0x00 };                     // PRINT S
    b.add_line(20, l20, 3);

    static const int l30[] = { 0x42, 0x00 };                           // STOP
    b.add_line(30, l30, 2);

    // 27 07 | 64 00 00 00 00 00 01 — DEFFN '100(S,N). Четыре нуля после
    // метки — адрес возврата, машина заполняет его при первом вызове;
    // формальные параметры идут подряд, без разделителей.
    static const int l40[] = { 0x27, 0x07, 0x64, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01 };
    b.add_line(40, l40, 9);

    // 36 05 | 00 D9 00 DF 01 : 5E 00 — S=S*N:RETURN
    static const int l50[] = { 0x36, 0x05, 0x00, 0xD9, 0x00, 0xDF, 0x01,
                               0x5E, 0x00 };
    b.add_line(50, l50, 9);

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
    CHECK_STR(line_of(screen, 1), " 12");

    // Та же программа текстом должна дать тот же экран.
    const char * src =
        "10 GOSUB '100(3,4)\n"
        "20 PRINT S\n"
        "30 STOP\n"
        "40 DEFFN '100(S,N)\n"
        "50 S=S*N:RETURN\n";
    std::string text_screen;
    if (!run_text(src, text_screen, error)) { std::printf("  %s\n", error.c_str()); CHECK(false); return; }
    CHECK(screen == text_screen);
}

void test_tokenized_key_text()
{
    // 3A 0C | 0C 18 00 7E 04 E3 05 'LIST ' — определение клавиши
    // специальных функций (CHANAL 10). Разбирается, но подпрограммой не
    // становится.
    TokenBuilder b;

    static const int l10[] = { 0x3A, 0x0C, 0x0C, 0x18, 0x00, 0x7E, 0x04,
                               0xE3, 0x05, 0x4C, 0x49, 0x53, 0x54, 0x20 };
    b.add_line(10, l10, 14);

    // 3A 08 | 1F 00 00 A4 03 E2 01 0D — то же, но текст задан HEX(0D):
    // так записана первая строка SCOPE.
    static const int l15[] = { 0x3A, 0x08, 0x1F, 0x00, 0x00, 0xA4, 0x03,
                               0xE2, 0x01, 0x0D };
    b.add_line(15, l15, 10);

    static const int l20[] = { 0x4C, 0x07, 0xE3, 0x05, 0x44, 0x41, 0x4C, 0x53, 0x48 };
    b.add_line(20, l20, 9);

    ProgramImage img;
    std::string error;
    if (!img.load_file(b.file(), error)) {
        std::printf("  разбор: %s\n", error.c_str());
        CHECK(false);
        return;
    }
    CHECK_EQ(img.line_count(), 3u);
    // Глагол 3A — определение клавиши; подпрограммой оно не становится,
    // и исполнение просто идёт мимо него.
    CHECK_EQ(static_cast<unsigned>(img.line(0).body[0]), 0x3Au);
    CHECK_EQ(static_cast<unsigned>(img.line(1).body[0]), 0x3Au);

    std::string screen;
    if (!run_program(img, screen, error)) {
        std::printf("  исполнение: %s\n", error.c_str());
        CHECK(false);
        return;
    }
    CHECK_STR(line_of(screen, 1), "DALSH");
}

} // namespace

int main()
{
    test_call_without_parameters();
    test_numeric_parameters();
    test_expression_parameters();
    test_two_entry_points();
    test_string_parameter();
    test_parameters_evaluated_before_assignment();
    test_nested_calls();
    test_first_definition_wins();
    test_errors();
    test_key_text_is_not_a_subroutine();
    test_tokenized();
    test_tokenized_key_text();
    return test::summary("помеченные подпрограммы");
}