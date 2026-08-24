// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Mikhail Revzin <p3.141592653589793238462643@gmail.com>
// Part of the Iskra-226 project: https://github.com/Ptr314/Iskra-226
// Description: трансляция «текст → токены», сверка байт с корпусом

#include <cstdio>
#include <map>
#include <string>
#include <vector>

#include "check.h"
#include "core/koi8.h"
#include "core/program.h"
#include "core/tokenize.h"

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

// Транслирует одну строку и возвращает тело в шестнадцатеричном виде.
std::string enc(const char * utf8)
{
    std::string koi8;
    utf8_to_koi8(utf8, koi8);

    NameTable names;
    unsigned number = 0;
    std::vector<uint8_t> body;
    std::string error;
    if (!tokenize_line(koi8, names, number, body, error)) return "ОШИБКА: " + error;
    return hexs(body, 0, static_cast<unsigned>(body.size()));
}

void test_statements()
{
    // Байты сверены с корпусом и docs/format.md.
    CHECK_STR(enc("10 STOP"), "42 00");
    CHECK_STR(enc("20 END"), "59 00");
    CHECK_STR(enc("30 GOTO 150"), "21 02 01 50");
    CHECK_STR(enc("40 GOSUB 2200"), "22 02 22 00");
    CHECK_STR(enc("50 RETURN"), "5E 00");

    // Два оператора в строке идут подряд, двоеточие не кодируется.
    CHECK_STR(enc("60 STOP:END"), "42 00 59 00");

    // REM забирает остаток строки как есть.
    CHECK_STR(enc("70 REM ABC"), "56 04 20 41 42 43");
}

void test_expressions()
{
    // Присваивание: цель, D9, выражение.
    CHECK_STR(enc("10 A=1"), "36 04 00 D9 E8 01");
    CHECK_STR(enc("20 A=200"), "36 05 00 D9 E7 02 00");
    // Знаки операций в позиции операции.
    CHECK_STR(enc("30 A=B+C"), "36 05 00 D9 01 EA 02");
    CHECK_STR(enc("40 A=B-C"), "36 05 00 D9 01 E9 02");
    CHECK_STR(enc("50 A=B*C"), "36 05 00 D9 01 DF 02");
    CHECK_STR(enc("60 A=B/C"), "36 05 00 D9 01 DC 02");
    CHECK_STR(enc("70 A=B^C"), "36 05 00 D9 01 E0 02");
    // Унарный минус — тот же байт, что и бинарный, но в позиции операнда.
    CHECK_STR(enc("80 A=-B"), "36 04 00 D9 E9 01");
    // Скобки.
    CHECK_STR(enc("90 A=(B+C)*D"), "36 09 00 D9 EB 01 EA 02 D0 DF 03");
    // Дробная константа: описатель «цифр до точки / всего цифр».
    CHECK_STR(enc("100 A=.5"), "36 05 00 D9 E5 01 50");
    CHECK_STR(enc("110 A=2.5"), "36 05 00 D9 E5 12 25");
}

void test_control()
{
    CHECK_STR(enc("10 IF A>0 THEN 100"), "24 07 00 D4 E8 00 D3 01 00");
    // Связки условий: E7 — AND, E6 — OR (EDITOR 360 и 1360).
    CHECK_STR(enc("20 IF A>0 AND B<9 THEN 100"),
              "24 0C 00 D4 E8 00 E7 01 D7 E8 09 D3 01 00");
    // FOR — глагол 57, знак равенства не кодируется (EDITOR 76).
    CHECK_STR(enc("30 FOR I=1 TO 10"), "57 06 00 E8 01 D1 E8 10");
    CHECK_STR(enc("40 FOR I=1 TO 10 STEP 2"),
              "57 09 00 E8 01 D1 E8 10 D2 E8 02");
    CHECK_STR(enc("50 NEXT I"), "52 01 00");
}

