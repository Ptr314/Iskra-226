// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Mikhail Revzin <p3.141592653589793238462643@gmail.com>
// Part of the Iskra-226 project: https://github.com/Ptr314/Iskra-226
// Description: точка входа хоста без окна

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include "core/front_text.h"
#include "core/front_tokens.h"
#include "core/interp.h"
#include "core/koi8.h"
#include "font/font.h"
#include "host_headless/headless_host.h"

#ifdef ISKRA_WITH_DSK_TOOLS
#include "diskio/image.h"
#endif

namespace {

void out(const std::string & s)
{
    std::fwrite(s.data(), 1, s.size(), stdout);
}

// Дополнение строки пробелами по числу символов UTF-8, а не байтов.
std::string pad(const std::string & s, unsigned width)
{
    unsigned chars = 0;
    for (std::size_t i = 0; i < s.size(); ++i)
        if ((s[i] & 0xC0) != 0x80) ++chars;

    std::string r = s;
    while (chars++ < width) r += ' ';
    return r;
}

int usage()
{
    out("Искра-226 — хост без окна\n\n"
        "  iskra --screen            проверка экрана и знакогенератора\n"
        "  iskra --chart [8|14|16]   таблица кодов КОИ-8 знакогенератором\n"
#ifdef ISKRA_WITH_DSK_TOOLS
        "  iskra --list ОБРАЗ        каталог образа дискеты\n"
        "  iskra --cat ОБРАЗ ИМЯ     листинг программы с образа\n"
        "  iskra --detok ФАЙЛ        листинг ранее извлечённого файла\n"
        "  iskra --run ОБРАЗ ИМЯ [ВВОД…]  исполнить программу с образа\n"
        "  iskra --run-text ФАЙЛ [ВВОД…]  исполнить текстовый листинг\n"
#else
        "\nСобрано без dsk_tools: работа с образами дискет недоступна.\n"
#endif
        );
    return 1;
}

// Экран и знакогенератор: печатаем всё, что умеет Screen, и смотрим глазами.
int cmd_screen()
{
    iskra::HeadlessHost host;
    iskra::Screen & s = host.screen();

    const char * title = "\x03";                 // КТ — очистка экрана
    s.write(reinterpret_cast<const uint8_t *>(title), 1);

    s.at(1, 30);
    const char * t1 = "\x12 \xE9\xF3\xEB\xF2\xE1 226 \x11";  // СУ2 ИСКРА 226 СУ1
    s.write(reinterpret_cast<const uint8_t *>(t1), std::strlen(t1));

    s.at(3, 1);
    const char * t2 = "\xF3\xF4\xF2\xEF\xEB\xE1 3, \xF0\xEF\xFA\xE9\xE3\xE9\xF1 1";
    s.write(reinterpret_cast<const uint8_t *>(t2), std::strlen(t2));

    // Символьная переменная: знак 0x24 должен выглядеть как ¤, не как $.
    s.at(5, 1);
    const char * t3 = "10 A$ = \"\xF4\xE5\xF3\xF4\"";
    s.write(reinterpret_cast<const uint8_t *>(t3), std::strlen(t3));

    // Перенос за 80-й позицией.
    s.at(7, 75);
    const char * t4 = "1234567890";
    s.write(reinterpret_cast<const uint8_t *>(t4), std::strlen(t4));

    // AT(10,20,15) — стирание пятнадцати позиций, пример 17.6 из книги.
    s.at(10, 1);
    const char * t5 = "0123456789012345678901234567890123456789";
    s.write(reinterpret_cast<const uint8_t *>(t5), std::strlen(t5));
    s.at(10, 20);
    s.erase(15);

    // Прокрутка: печать ниже 24-й строки.
    s.at(24, 1);
    const char * t6 = "\xF0\xEF\xF3\xEC\xE5\xE4\xEE\xF1\xF1 \xF3\xF4\xF2\xEF\xEB\xE1";
    s.write(reinterpret_cast<const uint8_t *>(t6), std::strlen(t6));

    out(host.dump());
    std::printf("\nкурсор: строка %u, позиция %u; звонков: %u\n",
                s.row(), s.col(), s.take_bells());
    return 0;
}

// Таблица кодов растром знакогенератора: колонки — старшая цифра кода.
int cmd_chart(unsigned height)
{
    const iskra::Font * f = iskra::Font::by_height(height);
    if (!f) {
        std::printf("нет знакогенератора высотой %u (есть 8, 14, 16)\n", height);
        return 1;
    }

    std::printf("знакогенератор 8x%u, коды 20-FF\n\n", f->height());
    for (unsigned base = 0x20; base < 0x100; base += 0x10) {
        std::printf("      ");
        for (unsigned i = 0; i < 16; ++i) std::printf("%02X       ", base + i);
        std::printf("\n");
        for (unsigned y = 0; y < f->height(); ++y) {
            std::printf("      ");
            for (unsigned i = 0; i < 16; ++i) {
                const unsigned char bits = f->glyph(static_cast<unsigned char>(base + i))[y];
                for (int x = 7; x >= 0; --x)
                    std::printf("%c", (bits & (1 << x)) ? '#' : '.');
                std::printf(" ");
            }
            std::printf("\n");
        }
        std::printf("\n");
    }
    return 0;
}

#ifdef ISKRA_WITH_DSK_TOOLS

int cmd_list(const char * path)
{
    iskra::DiskImage img;
    if (!img.open(path)) {
        std::printf("%s\n", img.error().c_str());
        return 1;
    }

    std::printf("формат %s, секторов %u\n\n",
                img.format_id().c_str(), img.sector_count());

    std::vector<iskra::DiskFile> files;
    if (!img.dir(files)) {
        std::printf("%s\n", img.error().c_str());
        return 1;
    }

    for (unsigned i = 0; i < files.size(); ++i) {
        // Имена бывают кириллицей, поэтому дополняем по числу символов,
        // а не байтов: %-10s в UTF-8 считает байты и ломает столбцы.
        out(pad(files[i].name, 10));
        std::printf(" %8u  ", files[i].size);
        out(pad(files[i].type, 6));
        std::printf("%s\n", files[i].is_protected ? "защищён" : "");
    }
    std::printf("\nвсего файлов: %u\n", static_cast<unsigned>(files.size()));
    return 0;
}

// Детокенизация файла, снятого с диска раньше: так гоняется корпус.
int cmd_detok(const char * path)
{
    std::FILE * f = std::fopen(path, "rb");
    if (!f) {
        std::printf("не удалось открыть %s\n", path);
        return 1;
    }
    std::vector<uint8_t> data;
    uint8_t buf[4096];
    std::size_t n;
    while ((n = std::fread(buf, 1, sizeof(buf), f)) > 0)
        data.insert(data.end(), buf, buf + n);
    std::fclose(f);

    out(iskra::detokenize(data));
    return 0;
}

bool read_file_bytes(const char * path, std::string & out)
{
    std::FILE * f = std::fopen(path, "rb");
    if (!f) return false;
    char buf[4096];
    std::size_t n;
    while ((n = std::fread(buf, 1, sizeof(buf), f)) > 0) out.append(buf, n);
    std::fclose(f);
    return true;
}

// Исполнение программы на хосте без окна. Строки ввода подаются аргументами
// командной строки — прогон получается воспроизводимым.
int run_program(const iskra::Program & prog, char ** input, int inputs)
{
    iskra::HeadlessHost host;
    for (int i = 0; i < inputs; ++i) {
        std::string koi8;
        iskra::utf8_to_koi8(input[i], koi8);
        koi8 += '\r';
        host.feed_keys(reinterpret_cast<const uint8_t *>(koi8.data()),
                       static_cast<unsigned>(koi8.size()));
    }

    iskra::Interp interp(prog, host);
    std::string error;
    const bool ok = interp.run(error);

    out(host.dump());
    if (!ok) {
        std::printf("\nостановлено: %s\n", error.c_str());
        return 1;
    }
    return 0;
}

int cmd_run(const char * path, const char * name, char ** input, int inputs)
{
    iskra::DiskImage img;
    if (!img.open(path)) {
        std::printf("%s\n", img.error().c_str());
        return 1;
    }
    std::vector<uint8_t> data;
    if (!img.read_file(name, data)) {
        std::printf("%s\n", img.error().c_str());
        return 1;
    }

    iskra::Program prog;
    std::string error;
    if (!iskra::parse_tokenized(data, prog, error)) {
        std::printf("разбор: %s\n", error.c_str());
        return 1;
    }
    return run_program(prog, input, inputs);
}

// Тот же прогон, но из текстового листинга. Файлы корпуса лежат в UTF-8,
// внутри эмулятора всё в КОИ-8.
int cmd_run_text(const char * path, char ** input, int inputs)
{
    std::string utf8;
    if (!read_file_bytes(path, utf8)) {
        std::printf("не удалось открыть %s\n", path);
        return 1;
    }
    std::string koi8;
    iskra::utf8_to_koi8(utf8, koi8);

    iskra::Program prog;
    iskra::NameTable names;
    std::string error;
    if (!iskra::parse_text(koi8, prog, names, error)) {
        std::printf("разбор: %s\n", error.c_str());
        return 1;
    }
    return run_program(prog, input, inputs);
}

int cmd_cat(const char * path, const char * name)
{
    iskra::DiskImage img;
    if (!img.open(path)) {
        std::printf("%s\n", img.error().c_str());
        return 1;
    }

    std::string text;
    if (!img.listing(name, text)) {
        std::printf("%s\n", img.error().c_str());
        return 1;
    }
    out(text);
    return 0;
}

#endif // ISKRA_WITH_DSK_TOOLS

} // namespace

int main(int argc, char ** argv)
{
    if (argc < 2) return usage();

    const std::string cmd = argv[1];

    if (cmd == "--screen") return cmd_screen();

    if (cmd == "--chart") {
        unsigned h = 16;
        if (argc > 2) h = static_cast<unsigned>(std::atoi(argv[2]));
        return cmd_chart(h);
    }

#ifdef ISKRA_WITH_DSK_TOOLS
    if (cmd == "--list" && argc > 2) return cmd_list(argv[2]);
    if (cmd == "--cat" && argc > 3) return cmd_cat(argv[2], argv[3]);
    if (cmd == "--detok" && argc > 2) return cmd_detok(argv[2]);
    if (cmd == "--run" && argc > 3) return cmd_run(argv[2], argv[3], argv + 4, argc - 4);
    if (cmd == "--run-text" && argc > 2) return cmd_run_text(argv[2], argv + 3, argc - 3);
#endif

    return usage();
}