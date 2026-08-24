// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Mikhail Revzin <p3.141592653589793238462643@gmail.com>
// Part of the Iskra-226 project: https://github.com/Ptr314/Iskra-226
// Description: открытие файла по имени в UTF-8 на любой из систем

#pragma once

#include <cstdio>

#ifdef _WIN32
#include <string>
#include <vector>
#endif

namespace iskra {

// Имена файлов внутри эмулятора живут в UTF-8. На Unix их так и отдают ядру,
// а Windows принимает узкие имена в кодовой странице системы — и кириллица
// через fopen() туда не проходит. Поэтому единственное место, где это
// расходится, вынесено сюда.
std::FILE * open_utf8(const char * path, const char * mode);
bool remove_utf8(const char * path);

#ifdef _WIN32
// Широкая строка для системных вызовов Windows: заголовок окна, имя файла.
// Своя, а не через MultiByteToWideChar, — чтобы не тащить windows.h в общую
// часть ради десятка строк.
void utf8_to_utf16(const char * s, std::vector<wchar_t> & out);

// И обратно: аргументы командной строки Windows отдаёт широкими, а внутри
// эмулятора всё в UTF-8. Через них проходят и имена файлов с кириллицей.
void utf16_to_utf8(const wchar_t * s, std::string & out);
#endif

} // namespace iskra
