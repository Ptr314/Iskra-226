// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Mikhail Revzin <p3.141592653589793238462643@gmail.com>
// Part of the Iskra-226 project: https://github.com/Ptr314/Iskra-226
// Description: обёртка над dsk_tools — единственное место, где он виден

#pragma once

#include <string>
#include <vector>
#include <cstdint>

namespace iskra {

// Запись каталога «Искры» в виде, нужном эмулятору.
struct DiskFile {
    std::string name;
    uint32_t size;          // байт
    std::string type;       // метка типа из файловой системы
    bool is_protected;
    bool is_deleted;
};

// Детокенизация файла, уже извлечённого с диска: листинг в UTF-8. Работает
// детокенизатором из dsk_tools — тем же, что в DISK Commander.
std::string detokenize(const std::vector<uint8_t> & data);

// Образ дискеты, открытый через dsk_tools. Всё касание библиотеки
// заперто в image.cpp, чтобы ядро осталось от неё независимым: под WASM
// этот модуль не собирается вовсе, там образ приходит массивом байт.
class DiskImage
{
public:
    DiskImage();
    ~DiskImage();

    // Открывает образ как дискету «Искры». Автоопределением берётся только
    // формат файла (RAW, IMD, HFE и прочее), а тип диска и файловая система
    // задаются всегда: других дисков у эмулятора не бывает, а геометрия
    // «Искры» совпадает с CP/M GMD-7012, и detect_fdd_type() выбирает CP/M.
    //
    // При неудаче возвращает false, причина — в error().
    bool open(const std::string & path);

    const std::string & error() const { return error_; }
    const std::string & format_id() const { return format_id_; }
    const std::string & type_id() const { return type_id_; }
    const std::string & filesystem_id() const { return filesystem_id_; }

    bool dir(std::vector<DiskFile> & files, bool show_deleted = false);

    // Содержимое файла как оно лежит на диске.
    bool read_file(const std::string & name, std::vector<uint8_t> & data);

    // Детокенизированный листинг в UTF-8 (детокенизатор из dsk_tools).
    bool listing(const std::string & name, std::string & text);

    // Сырые сектора по 256 байт — для будущей дисковой подсистемы.
    unsigned sector_count() const;
    bool read_sector(unsigned sector, uint8_t * buf);

private:
    DiskImage(const DiskImage &);
    DiskImage & operator=(const DiskImage &);

    struct Impl;
    Impl * d_;
    std::string error_;
    std::string format_id_;
    std::string type_id_;
    std::string filesystem_id_;
};

} // namespace iskra