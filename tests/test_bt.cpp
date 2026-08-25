// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Mikhail Revzin <p3.141592653589793238462643@gmail.com>
// Part of the Iskra-226 project: https://github.com/Ptr314/Iskra-226
// Description: блочный обмен с устройством — DATA SAVE BT и DATA LOAD BT

#include <cstdio>
#include <string>
#include <vector>

#include "check.h"
#include "core/interp.h"
#include "core/koi8.h"
#include "core/names.h"
#include "core/tokenize.h"
#include "host_headless/headless_host.h"

using namespace iskra;

namespace {

// Хост с приёмником: отдаёт заготовленные байты по любому адресу, который
// ему назвали, и запоминает, что у него спрашивали. Своих устройств ввода,
// кроме клавиатуры, у обычного хоста нет вовсе.
class TapeHost : public HeadlessHost
{
public:
    TapeHost() : read_addr_(0), write_addr_(0) {}

    bool device_read(uint8_t addr, uint8_t * data, unsigned len)
    {
        read_addr_ = addr;
        for (unsigned i = 0; i < len; ++i)
            data[i] = (i < incoming_.size())
                          ? static_cast<uint8_t>(incoming_[i]) : 0x20;
        return true;
    }

    bool device_write(uint8_t addr, const uint8_t * data, unsigned len)
    {
        write_addr_ = addr;
        outgoing_.append(reinterpret_cast<const char *>(data), len);
        return true;
    }

