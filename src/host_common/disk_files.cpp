// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Mikhail Revzin <p3.141592653589793238462643@gmail.com>
// Part of the Iskra-226 project: https://github.com/Ptr314/Iskra-226
// Description: образы дискет в файлах — общее для всех оконных хостов

#include "host_common/disk_files.h"

#include <cstring>

#include "host_common/fileio.h"

namespace iskra {

DiskFiles::DiskFiles() {}

DiskFiles::~DiskFiles()
{
    for (unsigned d = 0; d < DRIVES; ++d) unmount(d);
}

void DiskFiles::unmount(unsigned drive)
{
    if (drive >= DRIVES) return;
    Drive & dr = drives_[drive];
    if (dr.file) std::fclose(dr.file);
    dr.file = 0;
    dr.writable = false;
    dr.path.clear();
    dr.data.clear();
}

bool DiskFiles::mount(unsigned drive, const char * path, std::string & error)
{
    error.clear();
    if (drive >= DRIVES) { error = "нет такого дисковода"; return false; }
    unmount(drive);

    Drive & dr = drives_[drive];
    dr.writable = true;
    dr.file = open_utf8(path, "r+b");
    if (!dr.file) {                       // только для чтения — тоже годится
        dr.writable = false;
        dr.file = open_utf8(path, "rb");
    }
    if (!dr.file) { error = "не удалось открыть "; error += path; return false; }

    uint8_t buf[Host::SECTOR_SIZE];
    std::size_t n;
    while ((n = std::fread(buf, 1, sizeof(buf), dr.file)) > 0)
        dr.data.insert(dr.data.end(), buf, buf + n);

    if (dr.data.empty() || dr.data.size() % Host::SECTOR_SIZE) {
        error = path;
        error += " не похож на образ: размер не кратен 256 байтам";
        unmount(drive);
        return false;
    }

    dr.path = path;
    return true;
}

bool DiskFiles::mounted(unsigned drive) const
{
    return drive < DRIVES && !drives_[drive].data.empty();
}

bool DiskFiles::writable(unsigned drive) const
{
    return drive < DRIVES && drives_[drive].writable;
}

const std::string & DiskFiles::path(unsigned drive) const
{
    static const std::string none;
    return drive < DRIVES ? drives_[drive].path : none;
}

unsigned DiskFiles::sectors(unsigned drive) const
{
    if (!mounted(drive)) return 0;
    return static_cast<unsigned>(drives_[drive].data.size() / Host::SECTOR_SIZE);
}

bool DiskFiles::read(unsigned drive, unsigned sector, uint8_t * buf) const
{
    if (sector >= sectors(drive)) return false;
    std::memcpy(buf, &drives_[drive].data[sector * Host::SECTOR_SIZE],
                Host::SECTOR_SIZE);
    return true;
}

bool DiskFiles::write(unsigned drive, unsigned sector, const uint8_t * buf)
{
    if (sector >= sectors(drive)) return false;
    Drive & dr = drives_[drive];
    if (!dr.writable) return false;       // дискета защищена от записи

    std::memcpy(&dr.data[sector * Host::SECTOR_SIZE], buf, Host::SECTOR_SIZE);

    // Насквозь в файл: fseek от начала, потому что образ плоский и сектор
    // лежит ровно там, где его номер.
    if (std::fseek(dr.file, static_cast<long>(sector) * Host::SECTOR_SIZE,
                   SEEK_SET) != 0)
        return false;
    if (std::fwrite(buf, 1, Host::SECTOR_SIZE, dr.file) != Host::SECTOR_SIZE)
        return false;
    return std::fflush(dr.file) == 0;
}

} // namespace iskra
