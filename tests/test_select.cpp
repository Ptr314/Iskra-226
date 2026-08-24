// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Mikhail Revzin <p3.141592653589793238462643@gmail.com>
// Part of the Iskra-226 project: https://github.com/Ptr314/Iskra-226
// Description: оператор SELECT и таблица устройств

#include <cstdio>
#include <string>
#include <vector>

#include "check.h"
#include "core/devtable.h"
#include "core/front_text.h"
#include "core/front_tokens.h"
#include "core/interp.h"
#include "core/koi8.h"
#include "host_headless/headless_host.h"

using namespace iskra;

namespace {

// Прогон текстовой программы; таблица устройств возвращается наружу.
bool run_text(const char * utf8, DeviceTable & out, std::string & error)
{
    std::string koi8;
    utf8_to_koi8(utf8, koi8);

    Program prog;
    NameTable names;
    if (!parse_text(koi8, prog, names, error)) return false;

    HeadlessHost host;
    Interp interp(prog, host);
    if (!interp.run(error)) return false;
    out = interp.devices();
    return true;
}

// --- Сборка оттранслированного файла (тот же приём, что в test_deffn) ------

class TokenBuilder
{
public:
    void add_line(unsigned number, const int * body, unsigned n)
    {
        if (!lines_.empty()) lines_.push_back(0xFE);
        lines_.push_back(static_cast<uint8_t>(((number / 1000) % 10) * 16
                                              + (number / 100) % 10));
        lines_.push_back(static_cast<uint8_t>(((number / 10) % 10) * 16 + number % 10));
        lines_.push_back(static_cast<uint8_t>(n + 1));
        for (unsigned i = 0; i < n; ++i) lines_.push_back(static_cast<uint8_t>(body[i]));
    }

