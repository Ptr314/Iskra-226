// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Mikhail Revzin <p3.141592653589793238462643@gmail.com>
// Part of the Iskra-226 project: https://github.com/Ptr314/Iskra-226
// Description: графический буфер: заголовок, поток записей, вывод в растр

#pragma once

#include <string>

#include "core/raster.h"

namespace iskra {

// Буфер графики — **обычный символьный массив Бейсика**, а не память
// системы: `¤OPEN` объявляет массив буфером, `NPLOT`/`DRAW`/`DOT`/`LABEL`
// дописывают в него записи, `¤COPY /адрес` выкладывает всё разом на
// устройство. Разбор с доказательствами — `docs/format.md`, разд. 5,
// «Заголовок и поток графического буфера».
//
// Заголовок — 43 байта, поток начинается с 44-го:
//
//     1-2   длина занятого, считая сам заголовок; у пустого 43
//     3-4   текущая точка, X
//     5-6   текущая точка, Y
//     7-43  не установлены: ни одна программа корпуса их не трогает
//
// **Числа заголовка лежат младшим байтом вперёд**, а координаты потока —
// старшим. `SLIDE` эту разницу называет прямо: у него две подпрограммы
// перевода числа в два байта, `' 35` помечена «2 Б НОРМ.», а `' 36` —
// «2 Б ИНВ.», и заголовок ходит через инвертированную.
const unsigned GBUF_HEADER = 43;

// Коды записей потока. `SLIDE` 160 держит их имена таблицей по пять знаков,
// а строка 1400 индексирует её кодом из буфера: `VAL(STR(A¤(),J))-127`.
enum GOp {
    GOP_NPLOT = 0x80,
    GOP_NDRAW = 0x81,
    GOP_NLAB  = 0x82,
    GOP_DOT   = 0x84,
    GOP_DRAW  = 0x85,
    GOP_LABEL = 0x86
};

// Вид на поле символьной переменной, объявленное буфером. Своей памяти не
// держит: буфер живёт в переменной программы, и она вольна его читать и
// править сама — на этом стоит весь `SLIDE`.
class GBuffer
{
public:
    GBuffer(std::string & field, unsigned off, unsigned len)
        : d_(field), off_(off), len_(len) {}

    // Влезает ли хотя бы заголовок.
    bool fits() const { return len_ >= GBUF_HEADER; }

    // `¤OPEN`: записать пустой заголовок. Длина 43, точка 0,0 — ровно то,
    // что `SLIDE` 5180 кладёт руками: `HEX(2B0000000000)`.
    void open();

    unsigned used() const { return get16(0); }
    long x() const { return static_cast<long>(get16(2)); }
    long y() const { return static_cast<long>(get16(4)); }
    void set_point(long x, long y);

    // Дописать запись в конец потока и подвинуть длину.
    bool append(const uint8_t * rec, unsigned n, std::string & error);

    // Поток целиком: с 44-го байта и до конца занятого.
    const char * stream() const;
    unsigned stream_len() const;

    // Похоже ли содержимое на буфер: длина не меньше заголовка и не больше
    // поля. Нужна `¤COPY`, чтобы не выводить мусор из невскрытой переменной.
    bool looks_open() const;

private:
    unsigned get16(unsigned pos) const;         // младшим байтом вперёд
    void put16(unsigned pos, unsigned v);

    std::string & d_;
    unsigned off_;
    unsigned len_;
};

// Собрать запись точки: код и две координаты старшим байтом вперёд.
// Возвращает false, если координата не влезает в два байта.
bool gbuf_point_record(uint8_t op, long x, long y, uint8_t * out);

// Собрать запись надписи: код, длина (считая себя), четыре байта приращения
// точки, три байта признаков и текст. Приращение — семь дискрет на знак,
// как в живой картинке с образа (`docs/format.md`, разд. 5).
bool gbuf_label_record(const std::string & text, unsigned size,
                       uint8_t a2, uint8_t a3, std::string & out);

// Поток записей — в растр. Разбирается четвёрка, установленная на живой
// картинке: `NPLOT`, `DOT`, `DRAW`, `LABEL`. На прочих кодах — отказ:
// молча пропускать нельзя, картинка вышла бы неполной, а выглядела бы целой.
bool gbuf_draw(const char * stream, unsigned n, Raster & r, std::string & error);

} // namespace iskra
