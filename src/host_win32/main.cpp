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

const char * USAGE =
    "iskra-win [ОБРАЗ] [--scale N]\n"
    "\n"
    "ОБРАЗ    — плоский образ дискеты «Искры»: сектора по 256 байт подряд.\n"
    "           Он подставляется в дисковод 0, то есть в SELECT DISK 18F.\n"
    "--scale N — целое увеличение окна при открытии, по умолчанию 2.";

int run(int argc, wchar_t ** argv, std::string & error)
{
    std::string image;
    unsigned scale = 2;

    for (int i = 1; i < argc; ++i) {
        std::string arg;
        iskra::utf16_to_utf8(argv[i], arg);
        if (arg == "--scale" && i + 1 < argc) {
            std::string n;
            iskra::utf16_to_utf8(argv[++i], n);
            scale = static_cast<unsigned>(std::atoi(n.c_str()));
            if (!scale) scale = 1;
        } else if (arg == "--help" || arg == "-h" || arg == "/?") {
            say(USAGE, "Искра 226", MB_ICONINFORMATION);
            return 0;
        } else if (arg.size() && arg[0] == '-') {
            error = "неизвестный ключ: " + arg;
            return 2;
        } else {
            image = arg;
        }
    }

    iskra::Win32Host host;

    std::string title = "Искра 226 — BASIC 02";
    if (!image.empty()) {
        const std::string::size_type slash = image.find_last_of("/\\");
        title += " — ";
        title += slash == std::string::npos ? image : image.substr(slash + 1);
    }
    if (!host.open(title, scale, error)) return 1;

    // Дисковод 0 — это адрес 18F: тот, с которого «Искра» и загружается.
    if (!image.empty() && !host.disks().mount(0, image.c_str(), error))
        return 1;

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
