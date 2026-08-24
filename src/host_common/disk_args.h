// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Mikhail Revzin <p3.141592653589793238462643@gmail.com>
// Part of the Iskra-226 project: https://github.com/Ptr314/Iskra-226
// Description: ключи подключения образов — общее для окна и терминала

#pragma once

#include <string>
#include <vector>

#include "host_common/disk_files.h"

namespace iskra {

// Разбор ключей `--d0 ОБРАЗ`…`--d3 ОБРАЗ` и `--r0`…`--r3`. Одинаков у окна и
// у терминала, поэтому живёт здесь, а не в двух `main.cpp`.
//
// Дисководы соответствуют адресам, которые «Искра» знает в операторе SELECT
// (`DeviceTable::drive_index`): 0 — 18F, 1 — 18R, 2 — 1CF, 3 — 1CR.
//
// Разбор отделён от подстановки нарочно: `--r0` может стоять и до `--d0`,
// а запрет записи накладывается только на уже подставленный образ.
class DiskArgs
{
public:
    DiskArgs();

    // Разбирает аргумент args[i], при нужде забирая и следующий (тогда i
    // сдвигается). handled = false значит «ключ не наш». Возврат false —
    // ключ наш, но с ним беда, и error объясняет какая.
    bool take(const std::vector<std::string> & args, std::size_t & i,
              bool & handled, std::string & error);

    // Образ, названный без ключа. Идёт в дисковод 0, если `--d0` не было.
    void set_default(const std::string & path);

    // Подставить всё разобранное разом.
    bool apply(DiskFiles & disks, std::string & error) const;

    bool empty() const;

    // Строки для подсказки по ключам, без завершающего перевода строки.
    static const char * help();

private:
    std::string path_[DiskFiles::DRIVES];
    bool ro_[DiskFiles::DRIVES];
};

} // namespace iskra
