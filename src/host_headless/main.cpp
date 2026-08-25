// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Mikhail Revzin <p3.141592653589793238462643@gmail.com>
// Part of the Iskra-226 project: https://github.com/Ptr314/Iskra-226
// Description: точка входа хоста без окна

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

// Единственное место во всём эмуляторе, где нужна платформа: диалоговый режим
// в терминале должен знать, показывает ли терминал набранное сам.
#ifdef _WIN32
#  include <io.h>
#else
#  include <unistd.h>
#endif

#include "core/names.h"
#include "core/tokenize.h"
#include "core/interp.h"
#include "core/koi8.h"
#include "font/font.h"
#include "host_headless/headless_host.h"

#include "core/catalog.h"
#include "core/console.h"
#include "core/detokenize.h"
#include "host_common/disk_args.h"

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
        "  iskra --chart [8|14|16]   таблица кодов КОИ-8; без числа — 7x8\n"
        "  iskra --list ОБРАЗ        каталог образа дискеты\n"
        "  iskra --cat ОБРАЗ ИМЯ     листинг программы с образа\n"
        "  iskra --detok ФАЙЛ        листинг ранее извлечённого файла\n"
        "  iskra --run ОБРАЗ ИМЯ [ВВОД…]  исполнить программу с образа\n"
        "  iskra --run-text ФАЙЛ [ВВОД…]  исполнить текстовый листинг\n"
        "  iskra --console [ОБРАЗ]   диалоговый режим в терминале\n"
        "\nКлючи:\n"
        "  -i                        пропускать машинозависимые операторы\n"
        "                            (ASMB, $GIO) вместо остановки\n"
        "\nКлючи диалогового режима:\n");
    out(iskra::DiskArgs::help());
    out("\n\nЗапись на дискету в диалоговом режиме идёт прямо в файл образа.\n");
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
    // Без числа — достоверный знакогенератор; с числом — крупный.
    const iskra::Font * f = height ? iskra::Font::by_height(height)
                                   : &iskra::Font::standard();
    if (!f) {
        std::printf("нет знакогенератора высотой %u (есть 8, 14, 16)\n", height);
        return 1;
    }

    std::printf("знакогенератор %ux%u в знакоместе %ux%u, коды 20-FF\n\n",
                f->width(), f->height(), f->cell_width(), f->cell_height());
    for (unsigned base = 0x20; base < 0x100; base += 0x10) {
        std::printf("      ");
        for (unsigned i = 0; i < 16; ++i) std::printf("%02X       ", base + i);
        std::printf("\n");
        for (unsigned y = 0; y < f->height(); ++y) {
            std::printf("      ");
            for (unsigned i = 0; i < 16; ++i) {
                const unsigned char code = static_cast<unsigned char>(base + i);
                for (unsigned x = 0; x < f->width(); ++x)
                    std::printf("%c", f->dot(code, x, y) ? '#' : '.');
                std::printf(" ");
            }
            std::printf("\n");
        }
        std::printf("\n");
    }
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

// Образ дискеты «Искры» — плоский файл: 1001 сектор по 256 байт, без
// контейнера и без чередования. Никакой распаковки не требуется, поэтому
// командная строка ходит на диск тем же путём, что и сам эмулятор:
// монтирует байты в хост и работает через Catalog.
bool mount_disk(iskra::HeadlessHost & host, const char * path)
{
    std::string raw;
    if (!read_file_bytes(path, raw)) {
        std::printf("не удалось открыть %s\n", path);
        return false;
    }
    if (raw.size() < iskra::Host::SECTOR_SIZE ||
        raw.size() % iskra::Host::SECTOR_SIZE) {
        std::printf("%s не похож на образ: размер не кратен 256 байтам\n", path);
        return false;
    }
    host.mount(0, std::vector<uint8_t>(raw.begin(), raw.end()));
    return true;
}

// Файл целиком по записи каталога.
bool read_disk_file(iskra::HeadlessHost & host, const iskra::CatalogEntry & e,
                    std::vector<uint8_t> & data)
{
    data.assign(static_cast<std::size_t>(e.sectors()) * iskra::Host::SECTOR_SIZE, 0);
    for (unsigned i = 0; i < e.sectors(); ++i)
        if (!host.disk_read(0, e.first + i, &data[i * iskra::Host::SECTOR_SIZE]))
            return false;
    return true;
}