void test_print()
{
    CHECK_STR(enc("10 PRINT"), "4C 00");
    CHECK_STR(enc("20 PRINT A"), "4C 01 00");
    CHECK_STR(enc("30 PRINT A;B"), "4C 03 00 DD 01");
    CHECK_STR(enc("40 PRINT A,B"), "4C 03 00 DE 01");
    // Литерал: E3, длина, байты.
    CHECK_STR(enc("50 PRINT \"AB\""), "4C 04 E3 02 41 42");
    // HEX( — E2, длина, байты; скобка не кодируется.
    CHECK_STR(enc("60 PRINT HEX(0A0A)"), "4C 04 E2 02 0A 0A");
    // Функция: скобка не кодируется, закрывается D0.
    CHECK_STR(enc("70 A=SQR(B)"), "36 05 00 D9 F6 01 D0");
    // У AT( закрывающей скобки в потоке нет (STAT04 10, EDITOR 241).
    CHECK_STR(enc("80 PRINT AT(5,14)"), "4C 06 D5 E8 05 DE E8 14");
}

void test_string_functions()
{
    // Скобок у неявных функций в потоке нет вовсе, у STR( первая
    // запятая не кодируется (docs/format.md, разд. 5).
    std::printf("  STR(A$,67)       %s\n", enc("10 PRINT STR(A$,67)").c_str());
    std::printf("  STR(A$,1,2)      %s\n", enc("10 PRINT STR(A$,1,2)").c_str());
    std::printf("  LEN(A$)          %s\n", enc("10 PRINT LEN(A$)").c_str());
    std::printf("  VAL(A$,2)        %s\n", enc("10 PRINT VAL(A$,2)").c_str());
    std::printf("  STR(A$,1,LEN(A$)) %s\n", enc("10 PRINT STR(A$,1,LEN(A$))").c_str());
}

