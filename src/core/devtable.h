// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Mikhail Revzin <p3.141592653589793238462643@gmail.com>
// Part of the Iskra-226 project: https://github.com/Ptr314/Iskra-226
// Description: таблица устройств — то, чем управляет оператор SELECT

#pragma once

#include <cstdint>

namespace iskra {

// Таблица устройств — собственное состояние машины, а не выдумка эмулятора:
// программа видит её оператором `LIST%` и читает второй формой `LIMITS`
// (руководство, разд. 11.5 и 18.8.3). Поэтому «дескриптор файла» здесь —
// не выданный системой номер, а строка этой таблицы, которую программа
// выбирает сама оператором `SELECT #n`.

// Коды групп — первый байт операндов глагола `54` (docs/format.md, разд. 4).
// Парами «текст + токены» подтверждены только `07` PRINT, `0A` DISK,
// `05` P и `00` #n; `06`, `08`, `09`, `0C` опознаны косвенно, по перечню
// групп в книге (разд. 11.5), а `01` не опознан вовсе.
enum SelectCode {
    SC_ROW    = 0x00,   // #n — строка таблицы
    SC_UNKN   = 0x01,   // без параметров, назначение неизвестно
    SC_PAUSE  = 0x05,   // P<цифра>
    SC_LIST   = 0x06,
    SC_PRINT  = 0x07,
    SC_PLOT   = 0x08,
    SC_TAPE   = 0x09,
    SC_DISK   = 0x0A,
    SC_CO     = 0x0C,

    // `SELECT D/R/G` — единицы измерения углов (руководство, разд. 4.6).
    // Коды прочитаны в таблице ключевых слов интерпретатора
    // (`docs/format.md`, разд. 4); адреса за ними не идёт.
    SC_DEG    = 0x01,
    SC_RAD    = 0x02,
    SC_GRAD   = 0x03
};

// Единицы измерения углов.
enum AngleMode { ANG_RAD = 0, ANG_DEG = 1, ANG_GRAD = 2 };

// Группы, для которых таблица хранит адрес и ширину.
enum DeviceGroup { DG_CI, DG_CO, DG_PRINT, DG_LIST, DG_TAPE, DG_PLOT, DG_COUNT };

// Код группы из потока → группа таблицы. false для `#n`, `P`, `DISK`
// (у них своя форма) и для неизвестных кодов.
bool group_of_code(unsigned code, DeviceGroup & g);

// Имя группы для сообщений об ошибках; для неизвестного кода — "?".
const char * group_name(unsigned code);

// Строка таблицы устройств: адрес устройства и, если строка связана с
// файлом, его границы и текущий сектор.
struct DeviceRow {
    DeviceRow() : addr(0), removable(false), bound(false),
                  first(0), current(0), last(0) {}

    uint8_t  addr;          // ФАУ: 18, 1C…
    bool     removable;     // R — сменный диск, F — несменный
    bool     bound;         // связана с файлом
    unsigned first, current, last;
};

class DeviceTable
{
public:
    static const unsigned ROWS = 8;         // #0…#7

    DeviceTable() { reset(); }

    // Состояние при включении и после CLEAR (руководство, разд. 11.5):
    // CI = 01, CO = 05(80), PRINT = 05(80), LIST = 05(80), TAPE = 08(256),
    // PLOT = 10, строка #0 — дисковод 18F.
    void reset();

    uint8_t  addr(DeviceGroup g) const { return groups_[g].addr; }
    unsigned width(DeviceGroup g) const { return groups_[g].width; }
    // width == 0 — ширину не менять.
    void select(DeviceGroup g, uint8_t addr, unsigned width);

    unsigned pause() const { return pause_; }
    void set_pause(unsigned p) { pause_ = p; }

    AngleMode angle() const { return angle_; }
    void set_angle(AngleMode a) { angle_ = a; }

    // `SELECT #n <адрес>[F|R]`.
    void select_row(unsigned n, uint8_t addr, bool removable);
    // `SELECT DISK <адрес>F|R` — то же, что `SELECT #0`: книга называет
    // строку #0 дисководом по умолчанию (разд. 18.10), отдельной строки
    // для дисков в выводе `LIST%` нет.
    void select_disk(uint8_t addr, bool removable) { select_row(0, addr, removable); }

    DeviceRow & row(unsigned n) { return rows_[n < ROWS ? n : 0]; }
    const DeviceRow & row(unsigned n) const { return rows_[n < ROWS ? n : 0]; }
    static bool valid_row(unsigned n) { return n < ROWS; }

    // Дисковод хоста для пары «ФАУ + F/R». У машины дисковых устройств
    // может быть несколько; в корпусе встречаются адреса 18 и 1C, поэтому
    // дисководов четыре: два адреса по два диска. Неизвестный адрес — false.
    static bool drive_index(uint8_t addr, bool removable, unsigned & out);

private:
    struct Group { uint8_t addr; unsigned width; };

    Group groups_[DG_COUNT];
    DeviceRow rows_[ROWS];
    unsigned pause_;
    AngleMode angle_;
};

} // namespace iskra