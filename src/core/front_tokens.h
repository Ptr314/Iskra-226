// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Mikhail Revzin <p3.141592653589793238462643@gmail.com>
// Part of the Iskra-226 project: https://github.com/Ptr314/Iskra-226
// Description: токенизированная форма программы → промежуточное представление

#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "core/ir.h"

namespace iskra {

// Разбор «оттранслированной» программы прямо из файла, снятого с дискеты.
// Формат описан в docs/format.md, раздел 3.
bool parse_tokenized(const std::vector<uint8_t> & file, Program & prog,
                     std::string & error);

// То же, но из уже собранного потока (без заголовочного сектора и служебных
// байтов) — удобно для проверок.
bool parse_tokenized_stream(const std::vector<uint8_t> & code, Program & prog,
                            std::string & error);

} // namespace iskra