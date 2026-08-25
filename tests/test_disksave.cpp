// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Mikhail Revzin <p3.141592653589793238462643@gmail.com>
// Part of the Iskra-226 project: https://github.com/Ptr314/Iskra-226
// Description: запись файлов данных — DATA SAVE DC, IF END THEN (гл. 18)

#include <cstdio>
#include <string>
#include <vector>

#include "check.h"
#include "core/catalog.h"
#include "core/disk_record.h"
#include "core/interp.h"
#include "core/koi8.h"
#include "core/names.h"
#include "core/tokenize.h"
#include "host_headless/headless_host.h"

using namespace iskra;

namespace {

const unsigned SEC = Host::SECTOR_SIZE;
const unsigned SECTORS = 200;

// Пустой отформатированный диск: указатель на пять секторов, область
// каталога до конца образа.
bool blank_disk(HeadlessHost & host)
{
    host.mount(0, std::vector<uint8_t>(
        static_cast<std::size_t>(SECTORS) * SEC, 0));
    Catalog cat(host, 0);
    std::string err;
    if (!cat.format(5, SECTORS - 1, err)) { std::printf("  %s\n", err.c_str()); return false; }
    return true;
}

// Прогон на пустом диске; образ отдаётся наружу — проверять надо и его.
bool run_text(const char * utf8, std::string & screen, std::string & error,
              std::vector<uint8_t> * image = 0, std::string * code = 0)
{
    std::string koi8;
    utf8_to_koi8(utf8, koi8);

    NameTable names;
    ProgramImage img;
    if (!tokenize(koi8, img, names, error)) return false;

    HeadlessHost host;
    if (!blank_disk(host)) { error = "не собрался образ"; return false; }

    Interp interp(img, host);
    const bool ok = interp.run(error);
    screen = host.dump();
    if (image) *image = host.image(0);
    if (code) *code = interp.error_code();
    return ok;
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

const uint8_t * sector(const std::vector<uint8_t> & img, unsigned n)
{
    return &img[static_cast<std::size_t>(n) * SEC];
}

// --- создание файла ---------------------------------------------------------

// «Оператор DATA SAVE DC OPEN создаёт новый файл и открывает его… машина
// использует последний сектор файла для хранения служебной информации.
// Никакая другая информация по этому оператору в файл не записывается»
// (руководство, разд. 18.2.1).
void test_open_creates_file()
{
    std::string screen, error;
    std::vector<uint8_t> img;
    const char * src =
        "10 DATA SAVE DC OPEN F(20)\"ANKETA\"\n"
        "20 LIMITS F\"ANKETA\",S,E,U\n"
        "30 PRINT S;E;U\n";
    if (!run_text(src, screen, error, &img)) { std::printf("  %s\n", error.c_str()); CHECK(false); return; }

    // Файл заведён сразу за указателем: LS=5, значит секторы 5…24.
    CHECK_STR(line_of(screen, 1), " 5  24  1");

    // Служебная запись — концевая со счётчиком 1, и лежит она в последнем
    // секторе файла. «Если признак конца данных в файле не записан, то в
    // графе „Использовано“ всегда будет стоять 00001» (разд. 18.4).
    // Так выглядит нетронутый файл `B0001` на образе `w001-s2`.
    const uint8_t * last = sector(img, 24);
    CHECK_EQ(last[0], 0x1Cu);
    CHECK_EQ(last[1], 0x00u);
    CHECK_EQ(last[2], 0x01u);

    // Больше в файл ничего не записано.
    bool clean = true;
    for (unsigned s = 5; s < 24; ++s)
        for (unsigned i = 0; i < SEC; ++i)
            if (sector(img, s)[i]) clean = false;
    CHECK(clean);
}

// «Попытка создать новый файл с именем „АНКЕТА“ приведёт к останову по
// ошибке, так как в указателе каталога уже есть такое имя» (разд. 18.3).
void test_open_twice()
{
    std::string screen, error, code;
    const char * src =
        "10 DATA SAVE DC OPEN F(20)\"ANKETA\"\n"
        "20 DATA SAVE DC OPEN F(20)\"ANKETA\"\n";
    CHECK(!run_text(src, screen, error, 0, &code));
    CHECK_STR(code, "71");
}

// «На месте, занимаемом файлом „ДАННЫЕ“, можно открыть новый файл…
// содержимое секторов диска, занимаемых вычеркнутым файлом, не изменяется»
// (разд. 18.2.2).
void test_open_over_scratched()
{
    std::string screen, error;
    std::vector<uint8_t> img;
    const char * src =
        "10 DATA SAVE DC OPEN F(20)\"D1\"\n"
        "20 DATA SAVE DC 111,222\n"
        "30 DATA SAVE DC CLOSE\n"
        "40 SCRATCH F\"D1\"\n"
        "50 DATA SAVE DC OPEN F(\"D1\")\"D2\"\n"
        "60 LIMITS F\"D2\",S,E,U\n"
        "70 PRINT S;E\n";
    if (!run_text(src, screen, error, &img)) { std::printf("  %s\n", error.c_str()); CHECK(false); return; }
    // Границы достались от вычеркнутого файла.
    CHECK_STR(line_of(screen, 1), " 5  24");
    // И его содержимое осталось нетронутым: запись на месте.
    CHECK_EQ(sector(img, 5)[0], 0x8Bu);
}

// --- запись записей ---------------------------------------------------------

// «Результатом выполнения оператора DATA SAVE DC является создание одной
// логической записи в файле данных» (разд. 18.3), а прочитать её обратно
// должен DATA LOAD DC.
void test_write_and_read_back()
{
    std::string screen, error;
    const char * src =
        "10 DIM N\xC2\xA4""8\n"
        "20 DATA SAVE DC OPEN F(20)\"ANKETA\"\n"
        "30 N\xC2\xA4=\"IVANOV\":DATA SAVE DC N\xC2\xA4,10,1.5\n"
        "40 N\xC2\xA4=\"PETROV\":DATA SAVE DC N\xC2\xA4,20,2.5\n"
        "50 DATA SAVE DC END\n"
        "60 DATA SAVE DC CLOSE\n"
        "70 DIM M\xC2\xA4""8\n"
        "80 DATA LOAD DC OPEN F\"ANKETA\"\n"
        "90 DATA LOAD DC M\xC2\xA4,A,B\n"
        "100 IF END THEN 130\n"
        "110 PRINT M\xC2\xA4;A;B\n"
        "120 GOTO 90\n"
        "130 PRINT \"KONEC\"\n"
        "140 DATA SAVE DC CLOSE\n";
    if (!run_text(src, screen, error)) { std::printf("  %s\n", error.c_str()); CHECK(false); return; }
    CHECK_STR(line_of(screen, 1), "IVANOV   10  1.5");
    CHECK_STR(line_of(screen, 2), "PETROV   20  2.5");
    // «Оператор IF END THEN обеспечивает переход… когда оператор
    // DATA LOAD DC считает концевую запись» (разд. 18.5).
    CHECK_STR(line_of(screen, 3), "KONEC");
}

// «По окончании записи данных адрес текущего сектора изменяется на адрес
// сектора, следующего за последним сектором, занятым под данные»
// (разд. 18.3), а концевая запись «определяет сектор, откуда можно
// записывать данные», и текущего сектора не двигает (разд. 18.4).
void test_end_record_position()
{
    std::string screen, error;
    std::vector<uint8_t> img;
    const char * src =
        "10 DATA SAVE DC OPEN F(20)\"NUMS\"\n"
        "20 DATA SAVE DC 1,2\n"
        "30 DATA SAVE DC 3,4\n"
        "40 DATA SAVE DC 5,6\n"
        "50 DATA SAVE DC END\n"
        "60 DATA SAVE DC CLOSE\n"
        "70 LIMITS F\"NUMS\",S,E,U\n"
        "80 PRINT S;E;U\n";
    if (!run_text(src, screen, error, &img)) { std::printf("  %s\n", error.c_str()); CHECK(false); return; }

    // Три односекторные записи в 5, 6, 7 — концевая в 8, счётчик 4.
    for (unsigned s = 5; s <= 7; ++s) CHECK_EQ(sector(img, s)[0], 0x8Bu);
    CHECK_EQ(sector(img, 8)[0], 0x1Cu);
    CHECK_EQ(sector(img, 8)[2], 0x04u);
    // «Использовано» берётся из концевой записи.
    CHECK_STR(line_of(screen, 1), " 5  24  4");
}

// «Элементами оператора записи могут быть также числовые или символьные
// массивы… Массивы записываются строка за строкой» (разд. 18.3).
void test_arrays()
{
    std::string screen, error;
    const char * src =
        "10 DIM A(3),B\xC2\xA4(2)4\n"
        "20 A(1)=11:A(2)=22:A(3)=33\n"
        "30 B\xC2\xA4(1)=\"AAAA\":B\xC2\xA4(2)=\"BBBB\"\n"
        "40 DATA SAVE DC OPEN F(20)\"ARR\"\n"
        "50 DATA SAVE DC A(),B\xC2\xA4()\n"
        "60 DATA SAVE DC CLOSE\n"
        "70 DIM C(3),D\xC2\xA4(2)4\n"
        "80 DATA LOAD DC OPEN F\"ARR\"\n"
        "90 DATA LOAD DC C(),D\xC2\xA4()\n"
        "100 PRINT C(1);C(2);C(3);D\xC2\xA4(1);D\xC2\xA4(2)\n";
    if (!run_text(src, screen, error)) { std::printf("  %s\n", error.c_str()); CHECK(false); return; }
    CHECK_STR(line_of(screen, 1), " 11  22  33 AAAABBBB");
}

// «Элементами записи могут быть также символьные константы и выражения.
// В случае выражений на диск записываются их значения» (разд. 18.3).
void test_expressions()
{
    std::string screen, error;
    const char * src =
        "10 D=2:X=3:Z=4:A=8:E=5\n"
        "20 DATA SAVE DC OPEN F(20)\"EXPR\"\n"
        "30 DATA SAVE DC \"PROEKT\",D+X*Z/A,E\n"
        "40 DATA SAVE DC CLOSE\n"
        "50 DIM P\xC2\xA4""6\n"
        "60 DATA LOAD DC OPEN F\"EXPR\"\n"
        "70 DATA LOAD DC P\xC2\xA4,Q,R\n"
        "80 PRINT P\xC2\xA4;Q;R\n";
    if (!run_text(src, screen, error)) { std::printf("  %s\n", error.c_str()); CHECK(false); return; }
    CHECK_STR(line_of(screen, 1), "PROEKT 3.5  5");
}

// --- закрытие файла ---------------------------------------------------------

// «Оператор DATA SAVE DC CLOSE закрывает файл, записывая нули во все графы
// таблицы устройств. Так как в закрытый файл невозможно записывать данные…»
// (разд. 18.4).
void test_close()
{
    std::string screen, error;
    const char * src =
        "10 DATA SAVE DC OPEN F(20)\"D\"\n"
        "20 DATA SAVE DC CLOSE\n"
        "30 DATA SAVE DC 1,2\n";
    CHECK(!run_text(src, screen, error));
    CHECK(error.find("не открыт") != std::string::npos);
}

// Запись, не помещающаяся в отведённые секторы, — ошибка, а не порча
// соседнего файла.
void test_overflow()
{
    std::string screen, error;
    const char * src =
        "10 DATA SAVE DC OPEN F(2)\"SMALL\"\n"
        "20 DATA SAVE DC 1,2\n"
        "30 DATA SAVE DC 3,4\n"
        "40 DATA SAVE DC 5,6\n";
    CHECK(!run_text(src, screen, error));
    CHECK(error.find("не помещается") != std::string::npos);
}

// --- IF END THEN ------------------------------------------------------------

// Признак ставит только чтение концевой записи; обычная запись его снимает.
void test_if_end_resets()
{
    std::string screen, error;
    const char * src =
        "10 DATA SAVE DC OPEN F(20)\"D\"\n"
        "20 DATA SAVE DC 1,2\n"
        "30 DATA SAVE DC END\n"
        "40 DATA SAVE DC CLOSE\n"
        "50 DATA LOAD DC OPEN F\"D\"\n"
        "60 DATA LOAD DC A,B\n"
        "70 IF END THEN 200\n"
        "80 PRINT \"ZAPIS\";A;B\n"
        "90 DATA LOAD DC A,B\n"
        "100 IF END THEN 120\n"
        "110 PRINT \"NE DOLZHNO BYT\":STOP\n"
        "120 PRINT \"KONEC\"\n"
        "130 END\n"
        "200 PRINT \"RANO\"\n";
    if (!run_text(src, screen, error)) { std::printf("  %s\n", error.c_str()); CHECK(false); return; }
    CHECK_STR(line_of(screen, 1), "ZAPIS 1  2");
    CHECK_STR(line_of(screen, 2), "KONEC");
}

// --- оттранслированная форма ------------------------------------------------

// В токенах `IF END THEN` — глагол 1E с двухбайтовым BCD номером строки,
// `DATA SAVE DC END` — глагол 76 с байтом D7, а `DATA SAVE DC CLOSE` —
// отдельный глагол 77 (docs/format.md, разд. 5).
void test_tokenized_bytes()
{
    std::string koi8, error;
    utf8_to_koi8(
        "10 DATA SAVE DC OPEN F(20)\"D\"\n"
        "20 DATA SAVE DC 1,2\n"
        "30 DATA SAVE DC END\n"
        "40 DATA SAVE DC CLOSE\n"
        "50 IF END THEN 90\n"
        "90 END\n", koi8);

    NameTable names;
    ProgramImage img;
    if (!tokenize(koi8, img, names, error)) { std::printf("  %s\n", error.c_str()); CHECK(false); return; }
    CHECK_EQ(img.line_count(), 6u);

    const std::vector<uint8_t> & l30 = img.line(2).body;
    CHECK_EQ(l30.size(), 3u);
    CHECK_EQ(l30[0], 0x76u);
    CHECK_EQ(l30[2], 0xD7u);

    const std::vector<uint8_t> & l40 = img.line(3).body;
    CHECK_EQ(l40[0], 0x77u);

    const std::vector<uint8_t> & l50 = img.line(4).body;
    CHECK_EQ(l50.size(), 4u);
    CHECK_EQ(l50[0], 0x1Eu);
    CHECK_EQ(l50[1], 0x02u);
    CHECK_EQ(l50[2], 0x00u);
    CHECK_EQ(l50[3], 0x90u);
}

// --- режим абсолютной адресации (разд. 18.9) --------------------------------

// «По оператору DATA SAVE BA в заданный сектор записывается содержимое
// символьного массива. Если массив содержит больше 256 байт, то записываются
// первые 256» (разд. 18.9.4). Массив 16x16 — ровно сектор, как в примере
// 18.31 книги.
void test_block_io()
{
    std::string screen, error;
    std::vector<uint8_t> img;
    const char * src =
        "10 DIM A\xC2\xA4(16),B\xC2\xA4(16)\n"
        "20 STR(A\xC2\xA4(),1,5)=\"HELLO\"\n"
        "30 DATA SAVE BA F(100)A\xC2\xA4()\n"
        "40 DATA LOAD BA F(100)B\xC2\xA4()\n"
        "50 PRINT STR(B\xC2\xA4(),1,5)\n";
    if (!run_text(src, screen, error, &img)) { std::printf("  %s\n", error.c_str()); CHECK(false); return; }
    CHECK_STR(line_of(screen, 1), "HELLO");

    const uint8_t * s100 = sector(img, 100);
    CHECK_EQ(s100[0], static_cast<uint8_t>('H'));
    CHECK_EQ(s100[4], static_cast<uint8_t>('O'));
}

// «Если массив содержит меньше 256 байт, то оставшиеся байты сектора
// заполняются кодами HEX(00)» (разд. 18.9.4).
void test_block_short()
{
    std::string screen, error;
    std::vector<uint8_t> img;
    const char * src =
        "10 DIM A\xC2\xA4""4\n"
        "20 A\xC2\xA4=\"ABCD\"\n"
        "30 DATA SAVE BA F(50)A\xC2\xA4\n";
    if (!run_text(src, screen, error, &img)) { std::printf("  %s\n", error.c_str()); CHECK(false); return; }
    const uint8_t * s50 = sector(img, 50);
    CHECK_EQ(s50[0], static_cast<uint8_t>('A'));
    CHECK_EQ(s50[3], static_cast<uint8_t>('D'));
    bool zeros = true;
    for (unsigned i = 4; i < SEC; ++i) if (s50[i]) zeros = false;
    CHECK(zeros);
}

// «В режиме DA информация записывается в тех же форматах, что и в режиме
// каталога» (разд. 18.9): те же логические записи, только начальный сектор
// задан прямо в операторе. `IF END THEN` работает так же (разд. 18.9.3).
void test_abs_record()
{
    std::string screen, error;
    const char * src =
        "10 DIM N\xC2\xA4""6\n"
        "20 N\xC2\xA4=\"IVANOV\"\n"
        "30 DATA SAVE DA F(60)N\xC2\xA4,10\n"
        "40 DATA SAVE DA F(61)N\xC2\xA4,20\n"
        "50 DATA SAVE DA F(62)END\n"
        "60 P=60\n"
        "70 DIM M\xC2\xA4""6\n"
        "80 DATA LOAD DA F(P)M\xC2\xA4,A\n"
        "90 IF END THEN 130\n"
        "100 PRINT M\xC2\xA4;A\n"
        "110 P=P+1\n"
        "120 GOTO 80\n"
        "130 PRINT \"KONEC\"\n";
    if (!run_text(src, screen, error)) { std::printf("  %s\n", error.c_str()); CHECK(false); return; }
    CHECK_STR(line_of(screen, 1), "IVANOV 10");
    CHECK_STR(line_of(screen, 2), "IVANOV 20");
    CHECK_STR(line_of(screen, 3), "KONEC");
}

// «Значение адреса начального сектора должно быть меньше адреса конечного
// сектора, иначе выдаётся сообщение об ошибке» (разд. 18.9.5).
void test_verify()
{
    std::string screen, error;
    const char * src =
        "10 VERIFY F(10,20)\n"
        "20 PRINT \"OK\"\n";
    if (!run_text(src, screen, error)) { std::printf("  %s\n", error.c_str()); CHECK(false); return; }
    CHECK_STR(line_of(screen, 1), "OK");

    CHECK(!run_text("10 VERIFY F(20,10)\n", screen, error));
}

} // namespace

int main()
{
    test_open_creates_file();
    test_open_twice();
    test_open_over_scratched();
    test_write_and_read_back();
    test_end_record_position();
    test_arrays();
    test_expressions();
    test_close();
    test_overflow();
    test_if_end_resets();
    test_block_io();
    test_block_short();
    test_abs_record();
    test_verify();
    test_tokenized_bytes();
    return test::summary("запись файлов данных");
}
