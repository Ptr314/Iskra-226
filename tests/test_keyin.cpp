// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Mikhail Revzin <p3.141592653589793238462643@gmail.com>
// Part of the Iskra-226 project: https://github.com/Ptr314/Iskra-226
// Description: KEYIN — неблокирующий опрос клавиатуры

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

// Очередь нажатий задаётся снаружи: обычные клавиши и клавиши специальных
// функций различаются признаком, а не кодом.
struct Key {
    Key(uint8_t c, bool s) : code(c), special(s) {}
    uint8_t code;
    bool special;
};

bool run_text(const char * utf8, const std::vector<Key> & keys,
              std::string & screen, std::string & error)
{
    std::string koi8;
    utf8_to_koi8(utf8, koi8);

    NameTable names;
    ProgramImage img;
    if (!tokenize(koi8, img, names, error)) return false;

    HeadlessHost host;
    for (std::size_t i = 0; i < keys.size(); ++i) {
        if (keys[i].special) host.feed_special_key(keys[i].code);
        else host.feed_keys(&keys[i].code, 1);
    }

    Interp interp(img, host);
    interp.set_max_steps(20000);
    const bool ok = interp.run(error);
    screen = host.dump();
    return ok;
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

// Программа-опросчик: считает опросы, разводит два вида клавиш.
const char * POLLER =
    "10 DIM A\xC2\xA4""2\n"
    "20 I=0\n"
    "30 I=I+1:KEYIN A\xC2\xA4,100,200:IF I<5THEN30\n"
    "40 PRINT \"NET \";I:STOP\n"
    "100 PRINT \"OB \";VAL(A\xC2\xA4):GOTO 30\n"
    "200 PRINT \"SF \";VAL(A\xC2\xA4):GOTO 30\n";

// «Оператор KEYIN в данной книге не рассматривается» (руководство,
// разд. 18.1), поэтому всё ниже выведено из корпуса.

// Клавиша не нажата — исполнение идёт следующим оператором, а не ждёт.
// Так устроен пустой цикл ожидания `EDITOR` 244: `KEYIN A¤,246,246:GOTO 244`
// и опрос внутри цикла `EDITOR` 3312: `FOR I=1TO100:KEYIN A¤,3314,3500`.
void test_no_key()
{
    std::string screen, error;
    std::vector<Key> none;
    if (!run_text(POLLER, none, screen, error)) { std::printf("  %s\n", error.c_str()); CHECK(false); return; }
    CHECK_STR(line_of(screen, 1), "NET  5");
}

// Обычная клавиша: код в приёмник, управление на первую строку.
void test_ordinary_key()
{
    std::string screen, error;
    std::vector<Key> keys;
    keys.push_back(Key('A', false));
    keys.push_back(Key('B', false));
    if (!run_text(POLLER, keys, screen, error)) { std::printf("  %s\n", error.c_str()); CHECK(false); return; }
    CHECK_STR(line_of(screen, 1), "OB  65");
    CHECK_STR(line_of(screen, 2), "OB  66");
    CHECK_STR(line_of(screen, 3), "NET  5");
}

// Клавиша специальных функций: тот же приёмник, но вторая строка.
// Разные номера у двух видов видны в `EDITOR` 2505 (`KEYIN B¤,2510,2700`)
// и 6310 (`KEYIN A¤,6312,6315`).
void test_special_key()
{
    std::string screen, error;
    std::vector<Key> keys;
    keys.push_back(Key(3, true));
    keys.push_back(Key('A', false));
    if (!run_text(POLLER, keys, screen, error)) { std::printf("  %s\n", error.c_str()); CHECK(false); return; }
    CHECK_STR(line_of(screen, 1), "SF  3");
    CHECK_STR(line_of(screen, 2), "OB  65");
}

// Приём `EDITOR` 246: код клавиши сравнивается и как маленькое число
// (`ON VAL(A¤)+1 GOSUB …`), и как символ. Байт кладётся в первый байт поля,
// остальное добивается пробелами, поэтому сравнение с `HEX(..)` сходится.
void test_code_in_field()
{
    std::string screen, error;
    std::vector<Key> keys;
    keys.push_back(Key(0x9A, false));
    const char * src =
        "10 DIM A\xC2\xA4""4\n"
        "20 KEYIN A\xC2\xA4,100,100\n"
        "30 PRINT \"NET\":STOP\n"
        "100 IF A\xC2\xA4=HEX(9A)THEN120\n"
        "110 PRINT \"NE SOVPALO\":STOP\n"
        "120 PRINT \"SOVPALO \";VAL(A\xC2\xA4)\n";
    if (!run_text(src, keys, screen, error)) { std::printf("  %s\n", error.c_str()); CHECK(false); return; }
    CHECK_STR(line_of(screen, 1), "SOVPALO  154");
}

// В потоке `KEYIN` — глагол 25: приёмник и два номера строк сырыми парами
// BCD, без разделителей (`SCOPE` 4828 = `25 05 04 30 50 30 50`).
void test_tokenized()
{
    std::string koi8, error;
    utf8_to_koi8("10 KEYIN A\xC2\xA4,3050,3050\n", koi8);

    NameTable names;
    ProgramImage img;
    if (!tokenize(koi8, img, names, error)) { std::printf("  %s\n", error.c_str()); CHECK(false); return; }
    CHECK_EQ(img.line_count(), 1u);

    const std::vector<uint8_t> & b = img.line(0).body;
    CHECK_EQ(b.size(), 7u);
    CHECK_EQ(b[0], 0x25u);
    CHECK_EQ(b[1], 0x05u);
    CHECK_EQ(b[2], 0x00u);                 // приёмник — первая переменная
    CHECK_EQ(b[3], 0x30u); CHECK_EQ(b[4], 0x50u);
    CHECK_EQ(b[5], 0x30u); CHECK_EQ(b[6], 0x50u);
}

} // namespace

int main()
{
    test_no_key();
    test_ordinary_key();
    test_special_key();
    test_code_in_field();
    test_tokenized();
    return test::summary("KEYIN");
}