    std::string incoming_;
    std::string outgoing_;
    unsigned read_addr_;
    unsigned write_addr_;
};

bool run(Host & host, const char * utf8, std::string & error)
{
    std::string koi8;
    utf8_to_koi8(utf8, koi8);
    NameTable names;
    ProgramImage img;
    if (!tokenize(koi8, img, names, error)) return false;
    Interp interp(img, host);
    return interp.run(error);
}

std::string line_of(const std::string & text, unsigned n)
{
    std::size_t p = 0;
    for (unsigned i = 1; i < n; ++i) {
        const std::size_t e = text.find('\n', p);
        if (e == std::string::npos) return std::string();
        p = e + 1;
    }
    const std::size_t e = text.find('\n', p);
    return text.substr(p, e - p);
}

// --- устройства, которые у хоста есть ---------------------------------------

// «TAPE — устройство ввода и вывода для операторов DATA LOAD BT и
// DATA SAVE BT» (руководство, разд. 11.5); сами операторы книга не
// описывает, они шлют блок байтов без всякой служебной разметки.
//
// ФАУ 05 — блок отображения символьной информации, то есть экран
// (приложение 1, таблица физических адресов).
void test_to_screen()
{
    HeadlessHost host;
    std::string error;
    const char * src =
        "10 DIM W\xC2\xA4""6\n"
        "20 W\xC2\xA4=\"PRIVET\"\n"
        "30 DATA SAVE BT /05,W\xC2\xA4\n";
    if (!run(host, src, error)) { std::printf("  %s\n", error.c_str()); CHECK(false); return; }
    CHECK_STR(line_of(host.dump(), 1), "PRIVET");
}

// ФАУ 0C — алфавитно-цифровое печатающее устройство.
void test_to_printer()
{
    HeadlessHost host;
    std::string error;
    const char * src =
        "10 DIM A\xC2\xA4""4\n"
        "20 A\xC2\xA4=HEX(41424344)\n"
        "30 DATA SAVE BT /0C,A\xC2\xA4\n";
    if (!run(host, src, error)) { std::printf("  %s\n", error.c_str()); CHECK(false); return; }
    CHECK_STR(host.printer(), "ABCD");
}

// --- устройства, которых нет ------------------------------------------------

// ФАУ 34 — телекоммуникационный интерфейс (передатчик). Его у хоста нет, и
// программа останавливается: молча терять данные нельзя, обмен выглядел бы
// работающим.
void test_missing_device()
{
    HeadlessHost host;
    std::string error;
    const char * src =
        "10 DIM W\xC2\xA4""6\n"
        "20 W\xC2\xA4=\"PRIVET\"\n"
        "30 DATA SAVE BT /34,W\xC2\xA4\n";
    CHECK(!run(host, src, error));
    CHECK(error.find("/34") != std::string::npos);
    // Это ограничение хоста, а не ошибка машины: кода у неё нет.
}

// --- обмен с настоящим устройством ------------------------------------------

// Хост, у которого устройство есть, получает ровно те байты, что послали, и
// адрес, который назвали.
void test_write_bytes()
{
    TapeHost host;
    std::string error;
    const char * src =
        "10 DIM W\xC2\xA4""8\n"
        "20 W\xC2\xA4=HEX(1B062040)\n"
        // Вырезка задаёт длину блока точно: так делает VICT 2210.
        "30 DATA SAVE BT /34,STR(W\xC2\xA4,1,4)\n";
    if (!run(host, src, error)) { std::printf("  %s\n", error.c_str()); CHECK(false); return; }
    CHECK_EQ(host.write_addr_, 0x34u);
    CHECK_EQ(host.outgoing_.size(), 4u);
    CHECK_EQ(static_cast<unsigned char>(host.outgoing_[0]), 0x1Bu);
    CHECK_EQ(static_cast<unsigned char>(host.outgoing_[3]), 0x40u);
}

// ФАУ 35 — телекоммуникационный интерфейс (приёмник): принятое ложится в
// поле приёмной переменной, столько байт, сколько в поле помещается.
void test_read_bytes()
{
    TapeHost host;
    host.incoming_ = "ABCD";
    std::string error;
    const char * src =
        "10 DIM R\xC2\xA4""4\n"
        "20 DATA LOAD BT /35,R\xC2\xA4\n"
        "30 PRINT R\xC2\xA4\n";
    if (!run(host, src, error)) { std::printf("  %s\n", error.c_str()); CHECK(false); return; }
    CHECK_EQ(host.read_addr_, 0x35u);
    CHECK_STR(line_of(host.dump(), 1), "ABCD");
}

// --- формы приставки --------------------------------------------------------

// Адрес бывает и выражением: в корпусе так задано 188 операторов из 265 —
// программа вычисляет устройство сама (DISSM 7382 = `68 04 DC 0B DE 1B`).
void test_address_by_variable()
{
    TapeHost host;
    std::string error;
    const char * src =
        "10 DIM W\xC2\xA4""4\n"
        "20 W\xC2\xA4=\"TEXT\"\n"
        "30 D=52\n"
        "40 DATA SAVE BT /D,W\xC2\xA4\n";
    if (!run(host, src, error)) { std::printf("  %s\n", error.c_str()); CHECK(false); return; }
    CHECK_EQ(host.write_addr_, 52u);
    CHECK_STR(host.outgoing_, "TEXT");
}

// `#n` — устройство берётся из строки таблицы устройств.
void test_address_by_row()
{
    TapeHost host;
    std::string error;
    const char * src =
        "10 DIM W\xC2\xA4""4\n"
        "20 W\xC2\xA4=\"TEXT\"\n"
        "30 SELECT #3 34\n"
        "40 DATA SAVE BT #3,W\xC2\xA4\n";
    if (!run(host, src, error)) { std::printf("  %s\n", error.c_str()); CHECK(false); return; }
    CHECK_EQ(host.write_addr_, 0x34u);
}

// --- оттранслированная форма ------------------------------------------------

// `DATA SAVE BT /34,W¤` = `68 05 DC DE 34 DE 0F` (VICT 2190): приставка
// `DC`, адрес однобайтовым литералом `DE 34`, запятая `DE`, значение.
void test_tokenized()
{
    std::string koi8, error;
    utf8_to_koi8("10 DATA SAVE BT /34,W\xC2\xA4\n"
                 "20 DATA LOAD BT /35,W\xC2\xA4\n", koi8);
    NameTable names;
    ProgramImage img;
    if (!tokenize(koi8, img, names, error)) { std::printf("  %s\n", error.c_str()); CHECK(false); return; }
    CHECK_EQ(img.line_count(), 2u);

    const std::vector<uint8_t> & a = img.line(0).body;
    CHECK_EQ(a.size(), 7u);
    CHECK_EQ(a[0], 0x68u); CHECK_EQ(a[1], 0x05u);
    CHECK_EQ(a[2], 0xDCu); CHECK_EQ(a[3], 0xDEu); CHECK_EQ(a[4], 0x34u);
    CHECK_EQ(a[5], 0xDEu); CHECK_EQ(a[6], 0x00u);

    const std::vector<uint8_t> & b = img.line(1).body;
    CHECK_EQ(b[0], 0x66u);
    CHECK_EQ(b[4], 0x35u);
}

} // namespace

int main()
{
    test_to_screen();
    test_to_printer();
    test_missing_device();
    test_write_bytes();
    test_read_bytes();
    test_address_by_variable();
    test_address_by_row();
    test_tokenized();
    return test::summary("блочный обмен с устройством");
}
