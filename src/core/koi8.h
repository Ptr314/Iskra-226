// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Mikhail Revzin <p3.141592653589793238462643@gmail.com>
// Part of the Iskra-226 project: https://github.com/Ptr314/Iskra-226
// Description: перекодировка КОИ-8 в UTF-8

#pragma once

#include <cstdint>
#include <string>

namespace iskra {

// Внутри эмулятора текст живёт в КОИ-8, как на самой машине. Перекодировка
// нужна только на границе: дампы экрана, имена файлов, заголовок окна.
uint32_t koi8_to_unicode(uint8_t code);

void koi8_to_utf8(const uint8_t * data, unsigned len, std::string & out);
std::string koi8_to_utf8(const uint8_t * data, unsigned len);

// Обратное направление нужно на входе: листинги корпуса лежат в UTF-8, а
// внутри эмулятора всё в КОИ-8. Непредставимый символ даёт false, но
// перекодировка идёт до конца, подставляя вопросительный знак.
bool utf8_to_koi8(const std::string & in, std::string & out);

} // namespace iskra