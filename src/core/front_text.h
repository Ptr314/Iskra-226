// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Mikhail Revzin <p3.141592653589793238462643@gmail.com>
// Part of the Iskra-226 project: https://github.com/Ptr314/Iskra-226
// Description: текстовая форма программы → промежуточное представление

#pragma once

#include <string>
#include <vector>

#include "core/ir.h"

namespace iskra {

// Имена переменных хранятся только в текстовой форме; в оттранслированной
// от переменной остаётся индекс. Чтобы оба представления давали одинаковое
// промежуточное представление, текстовый разбор раздаёт индексы в порядке
// первого появления имени — правило из docs/format.md, разд. 6.
class NameTable
{
public:
    unsigned index(const std::string & name);
    unsigned count() const { return static_cast<unsigned>(names_.size()); }
    const std::string & name(unsigned i) const { return names_[i]; }

    // Тип переменной виден прямо из имени: ¤ — символьная, % — целая.
    // Заполняется при первом появлении имени, дальше уточняется из DIM.
    const std::vector<VarInfo> & vars() const { return vars_; }
    std::vector<VarInfo> & vars() { return vars_; }

private:
    std::vector<std::string> names_;
    std::vector<VarInfo> vars_;
};

// Текст программы в КОИ-8: строки разделяются байтом 85 (как на дискете)
// либо переводом строки.
bool parse_text(const std::string & koi8, Program & prog, NameTable & names,
                std::string & error);

} // namespace iskra