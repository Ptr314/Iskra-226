// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Mikhail Revzin <p3.141592653589793238462643@gmail.com>
// Part of the Iskra-226 project: https://github.com/Ptr314/Iskra-226
// Description: каталог диска — указатель, поиск по хешу, размещение файлов

#pragma once

#include <string>
#include <vector>

#include "core/host.h"

namespace iskra {

// Устройство каталога разобрано в docs/format.md, разд. 2. Указатель
// занимает первые LS секторов, дальше идёт область каталога. Номер сектора
// указателя даёт хеш имени, внутри сектора записи лежат подряд.

const unsigned NAME_LEN = 8;

struct CatalogEntry {
    CatalogEntry() : status(0), type(0), first(0), last(0), sector(0), slot(0)
    {
        for (unsigned i = 0; i < NAME_LEN; ++i) name[i] = ' ';
    }

    uint8_t  status;            // 0 — слот свободен; бит 0 — файл вычеркнут
    uint8_t  type;              // 80 программа, 00 файл данных
    unsigned first, last;       // границы файла в секторах, включительно
    uint8_t  name[NAME_LEN];    // КОИ-8, дополнено пробелами
    unsigned sector, slot;      // где лежит сама запись указателя

    bool exists() const { return status != 0; }
    // Статус нельзя сверять на равенство 10/11: встречается и 21.
    bool scratched() const { return status != 0 && (status & 1) != 0; }
    bool alive() const { return status != 0 && (status & 1) == 0; }
    bool is_program() const { return type == 0x80; }
    unsigned sectors() const { return last >= first ? last - first + 1 : 0; }

    // Имя без хвостовых пробелов, байты КОИ-8 как есть.
    std::string name_str() const;
};

// Код состояния файла для оператора LIMITS (руководство, разд. 18.8.3):
// 0 нет, 1 программный, 2 файл данных, 3 вычеркнутый программный,
// 4 вычеркнутый файл данных.
unsigned limits_code(const CatalogEntry & e);

class Catalog
{
public:
    Catalog(Host & host, unsigned drive);

    // Читает параметры нулевого сектора. false — устройства нет либо
    // указатель невозможен.
    bool open(std::string & err);

    unsigned index_sectors() const { return ls_; }
    unsigned current_end() const { return current_end_; }   // первый свободный
    unsigned area_end() const { return area_end_; }

    // Хеш имени в номер сектора указателя — «старый» указатель Wang 2200.
    // Проверен на всех 303 записях корпуса (docs/format.md, разд. 2).
    static unsigned hash(const uint8_t name[NAME_LEN], unsigned index_sectors);

    // Имя из строки КОИ-8: обрезать до восьми байт, дополнить пробелами.
    static void make_name(const std::string & s, uint8_t out[NAME_LEN]);

    // Ищет файл. false — ошибка чтения (err заполнен); если файла нет,
    // вернёт true и e.status == 0. Просматривается один сектор указателя —
    // тот, что дал хеш, — записи в нём слева направо, как на машине.
    bool find(const uint8_t name[NAME_LEN], CatalogEntry & e, std::string & err);

    // Все записи указателя — для LIST DC.
    bool list(std::vector<CatalogEntry> & out, bool with_scratched,
              std::string & err);

    // Заводит файл на n секторов сразу за последним занятым и заносит его в
    // указатель. Место переполнения сектора указателя не обрабатывается:
    // правило переполнения неизвестно, и вместо догадки возвращается ошибка.
    bool create(const uint8_t name[NAME_LEN], bool program, unsigned n,
                CatalogEntry & e, std::string & err);

    // SCRATCH: пометить вычеркнутым, поставив бит 0 статуса.
    bool scratch(const uint8_t name[NAME_LEN], std::string & err);

    // SCRATCH DISK: создать пустой указатель на ls секторов, область
    // каталога до сектора end включительно.
    bool format(unsigned ls, unsigned end, std::string & err);

    // MOVE END: подвинуть конец области каталога.
    bool move_end(unsigned end, std::string & err);

private:
    bool read_params(std::string & err);
    bool write_params(std::string & err);
    bool write_entry(const CatalogEntry & e, std::string & err);
    // Записи одного сектора указателя, включая свободные слоты.
    bool read_sector_entries(unsigned s, std::vector<CatalogEntry> & out,
                             std::string & err);

    Host & host_;
    unsigned drive_;
    unsigned ls_;
    unsigned current_end_;
    unsigned area_end_;
    bool open_;
};

} // namespace iskra