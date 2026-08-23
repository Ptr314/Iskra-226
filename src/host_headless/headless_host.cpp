// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Mikhail Revzin <p3.141592653589793238462643@gmail.com>
// Part of the Iskra-226 project: https://github.com/Ptr314/Iskra-226
// Description: хост без окна — для автотестов

#include "host_headless/headless_host.h"

#include <cstring>

#include "core/koi8.h"

namespace iskra {

HeadlessHost::HeadlessHost()
    : key_pos_(0), ticks_(0)
{
}

bool HeadlessHost::poll_key(uint8_t & code)
{
    if (key_pos_ >= keys_.size()) return false;
    code = keys_[key_pos_++];
    return true;
}

void HeadlessHost::feed_keys(const uint8_t * codes, unsigned len)
{
    keys_.insert(keys_.end(), codes, codes + len);
}

void HeadlessHost::mount(unsigned drive, const std::vector<uint8_t> & data)
{
    if (drive < 2) disks_[drive] = data;
}

unsigned HeadlessHost::disk_sectors(unsigned drive) const
{
    if (drive >= 2) return 0;
    return static_cast<unsigned>(disks_[drive].size() / SECTOR_SIZE);
}

bool HeadlessHost::disk_read(unsigned drive, unsigned sector, uint8_t * buf)
{
    if (sector >= disk_sectors(drive)) return false;
    std::memcpy(buf, &disks_[drive][static_cast<std::size_t>(sector) * SECTOR_SIZE],
                SECTOR_SIZE);
    return true;
}

bool HeadlessHost::disk_write(unsigned drive, unsigned sector, const uint8_t * buf)
{
    if (sector >= disk_sectors(drive)) return false;
    std::memcpy(&disks_[drive][static_cast<std::size_t>(sector) * SECTOR_SIZE], buf,
                SECTOR_SIZE);
    return true;
}

void HeadlessHost::print_char(uint8_t ch)
{
    printer_.push_back(ch);
}

std::string HeadlessHost::printer() const
{
    if (printer_.empty()) return std::string();
    return koi8_to_utf8(&printer_[0], static_cast<unsigned>(printer_.size()));
}

std::string HeadlessHost::dump() const
{
    std::string out;
    uint8_t line[SCREEN_COLS];

    for (unsigned r = 1; r <= SCREEN_ROWS; ++r) {
        unsigned len = 0;
        for (unsigned c = 1; c <= SCREEN_COLS; ++c) {
            line[c - 1] = screen_.cell(r, c).ch;
            if (line[c - 1] != 0x20) len = c;
        }
        koi8_to_utf8(line, len, out);
        out += '\n';
    }
    return out;
}

} // namespace iskra