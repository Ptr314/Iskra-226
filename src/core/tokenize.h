// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Mikhail Revzin <p3.141592653589793238462643@gmail.com>
// Part of the Iskra-226 project: https://github.com/Ptr314/Iskra-226
// Description: трансляция текстовой формы в оттранслированную

#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "core/names.h"
#include "core/program.h"

namespace iskra {

// «Интерпретатор осуществляет пошаговый перевод (трансляцию) операторов»
// (руководство, разд. 1.2). Текстовый файл переводится в тот же вид, что
// лежит в памяти машины и в `T`-файле на диске — см. docs/DECISIONS.md,
// разд. 12.
//
// Имена переменных живут только в текстовой форме; индексы раздаются в
// порядке первого появления (docs/format.md, разд. 6), поэтому таблица имён
// общая на всю программу и передаётся снаружи.

// Одна строка: «<номер> <операторы>». Тело — без номера и без длины.
bool tokenize_line(const std::string & koi8_line, NameTable & names,
                   unsigned & number, std::vector<uint8_t> & body,
                   std::string & error);

// Весь текст программы: строки разделяются байтом 85 (как на дискете) либо
// переводом строки. Таблицы переменных пока не строятся — только поток
// строк; это следующий шаг.
bool tokenize(const std::string & koi8, ProgramImage & out, NameTable & names,
              std::string & error);

} // namespace iskra