// Байты каждой проверки взяты из корпуса; отличаются только индексы
// переменных — тут таблица имён своя на каждую строку и начинается с нуля.
void test_more_statements()
{
    // READ, RUN, RETURN CLEAR, RESTORE (VICT 2200, EDITOR 30, 100, 4865).
    CHECK_STR(enc("10 READ W$"), "44 01 00");
    CHECK_STR(enc("20 RUN 100"), "2F 02 01 00");
    CHECK_STR(enc("30 RETURN CLEAR ALL"), "30 01 CB");
    CHECK_STR(enc("40 RESTORE 1,4850"), "51 05 E8 01 DE 48 50");
    // Номер строки образа — обычная константа, а не пара BCD (EDITOR 162).
    CHECK_STR(enc("50 PRINTUSING 10"), "28 03 E7 00 10");
    // KEYIN, наоборот, хранит номера строк парами BCD (EDITOR 242).
    CHECK_STR(enc("60 KEYIN A$,244,244"), "25 05 00 02 44 02 44");
    CHECK_STR(enc("70 ROTATE C(A$,8)"), "4D 07 D4 EB 00 DE E8 08 D0");

    // Двухбайтовые глаголы 06 <подкод> (STAT03 240, EDITOR 101, 346, 2630).
    CHECK_STR(enc("80 MAT Q=ZER"), "06 01 04 E0 00 D9 EF");
    CHECK_STR(enc("90 MAT REDIM V$(5)253"), "06 02 09 E0 00 EB E8 05 D0 E7 02 53");
    CHECK_STR(enc("100 MAT COPY -P$(I%)TOQ$"), "06 06 06 E9 00 01 D0 D1 02");
    CHECK_STR(enc("110 MAT SEARCH L$(),=STR(L0$,1,2)TOL1$()STEP2"),
              "06 0A 12 E0 00 DE D9 E1 01 E8 01 DE E8 02 D0 D1 E0 02 D2 E8 02");
    CHECK_STR(enc("120 LINPUT \"AB\",N0$"), "06 24 05 E3 02 41 42 00");
    CHECK_STR(enc("130 REPLACE C%,L4$(),HEX(0000),HEX(0000)"),
              "06 26 0E 00 DE E0 01 DE E2 02 00 00 DE E2 02 00 00");

    // Образы печати: CA — FROM, D1 — TO (EDITOR 344, 378).
    CHECK_STR(enc("140 PACK(##)L0$FROMT%,P%"), "48 09 E3 02 23 23 00 CA 01 DE 02");
    CHECK_STR(enc("150 UNPACK(##)T$(T%)TOQ%"), "5D 09 E3 02 23 23 00 01 D0 D1 02");

    // Дисковые (STAT03 140, 230; VICT 45, 6357; EDITOR 1120).
    CHECK_STR(enc("160 DSKIP END"), "7A 01 D7");
    CHECK_STR(enc("170 DBACKSPACE 1"), "79 02 E8 01");
    CHECK_STR(enc("180 SCRATCH R\"VIC\""), "81 06 01 E3 03 56 49 43");
    CHECK_STR(enc("190 SCRATCH DISK RLS=5,END=1000"),
              "82 0B 01 06 D9 E8 05 DE D7 D9 E7 10 00");
    CHECK_STR(enc("200 LIMITS T#D,F1$,O,A,Q,R"),
              "7B 09 02 DB 00 DE 01 02 03 04 05");
    CHECK_STR(enc("210 DATA LOAD DA T#D%(D),(X)V$()"),
              "71 0B 02 DB 00 01 D0 DE EB 02 D0 E0 03");
    CHECK_STR(enc("220 DATA SAVE BT /34,W$"), "68 05 DC DE 34 DE 00");

    // STOP с сообщением, RND(, ROUND( (STAT03 420, EDITOR 4132 и 4543).
    CHECK_STR(enc("230 STOP \"AB\""), "42 04 E3 02 41 42");
    CHECK_STR(enc("240 A=RND(1)"), "36 06 00 D9 F4 E8 01 D0");
    CHECK_STR(enc("250 A=ROUND(B,0)"), "36 08 00 D9 D8 01 DE E8 00 D0");

    // Второй аргумент VAL( — токен DB, своих операндов у него нет
    // (docs/format.md, разд. 5).
    CHECK_STR(enc("260 A=VAL(B$,2)"), "36 06 00 D9 EF 01 DE DB");
    CHECK_STR(enc("270 BIN(A$,2)=J%"), "4B 04 00 DE DB 01");
    // Справа от сравнения в POS( стоит код знака (EDITOR 2630).
    CHECK_STR(enc("280 E%=POS(Q$=20)"), "36 07 00 D9 EC 01 D9 DE 20");

    // Поразрядные операции и графика (EDITOR 3469, 1235, VICT 6150).
    CHECK_STR(enc("290 AND(B$,DF)"), "43 03 00 DE DF");
    CHECK_STR(enc("300 STRETCH S$(),0,0,1"),
              "06 1C 0B E0 00 DE E8 00 DE E8 00 DE E8 01");
    CHECK_STR(enc("310 NPLOT B$(),P6,248"), "06 19 08 E0 00 DE 01 DE E7 02 48");
    CHECK_STR(enc("320 $TRAN(V$(),N4$())"), "06 0C 06 E0 00 DE E0 01 D0");
    CHECK_STR(enc("330 $GIO 'HEX(ED74),A$()"), "40 07 D5 E2 02 ED 74 E0 00");

    // Плоский DATA: значения вплотную, в конце — место под адрес следующего
    // оператора DATA, машина заполняет его сама.
    CHECK_STR(enc("340 DATA 31,334,0"), "29 09 E8 31 E7 03 34 E8 00 00 00");

    // Запись с порядком — свой токен E6 (STAT08 480), а короткая форма E7
    // кончается на 7999 (VICT 705).
    CHECK_STR(enc("350 A=1E6"), "36 06 00 D9 E6 11 10 06");
    CHECK_STR(enc("360 A=7999"), "36 05 00 D9 E7 79 99");
    CHECK_STR(enc("370 A=8000"), "36 06 00 D9 E5 44 80 00");

    // `#<а.в.>` и `/адрес` в позиции операнда (EDITOR 6871, VICT 6000).
    CHECK_STR(enc("380 PRINT #5"), "4C 03 DB E8 05");
    CHECK_STR(enc("390 PRINT /10"), "4C 03 DC DE 10");
    // Пустая зона в начале PRINT (EDITOR 3231).
    CHECK_STR(enc("400 PRINT ,\"AB\""), "4C 05 DE E3 02 41 42");

    // Обмен программой через символьный буфер: DD — признак этой формы,
    // между буфером и первым номером строки разделителя нет (EDITOR 5195).
    CHECK_STR(enc("410 SAVE Z$5215,5215"), "2A 07 DD 00 52 15 DE 52 15");
    CHECK_STR(enc("420 LOAD Z$5215,5215,5225"),
              "2D 0A DD 00 52 15 DE 52 15 DE 52 25");
    CHECK_STR(enc("430 LIST DC R"), "7C 01 01");
    CHECK_STR(enc("440 LOAD DC F/1C,P$(E3%)"), "7D 08 00 DC DE 1C DE 00 01 D0");
    CHECK_STR(enc("450 WINDOW 0,C1,0,C2"),
              "06 23 09 E8 00 DE 00 DE E8 00 DE 01");
    // За скобкой $TRAN бывает буква режима (EDITOR 6837).
    CHECK_STR(enc("460 $TRAN(Q$,U$)R"), "06 0C 06 00 DE 01 D0 DE 00");
}

