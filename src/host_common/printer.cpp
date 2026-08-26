// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Mikhail Revzin <p3.141592653589793238462643@gmail.com>
// Part of the Iskra-226 project: https://github.com/Ptr314/Iskra-226
// Description: АЦПУ, ФАУ 0C — лента и её файл

#include "host_common/printer.h"

#include "core/koi8.h"
#include "host_common/fileio.h"

namespace iskra {

Printer::Printer() : f_(0) {}

Printer::~Printer() { close(); }

void Printer::close()
{
    if (f_) { std::fclose(f_); f_ = 0; }
}

bool Printer::open(const std::string & path_utf8, std::string & error)
{
    close();
    // Дозапись, а не перезапись: лента не перематывается.
    f_ = open_utf8(path_utf8.c_str(), "ab");
    if (!f_) {
        error = "не открыть файл ленты: " + path_utf8;
        return false;
    }
    return true;
}

void Printer::put(uint8_t ch)
{
    tape_.push_back(ch);
    if (!f_) return;

    // Знаки старшей половины переводятся поштучно: перевод строки машина
    // ставит сама (`0D 0A`), и в файле он остаётся как есть.
    if (ch < 0x80) {
        std::fputc(ch, f_);
    } else {
        const std::string s = koi8_to_utf8(&ch, 1);
        if (!s.empty()) std::fwrite(s.data(), 1, s.size(), f_);
    }
    std::fflush(f_);
}

std::string Printer::utf8() const
{
    if (tape_.empty()) return std::string();
    return koi8_to_utf8(&tape_[0], static_cast<unsigned>(tape_.size()));
}

} // namespace iskra
