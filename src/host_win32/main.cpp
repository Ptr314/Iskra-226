// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Mikhail Revzin <p3.141592653589793238462643@gmail.com>
// Part of the Iskra-226 project: https://github.com/Ptr314/Iskra-226
// Description: точка входа хоста с окном под Windows

#include <windows.h>
#include <shellapi.h>

#include <cstdlib>
#include <string>
#include <vector>

#include "core/console.h"
#include "core/koi8.h"
#include "core/program.h"
#include "host_common/disk_args.h"
#include "host_common/fileio.h"
#include "host_win32/win32_host.h"

namespace {

void say(const std::string & utf8_text, const std::string & utf8_title,
         UINT flags)
{
    std::vector<wchar_t> text, title;
    iskra::utf8_to_utf16(utf8_text.c_str(), text);
    iskra::utf8_to_utf16(utf8_title.c_str(), title);
    MessageBoxW(0, &text[0], &title[0], flags);
}

std::string usage()
{
    std::string s =
    "iskra-win [ОБРАЗ] [--dN ОБРАЗ] [--rN] [--scale N]\n"
    "\n"
    "ОБРАЗ — плоский образ дискеты «Искры»: сектора по 256 байт подряд.\n"
    "Названный без ключа, он идёт в дисковод 0 — тот же, что --d0.\n"
    "\n";
    s += iskra::DiskArgs::help();
    s += "\n  --scale N                увеличение по горизонтали; по вертикали\n"
         "                           вдвое больше, и окно выходит 4:3. Без\n"
         "                           ключа берётся наибольшее, влезающее в экран\n"
         "\nЗапись на дискету идёт прямо в файл образа.";
    return s;
}

int run(int argc, wchar_t ** argv, std::string & error)
{
    std::vector<std::string> args;
    for (int i = 1; i < argc; ++i) {
        std::string a;
        iskra::utf16_to_utf8(argv[i], a);
        args.push_back(a);
    }

    iskra::DiskArgs mounts;
    unsigned scale = 0;      // 0 — подобрать под экран

    for (std::size_t i = 0; i < args.size(); ++i) {
        bool handled = false;
        if (!mounts.take(args, i, handled, error)) return 2;
        if (handled) continue;

        const std::string & arg = args[i];
        if (arg == "--scale" && i + 1 < args.size()) {
            scale = static_cast<unsigned>(std::atoi(args[++i].c_str()));
            if (!scale) scale = 1;
        } else if (arg == "--help" || arg == "-h" || arg == "/?") {
            say(usage(), "Искра 226", MB_ICONINFORMATION);
            return 0;
        } else if (arg.size() && arg[0] == '-') {
            error = "неизвестный ключ: " + arg;
            return 2;
        } else {
            mounts.set_default(arg);
        }
    }

    iskra::Win32Host host;

    // Образы подставляются до окна: тогда в заголовке видно, что в дисководе
    // 0, а беда с образом не оставляет после себя пустого окна.
    if (!mounts.apply(host.disks(), error)) return 1;

    std::string title = "Искра 226 — BASIC 02";
    const std::string & mounted = host.disks().path(0);
    if (!mounted.empty()) {
        const std::string::size_type slash = mounted.find_last_of("/\\");
        title += " — ";
        title += slash == std::string::npos ? mounted : mounted.substr(slash + 1);
        if (!host.disks().writable(0)) title += " (только чтение)";
    }
    if (!host.open(title, scale, error)) return 1;

    iskra::ProgramImage img;
    iskra::Console console(img, host);
    return console.run(error) ? 0 : 1;
}

} // namespace

int WINAPI WinMain(HINSTANCE, HINSTANCE, LPSTR, int)
{
    // Аргументы берём широкими: узкие Windows отдаёт в кодовой странице
    // системы, и путь с кириллицей через них не проходит. Та же беда, что у
    // хоста без окна, — здесь её нет.
    int argc = 0;
    wchar_t ** argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    if (!argv) { say("не разобрать командную строку", "Искра 226", MB_ICONERROR); return 2; }

    std::string error;
    const int rc = run(argc, argv, error);
    LocalFree(argv);

    if (rc && !error.empty()) say(error, "Искра 226", MB_ICONERROR);
    return rc;
}