// Образ, собранный из текста, должен знать переменные так же, как знает их
// образ с дискеты: там размеры лежат в таблицах файла, тут берутся из
// таблицы имён. Без этого текстовая программа исполнялась бы без размеров
// массивов (docs/DECISIONS.md, разд. 12).
void test_image_vars()
{
    std::string koi8;
    utf8_to_koi8("10 DIM A(5),B$(3)8,C$40,D(2,7)\n20 COM E(4)\n", koi8);

    NameTable names;
    ProgramImage img;
    std::string error;
    if (!tokenize(koi8, img, names, error)) {
        std::printf("  %s\n", error.c_str());
        CHECK(false);
        return;
    }

    CHECK_EQ(static_cast<unsigned>(img.vars().size()), 5u);
    if (img.vars().size() != 5) return;

    // A( — числовой массив на 5 элементов.
    CHECK(img.vars()[0].is_array);
    CHECK_EQ(img.vars()[0].dim1, 5u);
    CHECK(!img.vars()[0].is_string);
    // B$( — символьный массив на 3 элемента по 8 байт.
    CHECK(img.vars()[1].is_array);
    CHECK(img.vars()[1].is_string);
    CHECK_EQ(img.vars()[1].dim1, 3u);
    CHECK_EQ(img.vars()[1].str_len, 8u);
    // C$ — скаляр длиной 40; массивом он не стал.
    CHECK(!img.vars()[2].is_array);
    CHECK_EQ(img.vars()[2].str_len, 40u);
    // D( — две размерности.
    CHECK_EQ(img.vars()[3].dim1, 2u);
    CHECK_EQ(img.vars()[3].dim2, 7u);
    // E( описана оператором COM.
    CHECK(img.vars()[4].is_common);
    CHECK_EQ(img.vars()[4].dim1, 4u);
}

// --- сверка с корпусом ------------------------------------------------------

// Транслируем текстовый листинг и сверяем тела строк с парным
// оттранслированным файлом. Это и есть та проверка, ради которой
// отказались от промежуточного представления (docs/DECISIONS.md, разд. 12).
std::map<std::string, unsigned> g_fails;

// Рабочие поля, которые машина заполняет при исполнении, а транслятор
// оставляет нулями: адрес возврата DEFFN' и цепочка операторов DATA
// (docs/format.md, разд. 6). Если все расхождения приходятся на нули с нашей
// стороны, строка разобрана верно, и восстановить её точнее нельзя.
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

