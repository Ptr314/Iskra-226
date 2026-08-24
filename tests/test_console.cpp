// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Mikhail Revzin <p3.141592653589793238462643@gmail.com>
// Part of the Iskra-226 project: https://github.com/Ptr314/Iskra-226
// Description: диалоговый режим — сеансы по сценарию, экран сверяется целиком

#include <string>
#include <vector>

#include "check.h"
#include "core/catalog.h"
#include "core/console.h"
#include "core/koi8.h"
#include "host_headless/headless_host.h"

using namespace iskra;

namespace {

// Экран дампится всеми 24 строками; пустой хвост под последней строкой
// диалога ничего не проверяет, и сверять его незачем.
std::string trim_tail(const std::string & dump)
{
    std::string::size_type e = dump.size();
    while (e >= 2 && dump[e - 1] == '\n' && dump[e - 2] == '\n') --e;
    return dump.substr(0, e);
}

// Сеанс: строки подаются как нажатия, каждая заканчивается ВК. Когда очередь
// кончается, диалог сам заканчивается — хост без окна ждать не умеет.
std::string session_on(HeadlessHost & host, const char * const * lines,
                       unsigned count)
{
    std::string keys;
    for (unsigned i = 0; i < count; ++i) {
        std::string koi8;
        utf8_to_koi8(lines[i], koi8);
        keys += koi8;
        keys += '\r';
    }
    host.feed_keys(reinterpret_cast<const uint8_t *>(keys.data()),
                   static_cast<unsigned>(keys.size()));

    ProgramImage img;
    Console con(img, host);
    std::string error;
    CHECK(con.run(error));
    CHECK_STR(error, "");
    return trim_tail(host.dump());
}

std::string session(const char * const * lines, unsigned count)
{
    HeadlessHost host;
    return session_on(host, lines, count);
}

// Пустая размеченная дискета в дисководе 0: указатель на пять секторов —
// кратным трём его брать нельзя, хеш имени тогда вырождается.
bool format_disk(HeadlessHost & host)
{
    const unsigned SECTORS = 200;
    host.mount(0, std::vector<uint8_t>(
        static_cast<std::size_t>(SECTORS) * Host::SECTOR_SIZE, 0));
    Catalog cat(host, 0);
    std::string err;
    if (!cat.format(5, SECTORS - 1, err)) {
        std::printf("  %s\n", err.c_str());
        return false;
    }
    return true;
}

// Ввод строк, замена, удаление и порядок выдачи: «текст программы всегда
// выводится в порядке возрастания номеров строк» (руководство, разд. 3.4).
void test_edit()
{
    const char * lines[] = {
        "30 PRINT \"ТРИ\"",
        "10 PRINT \"ОДИН\"",
        "20 PRINT \"ДВА\"",
        "20 PRINT \"ВТОРАЯ\"",
        "30",
        "LIST"
    };
    CHECK_STR(session(lines, sizeof(lines) / sizeof(lines[0])),
              "READY BASIC 02 05.10.84\n"
              ":30 PRINT \"ТРИ\"\n"
              ":10 PRINT \"ОДИН\"\n"
              ":20 PRINT \"ДВА\"\n"
              ":20 PRINT \"ВТОРАЯ\"\n"
              ":30\n"
              ":LIST\n"
              "10 PRINT \"ОДИН\"\n"
              "20 PRINT \"ВТОРАЯ\"\n"
              ":\n");
}

// Части листинга: LIST n, — с номера до конца; LIST ,n — с начала до номера.
void test_list_range()
{
    const char * lines[] = {
        "10 A=1",
        "20 A=2",
        "30 A=3",
        "LIST 20,",
        "LIST ,20"
    };
    CHECK_STR(session(lines, sizeof(lines) / sizeof(lines[0])),
              "READY BASIC 02 05.10.84\n"
              ":10 A=1\n"
              ":20 A=2\n"
              ":30 A=3\n"
              ":LIST 20,\n"
              "20 A=2\n"
              "30 A=3\n"
              ":LIST ,20\n"
              "10 A=1\n"
              "20 A=2\n"
              ":\n");
}

// RUN без номера обнуляет переменные, RUN с номером — сохраняет
// (руководство, разд. 4.1, пример 4.2). Между командами переменные живут:
// PRINT в режиме непосредственного счёта видит то, что оставил RUN.
void test_run()
{
    const char * lines[] = {
        "10 A=A+1",
        "20 PRINT A",
        "RUN",
        "RUN 10",
        "PRINT A*10"
    };
    CHECK_STR(session(lines, sizeof(lines) / sizeof(lines[0])),
              "READY BASIC 02 05.10.84\n"
              ":10 A=A+1\n"
              ":20 PRINT A\n"
              ":RUN\n"
              " 1\n"
              ":RUN 10\n"
              " 2\n"
              ":PRINT A*10\n"
              " 20\n"
              ":\n");
}

// CLEAR стирает и текст, и переменные, и экран: «экран и память машины
// очистятся, и на экране снова появится сообщение о готовности» (разд. 3.2).
// CLEAR P — только текст, значения переменных остаются.
void test_clear()
{
    const char * lines[] = {
        "10 PRINT 1",
        "A=5",
        "CLEAR P",
        "LIST",
        "PRINT A",
        "CLEAR",
        "PRINT A"
    };
    CHECK_STR(session(lines, sizeof(lines) / sizeof(lines[0])),
              "READY BASIC 02 05.10.84\n"
              ":PRINT A\n"
              " 0\n"
              ":\n");
}

// Неудачная строка не должна оставлять после себя выдуманных имён: индексы
// переменных раздаются по первому появлению, и сдвиг сломал бы всю программу.
void test_bad_line()
{
    const char * lines[] = {
        "10 B=1",
        "20 PRINT B*",
        "20 PRINT B+1",
        "RUN"
    };
    const std::string got = session(lines, sizeof(lines) / sizeof(lines[0]));
    CHECK(got.find(" 2\n") != std::string::npos);
    CHECK(got.find("20 PRINT B*") != std::string::npos);
}

// Запись программы на диск и чтение её обратно. «В режиме непосредственного
// счёта оператор LOAD DC только загружает программу в оперативную память»
// (руководство, разд. 19.1) — то есть не запускает её: строка 10 не печатает
// ничего, пока не наберут RUN.
void test_disk()
{
    HeadlessHost host;
    if (!format_disk(host)) { CHECK(false); return; }

    const char * lines[] = {
        "SELECT DISK 18F",
        "10 PRINT \"ПРИВЕТ\"",
        "20 A=7",
        "SAVE DC F \"ПРОБА\"",
        "CLEAR",
        "LIST",
        "LOAD DC F \"ПРОБА\"",
        "LIST",
        "RUN"
    };
    CHECK_STR(session_on(host, lines, sizeof(lines) / sizeof(lines[0])),
              "READY BASIC 02 05.10.84\n"
              ":LIST\n"
              ":LOAD DC F \"ПРОБА\"\n"
              ":LIST\n"
              "10 PRINT \"ПРИВЕТ\"\n"
              "20 A=7\n"
              ":RUN\n"
              "ПРИВЕТ\n"
              ":\n");
}

} // namespace

int main()
{
    test_edit();
    test_list_range();
    test_run();
    test_clear();
    test_bad_line();
    test_disk();
    return test::summary("test_console");
}
