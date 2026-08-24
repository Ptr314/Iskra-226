// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Mikhail Revzin <p3.141592653589793238462643@gmail.com>
// Part of the Iskra-226 project: https://github.com/Ptr314/Iskra-226
// Description: каталог диска

#include "check.h"
#include "core/catalog.h"
#include "host_headless/headless_host.h"

using namespace iskra;

namespace {

const unsigned SEC = Host::SECTOR_SIZE;

unsigned hash_of(const char * name, unsigned ls)
{
    uint8_t n[NAME_LEN];
    Catalog::make_name(name, n);
    return Catalog::hash(n, ls);
}

bool find_by(Catalog & cat, const char * name, CatalogEntry & e, std::string & err)
{
    uint8_t n[NAME_LEN];
    Catalog::make_name(name, n);
    return cat.find(n, e, err);
}

bool create_by(Catalog & cat, const char * name, bool prog, unsigned n,
               CatalogEntry & e, std::string & err)
{
    uint8_t nm[NAME_LEN];
    Catalog::make_name(name, nm);
    return cat.create(nm, prog, n, e, err);
}

std::vector<uint8_t> blank(unsigned n)
{
    return std::vector<uint8_t>(static_cast<std::size_t>(n) * SEC, 0);
}

// Хеш имени сверен со всеми 303 записями корпуса; здесь — образцы с
// настоящих дискет (klerk.dsk, указатель 10 секторов; BUKWA — с указателя
// в 5 секторов). Проверить можно через tools/probes/hash.py.
void test_hash()
{
    CHECK_EQ(hash_of("L0", 10), 7u);
    CHECK_EQ(hash_of("L1", 10), 0u);
    CHECK_EQ(hash_of("L2", 10), 3u);
    CHECK_EQ(hash_of("L3", 10), 6u);
    CHECK_EQ(hash_of("L4", 10), 5u);        // тут рвётся любая аддитивная модель
    CHECK_EQ(hash_of("L5", 10), 8u);
    CHECK_EQ(hash_of("LL", 10), 0u);
    CHECK_EQ(hash_of("D0", 10), 3u);
    CHECK_EQ(hash_of("D1", 10), 6u);
    CHECK_EQ(hash_of("D2", 10), 9u);
    CHECK_EQ(hash_of("D21", 10), 4u);
    CHECK_EQ(hash_of("*PACK", 10), 7u);
    CHECK_EQ(hash_of("*L2", 10), 3u);
    CHECK_EQ(hash_of("BUKWA", 5), 3u);

    // Указатель, кратный трём, вырождается: хеш всегда кратен трём.
    for (unsigned i = 0; i < 10; ++i) {
        char nm[8];
        std::sprintf(nm, "F%u", i);
        CHECK_EQ(hash_of(nm, 3), 0u);
        CHECK_EQ(hash_of(nm, 6) % 3, 0u);
        CHECK_EQ(hash_of(nm, 24) % 3, 0u);
    }
}

void test_format_and_params()
{
    HeadlessHost host;
    host.mount(0, blank(200));
    Catalog cat(host, 0);

    std::string err;
    CHECK(!cat.open(err));                      // пустой диск — каталога нет

    CHECK(cat.format(5, 199, err));
    CHECK_EQ(cat.index_sectors(), 5u);
    CHECK_EQ(cat.current_end(), 5u);            // файлы идут сразу за указателем
    CHECK_EQ(cat.area_end(), 199u);

    // Параметры действительно легли в нулевой сектор.
    uint8_t sec[Host::SECTOR_SIZE];
    CHECK(host.disk_read(0, 0, sec));
    CHECK_EQ(static_cast<unsigned>((sec[0] << 8) | sec[1]), 5u);
    CHECK_EQ(static_cast<unsigned>((sec[2] << 8) | sec[3]), 5u);
    CHECK_EQ(static_cast<unsigned>((sec[4] << 8) | sec[5]), 199u);

    Catalog again(host, 0);
    CHECK(again.open(err));
    CHECK_EQ(again.index_sectors(), 5u);

    CHECK(!cat.format(0, 199, err));            // указатель не бывает нулевым
    CHECK(!cat.format(5, 4, err));              // конец раньше указателя
    CHECK(!cat.format(5, 500, err));            // за краем диска
}

void test_create_find_scratch()
{
    HeadlessHost host;
    host.mount(0, blank(200));
    Catalog cat(host, 0);

    std::string err;
    CHECK(cat.format(5, 199, err));

    CatalogEntry a, b, e;
    CHECK(create_by(cat, "ALPHA", false, 10, a, err));
    CHECK_EQ(a.first, 5u);
    CHECK_EQ(a.last, 14u);
    CHECK_EQ(cat.current_end(), 15u);
    // Запись легла в сектор указателя, который дал хеш.
    CHECK_EQ(a.sector, hash_of("ALPHA", 5));

    CHECK(create_by(cat, "BETA", true, 3, b, err));
    CHECK_EQ(b.first, 15u);
    CHECK_EQ(b.last, 17u);
    CHECK_EQ(cat.current_end(), 18u);

    CHECK(find_by(cat, "ALPHA", e, err));
    CHECK(e.exists());
    CHECK(e.alive());
    CHECK(!e.is_program());
    CHECK_STR(e.name_str(), "ALPHA");
    CHECK_EQ(e.sectors(), 10u);
    CHECK_EQ(limits_code(e), 2u);

    CHECK(find_by(cat, "BETA", e, err));
    CHECK(e.is_program());
    CHECK_EQ(limits_code(e), 1u);

    // Файла нет — не ошибка, просто пустая запись.
    CHECK(find_by(cat, "GAMMA", e, err));
    CHECK(!e.exists());
    CHECK_EQ(limits_code(e), 0u);

    // Повторное имя не заводится.
    CHECK(!create_by(cat, "ALPHA", false, 1, e, err));

    // Вычёркивание ставит бит 0 и не трогает остального.
    uint8_t nm[NAME_LEN];
    Catalog::make_name("ALPHA", nm);
    CHECK(cat.scratch(nm, err));
    CHECK(find_by(cat, "ALPHA", e, err));
    CHECK(e.exists());
    CHECK(e.scratched());
    CHECK(!e.alive());
    CHECK_EQ(e.first, 5u);
    CHECK_EQ(limits_code(e), 4u);

    Catalog::make_name("GAMMA", nm);
    CHECK(!cat.scratch(nm, err));               // вычёркивать нечего
}

void test_list()
{
    HeadlessHost host;
    host.mount(0, blank(200));
    Catalog cat(host, 0);

    std::string err;
    CHECK(cat.format(5, 199, err));

    CatalogEntry e;
    CHECK(create_by(cat, "ONE", false, 2, e, err));
    CHECK(create_by(cat, "TWO", false, 2, e, err));
    CHECK(create_by(cat, "THREE", true, 2, e, err));

    uint8_t nm[NAME_LEN];
    Catalog::make_name("TWO", nm);
    CHECK(cat.scratch(nm, err));

    std::vector<CatalogEntry> all;
    CHECK(cat.list(all, false, err));
    CHECK_EQ(all.size(), 2u);
    CHECK(cat.list(all, true, err));
    CHECK_EQ(all.size(), 3u);
}

// Статус нельзя сверять на равенство 10/11: в корпусе встречается ещё 21
// (w001-s1/МОНИТОР, w005-s1v1/*DASB2). Значим только бит 0; назначение
// бита 5 неизвестно, но запись остаётся вычеркнутой программой.
void test_status_bit()
{
    HeadlessHost host;
    host.mount(0, blank(64));
    Catalog cat(host, 0);

    std::string err;
    CHECK(cat.format(2, 63, err));
    CatalogEntry e;
    CHECK(create_by(cat, "PROG", true, 4, e, err));

    uint8_t sec[Host::SECTOR_SIZE];
    CHECK(host.disk_read(0, e.sector, sec));
    sec[e.slot * 16] = 0x21;
    CHECK(host.disk_write(0, e.sector, sec));

    CatalogEntry got;
    CHECK(find_by(cat, "PROG", got, err));
    CHECK(got.exists());                        // слот занят, а не свободен
    CHECK(got.scratched());
    CHECK(!got.alive());
    CHECK_EQ(limits_code(got), 3u);             // вычеркнутый программный

    // А статус 10 — живой файл, и точное сравнение тут ни при чём.
    CHECK(host.disk_read(0, e.sector, sec));
    sec[e.slot * 16] = 0x30;
    CHECK(host.disk_write(0, e.sector, sec));
    CHECK(find_by(cat, "PROG", got, err));
    CHECK(got.alive());
    CHECK_EQ(limits_code(got), 1u);
}

void test_space()
{
    HeadlessHost host;
    host.mount(0, blank(64));
    Catalog cat(host, 0);

    std::string err;
    CHECK(cat.format(2, 40, err));              // область каталога 2…40

    CatalogEntry e;
    CHECK(!create_by(cat, "HUGE", false, 100, e, err));   // не помещается
    CHECK(create_by(cat, "FILL", false, 39, e, err));     // ровно до конца
    CHECK_EQ(e.last, 40u);
    CHECK(!create_by(cat, "MORE", false, 1, e, err));     // места больше нет

    CHECK(cat.move_end(63, err));
    CHECK_EQ(cat.area_end(), 63u);
    CHECK(create_by(cat, "MORE", false, 1, e, err));
    CHECK(!cat.move_end(50, err));              // назад двигать нельзя
}

} // namespace

int main()
{
    test_hash();
    test_format_and_params();
    test_create_find_scratch();
    test_list();
    test_status_bit();
    test_space();
    return test::summary("каталог диска");
}