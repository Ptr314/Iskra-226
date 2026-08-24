// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Mikhail Revzin <p3.141592653589793238462643@gmail.com>
// Part of the Iskra-226 project: https://github.com/Ptr314/Iskra-226
// Description: таблица устройств — то, чем управляет оператор SELECT

#include "core/devtable.h"

namespace iskra {

bool group_of_code(unsigned code, DeviceGroup & g)
{
    switch (code) {
        case SC_CO:    g = DG_CO;    return true;
        case SC_PRINT: g = DG_PRINT; return true;
        case SC_LIST:  g = DG_LIST;  return true;
        case SC_TAPE:  g = DG_TAPE;  return true;
        case SC_PLOT:  g = DG_PLOT;  return true;
        default:       return false;
    }
}

const char * group_name(unsigned code)
{
    switch (code) {
        case SC_ROW:   return "#n";
        case SC_PAUSE: return "P";
        case SC_LIST:  return "LIST";
        case SC_PRINT: return "PRINT";
        case SC_PLOT:  return "PLOT";
        case SC_TAPE:  return "TAPE";
        case SC_DISK:  return "DISK";
        case SC_CO:    return "CO";
        case SC_TRIG:  return "D/R/G";
        default:       return "?";
    }
}

void DeviceTable::reset()
{
    // Значения из примера вывода LIST% в книге, разд. 11.5.
    groups_[DG_CI].addr    = 0x01; groups_[DG_CI].width    = 0;
    groups_[DG_CO].addr    = 0x05; groups_[DG_CO].width    = 80;
    groups_[DG_PRINT].addr = 0x05; groups_[DG_PRINT].width = 80;
    groups_[DG_LIST].addr  = 0x05; groups_[DG_LIST].width  = 80;
    groups_[DG_TAPE].addr  = 0x08; groups_[DG_TAPE].width  = 256;
    groups_[DG_PLOT].addr  = 0x10; groups_[DG_PLOT].width  = 0;

    for (unsigned i = 0; i < ROWS; ++i) rows_[i] = DeviceRow();
    // «Автоматически для файла с номером 0 определен дисковод 18F».
    rows_[0].addr = 0x18;
    rows_[0].removable = false;

    pause_ = 0;
    angle_ = ANG_RAD;          // по умолчанию радианы
}

void DeviceTable::select(DeviceGroup g, uint8_t addr, unsigned width)
{
    groups_[g].addr = addr;
    if (width) groups_[g].width = width;
}

void DeviceTable::select_row(unsigned n, uint8_t addr, bool removable)
{
    if (n >= ROWS) return;
    // Смена устройства строку не отвязывает от файла: адресная информация
    // остаётся до следующего открытия (руководство, разд. 18.10).
    rows_[n].addr = addr;
    rows_[n].removable = removable;
}

bool DeviceTable::drive_index(uint8_t addr, bool removable, unsigned & out)
{
    unsigned base;
    switch (addr) {
        case 0x18: base = 0; break;
        case 0x1C: base = 2; break;
        default: return false;
    }
    out = base + (removable ? 1u : 0u);
    return true;
}

} // namespace iskra