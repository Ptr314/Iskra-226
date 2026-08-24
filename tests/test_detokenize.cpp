// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Mikhail Revzin <p3.141592653589793238462643@gmail.com>
// Part of the Iskra-226 project: https://github.com/Ptr314/Iskra-226
// Description: круговая проверка «токены → текст → токены»

#include <cstdio>
#include <map>
#include <string>
#include <vector>

#include "check.h"
#include "core/detokenize.h"
#include "core/koi8.h"
#include "core/program.h"
#include "core/tokenize.h"

using namespace iskra;

namespace {

// КОИ-8 в UTF-8 для сообщений.
std::string to_utf8(const std::string & s)
{
    return s.empty() ? std::string()
                     : koi8_to_utf8(reinterpret_cast<const uint8_t *>(s.data()),
                                    static_cast<unsigned>(s.size()));
}

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
                    nibbles = 0; value = 0;
                }
            }
        }
        p = (e < text.size()) ? e + 1 : e;
    }
    return !out.empty();
}

std::string hexs(const std::vector<uint8_t> & b, unsigned from, unsigned n)
{
    static const char * D = "0123456789ABCDEF";
    std::string r;
    for (unsigned i = from; i < b.size() && i < from + n; ++i) {
        if (i > from) r += ' ';
        r += D[b[i] >> 4];
        r += D[b[i] & 15];
    }
    return r;
}

// --- отдельные конструкции --------------------------------------------------

// Строка туда и обратно: текст → токены → текст. Возвращает получившийся
// текст, чтобы видеть его глазами.
std::string round(const char * utf8)
{
    std::string koi8;
    utf8_to_koi8(utf8, koi8);

    NameTable names;
    ProgramImage img;
    std::string error;
    if (!tokenize(koi8, img, names, error)) return "ТРАНСЛЯЦИЯ: " + error;

    std::string back;
    if (!detokenize_line(img.line(0), names, back, error))
        return "ДЕТОКЕНИЗАЦИЯ: " + error;

    return to_utf8(back);
}

void test_statements()
{
    // Пробелы транслятору безразличны, поэтому обратно выходит канонический
    // вид: без пробелов там, где машина их не хранит.
    CHECK_STR(round("10 STOP"), "10 STOP");
    CHECK_STR(round("20 END"), "20 END");
    CHECK_STR(round("30 GOTO 150"), "30 GOTO 150");
    CHECK_STR(round("40 STOP:END"), "40 STOP:END");
    CHECK_STR(round("50 REM ABC"), "50 REM ABC");
    // Имена тут настоящие: их дала трансляция текста. Придумывает имена
    // только detokenize() целой программы, где их взять неоткуда.
    CHECK_STR(round("60 A=B+C"), "60 A=B+C");
    CHECK_STR(round("70 A=(B+C)*D"), "70 A=(B+C)*D");
    CHECK_STR(round("80 IF A>0 THEN 100"), "80 IF A>0THEN100");
    CHECK_STR(round("90 FOR I=1 TO 10 STEP 2"), "90 FOR I=1TO10STEP2");
    CHECK_STR(round("100 PRINT A;B"), "100 PRINT A;B");
    CHECK_STR(round("120 A=SQR(B)"), "120 A=SQR(B)");
    CHECK_STR(round("140 ON A GOTO 10,20"), "140 ON AGOTO10,20");
}

// --- круговая проверка по корпусу -------------------------------------------

std::map<std::string, unsigned> g_fails;

// Рабочие поля, которые машина заполняет при исполнении, а трансляция
// оставляет нулями: адрес возврата DEFFN' и цепочка DATA.
bool only_runtime_fields(const std::vector<uint8_t> & want,
                         const std::vector<uint8_t> & got)
{
    if (want.size() != got.size()) return false;
    bool any = false;
    for (unsigned i = 0; i < want.size(); ++i) {
        if (want[i] == got[i]) continue;
        if (got[i] != 0) return false;
        any = true;
    }
    return any;
}

