// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Mikhail Revzin <p3.141592653589793238462643@gmail.com>
// Part of the Iskra-226 project: https://github.com/Ptr314/Iskra-226
// Description: логическая запись файла данных

#include "check.h"
#include "core/disk_record.h"
#include "host_headless/headless_host.h"

using namespace iskra;

namespace {

const unsigned SEC = Host::SECTOR_SIZE;

Number num(const char * s)
{
    Number n;
    if (!Number::parse(s, n)) std::printf("  не разобралось: %s\n", s);
    return n;
}

Value vnum(const char * s) { Value v; v.num = num(s); return v; }
Value vstr(const std::string & s) { Value v; v.is_str = true; v.str = s; return v; }

// Пустой образ на n секторов.
std::vector<uint8_t> blank(unsigned n)
{
    return std::vector<uint8_t>(static_cast<std::size_t>(n) * SEC, 0);
}

// Кладёт байты в сектор образа, остальное оставляет нулями.
void put(std::vector<uint8_t> & img, unsigned sector,
         const uint8_t * bytes, unsigned len)
{
    for (unsigned i = 0; i < len; ++i)
        img[static_cast<std::size_t>(sector) * SEC + i] = bytes[i];
}

std::string hexs(const uint8_t * b, unsigned n)
{
    static const char * D = "0123456789ABCDEF";
    std::string r;
    for (unsigned i = 0; i < n; ++i) {
        if (i) r += ' ';
        r += D[b[i] >> 4];
        r += D[b[i] & 15];
    }
    return r;
}

// --- чтение настоящих байт ------------------------------------------------

// Начало сектора 368 образа klerk.dsk — файл данных D21, шаблон экранной
// формы: число 7, затем строки по 15 знаков, рисующие рамку. Здесь код
// сектора заменён на 8B (запись целиком в одном секторе), чтобы кусок был
// самодостаточным; остальное — как на диске.
void test_read_real_sector()
{
    static const uint8_t data[] = {
        0x8B,
        // число 7
        0x00, 0x08, 0x00, 0x70, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        // строка 15 знаков: пробел, 13 минусов, пробел
        0x40, 0x0F, 0x20,
        0x2D, 0x2D, 0x2D, 0x2D, 0x2D, 0x2D, 0x2D, 0x2D, 0x2D, 0x2D, 0x2D, 0x2D, 0x2D,
        0x20,
        // строка 15 знаков: «!», 13 пробелов, «!»
        0x40, 0x0F, 0x21,
        0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20,
        0x21
    };
    std::vector<uint8_t> img = blank(4);
    put(img, 0, data, sizeof(data));

    HeadlessHost host;
    host.mount(0, img);

    std::vector<Value> vals;
    unsigned next = 0;
    std::string err;
    CHECK(read_record(host, 0, 0, vals, next, err));
    CHECK_EQ(vals.size(), 3u);
    CHECK_EQ(next, 1u);

    CHECK(!vals[0].is_str);
    CHECK_STR(vals[0].num.to_display(), " 7");
    CHECK(vals[1].is_str);
    CHECK_STR(vals[1].str, " ------------- ");
    CHECK_STR(vals[2].str, "!             !");
}

// --- запись и обратное чтение --------------------------------------------

void test_single_sector_roundtrip()
{
    HeadlessHost host;
    host.mount(0, blank(8));

    // Строки здесь латиницей: исходник в UTF-8, а на диске КОИ-8, и
    // перекодировка — не дело кодека записей, он переносит байты как есть.
    std::vector<Value> in;
    in.push_back(vstr("ANKETA"));
    in.push_back(vnum("2100"));
    in.push_back(vnum("-441.546992295"));
    in.push_back(vnum("0"));

    unsigned next = 0;
    std::string err;
    CHECK(write_record(host, 0, 2, 7, in, next, err));
    CHECK_EQ(next, 3u);

    uint8_t sec[Host::SECTOR_SIZE];
    CHECK(host.disk_read(0, 2, sec));
    CHECK_EQ(static_cast<unsigned>(sec[0]), static_cast<unsigned>(REC_SINGLE));
    // <тип><длина> и байты числа — ровно как на настоящих дисках.
    CHECK_STR(hexs(sec + 1, 8), "40 06 41 4E 4B 45 54 41");
    CHECK_STR(hexs(sec + 9, 10), "00 08 00 21 00 00 00 00 01 00");

    std::vector<Value> out;
    CHECK(read_record(host, 0, 2, out, next, err));
    CHECK_EQ(out.size(), in.size());
    CHECK(out[0].is_str);
    CHECK_STR(out[0].str, "ANKETA");
    CHECK_STR(out[1].num.to_display(), " 2100");
    CHECK_STR(out[2].num.to_display(), "-441.546992295");
    CHECK_STR(out[3].num.to_display(), " 0");
}

void test_multi_sector()
{
    HeadlessHost host;
    host.mount(0, blank(16));

    // 30 строк по 20 байт: 22 байта на значение, в сектор влезает 11 штук
    // (1 + 11*22 = 243), значит записи нужно три сектора.
    std::vector<Value> in;
    for (unsigned i = 0; i < 30; ++i) {
        std::string s(20, static_cast<char>('A' + i % 26));
        in.push_back(vstr(s));
    }

    unsigned next = 0;
    std::string err;
    CHECK(write_record(host, 0, 4, 15, in, next, err));
    CHECK_EQ(next, 7u);

    uint8_t sec[Host::SECTOR_SIZE];
    CHECK(host.disk_read(0, 4, sec));
    CHECK_EQ(static_cast<unsigned>(sec[0]), static_cast<unsigned>(REC_FIRST));
    CHECK(host.disk_read(0, 5, sec));
    CHECK_EQ(static_cast<unsigned>(sec[0]), static_cast<unsigned>(REC_MIDDLE));
    CHECK(host.disk_read(0, 6, sec));
    CHECK_EQ(static_cast<unsigned>(sec[0]), static_cast<unsigned>(REC_LAST));

    std::vector<Value> out;
    CHECK(read_record(host, 0, 4, out, next, err));
    CHECK_EQ(out.size(), 30u);
    CHECK_EQ(next, 7u);
    CHECK_STR(out[0].str, std::string(20, 'A'));
    CHECK_STR(out[29].str, std::string(20, 'D'));
}

// Значение, не влезающее в остаток сектора, уходит в следующий целиком
// (руководство, разд. 18.6): хвост предыдущего остаётся неиспользованным.
void test_value_moves_to_next_sector()
{
    HeadlessHost host;
    host.mount(0, blank(8));

    std::vector<Value> in;
    in.push_back(vstr(std::string(200, 'X')));
    in.push_back(vstr(std::string(100, 'Y')));

    unsigned next = 0;
    std::string err;
    CHECK(write_record(host, 0, 0, 7, in, next, err));
    CHECK_EQ(next, 2u);

    uint8_t sec[Host::SECTOR_SIZE];
    CHECK(host.disk_read(0, 0, sec));
    // 1 + 2 + 200 = 203; дальше 53 байта нулей, второе значение туда не влезло.
    CHECK_EQ(static_cast<unsigned>(sec[203]), 0u);
    CHECK_EQ(static_cast<unsigned>(sec[255]), 0u);
    CHECK(host.disk_read(0, 1, sec));
    CHECK_EQ(static_cast<unsigned>(sec[1]), static_cast<unsigned>(VAL_STR));
    CHECK_EQ(static_cast<unsigned>(sec[2]), 100u);

    std::vector<Value> out;
    CHECK(read_record(host, 0, 0, out, next, err));
    CHECK_EQ(out.size(), 2u);
    CHECK_STR(out[1].str, std::string(100, 'Y'));
}

void test_end_record()
{
    HeadlessHost host;
    host.mount(0, blank(32));

    std::string err;
    // klerk/ШТАМПЫ: файл с 381, концевая запись в 387, счётчик 7.
    CHECK(write_end_record(host, 0, 10, 16, err));
    uint8_t sec[Host::SECTOR_SIZE];
    CHECK(host.disk_read(0, 16, sec));
    CHECK_STR(hexs(sec, 3), "1C 00 07");

    unsigned used = 0;
    CHECK(read_end_record(host, 0, 16, used));
    CHECK_EQ(used, 7u);
    CHECK(!read_end_record(host, 0, 15, used));      // там не концевая запись
}

void test_errors()
{
    HeadlessHost host;
    host.mount(0, blank(4));

    std::vector<Value> vals;
    unsigned next = 0;
    std::string err;
    // Пустой сектор записью не начинается.
    CHECK(!read_record(host, 0, 0, vals, next, err));
    CHECK(!read_record(host, 0, 99, vals, next, err));   // за краем диска

    std::vector<Value> in;
    in.push_back(vstr(std::string(254, 'Z')));
    CHECK(!write_record(host, 0, 0, 3, in, next, err));  // длиннее 253

    in.clear();
    for (unsigned i = 0; i < 40; ++i) in.push_back(vstr(std::string(200, 'Q')));
    CHECK(!write_record(host, 0, 0, 3, in, next, err));  // не влезает в файл
}

} // namespace

int main()
{
    test_read_real_sector();
    test_single_sector_roundtrip();
    test_multi_sector();
    test_value_moves_to_next_sector();
    test_end_record();
    test_errors();
    return test::summary("запись файла данных");
}