// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Mikhail Revzin <p3.141592653589793238462643@gmail.com>
// Part of the Iskra-226 project: https://github.com/Ptr314/Iskra-226
// Description: образ программы в памяти — загрузка, правка, выгрузка

#include <cstdio>
#include <string>
#include <vector>

#include "check.h"
#include "core/program.h"

using namespace iskra;

namespace {

std::string corpus(const char * name)
{
    return std::string(ISKRA_CORPUS_DIR) + "/" + name;
}

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

// Дампы корпуса лежат в виде «СМЕЩЕНИЕ | 16 байт | текст».
bool load_hex_dump(const std::string & path, std::vector<uint8_t> & out)
{
    std::string text;
    if (!read_bytes(path, text)) return false;

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
                if (++nibbles == 2) {
                    out.push_back(static_cast<uint8_t>(value));
                    nibbles = 0;
                    value = 0;
                }
            }
        }
        p = (e < text.size()) ? e + 1 : e;
    }
    return !out.empty();
}

bool load(const char * name, ProgramImage & img)
{
    std::vector<uint8_t> file;
    if (!load_hex_dump(corpus(name), file)) {
        std::printf("  не прочитался %s\n", name);
        return false;
    }
    std::string error;
    if (!img.load_file(file, error)) {
        std::printf("  %s: %s\n", name, error.c_str());
        return false;
    }
    return true;
}

// --- загрузка настоящих файлов ---------------------------------------------

void test_load_corpus()
{
    // Файлы корпуса с разными свойствами: маленький, большой, с четырьмя
    // лишними байтами перед потоком (docs/format.md, разд. 3).
    static const struct { const char * file; unsigned lines; } CASES[] = {
        { "STAT04_bin.txt", 17 },
        { "STAT09_bin.txt", 48 },
        { "VICT_bin.txt",  170 }
    };
    for (unsigned k = 0; k < sizeof(CASES) / sizeof(CASES[0]); ++k) {
        ProgramImage img;
        if (!load(CASES[k].file, img)) { CHECK(false); continue; }
        CHECK_EQ(img.line_count(), CASES[k].lines);
        // Номера строк идут по возрастанию.
        for (unsigned i = 1; i < img.line_count(); ++i)
            CHECK(img.line(i - 1).number < img.line(i).number);
        CHECK(!img.vars().empty());
    }
}

// Загрузили и выгрузили — поток обязан совпасть побайтово. Это и есть
// основа для SAVE DC.
void test_round_trip()
{
    static const char * FILES[] = {
        "STAT04_bin.txt", "STAT09_bin.txt", "VICT_bin.txt", "EDITOR_bin.txt"
    };
    for (unsigned k = 0; k < sizeof(FILES) / sizeof(FILES[0]); ++k) {
        ProgramImage img;
        if (!load(FILES[k], img)) { CHECK(false); continue; }

        std::vector<uint8_t> out;
        img.save_stream(out);

        ProgramImage again;
        std::string error;
        if (!again.load_stream(out, error)) {
            std::printf("  %s: обратно не читается: %s\n", FILES[k], error.c_str());
            CHECK(false);
            continue;
        }
        CHECK_EQ(again.line_count(), img.line_count());
        bool same = true;
        for (unsigned i = 0; i < img.line_count() && same; ++i)
            same = (again.line(i).number == img.line(i).number)
                && (again.line(i).body == img.line(i).body);
        if (!same) std::printf("  %s: строки после выгрузки разошлись\n", FILES[k]);
        CHECK(same);
    }
}

// --- правка строк ----------------------------------------------------------

ProgramImage tiny()
{
    ProgramImage img;
    std::string error;
    // Пустые таблицы и три строки: 10, 20, 30.
    static const uint8_t code[] = {
        0, 0, 0, 0, 0, 0,
        0x00, 0x10, 0x03, 0x42, 0x00,     // 10 STOP
        0xFE,
        0x00, 0x20, 0x03, 0x42, 0x00,     // 20 STOP
        0xFE,
        0x00, 0x30, 0x03, 0x42, 0x00      // 30 STOP
    };
    std::vector<uint8_t> v(code, code + sizeof(code));
    if (!img.load_stream(v, error)) std::printf("  %s\n", error.c_str());
    return img;
}

void test_edit()
{
    ProgramImage img = tiny();
    CHECK_EQ(img.line_count(), 3u);
    CHECK_EQ(img.line(0).number, 10u);
    CHECK_EQ(img.line(2).number, 30u);

    // Вставка в середину сохраняет порядок.
    static const uint8_t body[] = { 0x42, 0x00 };
    img.put_line(15, body, 2);
    CHECK_EQ(img.line_count(), 4u);
    CHECK_EQ(img.line(1).number, 15u);

    // Строка с тем же номером замещает прежнюю, а не добавляется:
    // «строки, имеющие одинаковые номера, будут изменены на новые»
    // (руководство, разд. 5.2).
    static const uint8_t other[] = { 0x59, 0x00 };
    img.put_line(15, other, 2);
    CHECK_EQ(img.line_count(), 4u);
    CHECK_EQ(static_cast<unsigned>(img.line(1).body[0]), 0x59u);

    unsigned i = 0;
    CHECK(img.find(20, i));
    CHECK_EQ(i, 2u);
    CHECK(!img.find(25, i));

    // Переход на несуществующую строку — к ближайшей большей.
    CHECK_EQ(img.lower_bound(25), 3u);
    CHECK_EQ(img.lower_bound(0), 0u);
    CHECK_EQ(img.lower_bound(999), 4u);

    CHECK(img.erase_line(15));
    CHECK(!img.erase_line(15));
    CHECK_EQ(img.line_count(), 3u);
}

// CLEAR P оператора LOAD DC: «если параметры не указаны, стирается вся
// программа; если только первый — начиная с него» (разд. 19.1).
void test_erase_range()
{
    ProgramImage a = tiny();
    a.erase_range(20, 0);
    CHECK_EQ(a.line_count(), 1u);
    CHECK_EQ(a.line(0).number, 10u);

    ProgramImage b = tiny();
    b.erase_range(0, 20);
    CHECK_EQ(b.line_count(), 1u);
    CHECK_EQ(b.line(0).number, 30u);

    ProgramImage c = tiny();
    c.erase_range(20, 20);
    CHECK_EQ(c.line_count(), 2u);

    ProgramImage d = tiny();
    d.erase_range(0, 0);
    CHECK_EQ(d.line_count(), 0u);
}

// Файл для записи на диск: заголовочный сектор и секторы потока.
void test_save_file()
{
    ProgramImage img;
    if (!load("STAT04_bin.txt", img)) { CHECK(false); return; }

    std::vector<uint8_t> file;
    img.save_file("STAT04", file);
    CHECK_EQ(static_cast<unsigned>(file[0]), 1u);
    CHECK_STR(std::string(reinterpret_cast<const char *>(&file[1]), 8), "STAT04  ");
    CHECK_EQ(static_cast<unsigned>(file[9]), 0x21u);      // оттранслирована
    CHECK_EQ(file.size() % 256, 0u);
    CHECK_EQ(static_cast<unsigned>(file[256]), 0x02u);
    CHECK_EQ(static_cast<unsigned>(file[257]), 0x80u);

    ProgramImage again;
    std::string error;
    if (!again.load_file(file, error)) {
        std::printf("  %s\n", error.c_str());
        CHECK(false);
        return;
    }
    CHECK_EQ(again.line_count(), img.line_count());
}

} // namespace

int main()
{
    test_load_corpus();
    test_round_trip();
    test_edit();
    test_erase_range();
    test_save_file();
    return test::summary("образ программы");
}