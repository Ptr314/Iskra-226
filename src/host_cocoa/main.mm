// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Mikhail Revzin <p3.141592653589793238462643@gmail.com>
// Part of the Iskra-226 project: https://github.com/Ptr314/Iskra-226
// Description: точка входа хоста с окном под macOS

#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

#include "core/console.h"
#include "core/koi8.h"
#include "core/names.h"
#include "core/program.h"
#include "core/tokenize.h"
#include "core/version.h"
#include "host_common/disk_args.h"
#include "host_common/fileio.h"
#include "host_cocoa/cocoa_host.h"

namespace {

// Прочитать файл целиком. Имена файлов внутри эмулятора живут в UTF-8, и на
// Unix они так и уходят ядру — открывается всё тем же общим open_utf8.
bool read_text_file(const std::string & path, std::string & out)
{
    std::FILE * f = iskra::open_utf8(path.c_str(), "rb");
    if (!f) return false;
    char buf[4096];
    std::size_t n;
    while ((n = std::fread(buf, 1, sizeof(buf), f)) > 0) out.append(buf, n);
    std::fclose(f);
    return true;
}

// Беда с ключами и образами показывается в терминале, а не окном: окна к
// этому времени ещё нет, а запущено это чаще всего из терминала.
void usage()
{
    std::printf("Искра 226 — эмулятор Бейсика BASIC 02, версия %s\n\n"
    "iskra [ОБРАЗ] [--dN ОБРАЗ] [--rN] [--text ЛИСТИНГ] [--scale N] [-i]\n"
    "\n"
    "ОБРАЗ — плоский образ дискеты «Искры»: сектора по 256 байт подряд.\n"
    "Названный без ключа, он идёт в дисковод 0 — тот же, что --d0.\n"
    "\n%s\n", iskra::version(), iskra::DiskArgs::help());

    std::printf(
    "\n  --printer ФАЙЛ           увести ленту АЦПУ ещё и в файл,\n"
    "                           дозаписью и в UTF-8. Само окно ленты\n"
    "                           открывается по первому же напечатанному\n"
    "                           знаку\n"
    "  --text ЛИСТИНГ           положить текстовую программу в память,\n"
    "                           как если бы её набрали с клавиатуры\n"
    "  --scale N                целое увеличение кадра 560x256. Без\n"
    "                           ключа берётся наибольшее, влезающее в экран\n"
    "  -i                       пропускать то, чего здесь нет: ASMB,\n"
    "                           $GIO и вывод на устройство, которого у\n"
    "                           хоста нет, — вместо остановки. Чтение с\n"
    "                           такого устройства останавливает всё равно\n"
    "\nЗапись на дискету идёт прямо в файл образа.\n"
    "\nКлавиатура «Искры» разложена по обычной так:\n"
    "\n  Esc, F1…F12                          клавиши спецфункций 0…12\n"
    "  с Shift                             они же, 16…28\n"
    "  Ctrl+F1…F3                          дубли 13…15 — на Mac нет\n"
    "                                      PrtScr, ScrLk и Pause\n"
    "  ←  →                                курсор на позицию\n"
    "  Ctrl+←  Ctrl+→                      курсор на пять позиций\n"
    "  ↑  ↓                                курсор на строку экрана\n"
    "  Insert*, Delete (fn+⌫), End          INSERT, DELETE, ERASE\n"
    "  Ctrl+E, Ctrl+R                      EDIT, RECALL\n"
    "  ⌫, Ctrl+⌫                            BACKSPACE, LINE ERASE\n"
    "  Return                              CR/LF\n"
    "  Cmd+.                                HALT/STEP\n"
    "  Ctrl+Return                          CONTINUE\n"
    "  Cmd+Option+.                         RESET\n"
    "  Ctrl+N                               STMT NUMBER\n"
    "  Option+<клавиша>                     слово Бейсика верхнего\n"
    "                                      регистра: Option+J и Option+Й\n"
    "                                      дают FOR, Option+S — SELECT\n"
    "\n* клавиши Insert на клавиатуре Mac нет — она есть только на внешних\n"
    "  клавиатурах PC-раскладки.\n");
}

int run(const std::vector<std::string> & args, std::string & error)
{
    iskra::DiskArgs mounts;
    unsigned scale = 0;      // 0 — подобрать под экран
    bool skip_machine = false;
    std::string listing;
    std::string printer;

    for (std::size_t i = 0; i < args.size(); ++i) {
        bool handled = false;
        if (!mounts.take(args, i, handled, error)) return 2;
        if (handled) continue;

        const std::string & arg = args[i];
        if (arg == "--text" && i + 1 < args.size()) {
            listing = args[++i];
        } else if (arg == "--printer" && i + 1 < args.size()) {
            printer = args[++i];
        } else if (arg == "--scale" && i + 1 < args.size()) {
            scale = static_cast<unsigned>(std::atoi(args[++i].c_str()));
            if (!scale) scale = 1;
        } else if (arg == "-i") {
            // Пропускать то, чего здесь нет: `ASMB` и `$GIO` требуют
            // эмуляции процессора, у графического устройства `/10` не
            // разобрана часть управляющих кодов, а прочих устройств у хоста
            // нет вовсе. Вывод в такое устройство пропускается, а чтение
            // с него останавливает программу и с ключом.
            skip_machine = true;
        } else if (arg == "--help" || arg == "-h") {
            usage();
            return 0;
        } else if (arg.size() && arg[0] == '-') {
            error = "неизвестный ключ: " + arg;
            return 2;
        } else {
            mounts.set_default(arg);
        }
    }

    iskra::CocoaHost host;

    // Файл ленты открывается до окна: не открылся — говорим сразу, а не
    // посреди печати.
    if (!printer.empty() && !host.tape().open(printer, error)) return 1;

    // Образы подставляются до окна: тогда в заголовке видно, что в дисководе
    // 0, а беда с образом не оставляет после себя пустого окна.
    if (!mounts.apply(host.disks(), error)) return 1;

    std::string title = "Искра 226 ";
    title += iskra::version();
    title += " — BASIC 02";
    const std::string & mounted = host.disks().path(0);
    if (!mounted.empty()) {
        const std::string::size_type slash = mounted.find_last_of('/');
        title += " — ";
        title += slash == std::string::npos ? mounted : mounted.substr(slash + 1);
        if (!host.disks().writable(0)) title += " (только чтение)";
    }
    if (!host.open(title, scale, error)) return 1;

    iskra::ProgramImage img;
    iskra::Console console(img, host);
    console.interp().set_skip_machine(skip_machine);

    // Листинг, названный ключом `--text`, кладётся в память до приглашения —
    // ровно так же, как если бы его набрали с клавиатуры. Дальше `RUN`, и
    // это единственный способ прогнать в окне программу, которой нет на
    // дискете: файлового диалога у нас нет.
    if (!listing.empty()) {
        std::string utf8;
        if (!read_text_file(listing, utf8)) {
            error = "не удалось прочитать " + listing;
            return 1;
        }
        std::string koi8;
        iskra::utf8_to_koi8(utf8, koi8);
        if (!iskra::tokenize(koi8, img, console.names(), error)) {
            error = "трансляция: " + error;
            return 1;
        }
    }

    return console.run(error) ? 0 : 1;
}

} // namespace

int main(int argc, char ** argv)
{
    // На Unix аргументы приходят байтами, и это уже UTF-8: перекодировать
    // нечего. Ровно поэтому здесь нет широкой командной строки, какая нужна
    // Windows.
    std::vector<std::string> args;
    for (int i = 1; i < argc; ++i) args.push_back(argv[i]);

    std::string error;
    const int rc = run(args, error);
    if (rc && !error.empty()) std::fprintf(stderr, "%s\n", error.c_str());
    return rc;
}
