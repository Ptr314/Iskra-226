// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Mikhail Revzin <p3.141592653589793238462643@gmail.com>
// Part of the Iskra-226 project: https://github.com/Ptr314/Iskra-226
// Description: верхний регистр зоны 1 — ключевые слова Бейсика по одной клавише

#include "host_common/keywords.h"

#include <string>

#include "core/koi8.h"

namespace iskra {

namespace {

// Знаки клавиши в UTF-8 (нижний и верхний регистры) и слово, которое она
// вводит. Порядок — как на рис. 2.2, по зонам.
//
// Три надписи на рисунке обрезаны шириной клавиши и дописаны по смыслу:
// `PRINTUS` → `PRINTUSING`, `RENUMB` → `RENUMBER`, `BACKSP` → `BACKSPACE`.
// Одну прочитать не удалось, и клавиши у неё здесь нет: `Д`/`D`, надпись
// `RES` обрезана. Выдумывать её незачем — клавиша просто не работает.
//
// Где знак клавиши совпадает с арифметическим знаком цифрового блока
// (`+`, `-`, `*`, `/`), взят только второй знак клавиши: цифровой блок
// отдан функциям зоны 5, и по знаку их не различить.
struct Entry {
    const char * chars;
    const char * word;
};

const Entry TABLE[] = {
    // Зона 1, буквы
    { "ЙJ", "FOR" },
    { "ЦC", "¤GIO" },
    { "УU", "GOSUB" },
    { "КK", "GOSUB'" },
    { "ЕE", "GOTO" },
    { "НN", "HEX(" },
    { "ГG", "HEXPRINT" },
    { "Ш[", "IF" },
    { "Щ]", "INIT" },
    { "ЗZ", "INPUT" },
    { "ХH", "INT(" },
    { "ФF", "NEXT" },
    { "ЫY", "ON" },
    { "ВW", "OR(" },
    { "АA", "PACK(" },
    { "ПP", "PRINTUSING" },
    { "РR", "READ" },
    { "ОO", "REM" },
    { "ЛL", "RENUMBER" },
    { "ЖV", "RESTORE" },
    { "Э\\", "RETURN" },
    { "ЯQ", "RND(" },
    { "Ч^", "ROTATE" },
    { "СS", "SELECT" },
    { "МM", "SKIP" },
    { "ИI", "SGN(" },
    { "ТT", "STEP" },
    { "ЬX", "STOP" },
    { "БB", "STR(" },
    { "Ю@", "TAB(" },
    // Зона 1, цифры и знаки
    { "1!", "AND(" },
    { "2\"", "BACKSPACE" },
    { "3#", "BIN(" },
    { "4$", "BOOL" },
    { "5%", "COM" },
    { "6&", "CONVERT" },
    { "7'", "DATA" },
    { "8(", "DEFFN" },
    { "9)", "DEFFN'" },
    { "0",  "DIM" },
    { ":",  "KEYIN" },
    { "=",  "END" },
    { ".>", "REWIND" },
    { ",<", "TEN" },
    { "?",  "TRACE" },
    { ";",  "ADD" },
    { "_",  "UNPACK" }
};

const unsigned COUNT = sizeof(TABLE) / sizeof(TABLE[0]);

} // namespace

const char * keyword_for_char(uint8_t koi8)
{
    if (!koi8) return 0;
    for (unsigned i = 0; i < COUNT; ++i) {
        std::string keys;
        utf8_to_koi8(TABLE[i].chars, keys);
        for (std::size_t k = 0; k < keys.size(); ++k)
            if (koi8_upper(static_cast<uint8_t>(keys[k])) == koi8)
                return TABLE[i].word;
    }
    return 0;
}

// Зона 5: «в верхнем регистре клавиши этой зоны служат для ввода
// обозначений стандартных математических функций Бейсика» (разд. 2.1).
// На рисунке их восемь: `ARC`, `TAN(`, `SIN(`, `COS(`, `EXP(`, `LOG(`,
// `SQR(`, `ABS(`. Первые четыре знаками не отличить от клавиш зоны 1, а
// байтов у тригонометрии всё равно нет (`docs/format.md`, разд. 5, «Опись»),
// так что клавиш у них здесь нет.
const char * keyword_for_pad(PadFunc kind)
{
    switch (kind) {
    case PAD_MUL: return "ABS(";
    case PAD_ADD: return "SQR(";
    case PAD_SUB: return "EXP(";
    case PAD_DIV: return "LOG(";
    }
    return 0;
}

} // namespace iskra
