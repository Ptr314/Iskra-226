// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Mikhail Revzin <p3.141592653589793238462643@gmail.com>
// Part of the Iskra-226 project: https://github.com/Ptr314/Iskra-226
// Description: открытие файла по имени в UTF-8 на любой из систем

#include "host_common/fileio.h"

#ifdef _WIN32
#include <cstdint>
#endif

namespace iskra {

#ifdef _WIN32

void utf8_to_utf16(const char * s, std::vector<wchar_t> & out)
{
    out.clear();
    for (const unsigned char * p = reinterpret_cast<const unsigned char *>(s);
         *p; ) {
        uint32_t u;
        unsigned extra;
        if (*p < 0x80)      { u = *p;        extra = 0; }
        else if (*p < 0xE0) { u = *p & 0x1F; extra = 1; }
        else if (*p < 0xF0) { u = *p & 0x0F; extra = 2; }
        else                { u = *p & 0x07; extra = 3; }
        ++p;
        for (unsigned k = 0; k < extra && *p; ++k, ++p)
            u = (u << 6) | (*p & 0x3F);

        if (u >= 0x10000) {                       // суррогатная пара
            u -= 0x10000;
            out.push_back(static_cast<wchar_t>(0xD800 + (u >> 10)));
            out.push_back(static_cast<wchar_t>(0xDC00 + (u & 0x3FF)));
        } else {
            out.push_back(static_cast<wchar_t>(u));
        }
    }
    out.push_back(0);
}

void utf16_to_utf8(const wchar_t * s, std::string & out)
{
    out.clear();
    for (; *s; ++s) {
        uint32_t u = static_cast<uint32_t>(*s) & 0xFFFF;
        if (u >= 0xD800 && u < 0xDC00 && s[1] >= 0xDC00 && s[1] < 0xE000) {
            u = 0x10000 + ((u - 0xD800) << 10) +
                ((static_cast<uint32_t>(s[1]) & 0xFFFF) - 0xDC00);
            ++s;
        }
        if (u < 0x80) {
            out += static_cast<char>(u);
        } else if (u < 0x800) {
            out += static_cast<char>(0xC0 | (u >> 6));
            out += static_cast<char>(0x80 | (u & 0x3F));
        } else if (u < 0x10000) {
            out += static_cast<char>(0xE0 | (u >> 12));
            out += static_cast<char>(0x80 | ((u >> 6) & 0x3F));
            out += static_cast<char>(0x80 | (u & 0x3F));
        } else {
            out += static_cast<char>(0xF0 | (u >> 18));
            out += static_cast<char>(0x80 | ((u >> 12) & 0x3F));
            out += static_cast<char>(0x80 | ((u >> 6) & 0x3F));
            out += static_cast<char>(0x80 | (u & 0x3F));
        }
    }
}

std::FILE * open_utf8(const char * path, const char * mode)
{
    std::vector<wchar_t> wpath, wmode;
    utf8_to_utf16(path, wpath);
    utf8_to_utf16(mode, wmode);
    return _wfopen(&wpath[0], &wmode[0]);
}

bool remove_utf8(const char * path)
{
    std::vector<wchar_t> wpath;
    utf8_to_utf16(path, wpath);
    return _wremove(&wpath[0]) == 0;
}

#else

std::FILE * open_utf8(const char * path, const char * mode)
{
    return std::fopen(path, mode);
}

bool remove_utf8(const char * path)
{
    return std::remove(path) == 0;
}

#endif

} // namespace iskra
