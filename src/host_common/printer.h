// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Mikhail Revzin <p3.141592653589793238462643@gmail.com>
// Part of the Iskra-226 project: https://github.com/Ptr314/Iskra-226
// Description: АЦПУ, ФАУ 0C — лента и её файл

#pragma once

#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

namespace iskra {

// Приёмник печати. Всё, что ушло на АЦПУ, копится здесь; если задан файл,
// байты уходят в него сразу же — как и запись на дискету, не дожидаясь
// выхода: программа на машине может уронить эмулятор, и терять из-за этого
// напечатанное незачем.
//
// В файл лента пишется в UTF-8: читать её будет человек, а не машина —
// обратного пути в эмулятор у неё нет. Внутри лента хранится как пришла,
// в КОИ-8.
class Printer
{
public:
    Printer();
    ~Printer();

    // Открыть файл ленты. Дозаписью: два прогона подряд ложатся один за
    // другим, как на настоящей бумаге.
    bool open(const std::string & path_utf8, std::string & error);
    bool to_file() const { return f_ != 0; }
    void close();

    void put(uint8_t ch);

    bool empty() const { return tape_.empty(); }
    const std::vector<uint8_t> & tape() const { return tape_; }

    // Лента в UTF-8.
    std::string utf8() const;

private:
    std::vector<uint8_t> tape_;
    std::FILE * f_;

    Printer(const Printer &);
    Printer & operator=(const Printer &);
};

} // namespace iskra
