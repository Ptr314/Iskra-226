// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Mikhail Revzin <p3.141592653589793238462643@gmail.com>
// Part of the Iskra-226 project: https://github.com/Ptr314/Iskra-226
// Description: значение — число либо строка символов

#pragma once

#include <string>

#include "core/number.h"

namespace iskra {

// Значение выражения: число либо строка символов в КОИ-8. Оно же — элемент
// логической записи файла данных, поэтому лежит отдельно от интерпретатора.
struct Value {
    Value() : is_str(false) {}
    bool is_str;
    Number num;
    std::string str;
};

} // namespace iskra