void compare_pair(const char * name, unsigned & total, unsigned & same,
                  unsigned & runtime)
{
    std::string utf8;
    if (!read_bytes(corpus((std::string(name) + "_text.txt").c_str()), utf8)) {
        std::printf("  нет листинга %s\n", name);
        return;
    }
    std::string koi8;
    utf8_to_koi8(utf8, koi8);

    std::vector<uint8_t> file;
    if (!load_hex_dump(corpus((std::string(name) + "_bin.txt").c_str()), file)) {
        std::printf("  нет файла %s\n", name);
        return;
    }
    ProgramImage want;
    std::string error;
    if (!want.load_file(file, error)) {
        std::printf("  %s: %s\n", name, error.c_str());
        return;
    }

    NameTable names;
    ProgramImage got;
    // Транслируем построчно: одна незнакомая конструкция не должна ронять
    // весь листинг — иначе не видно, сколько уже получается.
    unsigned mine = 0, failed = 0, differ = 0, mycmp = 0, mysame = 0, myrt = 0;
    unsigned p = 0;
    const unsigned n = static_cast<unsigned>(koi8.size());
    while (p < n) {
        unsigned e = p;
        while (e < n && koi8[e] != '\n' && koi8[e] != '\r') ++e;
        const std::string line = koi8.substr(p, e - p);
        p = e;
        while (p < n && (koi8[p] == '\n' || koi8[p] == '\r')) ++p;
        if (line.empty()) continue;

        unsigned number = 0;
        std::vector<uint8_t> body;
        std::string err;
        if (!tokenize_line(line, names, number, body, err)) {
            ++failed;
            ++g_fails[err];
            std::printf("  ОТКАЗ %s %u\n", name, number);
            continue;
        }
        ++mine;

        unsigned i = 0;
        if (!want.find(number, i)) continue;
        ++total;
        ++mycmp;
        if (want.line(i).body == body) { ++same; ++mysame; continue; }
        if (only_runtime_fields(want.line(i).body, body)) { ++runtime; ++myrt; continue; }
        ++differ;
        if (differ <= 6) {
            // Показываем окно вокруг первого расходящегося байта:
            // начало строк часто совпадает, и без этого не видно, где.
            unsigned d = 0;
            const std::vector<uint8_t> & w = want.line(i).body;
            while (d < w.size() && d < body.size() && w[d] == body[d]) ++d;
            const unsigned from = (d > 4) ? d - 4 : 0;
            std::printf("    %s %u, расходится с байта %u:\n"
                        "      ждали  %s\n"
                        "      вышло  %s\n",
                        name, number, d,
                        hexs(w, from, 12).c_str(),
                        hexs(body, from, 12).c_str());
        }
    }
    std::printf("  %-8s строк %4u, оттранслировано %4u, сравнимо %4u, "
                "сошлось %4u, рабочие поля %3u\n",
                name, want.line_count(), mine, mycmp, mysame, myrt);
}

void test_corpus_pairs()
{
    // Пары, у которых текст и токены — одна редакция программы
    // (CLAUDE.md, раздел «Корпус»).
    static const char * PAIRS[] = {
        "STAT04", "STAT02", "STAT03", "STAT08", "STAT09", "VICT", "EDITOR"
    };
    unsigned total = 0, same = 0, runtime = 0;
    for (unsigned k = 0; k < sizeof(PAIRS) / sizeof(PAIRS[0]); ++k)
        compare_pair(PAIRS[k], total, same, runtime);
    std::printf("  ИТОГО: сошлось %u из %u сравнимых строк; ещё %u разошлись\n"
                "  только рабочими полями (адрес DEFFN', цепочка DATA)\n",
                same, total, runtime);

    // Что чаще всего не транслируется — это и есть очередь работ.
    std::printf("  не транслируется чаще всего:\n");
    for (unsigned k = 0; k < 16; ++k) {
        std::map<std::string, unsigned>::iterator best = g_fails.end();
        for (std::map<std::string, unsigned>::iterator it = g_fails.begin();
             it != g_fails.end(); ++it)
            if (best == g_fails.end() || it->second > best->second) best = it;
        if (best == g_fails.end()) break;
        std::printf("    %5u  %s\n", best->second, best->first.c_str());
        g_fails.erase(best);
    }

    // Порог, ниже которого падаем: он растёт по мере покрытия операторов.
    CHECK(same >= 1800);
}

} // namespace

int main()
{
    test_statements();
    test_expressions();
    test_control();
    test_print();
    test_string_functions();
    test_more_statements();
    test_image_vars();
    test_corpus_pairs();
    return test::summary("трансляция текст → токены");
}