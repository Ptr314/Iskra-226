// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Mikhail Revzin <p3.141592653589793238462643@gmail.com>
// Part of the Iskra-226 project: https://github.com/Ptr314/Iskra-226
// Description: хост без окна — для автотестов

#pragma once

#include <string>
#include <vector>

#include "core/host.h"

namespace iskra {

// Хост без графики и без зависимостей: экран выгружается текстом,
// нажатия подаются заранее заготовленной строкой. На нём гоняются
// автотесты и сверка «текст против токенов».
class HeadlessHost : public Host
{
public:
    HeadlessHost();

    Screen & screen() { return screen_; }
    bool present() { screen_.clear_dirty(); return true; }

    bool poll_key(uint8_t & code);
    bool key_was_special() const { return special_; }
    // Ждать тут нечего: очередь задана заранее, и пустая очередь — это конец
    // сценария, а не «пока не нажали».
    bool wait_key(uint8_t & code) { return poll_key(code); }
    unsigned disk_sectors(unsigned drive) const;
    bool disk_read(unsigned drive, unsigned sector, uint8_t * buf);
    bool disk_write(unsigned drive, unsigned sector, const uint8_t * buf);
    void print_char(uint8_t ch);
    uint32_t ticks_ms() const { return ticks_; }

    // Очередь нажатий, которую разбирает poll_key().
    // Клавиша специальных функций: тот же код, но `KEYIN` уводит её
    // на другую строку.
    void feed_special_key(uint8_t code);

    void feed_keys(const uint8_t * codes, unsigned len);

    // Время идёт только тогда, когда его двигают: прогоны воспроизводимы.
    void advance_ms(uint32_t ms) { ticks_ += ms; }

    // Содержимое экрана в UTF-8: 24 строки, хвостовые пробелы срезаны.
    std::string dump() const;

    // Всё, что ушло на АЦПУ, в UTF-8.
    std::string printer() const;

    // Подставить образ дискеты: содержимое целиком, сектора по 256 байт.
    void mount(unsigned drive, const std::vector<uint8_t> & data);

    // Образ обратно — чтобы проверить, что записалось, и перенести его на
    // другой хост.
    const std::vector<uint8_t> & image(unsigned drive) const
    {
        return disks_[drive < 2 ? drive : 0];
    }

private:
    Screen screen_;
    std::vector<uint8_t> keys_;
    std::vector<uint8_t> keys_sf_;      // признак «клавиша спецфункции»
    bool special_;
    unsigned key_pos_;
    std::vector<uint8_t> printer_;
    std::vector<uint8_t> disks_[2];
    uint32_t ticks_;
};

} // namespace iskra