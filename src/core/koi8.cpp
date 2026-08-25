// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Mikhail Revzin <p3.141592653589793238462643@gmail.com>
// Part of the Iskra-226 project: https://github.com/Ptr314/Iskra-226
// Description: перекодировка КОИ-8 в UTF-8

#include "core/koi8.h"

namespace iskra {

namespace {

// Верхняя половина КОИ-8 (ГОСТ 19768-74 в варианте KOI8-R). На «Искре»
// используется только кириллица C0-FF; диапазон 80-BF занят псевдографикой
// и на экран машины не попадает, но перекодировку держим полной.
const uint16_t UPPER[128] = {
    0x2500, 0x2502, 0x250C, 0x2510, 0x2514, 0x2518, 0x251C, 0x2524, // 80
    0x252C, 0x2534, 0x253C, 0x2580, 0x2584, 0x2588, 0x258C, 0x2590,
    0x2591, 0x2592, 0x2593, 0x2320, 0x25A0, 0x2219, 0x221A, 0x2248, // 90
    0x2264, 0x2265, 0x00A0, 0x2321, 0x00B0, 0x00B2, 0x00B7, 0x00F7,
    0x2550, 0x2551, 0x2552, 0x0451, 0x2553, 0x2554, 0x2555, 0x2556, // A0
    0x2557, 0x2558, 0x2559, 0x255A, 0x255B, 0x255C, 0x255D, 0x255E,
    0x255F, 0x2560, 0x2561, 0x0401, 0x2562, 0x2563, 0x2564, 0x2565, // B0
    0x2566, 0x2567, 0x2568, 0x2569, 0x256A, 0x256B, 0x256C, 0x00A9,
    0x044E, 0x0430, 0x0431, 0x0446, 0x0434, 0x0435, 0x0444, 0x0433, // C0
    0x0445, 0x0438, 0x0439, 0x043A, 0x043B, 0x043C, 0x043D, 0x043E,
    0x043F, 0x044F, 0x0440, 0x0441, 0x0442, 0x0443, 0x0436, 0x0432, // D0
    0x044C, 0x044B, 0x0437, 0x0448, 0x044D, 0x0449, 0x0447, 0x044A,
    0x042E, 0x0410, 0x0411, 0x0426, 0x0414, 0x0415, 0x0424, 0x0413, // E0
    0x0425, 0x0418, 0x0419, 0x041A, 0x041B, 0x041C, 0x041D, 0x041E,
    0x041F, 0x042F, 0x0420, 0x0421, 0x0422, 0x0423, 0x0416, 0x0412, // F0
    0x042C, 0x042B, 0x0417, 0x0428, 0x042D, 0x0429, 0x0427, 0x042A
};

} // namespace

uint32_t koi8_to_unicode(uint8_t code)
{
    // Единственное отличие нижней половины от ASCII: в позиции 0x24
    // «Искра» высвечивает ¤, а не доллар.
    if (code == 0x24) return 0x00A4;
    if (code < 0x80) return code;
    return UPPER[code - 0x80];
}

bool unicode_to_koi8(uint32_t cp, uint8_t & code)
{
    if (cp < 0x80) { code = static_cast<uint8_t>(cp); return true; }
    if (cp == 0x00A4) { code = 0x24; return true; }              // ¤
    for (unsigned k = 0; k < 128; ++k)
        if (UPPER[k] == cp) {
            code = static_cast<uint8_t>(0x80 + k);
            return true;
        }
    code = '?';
    return false;
}

void koi8_to_utf8(const uint8_t * data, unsigned len, std::string & out)
{
    for (unsigned i = 0; i < len; ++i) {
        const uint32_t u = koi8_to_unicode(data[i]);
        if (u < 0x80) {
            out += static_cast<char>(u);
        } else if (u < 0x800) {
            out += static_cast<char>(0xC0 | (u >> 6));
            out += static_cast<char>(0x80 | (u & 0x3F));
        } else {
            out += static_cast<char>(0xE0 | (u >> 12));
            out += static_cast<char>(0x80 | ((u >> 6) & 0x3F));
            out += static_cast<char>(0x80 | (u & 0x3F));
        }
    }
}

bool utf8_to_koi8(const std::string & in, std::string & out)
{
    out.clear();
    out.reserve(in.size());
    bool ok = true;

    for (std::size_t i = 0; i < in.size(); ) {
        const unsigned char c = static_cast<unsigned char>(in[i]);
        uint32_t u;
        unsigned extra;

        if (c < 0x80)        { u = c;          extra = 0; }
        else if (c < 0xE0)   { u = c & 0x1F;   extra = 1; }
        else if (c < 0xF0)   { u = c & 0x0F;   extra = 2; }
        else                 { u = c & 0x07;   extra = 3; }

        if (i + extra >= in.size() + 1) { out += '?'; ok = false; break; }
        ++i;
        for (unsigned k = 0; k < extra; ++k) {
            u = (u << 6) | (static_cast<unsigned char>(in[i]) & 0x3F);
            ++i;
        }

        uint8_t code = 0;
        if (!unicode_to_koi8(u, code)) ok = false;
        out += static_cast<char>(code);
    }
    return ok;
}

uint8_t koi8_upper(uint8_t code)
{
    // В КОИ-8 регистр различает бит 0x20, но у кириллицы наоборот
    // относительно латиницы: строчные лежат ниже (C0-DF), прописные выше.
    if (code >= 'a' && code <= 'z') return static_cast<uint8_t>(code - 0x20);
    if (code >= 0xC0 && code < 0xE0) return static_cast<uint8_t>(code + 0x20);
    return code;
}

uint8_t koi8_to_koi7(uint8_t code)
{
    code = koi8_upper(code);
    // Подчёркивание машине показать нечем: в позиции 5F у неё Ъ. Оставляем
    // как есть — что напечатано, то и высветится, — а вот Ъ из КОИ-8 (FF)
    // надо перевести на его настоящее место.
    if (code == 0xFF) return 0x5F;
    return static_cast<uint8_t>(code & 0x7F);
}

uint8_t koi7_to_koi8(uint8_t code)
{
    code = static_cast<uint8_t>(code & 0x7F);
    if (code == 0x5F) return 0xFF;                          // Ъ
    if (code == 0x7F) return 0x20;                          // пусто
    if (code >= 0x60) return static_cast<uint8_t>(0xE0 + code - 0x60);
    return code;
}

std::string koi8_to_utf8(const uint8_t * data, unsigned len)
{
    std::string s;
    s.reserve(len * 2);
    koi8_to_utf8(data, len, s);
    return s;
}

} // namespace iskra