// Листинг программы: разбор образа и обратная трансляция — те же, что и в
// эмуляторе (core/detokenize.*).
bool listing_of(const std::vector<uint8_t> & file, std::string & utf8)
{
    iskra::ProgramImage img;
    std::string error;
    if (!img.load_file(file, error)) {
        std::printf("разбор: %s\n", error.c_str());
        return false;
    }
    // Имена придумывает сама детокенизация: в потоке их нет.
    iskra::NameTable names;
    std::string whole;
    iskra::detokenize(img, names, whole, error);

    // Дальше — построчно: одна строка, которую детокенизатор ещё не умеет,
    // не должна съедать весь листинг. Причина при этом видна на месте.
    utf8.clear();
    for (unsigned i = 0; i < img.line_count(); ++i) {
        std::string koi8;
        std::string err;
        if (!iskra::detokenize_line(img.line(i), names, koi8, err)) {
            char b[32];
            std::sprintf(b, "%u", img.line(i).number);
            utf8 += std::string(b) + " ??? " + err + "\n";
            continue;
        }
        utf8 += iskra::koi8_to_utf8(reinterpret_cast<const uint8_t *>(koi8.data()),
                                    static_cast<unsigned>(koi8.size()));
        utf8 += '\n';
    }
    return true;
}

int cmd_list(const char * path)
{
    iskra::HeadlessHost host;
    if (!mount_disk(host, path)) return 1;

    iskra::Catalog cat(host, 0);
    std::string err;
    if (!cat.open(err)) {
        std::printf("%s\n", err.c_str());
        return 1;
    }
    std::printf("секторов %u, указатель %u, область до %u, занято до %u\n\n",
                host.disk_sectors(0), cat.index_sectors(), cat.area_end(),
                cat.current_end());

    std::vector<iskra::CatalogEntry> files;
    if (!cat.list(files, true, err)) {
        std::printf("%s\n", err.c_str());
        return 1;
    }

    for (unsigned i = 0; i < files.size(); ++i) {
        const iskra::CatalogEntry & e = files[i];
        // Имена бывают кириллицей, поэтому дополняем по числу символов,
        // а не байтов: %-10s в UTF-8 считает байты и ломает столбцы.
        const std::string koi8 = e.name_str();
        out(pad(koi8.empty() ? std::string()
                             : iskra::koi8_to_utf8(
                                   reinterpret_cast<const uint8_t *>(koi8.data()),
                                   static_cast<unsigned>(koi8.size())), 10));
        std::printf(" %5u %5u %5u  %s%s\n", e.first, e.last, e.sectors(),
                    e.is_program() ? "программа" : "данные",
                    e.scratched() ? ", вычеркнут" : "");
    }
    std::printf("\nвсего записей: %u\n", static_cast<unsigned>(files.size()));
    return 0;
}

// Детокенизация файла, снятого с диска раньше: так гоняется корпус.
int cmd_detok(const char * path)
{
    std::string raw;
    if (!read_file_bytes(path, raw)) {
        std::printf("не удалось открыть %s\n", path);
        return 1;
    }
    std::string utf8;
    if (!listing_of(std::vector<uint8_t>(raw.begin(), raw.end()), utf8)) return 1;
    out(utf8);
    return 0;
}

// Найти файл в каталоге по имени. Имя с командной строки приходит в UTF-8.
bool find_by_name(iskra::HeadlessHost & host, const char * name,
                  iskra::CatalogEntry & e)
{
    std::string koi8;
    iskra::utf8_to_koi8(name, koi8);

    iskra::Catalog cat(host, 0);
    uint8_t nm[iskra::NAME_LEN];
    iskra::Catalog::make_name(koi8, nm);

    std::string err;
    if (!cat.find(nm, e, err)) {
        std::printf("%s\n", err.c_str());
        return false;
    }
    if (!e.alive()) {
        std::printf("файла «%s» нет в каталоге\n", name);
        return false;
    }
    return true;
}

int cmd_cat(const char * path, const char * name)
{
    iskra::HeadlessHost host;
    if (!mount_disk(host, path)) return 1;

    iskra::CatalogEntry e;
    if (!find_by_name(host, name, e)) return 1;

    std::vector<uint8_t> data;
    if (!read_disk_file(host, e, data)) {
        std::printf("не читается файл\n");
        return 1;
    }
    std::string utf8;
    if (!listing_of(data, utf8)) return 1;
    out(utf8);
    return 0;
}

// Исполнение программы на хосте без окна. Строки ввода подаются аргументами
// командной строки — прогон получается воспроизводимым.
// Ключ `-i`: машинозависимые операторы (`ASMB`, `$GIO`) пропускать, а не
// останавливаться на них. Живёт глобально — его понимают все команды.
bool g_skip_machine = false;

