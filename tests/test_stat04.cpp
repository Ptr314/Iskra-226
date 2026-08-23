// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Mikhail Revzin <p3.141592653589793238462643@gmail.com>
// Part of the Iskra-226 project: https://github.com/Ptr314/Iskra-226
// Description: STAT04 из обоих представлений даёт один и тот же экран

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

bool load_tokenized(Program & prog)
{
    std::vector<uint8_t> file;
    if (!load_hex_dump(corpus("STAT04_bin.txt"), file)) {
        std::printf("  не прочитался STAT04_bin.txt\n");
        return false;
    }
    std::string error;
    if (!parse_tokenized(file, prog, error)) {
        std::printf("  токены: %s\n", error.c_str());
        return false;
    }
    return true;
}

bool load_text(Program & prog, NameTable & names)
{
    std::string utf8;
    if (!read_bytes(corpus("STAT04_text.txt"), utf8)) {
        std::printf("  не прочитался STAT04_text.txt\n");
        return false;
    }
    std::string koi8;
    utf8_to_koi8(utf8, koi8);

    std::string error;
    if (!parse_text(koi8, prog, names, error)) {
        std::printf("  текст: %s\n", error.c_str());
        return false;
    }
    return true;
}

// Прогон с заданным вводом; возвращает содержимое экрана.
bool run(const Program & prog, const char * df, const char * p, std::string & screen)
{
    HeadlessHost host;
    for (int i = 0; i < 2; ++i) {
        std::string koi8;
        utf8_to_koi8(i == 0 ? df : p, koi8);
        koi8 += '\r';
        host.feed_keys(reinterpret_cast<const uint8_t *>(koi8.data()),
                       static_cast<unsigned>(koi8.size()));
    }

    Interp interp(prog, host);
    std::string error;
    if (!interp.run(error)) {
        std::printf("  исполнение: %s\n", error.c_str());
        return false;
    }
    screen = host.dump();
    return true;
}

// --- Разбор ----------------------------------------------------------------

void test_structure(const Program & tok, const Program & txt)
{
    CHECK_EQ(tok.lines.size(), txt.lines.size());
    CHECK_EQ(tok.lines.size(), 17u);
    if (tok.lines.size() != txt.lines.size()) return;

    for (unsigned i = 0; i < tok.lines.size(); ++i) {
        CHECK_EQ(tok.lines[i].number, txt.lines[i].number);
        CHECK_EQ(tok.lines[i].stmts.size(), txt.lines[i].stmts.size());
        if (tok.lines[i].stmts.size() != txt.lines[i].stmts.size()) continue;
        for (unsigned j = 0; j < tok.lines[i].stmts.size(); ++j)
            CHECK_EQ(tok.lines[i].stmts[j].kind, txt.lines[i].stmts[j].kind);
    }
}

// Проверка правила из docs/format.md, разд. 6: текстовый разбор раздаёт
// индексы в порядке первого появления имени, и они совпадают с индексами
// из оттранслированной формы, где имён уже нет.
void test_variable_indices(const NameTable & names)
{
    static const char * const EXPECTED[] = {
        "K", "P", "E", "Z1", "Z2", "G1", "G2", "B0", "S1", "S", "T"
    };
    const unsigned n = sizeof(EXPECTED) / sizeof(EXPECTED[0]);

    CHECK_EQ(names.count(), n);
    if (names.count() != n) return;
    for (unsigned i = 0; i < n; ++i)
        CHECK_STR(names.name(i), EXPECTED[i]);
}

// --- Исполнение ------------------------------------------------------------

// Главное требование к эмулятору: оба представления одной программы
// исполняются одинаково.
void test_same_screen(const Program & tok, const Program & txt)
{
    static const char * const INPUTS[][2] = {
        { "1",  ".05" },
        { "5",  ".05" },
        { "10", ".05" },
        { "30", ".05" },
        { "10", ".01" }
    };
    const unsigned n = sizeof(INPUTS) / sizeof(INPUTS[0]);

    for (unsigned i = 0; i < n; ++i) {
        std::string a, b;
        if (!run(tok, INPUTS[i][0], INPUTS[i][1], a)) { CHECK(false); continue; }
        if (!run(txt, INPUTS[i][0], INPUTS[i][1], b)) { CHECK(false); continue; }
        if (a != b) {
            std::printf("  расхождение при вводе %s / %s\n", INPUTS[i][0], INPUTS[i][1]);
            CHECK_STR(a, b);
        }
    }
}

// Программа считает критерий Стьюдента, и при большом числе степеней
// свободы её ответ можно сверить с таблицей: 30 степеней, уровень 0.05 —
// табличное значение 2.042.
//
// При одной степени свободы (распределение Коши) программа врёт: она
// интегрирует от -6, а хвост там ещё тяжёлый. Это свойство самой программы,
// а не эмулятора, поэтому здесь только фиксируется, что обе формы врут
// одинаково.
void test_known_values(const Program & tok)
{
    struct Case { const char * df; const char * want; };
    static const Case CASES[] = {
        { "30", "2.04" },
        { "10", "2.23" },
        { "5",  "2.6"  }
    };
    const unsigned n = sizeof(CASES) / sizeof(CASES[0]);

    for (unsigned i = 0; i < n; ++i) {
        std::string screen;
        if (!run(tok, CASES[i].df, ".05", screen)) { CHECK(false); continue; }

        const std::string marker = "T-";
        const std::size_t at = screen.find(marker);
        if (at == std::string::npos) {
            std::printf("  на экране нет строки с критерием (df=%s)\n", CASES[i].df);
            CHECK(false);
            continue;
        }
        const std::size_t eol = screen.find('\n', at);
        const std::string got = screen.substr(at, eol - at);
        if (got.find(CASES[i].want) == std::string::npos) {
            std::printf("  df=%s: ожидалось %s, получено «%s»\n",
                        CASES[i].df, CASES[i].want, got.c_str());
            CHECK(false);
        }
    }
}

// Печать чисел: «с учетом знака перед числом и пробела после числа»
// (руководство, разд. 4.4). Проверяется прямо на экране программы.
void test_number_format(const Program & tok)
{
    std::string screen;
    if (!run(tok, "10", ".05", screen)) { CHECK(false); return; }

    // В литерале «СТ. СВ. = » пробел свой, и ещё один занимает позиция знака
    // числа, — отсюда два пробела перед значением.
    CHECK(screen.find("=  10") != std::string::npos);
    CHECK(screen.find("=  .05") != std::string::npos);

    // Пробел после числа тоже обязателен: без него слиплось бы с соседним.
    std::string t;
    if (!run(tok, "30", ".05", t)) { CHECK(false); return; }
    CHECK(t.find("=  2.04") != std::string::npos);
}

} // namespace

int main()
{
    Program tok, txt;
    NameTable names;

    if (!load_tokenized(tok) || !load_text(txt, names)) {
        std::printf("STAT04: не удалось загрузить корпус\n");
        return 1;
    }

    test_structure(tok, txt);
    test_variable_indices(names);
    test_same_screen(tok, txt);
    test_known_values(tok);
    test_number_format(tok);

    return test::summary("STAT04 из обоих представлений");
}