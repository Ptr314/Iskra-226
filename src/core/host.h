// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Mikhail Revzin <p3.141592653589793238462643@gmail.com>
// Part of the Iskra-226 project: https://github.com/Ptr314/Iskra-226
// Description: граница между ядром и внешним миром

#pragma once

#include <cstdint>

#include "core/screen.h"

namespace iskra {

// Единственное, что ядро знает о внешнем мире. Реализации: host_headless
// (для автотестов, без зависимостей) и host_sdl2 (окно, он же WASM).
//
// Ядру не полагается включать ничего, кроме этого заголовка: ни SDL,
// ни чего бы то ни было стороннего.
class Host
{
public:
    // Сектор гибкого диска «Искры»: 2 служебных байта + 254 данных.
    static const unsigned SECTOR_SIZE = 256;

    virtual ~Host() {}

    // --- Символьная половина БОСГИ, ФАУ 05 -------------------------------
    virtual Screen & screen() = 0;

    // Показать накопленные изменения экрана и обработать события хоста.
    // Возвращает false, если пользователь закрыл окно.
    virtual bool present() = 0;

    // --- Клавиатура, ФАУ 01 ----------------------------------------------
    // Неблокирующее чтение: false, если нажатий не было.
    virtual bool poll_key(uint8_t & code) = 0;

    // Ждать нажатия. Ждёт хост, а не ядро: у окна это прокрутка событий, у
    // хоста без окна — заготовленная очередь, и false значит «ввода больше
    // не будет» (конец сценария, закрытое окно). Ядру в обоих случаях
    // остаётся одно и то же — прекратить чтение.
    virtual bool wait_key(uint8_t & code)
    {
        for (;;) {
            if (poll_key(code)) return true;
            if (!present()) return false;
        }
    }

    // --- НГМД, ФАУ 18 ----------------------------------------------------
    // Число секторов на устройстве; 0 — устройство отсутствует.
    virtual unsigned disk_sectors(unsigned drive) const = 0;
    virtual bool disk_read(unsigned drive, unsigned sector, uint8_t * buf) = 0;
    virtual bool disk_write(unsigned drive, unsigned sector, const uint8_t * buf) = 0;

    // --- АЦПУ, ФАУ 0C ----------------------------------------------------
    // По умолчанию печать никуда не идёт.
    virtual void print_char(uint8_t) {}

    // --- Прочее ----------------------------------------------------------
    // Миллисекунды с запуска: для оператора TIME и заставок.
    virtual uint32_t ticks_ms() const = 0;
};

} // namespace iskra