int run_program(iskra::ProgramImage & img, iskra::HeadlessHost & host,
                char ** input, int inputs)
{
    for (int i = 0; i < inputs; ++i) {
        std::string koi8;
        iskra::utf8_to_koi8(input[i], koi8);
        koi8 += '\r';
        host.feed_keys(reinterpret_cast<const uint8_t *>(koi8.data()),
                       static_cast<unsigned>(koi8.size()));
    }

    iskra::Interp interp(img, host);
    interp.set_skip_machine(g_skip_machine);
    // Прогон пакетный: очередь нажатий задана заранее и не пополнится, а
    // `KEYIN` при пустой клавиатуре крутится вечно — это правильно для
    // машины, но здесь ждать некому. В окне и в диалоге ограничения нет.
    interp.set_max_steps(2000000);
    std::string error;
    const bool ok = interp.run(error);

    out(host.dump());
    if (!ok) {
        std::printf("\nостановлено: %s\n", error.c_str());
        return 1;
    }
    return 0;
}

// Запуск из файла, снятого с диска раньше: так гоняется корпус.
int cmd_run_file(const char * path, char ** input, int inputs)
{
    std::string raw;
    if (!read_file_bytes(path, raw)) {
        std::printf("не удалось открыть %s\n", path);
        return 1;
    }
    const std::vector<uint8_t> data(raw.begin(), raw.end());

    iskra::ProgramImage img;
    std::string error;
    if (!img.load_file(data, error)) {
        std::printf("разбор: %s\n", error.c_str());
        return 1;
    }
    iskra::HeadlessHost host;
    return run_program(img, host, input, inputs);
}

int cmd_run(const char * path, const char * name, char ** input, int inputs)
{
    iskra::HeadlessHost host;
    if (!mount_disk(host, path)) return 1;

    iskra::CatalogEntry e;
    if (!find_by_name(host, name, e)) return 1;

    std::vector<uint8_t> data;
    if (!read_disk_file(host, e, data)) {
        std::printf("не читается файл\n");
        return 1;
    }

    iskra::ProgramImage img;
    std::string error;
    if (!img.load_file(data, error)) {
        std::printf("разбор: %s\n", error.c_str());
        return 1;
    }
    // Образ смонтирован копией в памяти: SAVE DC во время прогона на файл
    // не попадёт. Так и задумано — корпус портить нельзя.
    return run_program(img, host, input, inputs);
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

    // Текстовый файл машина транслирует при загрузке; исполняется образ.
    iskra::ProgramImage img;
    iskra::NameTable names;
    std::string error;
    if (!iskra::tokenize(koi8, img, names, error)) {
        std::printf("трансляция: %s\n", error.c_str());
        return 1;
    }
    iskra::HeadlessHost host;
    return run_program(img, host, input, inputs);
}

// Диалоговый режим в терминале — временная замена окну. Настоящий экран
// придёт с хостом на SDL2 (docs/DECISIONS.md, разд. 1); здесь же строки
// читаются со стандартного ввода, а на стандартный вывод уходят те строки
// экрана, которые с прошлого раза изменились. Прокрутка при этом
// перепечатывает весь экран — так и должно быть, ведь изменились все строки.
class TermHost : public iskra::HeadlessHost
{
public:
    TermHost() : shown_(iskra::SCREEN_ROWS), pos_(0), open_(NONE),
                 interactive_(stdin_is_terminal()), printed_(false) {}

    // Диалог работает с настоящими файлами образов, а не с копией в памяти:
    // иначе всё записанное за сеанс пропадало бы при выходе. У образа в
    // памяти остаётся своё место — автотесты, где прогон должен быть
    // воспроизводимым и ничего на диске не трогать.
    iskra::DiskFiles & disks() { return disks_; }

    unsigned disk_sectors(unsigned drive) const { return disks_.sectors(drive); }
    bool disk_read(unsigned drive, unsigned sector, uint8_t * buf)
    { return disks_.read(drive, sector, buf); }
    bool disk_write(unsigned drive, unsigned sector, const uint8_t * buf)
    { return disks_.write(drive, sector, buf); }

    bool wait_key(uint8_t & code)
    {
        while (pos_ >= pending_.size())
            if (!next_line()) return false;
        code = static_cast<uint8_t>(pending_[pos_++]);
        return true;
    }

