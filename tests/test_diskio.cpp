// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Mikhail Revzin <p3.141592653589793238462643@gmail.com>
// Part of the Iskra-226 project: https://github.com/Ptr314/Iskra-226
// Description: чтение файлов данных — DATA LOAD DC, DSKIP, LIMITS

#include <cstdio>
#include <string>
#include <vector>

#include "check.h"
#include "core/catalog.h"
#include "core/disk_record.h"
#include "core/front_text.h"
#include "core/front_tokens.h"
#include "core/interp.h"
#include "core/koi8.h"
#include "host_headless/headless_host.h"

using namespace iskra;

namespace {

const unsigned SEC = Host::SECTOR_SIZE;
const unsigned SECTORS = 200;

Number num(const char * s)
{
    Number n;
    if (!Number::parse(s, n)) std::printf("  не разобралось: %s\n", s);
    return n;
}

Value vnum(const char * s) { Value v; v.num = num(s); return v; }
Value vstr(const std::string & s) { Value v; v.is_str = true; v.str = s; return v; }

// Образ с каталогом и двумя файлами данных:
//   ANKETA — три записи «имя(8), число, число» и концевая
//   NUMS   — три записи «число, число» и концевая
bool build_image(HeadlessHost & host)
{
    host.mount(0, std::vector<uint8_t>(
        static_cast<std::size_t>(SECTORS) * SEC, 0));

    Catalog cat(host, 0);
    std::string err;
    if (!cat.format(5, SECTORS - 1, err)) { std::printf("  %s\n", err.c_str()); return false; }

    uint8_t nm[NAME_LEN];
    CatalogEntry e;

    Catalog::make_name("ANKETA", nm);
    if (!cat.create(nm, false, 20, e, err)) { std::printf("  %s\n", err.c_str()); return false; }
    unsigned s = e.first, next = 0;
    static const char * NAMES[3] = { "IVANOV  ", "PETROV  ", "SIDOROV " };
    static const char * A[3] = { "10", "20", "30" };
    static const char * B[3] = { "1.5", "2.5", "3.5" };
    for (unsigned i = 0; i < 3; ++i) {
        std::vector<Value> vals;
        vals.push_back(vstr(NAMES[i]));
        vals.push_back(vnum(A[i]));
        vals.push_back(vnum(B[i]));
        if (!write_record(host, 0, s, e.last, vals, next, err)) {
            std::printf("  %s\n", err.c_str()); return false;
        }
        s = next;
    }
    if (!write_end_record(host, 0, e.first, s, err)) { std::printf("  %s\n", err.c_str()); return false; }

    Catalog::make_name("NUMS", nm);
    if (!cat.create(nm, false, 10, e, err)) { std::printf("  %s\n", err.c_str()); return false; }
    s = e.first;
    for (unsigned i = 0; i < 3; ++i) {
        std::vector<Value> vals;
        vals.push_back(vnum(A[i]));
        vals.push_back(vnum(B[i]));
        if (!write_record(host, 0, s, e.last, vals, next, err)) {
            std::printf("  %s\n", err.c_str()); return false;
        }
        s = next;
    }
    return write_end_record(host, 0, e.first, s, err);
}

bool run_text(const char * utf8, std::string & screen, std::string & error)
{
    std::string koi8;
    utf8_to_koi8(utf8, koi8);

    Program prog;
    NameTable names;
    if (!parse_text(koi8, prog, names, error)) return false;

    HeadlessHost host;
    if (!build_image(host)) { error = "не собрался образ"; return false; }

    Interp interp(prog, host);
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

// --- чтение записей --------------------------------------------------------

void test_open_and_read()
{
    const char * src =
        "10 DIM N$8\n"
        "20 SELECT DISK18F\n"
        "30 DATA LOAD DC OPEN T\"ANKETA\"\n"
        "40 DATA LOAD DC N$,A,B\n"
        "50 PRINT N$;A;B\n"
        "60 DATA LOAD DC N$,A,B\n"
        "70 PRINT N$;A;B\n"
        "80 DATA LOAD DC N$,A,B\n"
        "90 PRINT N$;A;B\n";

    std::string screen, error;
    if (!run_text(src, screen, error)) { std::printf("  %s\n", error.c_str()); CHECK(false); return; }
    CHECK_STR(line_of(screen, 1), "IVANOV   10  1.5");
    CHECK_STR(line_of(screen, 2), "PETROV   20  2.5");
    CHECK_STR(line_of(screen, 3), "SIDOROV  30  3.5");
}

// «Пользуясь операторами DSKIP и DBACKSPACE, невозможно выйти за границы
// файла» (руководство, разд. 18.7).
void test_skip()
{
    const char * src =
        "10 DIM N$8\n"
        "20 DATA LOAD DC OPEN T\"ANKETA\"\n"
        "30 DSKIP 2\n"
        "40 DATA LOAD DC N$,A,B\n"
        "50 PRINT N$\n"
        "60 DBACKSPACE BEG\n"
        "70 DATA LOAD DC N$,A,B\n"
        "80 PRINT N$\n"
        "90 DSKIP 1\n"
        "100 DBACKSPACE 1\n"
        "110 DATA LOAD DC N$,A,B\n"
        "120 PRINT N$\n";

    std::string screen, error;
    if (!run_text(src, screen, error)) { std::printf("  %s\n", error.c_str()); CHECK(false); return; }
    CHECK_STR(line_of(screen, 1), "SIDOROV");     // пропустили две записи
    CHECK_STR(line_of(screen, 2), "IVANOV");      // вернулись к началу
    CHECK_STR(line_of(screen, 3), "PETROV");      // вперёд и назад — на месте
}

// «Адрес текущего сектора устанавливается на концевую запись», и чтение
// такой записи — ошибка (руководство, разд. 18.4 и 18.7).
void test_skip_end()
{
    const char * src =
        "10 DIM N$8\n"
        "20 DATA LOAD DC OPEN T\"ANKETA\"\n"
        "30 DSKIP END\n"
        "40 DBACKSPACE 1\n"
        "50 DATA LOAD DC N$,A,B\n"
        "60 PRINT N$\n";

    std::string screen, error;
    if (!run_text(src, screen, error)) { std::printf("  %s\n", error.c_str()); CHECK(false); return; }
    CHECK_STR(line_of(screen, 1), "SIDOROV");     // последняя запись файла
}

void test_limits()
{
    // ANKETA: 20 секторов с 5-го, три односекторные записи и концевая.
    const char * src =
        "10 LIMITS T\"ANKETA\",X,Y,Z,C\n"
        "20 PRINT X;Y;Z;C\n"
        "30 LIMITS T\"NETU\",X,Y,Z,C\n"
        "40 PRINT X;Y;Z;C\n";

    std::string screen, error;
    if (!run_text(src, screen, error)) { std::printf("  %s\n", error.c_str()); CHECK(false); return; }
    CHECK_STR(line_of(screen, 1), " 5  24  4  2");
    CHECK_STR(line_of(screen, 2), " 0  0  0  0");
}

// Строка таблицы устройств выбирается программой, а не выдаётся системой:
// два файла открыты одновременно в разных строках.
void test_two_rows()
{
    const char * src =
        "10 DIM N$8\n"
        "20 SELECT #118F,#218F\n"
        "30 DATA LOAD DC OPEN T#1,\"ANKETA\"\n"
        "40 DATA LOAD DC OPEN T#2,\"NUMS\"\n"
        "50 DATA LOAD DC #1,N$,A,B\n"
        "60 DATA LOAD DC #2,C,D\n"
        "70 PRINT N$;A;B;C;D\n"
        "80 DATA LOAD DC #1,N$,A,B\n"
        "90 DATA LOAD DC #2,C,D\n"
        "100 PRINT N$;A;B;C;D\n";

    std::string screen, error;
    if (!run_text(src, screen, error)) { std::printf("  %s\n", error.c_str()); CHECK(false); return; }
    CHECK_STR(line_of(screen, 1), "IVANOV   10  1.5  10  1.5");
    CHECK_STR(line_of(screen, 2), "PETROV   20  2.5  20  2.5");
}

// --- оттранслированная форма ----------------------------------------------

class TokenBuilder
{
public:
    void add_numeric_var() { flags_.push_back(0x10); }

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
        std::vector<uint8_t> stream;
        const unsigned L2 = static_cast<unsigned>(flags_.size()) * 4;
        stream.push_back(0); stream.push_back(0);
        stream.push_back(static_cast<uint8_t>(L2 >> 8));
        stream.push_back(static_cast<uint8_t>(L2 & 0xFF));
        stream.push_back(0); stream.push_back(0);
        for (unsigned i = static_cast<unsigned>(flags_.size()); i-- > 0; ) {
            stream.push_back(0); stream.push_back(0);
            stream.push_back(flags_[i]);
            stream.push_back(0);
        }
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
    std::vector<uint8_t> flags_;
    std::vector<uint8_t> lines_;
};

// Та же программа в обеих формах должна напечатать одно и то же.
void test_tokens_match_text()
{
    TokenBuilder b;
    b.add_numeric_var();                                   // 00: A
    b.add_numeric_var();                                   // 01: B

    // 54 03 | 0A 18 00 — SELECT DISK18F
    static const int l10[] = { 0x54, 0x03, 0x0A, 0x18, 0x00 };
    b.add_line(10, l10, 5);
    // 75 09 | 02 E3 04 "NUMS" — DATA LOAD DC OPEN T"NUMS"
    static const int l20[] = { 0x75, 0x07, 0x02, 0xE3, 0x04, 'N', 'U', 'M', 'S' };
    b.add_line(20, l20, 9);
    // 74 03 | 00 01 — DATA LOAD DC A,B (приёмники без разделителей)
    static const int l30[] = { 0x74, 0x02, 0x00, 0x01 };
    b.add_line(30, l30, 4);
    // 4C 05 | 00 DD 01 — PRINT A;B
    static const int l40[] = { 0x4C, 0x03, 0x00, 0xDD, 0x01 };
    b.add_line(40, l40, 5);
    // 7A 02 | E8 01 — DSKIP 1
    static const int l50[] = { 0x7A, 0x02, 0xE8, 0x01 };
    b.add_line(50, l50, 4);
    static const int l60[] = { 0x74, 0x02, 0x00, 0x01 };
    b.add_line(60, l60, 4);
    static const int l70[] = { 0x4C, 0x03, 0x00, 0xDD, 0x01 };
    b.add_line(70, l70, 5);
    static const int l80[] = { 0x42, 0x00 };               // STOP
    b.add_line(80, l80, 2);

    Program prog;
    std::string error;
    if (!parse_tokenized(b.file(), prog, error)) {
        std::printf("  разбор: %s\n", error.c_str()); CHECK(false); return;
    }

    HeadlessHost host;
    if (!build_image(host)) { CHECK(false); return; }
    Interp interp(prog, host);
    if (!interp.run(error)) { std::printf("  %s\n", error.c_str()); CHECK(false); return; }
    const std::string tokens = host.dump();

    const char * src =
        "10 SELECT DISK18F\n"
        "20 DATA LOAD DC OPEN T\"NUMS\"\n"
        "30 DATA LOAD DC A,B\n"
        "40 PRINT A;B\n"
        "50 DSKIP 1\n"
        "60 DATA LOAD DC A,B\n"
        "70 PRINT A;B\n"
        "80 STOP\n";
    std::string text;
    if (!run_text(src, text, error)) { std::printf("  %s\n", error.c_str()); CHECK(false); return; }

    CHECK_STR(line_of(tokens, 1), " 10  1.5");
    CHECK_STR(line_of(tokens, 2), " 30  3.5");
    CHECK_STR(tokens, text);
}

// --- ON ERROR --------------------------------------------------------------

// «В символьную переменную 1 заносится код ошибки, в символьную переменную 2 —
// четырёхзначный номер программной строки» (руководство, разд. 11.6).
void test_onerror_goto()
{
    const char * src =
        "10 DIM E$4,N$4\n"
        "20 ON ERROR E$,N$GOTO 100\n"
        "30 DATA LOAD DC OPEN T\"NETU\"\n"
        "40 PRINT \"NE DOLZHNO BYT\"\n"
        "50 STOP\n"
        "100 PRINT E$;\"/\";N$\n";

    std::string screen, error;
    if (!run_text(src, screen, error)) { std::printf("  %s\n", error.c_str()); CHECK(false); return; }
    CHECK_STR(line_of(screen, 1), "73  /0030");     // «такого файла нет», строка 30
}

// «Выполнение оператора ON ERROR-GOTO без параметров отменяет программную
// обработку ошибок» (разд. 11.6).
void test_onerror_off()
{
    std::string screen, error;
    const char * src =
        "10 ON ERROR E$,N$GOTO 100\n"
        "20 ON ERROR\n"
        "30 DATA LOAD DC OPEN T\"NETU\"\n"
        "100 PRINT \"OBRABOTANO\"\n";
    CHECK(!run_text(src, screen, error));
}

// «Обработка ошибки проводится с учётом параметров последнего выполненного
// оператора обработки ошибок» (разд. 11.6).
void test_onerror_last_wins()
{
    const char * src =
        "10 ON ERROR GOTO 100\n"
        "20 ON ERROR GOTO 200\n"
        "30 DATA LOAD DC OPEN T\"NETU\"\n"
        "100 PRINT \"PERVYJ\"\n"
        "110 STOP\n"
        "200 PRINT \"VTOROJ\"\n";

    std::string screen, error;
    if (!run_text(src, screen, error)) { std::printf("  %s\n", error.c_str()); CHECK(false); return; }
    CHECK_STR(line_of(screen, 1), "VTOROJ");
}

// «При использовании ON ERROR-THEN при возврате происходит переход к
// оператору, следующему за тем, в котором произошла ошибка» (разд. 11.6).
void test_onerror_then()
{
    const char * src =
        "10 DIM E$4,N$4\n"
        "20 ON ERROR E$,N$THEN 100\n"
        "30 DATA LOAD DC OPEN T\"NETU\":PRINT \"POSLE\"\n"
        "40 STOP\n"
        "100 PRINT \"OBRABOTKA \";E$\n"
        "110 RETURN\n";

    std::string screen, error;
    if (!run_text(src, screen, error)) { std::printf("  %s\n", error.c_str()); CHECK(false); return; }
    CHECK_STR(line_of(screen, 1), "OBRABOTKA 73");
    CHECK_STR(line_of(screen, 2), "POSLE");         // ошибочный оператор пропущен
}

// «При использовании ON ERROR-GOSUB происходит переход к оператору, при
// выполнении которого произошла ошибка» — то есть он повторяется.
void test_onerror_gosub_retries()
{
    const char * src =
        "10 DIM E$4,N$4,F$8\n"
        "20 F$=\"NETU\"\n"
        "30 ON ERROR E$,N$GOSUB 100\n"
        "40 DATA LOAD DC OPEN T F$\n"
        "50 PRINT \"OTKRYT\"\n"
        "60 STOP\n"
        "100 PRINT \"POVTOR \";E$\n"
        "110 F$=\"NUMS\"\n"
        "120 RETURN\n";

    std::string screen, error;
    if (!run_text(src, screen, error)) { std::printf("  %s\n", error.c_str()); CHECK(false); return; }
    CHECK_STR(line_of(screen, 1), "POVTOR 73");
    CHECK_STR(line_of(screen, 2), "OTKRYT");        // со второй попытки удалось
}

// Ограничение эмулятора — не ошибка машины, и ON ERROR его не ловит: иначе
// нереализованное молча превратилось бы в «сбой ввода-вывода».
void test_onerror_does_not_hide_limits()
{
    std::string screen, error;
    const char * src =
        "10 ON ERROR GOTO 100\n"
        "20 DATA LOAD DC OPEN T\"ANKETA\"\n"
        "30 DATA LOAD DC A,B\n"          // первое значение записи символьное
        "100 PRINT \"POJMANO\"\n";
    CHECK(!run_text(src, screen, error));
}

// Та же программа в обеих формах.
void test_onerror_tokens()
{
    TokenBuilder b;
    b.add_numeric_var();                                   // 00: A
    b.add_numeric_var();                                   // 01: B

    // 34 05 | 2F 30 CD 01 00 — ON ERROR <v2F>,<v30> GOTO 100. Приёмники тут
    // числовые, поэтому их нет: форма без переменных короче.
    // 34 03 | CD 01 00 — ON ERROR GOTO 100
    static const int l10[] = { 0x34, 0x03, 0xCD, 0x01, 0x00 };
    b.add_line(10, l10, 5);
    // 75 07 | 02 E3 04 "NETU"
    static const int l20[] = { 0x75, 0x07, 0x02, 0xE3, 0x04, 'N', 'E', 'T', 'U' };
    b.add_line(20, l20, 9);
    static const int l30[] = { 0x42, 0x00 };               // STOP
    b.add_line(30, l30, 2);
    // 4C 07 | E3 05 "POJMA"
    static const int l100[] = { 0x4C, 0x07, 0xE3, 0x05, 'P', 'O', 'J', 'M', 'A' };
    b.add_line(100, l100, 9);

    Program prog;
    std::string error;
    if (!parse_tokenized(b.file(), prog, error)) {
        std::printf("  разбор: %s\n", error.c_str()); CHECK(false); return;
    }
    HeadlessHost host;
    if (!build_image(host)) { CHECK(false); return; }
    Interp interp(prog, host);
    if (!interp.run(error)) { std::printf("  %s\n", error.c_str()); CHECK(false); return; }
    const std::string tokens = host.dump();

    const char * src =
        "10 ON ERROR GOTO 100\n"
        "20 DATA LOAD DC OPEN T\"NETU\"\n"
        "30 STOP\n"
        "100 PRINT \"POJMA\"\n";
    std::string text;
    if (!run_text(src, text, error)) { std::printf("  %s\n", error.c_str()); CHECK(false); return; }

    CHECK_STR(line_of(tokens, 1), "POJMA");
    CHECK_STR(tokens, text);
}

void test_errors()
{
    std::string screen, error;
    // Чтение без открытия файла.
    CHECK(!run_text("10 DATA LOAD DC A,B\n", screen, error));
    // Файла нет в каталоге.
    CHECK(!run_text("10 DATA LOAD DC OPEN T\"NETU\"\n", screen, error));
    // Приёмнику не тот тип.
    CHECK(!run_text("10 DATA LOAD DC OPEN T\"ANKETA\"\n20 DATA LOAD DC A,B\n",
                    screen, error));
    // Приёмников больше, чем значений в записи.
    CHECK(!run_text("10 DATA LOAD DC OPEN T\"NUMS\"\n20 DATA LOAD DC A,B,C\n",
                    screen, error));
}

} // namespace

int main()
{
    test_open_and_read();
    test_skip();
    test_skip_end();
    test_limits();
    test_two_rows();
    test_tokens_match_text();
    test_onerror_goto();
    test_onerror_off();
    test_onerror_last_wins();
    test_onerror_then();
    test_onerror_gosub_retries();
    test_onerror_does_not_hide_limits();
    test_onerror_tokens();
    test_errors();
    return test::summary("чтение файлов данных");
}