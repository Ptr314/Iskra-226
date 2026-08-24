// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Mikhail Revzin <p3.141592653589793238462643@gmail.com>
// Part of the Iskra-226 project: https://github.com/Ptr314/Iskra-226
// Description: ключи подключения образов — общее для окна и терминала

#include "host_common/disk_args.h"

namespace iskra {

DiskArgs::DiskArgs()
{
    for (unsigned d = 0; d < DiskFiles::DRIVES; ++d) ro_[d] = false;
}

const char * DiskArgs::help()
{
    return
    "  --d0 ОБРАЗ … --d3 ОБРАЗ  образ в дисковод: 0 = 18F, 1 = 18R,\n"
    "                           2 = 1CF, 3 = 1CR — те же адреса, что в SELECT\n"
    "  --r0 … --r3              запретить запись на этот дисковод";
}

bool DiskArgs::take(const std::vector<std::string> & args, std::size_t & i,
                    bool & handled, std::string & error)
{
    handled = false;
    const std::string & a = args[i];

    // Ровно четыре знака: иначе `--detok` и `--run` попали бы под `--d`
    // и `--r`.
    if (a.size() != 4 || a[0] != '-' || a[1] != '-') return true;
    if (a[2] != 'd' && a[2] != 'r') return true;
    if (a[3] < '0' || a[3] > '9') return true;

    handled = true;
    const unsigned n = static_cast<unsigned>(a[3] - '0');
    if (n >= DiskFiles::DRIVES) {
        error = a + ": дисководы только 0…3";
        return false;
    }

    if (a[2] == 'r') { ro_[n] = true; return true; }

    if (i + 1 >= args.size()) { error = a + ": не указан образ"; return false; }
    path_[n] = args[++i];
    return true;
}

void DiskArgs::set_default(const std::string & path)
{
    if (path_[0].empty()) path_[0] = path;
}

bool DiskArgs::empty() const
{
    for (unsigned d = 0; d < DiskFiles::DRIVES; ++d)
        if (!path_[d].empty()) return false;
    return true;
}

bool DiskArgs::apply(DiskFiles & disks, std::string & error) const
{
    for (unsigned d = 0; d < DiskFiles::DRIVES; ++d) {
        if (path_[d].empty()) {
            // Запрет записи на дисковод, в котором нет дискеты, — это описка,
            // и молчать о ней хуже, чем сказать.
            if (ro_[d]) {
                char n[2] = { static_cast<char>('0' + d), 0 };
                error = "--r";
                error += n;
                error += " без --d";
                error += n;
                return false;
            }
            continue;
        }
        if (!disks.mount(d, path_[d].c_str(), error)) return false;
        if (ro_[d]) disks.protect(d);
    }
    return true;
}

} // namespace iskra
