// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Mikhail Revzin <p3.141592653589793238462643@gmail.com>
// Part of the Iskra-226 project: https://github.com/Ptr314/Iskra-226
// Description: каталог диска — указатель, поиск по хешу, размещение файлов

#include "core/catalog.h"

#include <cstring>

namespace iskra {

namespace {

const unsigned SEC = Host::SECTOR_SIZE;
const unsigned ENTRY = 16;              // размер записи указателя
const unsigned PARAMS = 16;             // параметры диска в начале сектора 0

std::string num_str(unsigned v)
{
    char buf[16];
    unsigned n = 0;
    if (!v) buf[n++] = '0';
    while (v) { buf[n++] = static_cast<char>('0' + v % 10); v /= 10; }
    std::string s;
    while (n) s += buf[--n];
    return s;
}

unsigned be16(const uint8_t * p) { return (static_cast<unsigned>(p[0]) << 8) | p[1]; }

void put_be16(uint8_t * p, unsigned v)
{
    p[0] = static_cast<uint8_t>((v >> 8) & 0xFF);
    p[1] = static_cast<uint8_t>(v & 0xFF);
}

// Слотов в секторе указателя: в нулевом первые 16 байт заняты параметрами.
unsigned slots_in(unsigned s) { return s == 0 ? SEC / ENTRY - 1 : SEC / ENTRY; }
unsigned first_slot(unsigned s) { return s == 0 ? 1 : 0; }

} // namespace

std::string CatalogEntry::name_str() const
{
    unsigned n = NAME_LEN;
    while (n && name[n - 1] == ' ') --n;
    return std::string(reinterpret_cast<const char *>(name), n);
}

unsigned limits_code(const CatalogEntry & e)
{
    if (!e.exists()) return 0;
    if (e.is_program()) return e.scratched() ? 3u : 1u;
    return e.scratched() ? 4u : 2u;
}

Catalog::Catalog(Host & host, unsigned drive)
    : host_(host), drive_(drive), ls_(0), current_end_(0), area_end_(0),
      open_(false)
{
}

unsigned Catalog::hash(const uint8_t name[NAME_LEN], unsigned index_sectors)
{
    if (!index_sectors) return 0;
    unsigned t = 0;
    for (unsigned i = 0; i < NAME_LEN; ++i) t ^= name[i];
    t *= 3;
    t = (t % 256) + t / 256;
    return t % index_sectors;
}

void Catalog::make_name(const std::string & s, uint8_t out[NAME_LEN])
{
    for (unsigned i = 0; i < NAME_LEN; ++i)
        out[i] = (i < s.size()) ? static_cast<uint8_t>(s[i])
                                : static_cast<uint8_t>(' ');
}

bool Catalog::read_params(std::string & err)
{
    uint8_t sec[SEC];
    if (!host_.disk_read(drive_, 0, sec)) {
        err = "дисковод не читается";
        return false;
    }
    ls_ = be16(sec);
    current_end_ = be16(sec + 2);
    area_end_ = be16(sec + 4);
    const unsigned total = host_.disk_sectors(drive_);
    if (ls_ < 1 || ls_ > 255 || ls_ > total) {
        err = "на диске нет каталога";
        return false;
    }
    if (area_end_ >= total) area_end_ = total ? total - 1 : 0;
    return true;
}

bool Catalog::write_params(std::string & err)
{
    uint8_t sec[SEC];
    if (!host_.disk_read(drive_, 0, sec)) { err = "дисковод не читается"; return false; }
    put_be16(sec, ls_);
    put_be16(sec + 2, current_end_);
    put_be16(sec + 4, area_end_);
    for (unsigned i = 6; i < PARAMS; ++i) sec[i] = 0;
    if (!host_.disk_write(drive_, 0, sec)) { err = "диск не пишется"; return false; }
    return true;
}

bool Catalog::open(std::string & err)
{
    open_ = read_params(err);
    return open_;
}

bool Catalog::read_sector_entries(unsigned s, std::vector<CatalogEntry> & out,
                                  std::string & err)
{
    uint8_t sec[SEC];
    if (!host_.disk_read(drive_, s, sec)) {
        err = "не читается сектор указателя " + num_str(s);
        return false;
    }
    const unsigned n = slots_in(s), f = first_slot(s);
    for (unsigned i = 0; i < n; ++i) {
        const uint8_t * r = sec + (f + i) * ENTRY;
        CatalogEntry e;
        e.status = r[0];
        e.type = r[1];
        e.first = be16(r + 2);
        e.last = be16(r + 4);
        std::memcpy(e.name, r + 8, NAME_LEN);
        e.sector = s;
        e.slot = f + i;
        out.push_back(e);
    }
    return true;
}

bool Catalog::find(const uint8_t name[NAME_LEN], CatalogEntry & e,
                   std::string & err)
{
    e = CatalogEntry();
    if (!open_ && !open(err)) return false;

    // Машина читает ровно один сектор — тот, что дал хеш, — и перебирает его
    // записи слева направо до совпадения имени.
    std::vector<CatalogEntry> ents;
    if (!read_sector_entries(hash(name, ls_), ents, err)) return false;
    for (std::size_t i = 0; i < ents.size(); ++i) {
        if (!ents[i].exists()) continue;
        if (std::memcmp(ents[i].name, name, NAME_LEN) == 0) { e = ents[i]; break; }
    }
    return true;
}

bool Catalog::list(std::vector<CatalogEntry> & out, bool with_scratched,
                   std::string & err)
{
    out.clear();
    if (!open_ && !open(err)) return false;
    for (unsigned s = 0; s < ls_; ++s) {
        std::vector<CatalogEntry> ents;
        if (!read_sector_entries(s, ents, err)) return false;
        for (std::size_t i = 0; i < ents.size(); ++i) {
            if (!ents[i].exists()) continue;
            if (ents[i].scratched() && !with_scratched) continue;
            out.push_back(ents[i]);
        }
    }
    return true;
}

bool Catalog::write_entry(const CatalogEntry & e, std::string & err)
{
    uint8_t sec[SEC];
    if (!host_.disk_read(drive_, e.sector, sec)) {
        err = "не читается сектор указателя " + num_str(e.sector);
        return false;
    }
    uint8_t * r = sec + e.slot * ENTRY;
    r[0] = e.status;
    r[1] = e.type;
    put_be16(r + 2, e.first);
    put_be16(r + 4, e.last);
    r[6] = r[7] = 0;
    std::memcpy(r + 8, e.name, NAME_LEN);
    if (!host_.disk_write(drive_, e.sector, sec)) {
        err = "не пишется сектор указателя " + num_str(e.sector);
        return false;
    }
    return true;
}

bool Catalog::create(const uint8_t name[NAME_LEN], bool program, unsigned n,
                     CatalogEntry & e, std::string & err)
{
    e = CatalogEntry();
    if (!open_ && !open(err)) return false;
    if (!n) { err = "нулевой размер файла"; return false; }

    CatalogEntry old;
    if (!find(name, old, err)) return false;
    if (old.exists()) { err = "файл с таким именем уже есть"; return false; }

    // Файлы лежат подряд в порядке создания, свободное место — за последним
    // занятым сектором. Обычно это и есть «текущий конец каталога», но у
    // полностью забитого диска поле упирается в конец области и отстаёт на
    // единицу (так на w002-s1), поэтому границу берём ещё и по записям.
    // Место вычеркнутых файлов не переиспользуется: на всех 32 образах
    // корпуса диапазоны файлов не пересекаются (docs/format.md, разд. 2).
    std::vector<CatalogEntry> all;
    if (!list(all, true, err)) return false;
    unsigned first = current_end_;
    for (std::size_t i = 0; i < all.size(); ++i)
        if (all[i].last + 1 > first) first = all[i].last + 1;
    const unsigned last = first + n - 1;
    if (first > area_end_ || last > area_end_ || last < first) {
        err = "на диске нет места";
        return false;
    }

    // Слот — первый свободный в секторе, который дал хеш.
    const unsigned s = hash(name, ls_);
    std::vector<CatalogEntry> ents;
    if (!read_sector_entries(s, ents, err)) return false;
    std::size_t k = 0;
    while (k < ents.size() && ents[k].exists()) ++k;
    if (k == ents.size()) {
        // Правило переполнения неизвестно — примера в корпусе нет.
        err = "переполнение сектора указателя " + num_str(s);
        return false;
    }

    e = ents[k];
    e.status = 0x10;
    e.type = program ? 0x80 : 0x00;
    e.first = first;
    e.last = last;
    std::memcpy(e.name, name, NAME_LEN);
    if (!write_entry(e, err)) return false;

    current_end_ = last + 1;
    if (current_end_ > area_end_) current_end_ = area_end_;
    return write_params(err);
}

bool Catalog::scratch(const uint8_t name[NAME_LEN], std::string & err)
{
    CatalogEntry e;
    if (!find(name, e, err)) return false;
    if (!e.exists()) { err = "файла нет в каталоге"; return false; }
    if (e.scratched()) return true;
    e.status = static_cast<uint8_t>(e.status | 1);
    return write_entry(e, err);
}

bool Catalog::format(unsigned ls, unsigned end, std::string & err)
{
    const unsigned total = host_.disk_sectors(drive_);
    if (!total) { err = "дисковода нет"; return false; }
    if (ls < 1 || ls > 255 || ls >= total) { err = "недопустимый размер указателя"; return false; }
    if (end < ls || end >= total) { err = "недопустимый конец каталога"; return false; }
    // Хеш вырождается, когда число секторов указателя кратно трём: он
    // домножает свёртку на 3, и остаток всегда кратен трём тоже.
    // Работать будет, но указатель используется на треть.

    uint8_t sec[SEC];
    for (unsigned s = 0; s < ls; ++s) {
        std::memset(sec, 0, SEC);
        if (!host_.disk_write(drive_, s, sec)) {
            err = "не пишется сектор указателя " + num_str(s);
            return false;
        }
    }
    ls_ = ls;
    area_end_ = end;
    current_end_ = ls;          // файлы начинаются сразу за указателем
    open_ = true;
    return write_params(err);
}

bool Catalog::move_end(unsigned end, std::string & err)
{
    if (!open_ && !open(err)) return false;
    const unsigned total = host_.disk_sectors(drive_);
    if (end < area_end_ || end >= total) {
        err = "конец каталога можно только отодвинуть";
        return false;
    }
    area_end_ = end;
    return write_params(err);
}

} // namespace iskra