// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Mikhail Revzin <p3.141592653589793238462643@gmail.com>
// Part of the Iskra-226 project: https://github.com/Ptr314/Iskra-226
// Description: образы дискет в файлах — общее для всех оконных хостов

#pragma once

#include <cstdio>
#include <string>
#include <vector>

#include "core/host.h"

namespace iskra {

// Образ дискеты «Искры» — плоский файл: сектора по 256 байт подряд, без
// контейнера и без чередования. Распаковывать нечего, поэтому дисковод здесь
// сводится к «файл в памяти плюс запись насквозь».
//
// Запись идёт в файл сразу, а не при выходе: программа на машине может
// уронить эмулятор, и терять из-за этого записанный SAVE DC незачем.
class DiskFiles
{
public:
    // Дисководов у «Искры» на одном контроллере четыре: F, R и два съёмных.
    static const unsigned DRIVES = 4;

    DiskFiles();
    ~DiskFiles();

    // Подставить образ. Файл открывается на запись, а если не выходит — на
    // чтение, и тогда дискета считается защищённой от записи.
    bool mount(unsigned drive, const char * path, std::string & error);
    void unmount(unsigned drive);

    // Заклеить прорезь: чтение остаётся, запись запрещена. Обратно не
    // снимается — дискету для этого вынимают и вставляют заново.
    void protect(unsigned drive);

    bool mounted(unsigned drive) const;
    bool writable(unsigned drive) const;
    const std::string & path(unsigned drive) const;

    unsigned sectors(unsigned drive) const;
    bool read(unsigned drive, unsigned sector, uint8_t * buf) const;
    bool write(unsigned drive, unsigned sector, const uint8_t * buf);

private:
    struct Drive {
        std::vector<uint8_t> data;
        std::string path;
        std::FILE * file;
        bool writable;
        Drive() : file(0), writable(false) {}
    };

    Drive drives_[DRIVES];

    DiskFiles(const DiskFiles &);
    DiskFiles & operator=(const DiskFiles &);
};

} // namespace iskra