    bool present()
    {
        if (screen().take_cleared()) {
            // Стереть уже напечатанное терминал не даст; отбиваем пустой
            // строкой и печатаем всё заново.
            if (open_ != NONE) { out("\n"); open_ = NONE; }
            shown_.assign(iskra::SCREEN_ROWS, std::string());
            if (printed_) out("\n");
        }
        const std::string all = dump();
        std::size_t p = 0;
        for (unsigned r = 0; r < iskra::SCREEN_ROWS && p <= all.size(); ++r) {
            std::size_t e = all.find('\n', p);
            if (e == std::string::npos) e = all.size();
            const std::string row = all.substr(p, e - p);
            p = e + 1;
            if (row == shown_[r]) continue;

            // Строка с приглашением остаётся незакрытой: набранное ляжет
            // сразу за двоеточием, как на машине. Дописанное к ней печатаем
            // хвостом — а если ввод шёл с клавиатуры, не печатаем вовсе:
            // терминал уже показал набранное сам.
            if (r == open_ && !shown_[r].empty() &&
                row.compare(0, shown_[r].size(), shown_[r]) == 0) {
                if (!interactive_) out(row.substr(shown_[r].size()));
                shown_[r] = row;
                if (r + 1 == screen().row()) { std::fflush(stdout); continue; }
                out("\n");
                open_ = NONE;
                continue;
            }

            if (open_ != NONE) { out("\n"); open_ = NONE; }
            shown_[r] = row;
            if (row.empty()) continue;      // опустевшей строке печатать нечего

            out(row);
            printed_ = true;
            if (r + 1 == screen().row()) { open_ = r; std::fflush(stdout); }
            else out("\n");
        }
        screen().clear_dirty();
        return true;
    }

private:
    // Терминал отдаёт ввод строками; машине они нужны нажатиями, и строку
    // завершает ВК — тот же код, что даёт клавиша CR/LF.
    bool next_line()
    {
        char buf[512];
        if (!std::fgets(buf, sizeof(buf), stdin)) return false;
        std::string utf8 = buf;
        while (!utf8.empty() &&
               (utf8[utf8.size() - 1] == '\n' || utf8[utf8.size() - 1] == '\r'))
            utf8.resize(utf8.size() - 1);
        pending_.clear();
        iskra::utf8_to_koi8(utf8, pending_);
        pending_ += '\r';
        pos_ = 0;
        return true;
    }

    // Набранное с клавиатуры терминал показывает сам, а из файла или конвейера
    // — нет, и тогда строку печатаем мы.
    static bool stdin_is_terminal()
    {
#ifdef _WIN32
        return _isatty(_fileno(stdin)) != 0;
#else
        return isatty(fileno(stdin)) != 0;
#endif
    }

    static const unsigned NONE = 0xFFFFFFFFu;

    iskra::DiskFiles disks_;
    std::vector<std::string> shown_;
    std::string pending_;
    std::size_t pos_;
    unsigned open_;          // строка экрана, чья строка терминала не закрыта
    bool interactive_;
    bool printed_;           // было ли что печатать до сих пор
};

int cmd_console(const iskra::DiskArgs & mounts)
{
    TermHost host;
    std::string error;
    if (!mounts.apply(host.disks(), error)) {
        std::printf("%s\n", error.c_str());
        return 1;
    }

    iskra::ProgramImage img;
    iskra::Console con(img, host);
    con.interp().set_skip_machine(g_skip_machine);
    if (!con.run(error)) {
        std::printf("%s\n", error.c_str());
        return 1;
    }
    return 0;
}

} // namespace

int main(int argc, char ** argv)
{
    // `-i` может стоять где угодно: выбираем его до разбора команды,
    // остальные аргументы остаются на своих местах.
    std::vector<char *> rest;
    rest.push_back(argv[0]);
    for (int i = 1; i < argc; ++i) {
        if (std::string(argv[i]) == "-i") { g_skip_machine = true; continue; }
        rest.push_back(argv[i]);
    }
    argv = &rest[0];
    argc = static_cast<int>(rest.size());

    if (argc < 2) return usage();

    const std::string cmd = argv[1];

    if (cmd == "--screen") return cmd_screen();

    if (cmd == "--chart") {
        unsigned h = 0;                 // 0 — достоверный знакогенератор
        if (argc > 2) h = static_cast<unsigned>(std::atoi(argv[2]));
        return cmd_chart(h);
    }

    if (cmd == "--list" && argc > 2) return cmd_list(argv[2]);
    if (cmd == "--cat" && argc > 3) return cmd_cat(argv[2], argv[3]);
    if (cmd == "--detok" && argc > 2) return cmd_detok(argv[2]);
    if (cmd == "--run" && argc > 3) return cmd_run(argv[2], argv[3], argv + 4, argc - 4);
    if (cmd == "--run-file" && argc > 2) return cmd_run_file(argv[2], argv + 3, argc - 3);
    if (cmd == "--run-text" && argc > 2) return cmd_run_text(argv[2], argv + 3, argc - 3);
    if (cmd == "--console") {
        std::vector<std::string> args;
        for (int i = 2; i < argc; ++i) args.push_back(argv[i]);

        iskra::DiskArgs mounts;
        for (std::size_t i = 0; i < args.size(); ++i) {
            bool handled = false;
            std::string error;
            if (!mounts.take(args, i, handled, error)) {
                std::printf("%s\n", error.c_str());
                return 1;
            }
            if (handled) continue;
            if (!args[i].empty() && args[i][0] == '-') {
                std::printf("неизвестный ключ: %s\n", args[i].c_str());
                return 1;
            }
            mounts.set_default(args[i]);
        }
        return cmd_console(mounts);
    }

    return usage();
}