void check_file(const char * name, unsigned & total, unsigned & same,
                unsigned & runtime)
{
    std::vector<uint8_t> file;
    if (!load_hex_dump(corpus((std::string(name) + "_bin.txt").c_str()), file)) {
        std::printf("  нет файла %s\n", name);
        return;
    }
    ProgramImage img;
    std::string error;
    if (!img.load_file(file, error)) {
        std::printf("  %s: %s\n", name, error.c_str());
        return;
    }

    NameTable names;
    std::string text;
    // Детокенизация целиком не пройдёт, пока не разобраны все глаголы, —
    // поэтому идём построчно: видно, сколько уже получается.
    // Имена раздаёт сам детокенизатор — своей же логикой.
    std::string whole_text;
    if (!detokenize(img, names, whole_text, error)) {
        // Целиком не вышло — не беда, таблица имён уже заполнена, идём
        // построчно и смотрим, сколько получается.
    }

    unsigned mine = 0, differ = 0, mycmp = 0, mysame = 0, myrt = 0;
    for (unsigned i = 0; i < img.line_count(); ++i) {
        std::string line;
        if (!detokenize_line(img.line(i), names, line, error)) {
            ++g_fails[error];
            continue;
        }
        ++mine;

        // Обратно в токены той же таблицей имён: иначе индексы разъедутся.
        NameTable back = names;
        unsigned number = 0;
        std::vector<uint8_t> body;
        std::string err;
        if (!tokenize_line(line, back, number, body, err)) {
            ++g_fails["обратно: " + err];
            continue;
        }
        ++total;
        ++mycmp;
        if (number != img.line(i).number) { ++differ; continue; }
        if (body == img.line(i).body) { ++same; ++mysame; continue; }
        if (only_runtime_fields(img.line(i).body, body)) { ++runtime; ++myrt; continue; }

        ++differ;
        if (differ <= 3) {
            unsigned d = 0;
            const std::vector<uint8_t> & w = img.line(i).body;
            while (d < w.size() && d < body.size() && w[d] == body[d]) ++d;
            const unsigned from = (d > 4) ? d - 4 : 0;
            const std::string utf = to_utf8(line);
            std::printf("    %s %u: %s\n      ждали  %s\n      вышло  %s\n",
                        name, img.line(i).number, utf.c_str(),
                        hexs(w, from, 12).c_str(), hexs(body, from, 12).c_str());
        }
    }
    std::printf("  %-8s строк %4u, детокенизировано %4u, сошлось %4u, "
                "рабочие поля %3u\n",
                name, img.line_count(), mine, mysame, myrt);
}

void test_corpus()
{
    static const char * FILES[] = {
        "STAT04", "STAT02", "STAT03", "STAT08", "STAT09", "VICT", "EDITOR"
    };
    unsigned total = 0, same = 0, runtime = 0;
    for (unsigned k = 0; k < sizeof(FILES) / sizeof(FILES[0]); ++k)
        check_file(FILES[k], total, same, runtime);
    std::printf("  ИТОГО: сошлось %u из %u сравнимых строк; ещё %u разошлись\n"
                "  только рабочими полями\n", same, total, runtime);

    std::printf("  не детокенизируется чаще всего:\n");
    for (unsigned k = 0; k < 12; ++k) {
        std::map<std::string, unsigned>::iterator best = g_fails.end();
        for (std::map<std::string, unsigned>::iterator it = g_fails.begin();
             it != g_fails.end(); ++it)
            if (best == g_fails.end() || it->second > best->second) best = it;
        if (best == g_fails.end()) break;
        std::printf("    %5u  %s\n", best->second, best->first.c_str());
        g_fails.erase(best);
    }

    CHECK(same >= 1750);
}

} // namespace

int main()
{
    test_statements();
    test_corpus();
    return test::summary("обратная трансляция");
}
