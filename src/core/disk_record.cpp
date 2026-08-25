// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Mikhail Revzin <p3.141592653589793238462643@gmail.com>
// Part of the Iskra-226 project: https://github.com/Ptr314/Iskra-226
// Description: логическая запись файла данных — чтение и запись

#include "core/disk_record.h"

#include <cstring>

namespace iskra {

namespace {

const unsigned SEC = Host::SECTOR_SIZE;
const unsigned NUM_LEN = 8;
const unsigned STR_MAX = 253;       // 2 служебных байта + 253 = 255

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

// Разбирает значения одного сектора. Останавливается на нулевой паре
// <тип> <длина> и на значении, которое в остаток сектора не влезает: такое
// значение писалось целиком в следующий сектор.
bool parse_sector(const uint8_t * sec, std::vector<Value> & out,
                  std::string & err)
{
    unsigned p = 1;
    while (p + 2 <= SEC) {
        const unsigned t = sec[p], len = sec[p + 1];
        if (t == 0 && len == 0) break;
        if (len == 0 || p + 2 + len > SEC) break;
        Value v;
        if (t == VAL_NUM) {
            if (len != NUM_LEN) {
                err = "число в записи длиной " + num_str(len) + " байт";
                return false;
            }
            if (!Number::from_disk8(sec + p + 2, v.num)) {
                err = "испорченное число в записи";
                return false;
            }
        } else if (t == VAL_STR) {
            v.is_str = true;
            v.str.assign(reinterpret_cast<const char *>(sec + p + 2), len);
        } else {
            err = "неизвестный тип значения " + num_str(t) + " в записи";
            return false;
        }
        out.push_back(v);
        p += 2 + len;
    }
    return true;
}

} // namespace

bool is_record_start(unsigned code)
{
    return code == REC_SINGLE || code == REC_FIRST;
}

bool record_code(Host & host, unsigned drive, unsigned sector, unsigned & code)
{
    uint8_t sec[SEC];
    if (!host.disk_read(drive, sector, sec)) return false;
    code = sec[0];
    return true;
}

bool read_record(Host & host, unsigned drive, unsigned start,
                 std::vector<Value> & out, unsigned & next, std::string & err)
{
    out.clear();
    uint8_t sec[SEC];
    if (!host.disk_read(drive, start, sec)) {
        err = "не читается сектор " + num_str(start);
        return false;
    }
    if (!is_record_start(sec[0])) {
        err = "сектор " + num_str(start) + " не начинает запись";
        return false;
    }
    if (!parse_sector(sec, out, err)) return false;
    if (sec[0] == REC_SINGLE) { next = start + 1; return true; }

    // Многосекторная: 02, затем 8F до 03 включительно.
    unsigned s = start;
    for (;;) {
        ++s;
        if (!host.disk_read(drive, s, sec)) {
            err = "запись обрывается: не читается сектор " + num_str(s);
            return false;
        }
        if (sec[0] != REC_MIDDLE && sec[0] != REC_LAST) {
            err = "запись обрывается в секторе " + num_str(s);
            return false;
        }
        if (!parse_sector(sec, out, err)) return false;
        if (sec[0] == REC_LAST) break;
    }
    next = s + 1;
    return true;
}

bool read_end_record(Host & host, unsigned drive, unsigned sector,
                     unsigned & used)
{
    uint8_t sec[SEC];
    if (!host.disk_read(drive, sector, sec)) return false;
    if (sec[0] != REC_END) return false;
    used = (static_cast<unsigned>(sec[1]) << 8) | sec[2];
    return true;
}

bool record_end(Host & host, unsigned drive, unsigned start, unsigned & next,
                std::string & err)
{
    uint8_t sec[SEC];
    if (!host.disk_read(drive, start, sec)) {
        err = "не читается сектор " + num_str(start);
        return false;
    }
    if (!is_record_start(sec[0])) {
        err = "сектор " + num_str(start) + " не начинает запись";
        return false;
    }
    if (sec[0] == REC_SINGLE) { next = start + 1; return true; }
    unsigned s = start;
    for (;;) {
        ++s;
        if (!host.disk_read(drive, s, sec)) {
            err = "запись обрывается: не читается сектор " + num_str(s);
            return false;
        }
        if (sec[0] == REC_LAST) break;
        if (sec[0] != REC_MIDDLE) {
            err = "запись обрывается в секторе " + num_str(s);
            return false;
        }
    }
    next = s + 1;
    return true;
}

bool find_end_record(Host & host, unsigned drive, unsigned first, unsigned last,
                     unsigned & sector, unsigned & used)
{
    uint8_t sec[SEC];
    for (unsigned s = first; s <= last; ++s) {
        if (!host.disk_read(drive, s, sec)) return false;
        if (sec[0] == REC_END) {
            sector = s;
            used = (static_cast<unsigned>(sec[1]) << 8) | sec[2];
            return true;
        }
    }
    return false;
}

bool record_start(Host & host, unsigned drive, unsigned first, unsigned s,
                  unsigned & start)
{
    uint8_t sec[SEC];
    while (s > first) {
        if (!host.disk_read(drive, s, sec)) return false;
        if (sec[0] != REC_MIDDLE && sec[0] != REC_LAST) break;
        --s;
    }
    start = s;
    return true;
}

bool write_record(Host & host, unsigned drive, unsigned start, unsigned last,
                  const std::vector<Value> & vals, unsigned & next,
                  std::string & err)
{
    if (vals.empty()) { err = "пустая запись"; return false; }

    // Раскладка значений по секторам: значение, не влезающее в остаток,
    // целиком переносится в следующий сектор.
    std::vector<std::vector<uint8_t> > secs;
    secs.push_back(std::vector<uint8_t>(1, 0));      // байт кода заполним потом
    for (std::size_t i = 0; i < vals.size(); ++i) {
        const Value & v = vals[i];
        unsigned len;
        if (v.is_str) {
            if (v.str.size() > STR_MAX) {
                err = "строка длиннее " + num_str(STR_MAX) + " байт";
                return false;
            }
            len = static_cast<unsigned>(v.str.size());
            // Символьная переменная — поле постоянной длины, пустой не бывает.
            if (!len) { err = "пустое символьное значение в записи"; return false; }
        } else {
            len = NUM_LEN;
        }
        if (secs.back().size() + 2 + len > SEC)
            secs.push_back(std::vector<uint8_t>(1, 0));
        std::vector<uint8_t> & s = secs.back();
        s.push_back(v.is_str ? VAL_STR : VAL_NUM);
        s.push_back(static_cast<uint8_t>(len));
        if (v.is_str) {
            for (unsigned k = 0; k < len; ++k)
                s.push_back(static_cast<uint8_t>(v.str[k]));
        } else {
            uint8_t buf[NUM_LEN];
            v.num.to_disk8(buf);
            for (unsigned k = 0; k < NUM_LEN; ++k) s.push_back(buf[k]);
        }
    }

    const unsigned n = static_cast<unsigned>(secs.size());
    if (start + n - 1 > last) {
        err = "запись не помещается в файл";
        return false;
    }
    for (unsigned i = 0; i < n; ++i) {
        secs[i][0] = static_cast<uint8_t>(
            n == 1 ? REC_SINGLE
                   : (i == 0 ? REC_FIRST : (i + 1 == n ? REC_LAST : REC_MIDDLE)));
        uint8_t sec[SEC];
        std::memset(sec, 0, SEC);
        std::memcpy(sec, &secs[i][0], secs[i].size());
        if (!host.disk_write(drive, start + i, sec)) {
            err = "не пишется сектор " + num_str(start + i);
            return false;
        }
    }
    next = start + n;
    return true;
}

bool write_end_record(Host & host, unsigned drive, unsigned file_start,
                      unsigned sector, std::string & err)
{
    if (sector < file_start) {
        err = "концевая запись раньше начала файла";
        return false;
    }
    const unsigned used = sector - file_start + 1;
    uint8_t sec[SEC];
    std::memset(sec, 0, SEC);
    sec[0] = REC_END;
    sec[1] = static_cast<uint8_t>((used >> 8) & 0xFF);
    sec[2] = static_cast<uint8_t>(used & 0xFF);
    if (!host.disk_write(drive, sector, sec)) {
        err = "не пишется сектор " + num_str(sector);
        return false;
    }
    return true;
}

} // namespace iskra