    std::vector<uint8_t> file() const
    {
        std::vector<uint8_t> stream(6, 0);          // все три таблицы пусты
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
    std::vector<uint8_t> lines_;
};

bool run_tokens(const TokenBuilder & b, DeviceTable & out, std::string & error)
{
    Program prog;
    if (!parse_tokenized(b.file(), prog, error)) return false;

    HeadlessHost host;
    Interp interp(prog, host);
    if (!interp.run(error)) return false;
    out = interp.devices();
    return true;
}

bool failed(const char * utf8, std::string & error)
{
    DeviceTable dev;
    return !run_text(utf8, dev, error);
}

// --- Проверки --------------------------------------------------------------

void test_defaults()
{
    // Значения из примера вывода LIST% в книге, разд. 11.5.
    DeviceTable d;
    CHECK_EQ(static_cast<unsigned>(d.addr(DG_CI)), 0x01u);
    CHECK_EQ(static_cast<unsigned>(d.addr(DG_CO)), 0x05u);
    CHECK_EQ(d.width(DG_CO), 80u);
    CHECK_EQ(static_cast<unsigned>(d.addr(DG_PRINT)), 0x05u);
    CHECK_EQ(d.width(DG_PRINT), 80u);
    CHECK_EQ(static_cast<unsigned>(d.addr(DG_LIST)), 0x05u);
    CHECK_EQ(static_cast<unsigned>(d.addr(DG_TAPE)), 0x08u);
    CHECK_EQ(d.width(DG_TAPE), 256u);
    CHECK_EQ(static_cast<unsigned>(d.addr(DG_PLOT)), 0x10u);
    CHECK_EQ(d.pause(), 0u);
    CHECK_EQ(static_cast<unsigned>(d.angle()), static_cast<unsigned>(ANG_RAD));

    // «Автоматически для файла с номером 0 определен дисковод 18F».
    CHECK_EQ(static_cast<unsigned>(d.row(0).addr), 0x18u);
    CHECK(!d.row(0).removable);
    for (unsigned i = 1; i < DeviceTable::ROWS; ++i)
        CHECK_EQ(static_cast<unsigned>(d.row(i).addr), 0u);
}

void test_text_forms()
{
    DeviceTable d;
    std::string err;

    // Формы взяты из корпуса: пробелов между словом и адресом нет.
    if (!run_text("10 SELECT PRINT0C(130)\n", d, err)) {
        std::printf("  %s\n", err.c_str()); CHECK(false); return;
    }
    CHECK_EQ(static_cast<unsigned>(d.addr(DG_PRINT)), 0x0Cu);
    CHECK_EQ(d.width(DG_PRINT), 130u);

    // Без скобок ширина не меняется.
    CHECK(run_text("10 SELECT PRINT0C\n", d, err));
    CHECK_EQ(static_cast<unsigned>(d.addr(DG_PRINT)), 0x0Cu);
    CHECK_EQ(d.width(DG_PRINT), 80u);

    CHECK(run_text("10 SELECT DISK18R\n", d, err));
    CHECK_EQ(static_cast<unsigned>(d.row(0).addr), 0x18u);
    CHECK(d.row(0).removable);

    CHECK(run_text("10 SELECT DISK1CF\n", d, err));
    CHECK_EQ(static_cast<unsigned>(d.row(0).addr), 0x1Cu);
    CHECK(!d.row(0).removable);

    // Перечисление: SELECT #118F,#218R,#31CF,#41CR — встречается в корпусе.
    CHECK(run_text("10 SELECT #118F,#218R,#31CF,#41CR\n", d, err));
    CHECK_EQ(static_cast<unsigned>(d.row(1).addr), 0x18u); CHECK(!d.row(1).removable);
    CHECK_EQ(static_cast<unsigned>(d.row(2).addr), 0x18u); CHECK(d.row(2).removable);
    CHECK_EQ(static_cast<unsigned>(d.row(3).addr), 0x1Cu); CHECK(!d.row(3).removable);
    CHECK_EQ(static_cast<unsigned>(d.row(4).addr), 0x1Cu); CHECK(d.row(4).removable);

    // Без буквы дисковода: SELECT #21C.
    CHECK(run_text("10 SELECT #21C\n", d, err));
    CHECK_EQ(static_cast<unsigned>(d.row(2).addr), 0x1Cu);

    CHECK(run_text("10 SELECT P1\n", d, err));
    CHECK_EQ(d.pause(), 1u);
    // «SELECT P без параметра» снимает паузу (руководство, разд. 11.4).
    CHECK(run_text("10 SELECT P1\n20 SELECT P\n", d, err));
    CHECK_EQ(d.pause(), 0u);

    // Единицы измерения углов (разд. 4.6). Тригонометрии пока нет, но
    // режим уже хранится.
    CHECK(run_text("10 SELECT D\n", d, err));
    CHECK_EQ(static_cast<unsigned>(d.angle()), static_cast<unsigned>(ANG_DEG));
    CHECK(run_text("10 SELECT G\n", d, err));
    CHECK_EQ(static_cast<unsigned>(d.angle()), static_cast<unsigned>(ANG_GRAD));
    CHECK(run_text("10 SELECT D\n20 SELECT R\n", d, err));
    CHECK_EQ(static_cast<unsigned>(d.angle()), static_cast<unsigned>(ANG_RAD));

    // Книга пишет со пробелами: SELECT DISK 1CF, PRINT 0C.
    CHECK(run_text("10 SELECT DISK 1CF, PRINT 0C\n", d, err));
    CHECK_EQ(static_cast<unsigned>(d.row(0).addr), 0x1Cu);
    CHECK_EQ(static_cast<unsigned>(d.addr(DG_PRINT)), 0x0Cu);

    CHECK(run_text("10 SELECT LIST0C(132)\n", d, err));
    CHECK_EQ(static_cast<unsigned>(d.addr(DG_LIST)), 0x0Cu);
    CHECK_EQ(d.width(DG_LIST), 132u);

    CHECK(run_text("10 SELECT TAPE27\n", d, err));
    CHECK_EQ(static_cast<unsigned>(d.addr(DG_TAPE)), 0x27u);
}

// Ключевое: обе формы должны дать одну и ту же таблицу.
void test_tokens_match_text()
{
    // 54 05 | 07 0C EB 00 82 — SELECT PRINT0C(130). Ширина после EB —
    // двоичное 16-битное BE (docs/format.md, разд. 4).
    TokenBuilder b;
    static const int l10[] = { 0x54, 0x05, 0x07, 0x0C, 0xEB, 0x00, 0x82 };
    b.add_line(10, l10, 7);
    // 54 03 | 0A 18 01 — SELECT DISK18R
    static const int l20[] = { 0x54, 0x03, 0x0A, 0x18, 0x01 };
    b.add_line(20, l20, 5);
    // 54 04 | 00 01 18 00 — SELECT #118F
    static const int l30[] = { 0x54, 0x04, 0x00, 0x01, 0x18, 0x00 };
    b.add_line(30, l30, 6);
    // 54 02 | 05 01 — SELECT P1
    static const int l40[] = { 0x54, 0x02, 0x05, 0x01 };
    b.add_line(40, l40, 4);
    static const int l50[] = { 0x42, 0x00 };                       // STOP
    b.add_line(50, l50, 2);

    DeviceTable tok;
    std::string err;
    if (!run_tokens(b, tok, err)) { std::printf("  %s\n", err.c_str()); CHECK(false); return; }

    DeviceTable txt;
    const char * src =
        "10 SELECT PRINT0C(130)\n"
        "20 SELECT DISK18R\n"
        "30 SELECT #118F\n"
        "40 SELECT P1\n"
        "50 STOP\n";
    if (!run_text(src, txt, err)) { std::printf("  %s\n", err.c_str()); CHECK(false); return; }

    CHECK_EQ(static_cast<unsigned>(tok.addr(DG_PRINT)),
             static_cast<unsigned>(txt.addr(DG_PRINT)));
    CHECK_EQ(tok.width(DG_PRINT), txt.width(DG_PRINT));
    CHECK_EQ(static_cast<unsigned>(tok.row(0).addr), static_cast<unsigned>(txt.row(0).addr));
    CHECK_EQ(tok.row(0).removable ? 1u : 0u, txt.row(0).removable ? 1u : 0u);
    CHECK_EQ(static_cast<unsigned>(tok.row(1).addr), static_cast<unsigned>(txt.row(1).addr));
    CHECK_EQ(tok.pause(), txt.pause());

    // И значения те, что задавались.
    CHECK_EQ(static_cast<unsigned>(tok.addr(DG_PRINT)), 0x0Cu);
    CHECK_EQ(tok.width(DG_PRINT), 130u);
    CHECK_EQ(static_cast<unsigned>(tok.row(0).addr), 0x18u);
    CHECK(tok.row(0).removable);
    CHECK_EQ(static_cast<unsigned>(tok.row(1).addr), 0x18u);
    CHECK(!tok.row(1).removable);
    CHECK_EQ(tok.pause(), 1u);
}

// Перечисление через DE в одном операторе.
void test_tokens_list()
{
    TokenBuilder b;
    // 54 09 | 00 01 18 00 DE 00 02 1C 01 — SELECT #118F,#21CR
    static const int l10[] = { 0x54, 0x09, 0x00, 0x01, 0x18, 0x00,
                               0xDE, 0x00, 0x02, 0x1C, 0x01 };
    b.add_line(10, l10, 11);
    static const int l20[] = { 0x42, 0x00 };
    b.add_line(20, l20, 2);

    DeviceTable d;
    std::string err;
    if (!run_tokens(b, d, err)) { std::printf("  %s\n", err.c_str()); CHECK(false); return; }
    CHECK_EQ(static_cast<unsigned>(d.row(1).addr), 0x18u);
    CHECK(!d.row(1).removable);
    CHECK_EQ(static_cast<unsigned>(d.row(2).addr), 0x1Cu);
    CHECK(d.row(2).removable);
}

// Неопознанный код группы разбирается, но не исполняется: догадка тут
// была бы хуже отказа. Код 01 встречается в корпусе только в УДАВ.
void test_unknown_group()
{
    TokenBuilder b;
    static const int l10[] = { 0x54, 0x01, 0x01 };                 // SELECT <?>
    b.add_line(10, l10, 3);

    Program prog;
    std::string err;
    CHECK(parse_tokenized(b.file(), prog, err));                   // разбор проходит
    CHECK_EQ(prog.lines.size(), 1u);
    CHECK_EQ(prog.lines[0].stmts.size(), 1u);
    CHECK_EQ(static_cast<unsigned>(prog.lines[0].stmts[0].kind),
             static_cast<unsigned>(ST_SELECT));

    HeadlessHost host;
    Interp interp(prog, host);
    CHECK(!interp.run(err));                                       // а исполнение — нет
}

void test_errors()
{
    std::string err;
    CHECK(failed("10 SELECT\n", err));
    CHECK(failed("10 SELECT PRINT\n", err));           // нет адреса
    CHECK(failed("10 SELECT #\n", err));               // нет номера строки
    CHECK(failed("10 SELECT #9 18F\n", err));          // строк только восемь
    CHECK(failed("10 SELECT PRINT0C(80\n", err));      // не закрыта скобка
    CHECK(failed("10 SELECT XYZ05\n", err));           // группы такой нет
}

} // namespace

int main()
{
    test_defaults();
    test_text_forms();
    test_tokens_match_text();
    test_tokens_list();
    test_unknown_group();
    test_errors();
    return test::summary("оператор SELECT");
}