// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Mikhail Revzin <p3.141592653589793238462643@gmail.com>
// Part of the Iskra-226 project: https://github.com/Ptr314/Iskra-226
// Description: логическая запись файла данных — чтение и запись

#pragma once

#include <string>
#include <vector>

#include "core/host.h"
#include "core/value.h"

namespace iskra {

// Устройство записи разобрано в docs/format.md, разд. 2. Сектор записи:
//
//     <код сектора> затем подряд <тип> <длина> <данные длиной «длина»>
//
// Значение, не помещающееся в сектор целиком, переносится в следующий
// (руководство, разд. 18.6). Хвост сектора после последнего значения —
// нули; признак конца значений — пара <тип> <длина> из двух нулей.

enum RecordSector {
    REC_SINGLE = 0x8B,      // запись целиком в одном секторе
    REC_FIRST  = 0x02,      // первый сектор многосекторной записи
    REC_MIDDLE = 0x8F,      // промежуточный
    REC_LAST   = 0x03,      // последний
    REC_END    = 0x1C       // концевая запись, DATA SAVE DC END
};

enum ValueType {
    VAL_NUM = 0x00,         // всегда длина 8
    VAL_STR = 0x40          // длина 1…253
};

// Байт кода сектора начинает запись?
bool is_record_start(unsigned code);

// Код сектора, не разбирая его. Нужен `DATA LOAD DC`, чтобы отличить
// концевую запись от обычной, не считая её ошибкой (разд. 18.5).
bool record_code(Host & host, unsigned drive, unsigned sector, unsigned & code);

// Читает запись, начинающуюся в секторе start. next — первый сектор за ней.
// Значения возвращаются в порядке записи; сколько из них нужно, решает
// вызывающий: числа значений в записи на диске нет, оператор DATA LOAD DC
// разбирает их по своему списку приёмников.
bool read_record(Host & host, unsigned drive, unsigned start,
                 std::vector<Value> & out, unsigned & next, std::string & err);

// Концевая запись: `1C <использовано секторов, 2 байта BE>`. used — счётчик.
bool read_end_record(Host & host, unsigned drive, unsigned sector,
                     unsigned & used);

// Сектор за концом записи — без разбора значений, только по кодам секторов.
bool record_end(Host & host, unsigned drive, unsigned start, unsigned & next,
                std::string & err);

// Ищет концевую запись в пределах файла. false — её в файле нет.
bool find_end_record(Host & host, unsigned drive, unsigned first, unsigned last,
                     unsigned & sector, unsigned & used);

// Начало записи, в которую попадает сектор s: шаг назад по кодам 8F и 03.
bool record_start(Host & host, unsigned drive, unsigned first, unsigned s,
                  unsigned & start);

// Пишет значения начиная с сектора start, не выходя за last (включительно).
// next — первый свободный сектор за записью.
bool write_record(Host & host, unsigned drive, unsigned start, unsigned last,
                  const std::vector<Value> & vals, unsigned & next,
                  std::string & err);

// Концевая запись в сектор `sector`; счётчик = его позиция от начала файла,
// считая с единицы (docs/format.md, разд. 2).
bool write_end_record(Host & host, unsigned drive, unsigned file_start,
                      unsigned sector, std::string & err);

} // namespace iskra