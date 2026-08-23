// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Mikhail Revzin <p3.141592653589793238462643@gmail.com>
// Part of the Iskra-226 project: https://github.com/Ptr314/Iskra-226
// Description: обёртка над dsk_tools — единственное место, где он виден

#include "diskio/image.h"

#include <cstring>

#include "dsk_tools/dsk_tools.h"
#include "diskdefs_embed.h"

namespace iskra {

namespace {
    const unsigned SECTOR_SIZE = 256;

    // Дискета «Искры»: 8 дюймов, 256 КБ. Задаётся всегда, см. open().
    const char * const ISKRA_TYPE_ID = "TYPE_OTHER:ISKRA-226";
    const char * const ISKRA_FS_ID   = "FILESYSTEM_ISKRA-226";

    std::string describe(const dsk_tools::Result & r, const char * what)
    {
        std::string s(what);
        if (!r.message.empty()) s += ": " + r.message;
        return s;
    }

// Детокенизатор рассчитан на QTextBrowser в DISK Commander и экранирует
// вывод под HTML. Нам нужен обычный текст, поэтому разворачиваем обратно;
// список сущностей — тот же, что в dsk_tools::escapeHtml().
std::string unescape_html(const std::string & in)
{
    static const struct { const char * entity; char ch; } ENTITIES[] = {
        {"&lt;",   '<'},
        {"&gt;",   '>'},
        {"&quot;", '"'},
        {"&apos;", '\''},
        {"&amp;",  '&'}     // последним: иначе развернём дважды
    };

    std::string out;
    out.reserve(in.size());
    for (std::size_t i = 0; i < in.size(); ) {
        bool matched = false;
        if (in[i] == '&') {
            for (unsigned e = 0; e < sizeof(ENTITIES) / sizeof(ENTITIES[0]); ++e) {
                const std::size_t len = std::strlen(ENTITIES[e].entity);
                if (in.compare(i, len, ENTITIES[e].entity) == 0) {
                    out += ENTITIES[e].ch;
                    i += len;
                    matched = true;
                    break;
                }
            }
        }
        if (!matched) out += in[i++];
    }
    return out;
}

} // namespace

std::string detokenize(const std::vector<uint8_t> & data)
{
    dsk_tools::ViewerBASIC_Iskra226 viewer;
    return unescape_html(viewer.process_as_text(data, "koi8_r"));
}

struct DiskImage::Impl {
    dsk_tools::DiskDefs diskdefs;
    std::unique_ptr<dsk_tools::diskImage> image;
    std::unique_ptr<dsk_tools::fileSystem> fs;
};

DiskImage::DiskImage()
    : d_(new Impl())
{
    d_->diskdefs = dsk_tools::parse_diskdefs(dsk_tools::embedded_diskdefs());
}

DiskImage::~DiskImage()
{
    delete d_;
}

bool DiskImage::open(const std::string & path)
{
    error_.clear();

    // format_only: из автоопределения нужен только формат файла, а тип
    // диска и файловую систему навязываем сами.
    dsk_tools::Result r = dsk_tools::detect_fdd_type(path, format_id_, type_id_,
                                                     filesystem_id_, true);
    if (!r.isOk()) {
        error_ = describe(r, "не удалось определить формат образа");
        return false;
    }

    type_id_ = ISKRA_TYPE_ID;
    filesystem_id_ = ISKRA_FS_ID;

    d_->image = dsk_tools::prepare_image(path, format_id_, type_id_, d_->diskdefs);
    if (!d_->image) {
        error_ = "формат " + format_id_ + " / " + type_id_ + " не поддерживается";
        return false;
    }

    r = d_->image->load();
    if (!r.isOk()) {
        error_ = describe(r, "не удалось прочитать образ");
        d_->image.reset();
        return false;
    }

    d_->fs = dsk_tools::prepare_filesystem(d_->image.get(), filesystem_id_,
                                           d_->diskdefs);
    if (!d_->fs) {
        error_ = "файловая система " + filesystem_id_ + " не поддерживается";
        return false;
    }

    r = d_->fs->open();
    if (!r.isOk()) {
        error_ = describe(r, "не удалось смонтировать файловую систему");
        d_->fs.reset();
        return false;
    }

    return true;
}

bool DiskImage::dir(std::vector<DiskFile> & files, bool show_deleted)
{
    files.clear();
    if (!d_->fs) { error_ = "образ не открыт"; return false; }

    dsk_tools::Files raw;
    dsk_tools::Result r = d_->fs->dir(raw, show_deleted);
    if (!r.isOk()) {
        error_ = describe(r, "не удалось прочитать каталог");
        return false;
    }

    files.reserve(raw.size());
    for (unsigned i = 0; i < raw.size(); ++i) {
        DiskFile f;
        f.name = raw[i].name;
        f.size = raw[i].size;
        f.type = raw[i].type_label;
        f.is_protected = raw[i].is_protected;
        f.is_deleted = raw[i].is_deleted;
        files.push_back(f);
    }
    return true;
}

bool DiskImage::read_file(const std::string & name, std::vector<uint8_t> & data)
{
    data.clear();
    if (!d_->fs) { error_ = "образ не открыт"; return false; }

    dsk_tools::Files raw;
    dsk_tools::Result r = d_->fs->dir(raw, false);
    if (!r.isOk()) {
        error_ = describe(r, "не удалось прочитать каталог");
        return false;
    }

    for (unsigned i = 0; i < raw.size(); ++i) {
        if (raw[i].name != name) continue;
        r = d_->fs->get_file(raw[i], "FILE_BINARY", data);
        if (!r.isOk()) {
            error_ = describe(r, "не удалось прочитать файл");
            return false;
        }
        return true;
    }

    error_ = "файл \"" + name + "\" в каталоге не найден";
    return false;
}

bool DiskImage::listing(const std::string & name, std::string & text)
{
    text.clear();
    std::vector<uint8_t> data;
    if (!read_file(name, data)) return false;

    text = detokenize(data);
    return true;
}

unsigned DiskImage::sector_count() const
{
    if (!d_->image) return 0;
    return static_cast<unsigned>(d_->image->get_buffer()->size() / SECTOR_SIZE);
}

bool DiskImage::read_sector(unsigned sector, uint8_t * buf)
{
    if (!d_->image) { error_ = "образ не открыт"; return false; }

    const dsk_tools::BYTES & b = *d_->image->get_buffer();
    const std::size_t offset = static_cast<std::size_t>(sector) * SECTOR_SIZE;
    if (offset + SECTOR_SIZE > b.size()) {
        error_ = "сектор за пределами образа";
        return false;
    }
    std::memcpy(buf, &b[offset], SECTOR_SIZE);
    return true;
}

} // namespace iskra