// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Mikhail Revzin <p3.141592653589793238462643@gmail.com>
// Part of the Iskra-226 project: https://github.com/Ptr314/Iskra-226
// Description: образ дискеты в файле — чтение, запись насквозь, кириллица в имени

#include <cstdio>
#include <string>
#include <vector>

#include "check.h"
#include "core/host.h"
#include "host_common/disk_args.h"
#include "host_common/disk_files.h"
#include "host_common/fileio.h"

using namespace iskra;

namespace {

const unsigned SECTORS = 8;
const char * PATH = "test_diskfiles.img";
// Имя с кириллицей: на Windows узкое имя ушло бы в кодовую страницу системы
// и до файловой системы не дошло. Ради этого и заведён open_utf8().
const char * PATH_CYR = "test_diskfiles_ПРОБА.img";

// Байт, каким заполнен сектор, — по его номеру: так видно, что читается
// именно тот сектор, о котором просили.
uint8_t fill_of(unsigned sector) { return static_cast<uint8_t>(0x40 + sector); }

bool make_image(const char * path)
{
    std::FILE * f = open_utf8(path, "wb");
    if (!f) return false;
    for (unsigned s = 0; s < SECTORS; ++s) {
        std::vector<uint8_t> buf(Host::SECTOR_SIZE, fill_of(s));
        std::fwrite(&buf[0], 1, buf.size(), f);
    }
    std::fclose(f);
    return true;
}

void drop(const char * path) { remove_utf8(path); }

// Плоский образ: сектор лежит ровно там, где его номер, и за краем образа
// сектора нет вовсе.
void test_read()
{
    CHECK(make_image(PATH));

    DiskFiles disks;
    std::string err;
    CHECK(disks.mount(0, PATH, err));
    CHECK_STR(err, "");
    CHECK_EQ(disks.sectors(0), SECTORS);
    CHECK(disks.mounted(0));
    CHECK(disks.writable(0));

    uint8_t buf[Host::SECTOR_SIZE];
    for (unsigned s = 0; s < SECTORS; ++s) {
        CHECK(disks.read(0, s, buf));
        CHECK_EQ(buf[0], fill_of(s));
        CHECK_EQ(buf[Host::SECTOR_SIZE - 1], fill_of(s));
    }
    CHECK(!disks.read(0, SECTORS, buf));

    // Незанятый дисковод — не ошибка, а пустой дисковод.
    CHECK_EQ(disks.sectors(1), 0u);
    CHECK(!disks.read(1, 0, buf));
}

// Запись идёт в файл сразу: программа на машине может уронить эмулятор, и
// терять из-за этого записанный SAVE DC незачем.
void test_write_through()
{
    CHECK(make_image(PATH));

    std::vector<uint8_t> want(Host::SECTOR_SIZE, 0xA5);
    want[0] = 0x11;
    want[Host::SECTOR_SIZE - 1] = 0x99;

    {
        DiskFiles disks;
        std::string err;
        CHECK(disks.mount(0, PATH, err));
        CHECK(disks.write(0, 3, &want[0]));

        // В памяти изменилось сразу.
        uint8_t buf[Host::SECTOR_SIZE];
        CHECK(disks.read(0, 3, buf));
        CHECK_EQ(buf[0], 0x11u);

        // И в файле — тоже, ещё до закрытия образа.
        DiskFiles again;
        CHECK(again.mount(1, PATH, err));
        CHECK(again.read(1, 3, buf));
        CHECK_EQ(buf[0], 0x11u);
        CHECK_EQ(buf[Host::SECTOR_SIZE - 1], 0x99u);
        // Соседние сектора не задеты.
        CHECK(again.read(1, 2, buf));
        CHECK_EQ(buf[0], fill_of(2));
    }

    // За краем образа не пишем: у настоящей дискеты столько секторов и нет.
    DiskFiles disks;
    std::string err;
    CHECK(disks.mount(0, PATH, err));
    CHECK(!disks.write(0, SECTORS, &want[0]));
}

// Не всякий файл — образ. Размер, не кратный сектору, отвергается с внятным
// объяснением, а не принимается наполовину.
void test_size_check()
{
    std::FILE * f = open_utf8(PATH, "wb");
    CHECK(f != 0);
    if (f) {
        const char junk[100] = { 0 };
        std::fwrite(junk, 1, sizeof(junk), f);
        std::fclose(f);
    }

    DiskFiles disks;
    std::string err;
    CHECK(!disks.mount(0, PATH, err));
    CHECK(err.find("256") != std::string::npos);
    CHECK(!disks.mounted(0));

    CHECK(!disks.mount(0, "нет-такого-файла.img", err));
    CHECK(err.find("не удалось открыть") != std::string::npos);
}

// Имя файла внутри эмулятора живёт в UTF-8, и кириллица в нём должна
// доходить до файловой системы на любой из систем.
void test_utf8_path()
{
    CHECK(make_image(PATH_CYR));

    DiskFiles disks;
    std::string err;
    CHECK(disks.mount(0, PATH_CYR, err));
    CHECK_STR(err, "");
    CHECK_EQ(disks.sectors(0), SECTORS);
    CHECK_STR(disks.path(0), PATH_CYR);
}

// Заклеенная прорезь: читается, но не пишется, и в файле ничего не меняется.
void test_protect()
{
    CHECK(make_image(PATH));

    DiskFiles disks;
    std::string err;
    CHECK(disks.mount(0, PATH, err));
    CHECK(disks.writable(0));
    disks.protect(0);
    CHECK(!disks.writable(0));

    std::vector<uint8_t> want(Host::SECTOR_SIZE, 0x5A);
    CHECK(!disks.write(0, 1, &want[0]));

    // Ни в памяти, ни в файле сектор не тронут.
    uint8_t buf[Host::SECTOR_SIZE];
    CHECK(disks.read(0, 1, buf));
    CHECK_EQ(buf[0], fill_of(1));

    DiskFiles again;
    CHECK(again.mount(1, PATH, err));
    CHECK(again.read(1, 1, buf));
    CHECK_EQ(buf[0], fill_of(1));
}

// Разбор ключей подключения: --dN берёт следующий аргумент, --rN стоит сам
// по себе, и порядок между ними значения не имеет.
void test_args()
{
    CHECK(make_image(PATH));

    const char * argv[] = { "--r1", "--d0", PATH, "--d1", PATH };
    std::vector<std::string> args(argv, argv + sizeof(argv) / sizeof(argv[0]));

    DiskArgs mounts;
    std::string err;
    for (std::size_t i = 0; i < args.size(); ++i) {
        bool handled = false;
        CHECK(mounts.take(args, i, handled, err));
        CHECK(handled);
    }
    CHECK(!mounts.empty());

    DiskFiles disks;
    CHECK(mounts.apply(disks, err));
    CHECK_STR(err, "");
    CHECK(disks.writable(0));
    CHECK(disks.mounted(1));
    CHECK(!disks.writable(1));      // --r1 сработал, хотя стоял первым
    CHECK(!disks.mounted(2));

    // Чужие ключи не наши: `--detok` и `--run` начинаются так же.
    const char * other[] = { "--detok", "--run", "--scale" };
    for (unsigned k = 0; k < 3; ++k) {
        std::vector<std::string> one(1, other[k]);
        std::size_t i = 0;
        bool handled = true;
        DiskArgs d;
        CHECK(d.take(one, i, handled, err));
        CHECK(!handled);
    }

    // Дисководов четыре, образ у --dN обязателен, --rN без --dN — описка.
    {
        std::vector<std::string> bad(1, "--d7");
        std::size_t i = 0;
        bool handled = false;
        DiskArgs d;
        CHECK(!d.take(bad, i, handled, err));
        CHECK(handled);
    }
    {
        std::vector<std::string> bad(1, "--d2");
        std::size_t i = 0;
        bool handled = false;
        DiskArgs d;
        CHECK(!d.take(bad, i, handled, err));
    }
    {
        std::vector<std::string> one(1, "--r3");
        std::size_t i = 0;
        bool handled = false;
        DiskArgs d;
        DiskFiles disks2;
        CHECK(d.take(one, i, handled, err));
        CHECK(!d.apply(disks2, err));
    }

    // Позиционный образ идёт в нулевой дисковод, но не перебивает --d0.
    {
        DiskArgs d;
        d.set_default(PATH);
        DiskFiles disks2;
        CHECK(d.apply(disks2, err));
        CHECK(disks2.mounted(0));
    }
}

} // namespace

int main()
{
    test_read();
    test_write_through();
    test_size_check();
    test_utf8_path();
    test_protect();
    test_args();
    drop(PATH);
    drop(PATH_CYR);
    return test::summary("test_diskfiles");
}
