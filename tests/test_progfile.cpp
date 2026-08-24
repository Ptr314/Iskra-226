// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Mikhail Revzin <p3.141592653589793238462643@gmail.com>
// Part of the Iskra-226 project: https://github.com/Ptr314/Iskra-226
// Description: запись и загрузка программ — SAVE DC и LOAD DC

#include <cstdio>
#include <string>
#include <vector>

#include "check.h"
#include "core/catalog.h"
#include "core/interp.h"
#include "core/koi8.h"
#include "core/names.h"
#include "core/tokenize.h"
#include "host_headless/headless_host.h"

using namespace iskra;

namespace {

const unsigned SEC = Host::SECTOR_SIZE;
const unsigned SECTORS = 200;

// Пустой диск с каталогом: указатель на пять секторов, область до конца.
bool fresh_disk(HeadlessHost & host)
{
    host.mount(0, std::vector<uint8_t>(
        static_cast<std::size_t>(SECTORS) * SEC, 0));
    Catalog cat(host, 0);
    std::string err;
    if (!cat.format(5, SECTORS - 1, err)) {
        std::printf("  %s\n", err.c_str());
        return false;
    }
    return true;
}

bool tokenize_text(const char * utf8, ProgramImage & img, std::string & error)
{
    std::string koi8;
    utf8_to_koi8(utf8, koi8);
    NameTable names;
    return tokenize(koi8, img, names, error);
}

// Положить программу на диск напрямую, минуя SAVE DC. Так проверяется
// загрузка: сохранённая через SAVE DC программа при загрузке снова дойдёт
// до собственного SAVE DC, и второй раз он откажет — это поведение машины,
// а не эмулятора, и в диалоговом режиме его не бывает.
bool put_program(HeadlessHost & host, const char * name, const char * text)
{
    ProgramImage img;
    std::string error;
    if (!tokenize_text(text, img, error)) {
        std::printf("  трансляция: %s\n", error.c_str());
        return false;
    }
    std::vector<uint8_t> file;
    img.save_file(name, file);

    Catalog cat(host, 0);
    uint8_t nm[NAME_LEN];
    Catalog::make_name(name, nm);
    CatalogEntry e;
    std::string err;
    const unsigned need = static_cast<unsigned>(file.size() / SEC);
    if (!cat.create(nm, true, need, e, err)) {
        std::printf("  каталог: %s\n", err.c_str());
        return false;
    }
    for (unsigned i = 0; i < need; ++i)
        if (!host.disk_write(0, e.first + i, &file[i * SEC])) return false;
    return true;
}

bool run(ProgramImage & img, HeadlessHost & host, std::string & screen,
         std::string & error)
{
    Interp interp(img, host);
    interp.set_max_steps(100000);
    if (!interp.run(error)) return false;
    screen = host.dump();
    return true;
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

// «В результате операции в каталог будет включен новый программный файл»
// (руководство, разд. 5.2). Пример 5.1 оттуда же.
void test_save_creates_file()
{
    HeadlessHost host;
    if (!fresh_disk(host)) { CHECK(false); return; }

    ProgramImage img;
    std::string error;
    if (!tokenize_text("10 PRINT \"AB\"\n20 SAVE DC F\"PROG1\"\n", img, error)) {
        std::printf("  трансляция: %s\n", error.c_str());
        CHECK(false);
        return;
    }
    std::string screen;
    if (!run(img, host, screen, error)) { std::printf("  %s\n", error.c_str()); CHECK(false); return; }

    Catalog cat(host, 0);
    uint8_t nm[NAME_LEN];
    Catalog::make_name("PROG1", nm);
    CatalogEntry e;
    std::string err;
    CHECK(cat.find(nm, e, err));
    CHECK(e.alive());
    CHECK(e.is_program());
    CHECK(e.sectors() >= 2u);            // заголовочный сектор и хотя бы один поток
}

// Записали — прочитали: загруженная программа обязана дать тот же экран.
void test_save_then_load()
{
    HeadlessHost host;
    if (!fresh_disk(host)) { CHECK(false); return; }

    ProgramImage saver;
    std::string error;
    if (!tokenize_text("10 A=6\n20 B=7\n30 PRINT A*B\n40 SAVE DC F\"MUL\"\n",
                       saver, error)) {
        std::printf("  трансляция: %s\n", error.c_str());
        CHECK(false);
        return;
    }
    std::string screen;
    if (!run(saver, host, screen, error)) { std::printf("  %s\n", error.c_str()); CHECK(false); return; }
    CHECK_STR(line_of(screen, 1), " 42");

    // Записанный файл читается обратно и даёт ту же программу побайтово.
    Catalog cat(host, 0);
    uint8_t nm[NAME_LEN];
    Catalog::make_name("MUL", nm);
    CatalogEntry e;
    std::string err;
    CHECK(cat.find(nm, e, err));
    if (!e.alive()) { CHECK(false); return; }

    std::vector<uint8_t> file(static_cast<std::size_t>(e.sectors()) * SEC, 0);
    for (unsigned i = 0; i < e.sectors(); ++i)
        CHECK(host.disk_read(0, e.first + i, &file[i * SEC]));

    ProgramImage back;
    if (!back.load_file(file, err)) { std::printf("  %s\n", err.c_str()); CHECK(false); return; }
    CHECK_EQ(back.line_count(), saver.line_count());
    for (unsigned i = 0; i < back.line_count() && i < saver.line_count(); ++i) {
        CHECK_EQ(back.line(i).number, saver.line(i).number);
        CHECK(back.line(i).body == saver.line(i).body);
    }
}

// «Предполагается, что ранее в каталоге файла с таким именем не было, иначе
// записи не произойдет и будет выдано сообщение об ошибке» (разд. 5.2).
void test_save_existing_refused()
{
    HeadlessHost host;
    if (!fresh_disk(host)) { CHECK(false); return; }

    ProgramImage img;
    std::string error;
    if (!tokenize_text("10 SAVE DC F\"ONE\"\n20 SAVE DC F\"ONE\"\n", img, error)) {
        std::printf("  трансляция: %s\n", error.c_str());
        CHECK(false);
        return;
    }
    std::string screen;
    CHECK(!run(img, host, screen, error));
    CHECK(error.find("уже есть") != std::string::npos);
}

// Ошибка машины, а не эмулятора: её ловит ON ERROR и отдаёт код 71.
void test_save_error_is_catchable()
{
    HeadlessHost host;
    if (!fresh_disk(host)) { CHECK(false); return; }

    ProgramImage img;
    std::string error;
    if (!tokenize_text("10 DIM E¤2,L¤4\n"
                       "20 SAVE DC F\"ONE\"\n"
                       "30 ON ERROR E¤,L¤GOTO100\n"
                       "40 SAVE DC F\"ONE\"\n"
                       "50 STOP\n"
                       "100 PRINT E¤\n", img, error)) {
        std::printf("  трансляция: %s\n", error.c_str());
        CHECK(false);
        return;
    }
    std::string screen;
    if (!run(img, host, screen, error)) { std::printf("  %s\n", error.c_str()); CHECK(false); return; }
    CHECK_STR(line_of(screen, 1), "71");
}

// «Для записи программы на место, которое занимает файл, помеченный как
// исключенный» (разд. 5.3): SAVE DC ("старое имя") "новое имя".
void test_save_over_scratched()
{
    HeadlessHost host;
    if (!fresh_disk(host)) { CHECK(false); return; }

    ProgramImage img;
    std::string error;
    if (!tokenize_text("10 SAVE DC F\"OLD\"\n"
                       "20 SCRATCH F\"OLD\"\n"
                       "30 SAVE DC F(\"OLD\")\"NEW\"\n", img, error)) {
        std::printf("  трансляция: %s\n", error.c_str());
        CHECK(false);
        return;
    }
    std::string screen;
    if (!run(img, host, screen, error)) { std::printf("  %s\n", error.c_str()); CHECK(false); return; }

    Catalog cat(host, 0);
    uint8_t nm[NAME_LEN];
    CatalogEntry e;
    std::string err;
    Catalog::make_name("NEW", nm);
    CHECK(cat.find(nm, e, err));
    CHECK(e.alive());
    // Старого имени в указателе больше нет — запись переиспользована.
    Catalog::make_name("OLD", nm);
    CatalogEntry old;
    CHECK(cat.find(nm, old, err));
    CHECK(!old.exists());
}

// «Стирает все переменные, не помеченные оператором COM» (разд. 19.1).
void test_load_clears_variables()
{
    HeadlessHost host;
    if (!fresh_disk(host)) { CHECK(false); return; }

    // Сегмент печатает общую переменную и необщую.
    std::string error;
    if (!put_program(host, "SEG", "10 COM C\n20 PRINT C;N\n30 STOP\n")) {
        CHECK(false);
        return;
    }

    ProgramImage main_prog;
    if (!tokenize_text("10 COM C\n20 C=5\n30 N=7\n40 LOAD DC F\"SEG\"\n",
                       main_prog, error)) {
        std::printf("  трансляция: %s\n", error.c_str());
        CHECK(false);
        return;
    }
    std::string screen;
    if (!run(main_prog, host, screen, error)) { std::printf("  %s\n", error.c_str()); CHECK(false); return; }
    // Общая переменная пережила загрузку, необщая обнулилась.
    CHECK_STR(line_of(screen, 1), " 5  0");
}

// Файла нет — ошибка машины с кодом 73.
void test_load_missing()
{
    HeadlessHost host;
    if (!fresh_disk(host)) { CHECK(false); return; }

    ProgramImage img;
    std::string error;
    if (!tokenize_text("10 LOAD DC F\"NETU\"\n", img, error)) {
        std::printf("  трансляция: %s\n", error.c_str());
        CHECK(false);
        return;
    }
    std::string screen;
    CHECK(!run(img, host, screen, error));
    CHECK(error.find("нет в каталоге") != std::string::npos);
}

// Описания переменных живут только в таблицах, и SAVE DC пишет на диск
// именно их: без круговой проверки образ, собранный из текста, терял бы
// размеры массивов (docs/format.md, разд. 6).
void test_tables_round_trip()
{
    ProgramImage img;
    std::string error;
    if (!tokenize_text("10 COM C(4)\n20 DIM A(5),B(2,3),D%(7),S¤(3)8\n",
                       img, error)) {
        std::printf("  трансляция: %s\n", error.c_str());
        CHECK(false);
        return;
    }

    std::vector<uint8_t> file;
    img.save_file("T", file);

    ProgramImage back;
    std::string err;
    if (!back.load_file(file, err)) { std::printf("  %s\n", err.c_str()); CHECK(false); return; }

    CHECK_EQ(static_cast<unsigned>(back.vars().size()),
             static_cast<unsigned>(img.vars().size()));
    if (back.vars().size() != img.vars().size()) return;

    for (unsigned i = 0; i < img.vars().size(); ++i) {
        const VarInfo & a = img.vars()[i];
        const VarInfo & b = back.vars()[i];
        CHECK_EQ(static_cast<unsigned>(b.is_string), static_cast<unsigned>(a.is_string));
        CHECK_EQ(static_cast<unsigned>(b.is_integer), static_cast<unsigned>(a.is_integer));
        CHECK_EQ(static_cast<unsigned>(b.is_common), static_cast<unsigned>(a.is_common));
        CHECK_EQ(b.dim1, a.dim1);
        CHECK_EQ(b.dim2, a.dim2);
        if (a.is_string) CHECK_EQ(b.str_len, a.str_len);
    }
}

} // namespace

int main()
{
    test_save_creates_file();
    test_save_then_load();
    test_save_existing_refused();
    test_save_error_is_catchable();
    test_save_over_scratched();
    test_load_clears_variables();
    test_load_missing();
    test_tables_round_trip();
    return test::summary("запись и загрузка программ");
}
