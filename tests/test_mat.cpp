// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Mikhail Revzin <p3.141592653589793238462643@gmail.com>
// Part of the Iskra-226 project: https://github.com/Ptr314/Iskra-226
// Description: матричные операторы MAT, MAT COPY, MAT SEARCH и HEXPRINT

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

bool run_program(ProgramImage & img, std::string & screen, std::string & error)
{
    HeadlessHost host;
    Interp interp(img, host);
    if (!interp.run(error)) return false;
    screen = host.dump();
    return true;
}

bool run_text(const char * utf8, std::string & screen, std::string & error)
{
    std::string koi8;
    utf8_to_koi8(utf8, koi8);

    NameTable names;
    ProgramImage img;
    if (!tokenize(koi8, img, names, error)) return false;
    return run_program(img, screen, error);
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

// --- матричное присваивание (разд. 12.2.4) ----------------------------------

// «Оператор предназначен для присваивания значения элемента массива А
// соответствующему элементу массива В» (руководство, разд. 12.2.4).
void test_mat_assign()
{
    std::string screen, error;
    const char * src =
        "10 DIM A(2,3),B(2,3)\n"
        "20 FOR I=1 TO 2:FOR J=1 TO 3:A(I,J)=I*10+J:NEXT J:NEXT I\n"
        "30 MAT B=A\n"
        "40 PRINT B(1,1);B(1,3);B(2,1);B(2,3)\n";
    if (!run_text(src, screen, error)) { std::printf("  %s\n", error.c_str()); CHECK(false); return; }
    CHECK_STR(line_of(screen, 1), " 11  13  21  23");
}

// «Размерность массива А изменяется в соответствии с размерностью массива В»
// (разд. 12.2.4).
void test_mat_assign_redim()
{
    std::string screen, error;
    const char * src =
        "10 DIM A(3),B(7)\n"
        "20 B(7)=77\n"
        "30 MAT A=B\n"
        "40 PRINT A(7)\n";
    if (!run_text(src, screen, error)) { std::printf("  %s\n", error.c_str()); CHECK(false); return; }
    CHECK_STR(line_of(screen, 1), " 77");
}

// «MAT ZER — каждый элемент матрицы = 0» (разд. 12.1). В корпусе это самая
// частая матричная форма: 141 оператор из 176.
void test_mat_zer()
{
    std::string screen, error;
    const char * src =
        "10 DIM A(2,2)\n"
        "20 A(1,1)=5:A(2,2)=7\n"
        "30 MAT A=ZER\n"
        "40 PRINT A(1,1);A(2,2)\n";
    if (!run_text(src, screen, error)) { std::printf("  %s\n", error.c_str()); CHECK(false); return; }
    CHECK_STR(line_of(screen, 1), " 0  0");
}

// --- MAT COPY (разд. 15.2) --------------------------------------------------

// Все четыре случая из книги на одном наборе: `DIM A¤5,B¤7`, `A¤="АБВГД"`.
// Знак минус слева значит «извлекать с последнего байта», справа —
// «записывать с последнего байта».
void test_mat_copy_book()
{
    std::string screen, error;
    const char * src =
        "10 DIM A\xC2\xA4""5,B\xC2\xA4""7\n"
        "20 A\xC2\xA4=\"ABCDE\"\n"
        "30 MAT COPY A\xC2\xA4 TO B\xC2\xA4:HEXPRINT B\xC2\xA4\n"
        "40 MAT COPY -A\xC2\xA4 TO B\xC2\xA4:HEXPRINT B\xC2\xA4\n"
        "50 MAT COPY A\xC2\xA4 TO -B\xC2\xA4:HEXPRINT B\xC2\xA4\n"
        "60 MAT COPY -A\xC2\xA4 TO -B\xC2\xA4:HEXPRINT B\xC2\xA4\n";
    if (!run_text(src, screen, error)) { std::printf("  %s\n", error.c_str()); CHECK(false); return; }
    // «Если переписываемых байтов недостаточно… в оставшиеся байты
    // записываются символы пробела».
    CHECK_STR(line_of(screen, 1), "41424344452020");
    CHECK_STR(line_of(screen, 2), "45444342412020");
    CHECK_STR(line_of(screen, 3), "20204544434241");
    CHECK_STR(line_of(screen, 4), "20204142434445");
}

// «В операции копирования в качестве входной и выходной переменных может
// использоваться одна и та же переменная или её часть» (разд. 15.2) — на
// этом стоит подпрограмма вставки из примера 15.5.
void test_mat_copy_overlap()
{
    std::string screen, error;
    const char * src =
        "10 DIM C\xC2\xA4""10\n"
        "20 C\xC2\xA4=\"ABCDEFG\"\n"
        // Сдвинуть вправо на два байта, начиная с третьего: длина куска —
        // та же, что в примере 15.5 книги, `<длина поля>+1-K-L`.
        "30 MAT COPY -STR(C\xC2\xA4,3,6) TO -STR(C\xC2\xA4,5)\n"
        "40 STR(C\xC2\xA4,3,2)=\"12\"\n"
        "50 PRINT C\xC2\xA4\n";
    if (!run_text(src, screen, error)) { std::printf("  %s\n", error.c_str()); CHECK(false); return; }
    CHECK_STR(line_of(screen, 1), "AB12CDEFG");
}

// --- MAT SEARCH (разд. 15.1) ------------------------------------------------

// Пример из книги целиком: `DIM A¤12,C¤8`, `A¤="ГАБГАБВАБААБ"`, `INIT(FF)C¤`.
// Латиницей — чтобы тест не зависел от перекодировки: буквы взяты так,
// чтобы совпадала раскладка повторов.
void test_mat_search_book()
{
    std::string screen, error;
    const char * src =
        "10 DIM A\xC2\xA4""12,C\xC2\xA4""8\n"
        "20 A\xC2\xA4=\"GABGABVABAAB\"\n"
        "90 INIT(FF)C\xC2\xA4\n"
        "100 MAT SEARCH A\xC2\xA4,=\"A\"TO C\xC2\xA4:HEXPRINT C\xC2\xA4\n"
        "110 INIT(FF)C\xC2\xA4:MAT SEARCH A\xC2\xA4,=\"AB\"TO C\xC2\xA4:HEXPRINT C\xC2\xA4\n"
        "120 INIT(FF)C\xC2\xA4:MAT SEARCH A\xC2\xA4,=\"AB\"TO C\xC2\xA4 STEP 2:HEXPRINT C\xC2\xA4\n"
        "130 INIT(FF)C\xC2\xA4:MAT SEARCH A\xC2\xA4,=\"A\"TO C\xC2\xA4 STEP -1:HEXPRINT C\xC2\xA4\n";
    if (!run_text(src, screen, error)) { std::printf("  %s\n", error.c_str()); CHECK(false); return; }
    // «Номер в списке занимает два байта» — и список заполнился целиком,
    // поэтому концевых нулей нет.
    CHECK_STR(line_of(screen, 1), "000200050008000A");
    CHECK_STR(line_of(screen, 2), "000200050008000B");
    // «Если задан параметр STEP, то сравниваются только строки,
    // начинающиеся через каждые k байт»; список не заполнился — в конец
    // записаны два байта HEX(0000), остальное осталось от INIT(FF).
    CHECK_STR(line_of(screen, 3), "0005000B0000FFFF");
    // «Если значение выражения отрицательно, поисковая переменная
    // просматривается начиная с её последнего байта».
    CHECK_STR(line_of(screen, 4), "000B000A00080005");
}

// «Если в операции поиска не было найдено ни одного искомого значения,
// первый и второй байты переменной списка номеров будут содержать двоичные
// нули» (разд. 15.1).
void test_mat_search_nothing()
{
    std::string screen, error;
    const char * src =
        "10 DIM A\xC2\xA4""6,C\xC2\xA4""4\n"
        "20 A\xC2\xA4=\"ABCDEF\"\n"
        "30 INIT(FF)C\xC2\xA4\n"
        "40 MAT SEARCH A\xC2\xA4,=\"ZZ\"TO C\xC2\xA4:HEXPRINT C\xC2\xA4\n";
    if (!run_text(src, screen, error)) { std::printf("  %s\n", error.c_str()); CHECK(false); return; }
    CHECK_STR(line_of(screen, 1), "0000FFFF");
}

// Знаки условия, кроме равенства: `<`, `>`, `<>`, `<=`, `>=`.
void test_mat_search_relations()
{
    std::string screen, error;
    const char * src =
        "10 DIM A\xC2\xA4""4,C\xC2\xA4""4\n"
        "20 A\xC2\xA4=\"ACBD\"\n"
        "30 INIT(FF)C\xC2\xA4:MAT SEARCH A\xC2\xA4,>\"C\"TO C\xC2\xA4:HEXPRINT C\xC2\xA4\n"
        "40 INIT(FF)C\xC2\xA4:MAT SEARCH A\xC2\xA4,<\"B\"TO C\xC2\xA4:HEXPRINT C\xC2\xA4\n";
    if (!run_text(src, screen, error)) { std::printf("  %s\n", error.c_str()); CHECK(false); return; }
    // Больше «C» — только «D» на четвёртой позиции.
    CHECK_STR(line_of(screen, 1), "00040000");
    // Меньше «B» — только «A» на первой.
    CHECK_STR(line_of(screen, 2), "00010000");
}

// --- HEXPRINT (разд. 13.5) --------------------------------------------------

// Три примера книги слово в слово.
void test_hexprint_book()
{
    std::string screen, error;
    const char * src =
        "10 HEXPRINT \"ABC\"\n"
        "20 A\xC2\xA4=\"ABC\"\n"
        "30 HEXPRINT A\xC2\xA4\n"
        "40 INIT(03)A\xC2\xA4\n"
        "50 HEXPRINT A\xC2\xA4\n";
    if (!run_text(src, screen, error)) { std::printf("  %s\n", error.c_str()); CHECK(false); return; }
    CHECK_STR(line_of(screen, 1), "414243");
    // Символьная переменная — поле в шестнадцать байт по умолчанию.
    CHECK_STR(line_of(screen, 2), "41424320202020202020202020202020");
    CHECK_STR(line_of(screen, 3), "03030303030303030303030303030303");
}

// «При использовании в качестве разделителя точки с запятой коды
// распечатываются вплотную… Если используется запятая, коды следующего
// элемента печатаются с новой строки» (разд. 13.5).
void test_hexprint_separators()
{
    std::string screen, error;
    const char * src =
        "10 DIM B\xC2\xA4""2\n"
        "20 B\xC2\xA4=\"XY\"\n"
        "30 HEXPRINT \"AB\";B\xC2\xA4\n"
        "40 HEXPRINT \"AB\",B\xC2\xA4\n";
    if (!run_text(src, screen, error)) { std::printf("  %s\n", error.c_str()); CHECK(false); return; }
    CHECK_STR(line_of(screen, 1), "41425859");
    CHECK_STR(line_of(screen, 2), "4142");
    CHECK_STR(line_of(screen, 3), "5859");
}

// --- REPLACE (разд. 15.3) ---------------------------------------------------

// Примеры книги: замена фамилии на фамилию с инициалами и подсчёт вхождений
// одной и той же строкой в обоих аргументах.
void test_replace_book()
{
    std::string screen, error;
    const char * src =
        "10 DIM A\xC2\xA4(2)16,B\xC2\xA4""40\n"
        "20 A\xC2\xA4()=\"TUT IVANOV I IVANOV\"\n"
        "30 REPLACE K,A\xC2\xA4(),\"IVANOV\",\"IVANOV V. I.\"\n"
        "40 PRINT K:PRINT \"[\";A\xC2\xA4();\"]\"\n"
        "50 B\xC2\xA4=\"TUT SHIFR I SHIFR TOZE\"\n"
        "60 REPLACE A,B\xC2\xA4,\"SHIFR\",\"SHIFR\":PRINT A\n";
    if (!run_text(src, screen, error)) { std::printf("  %s\n", error.c_str()); CHECK(false); return; }
    CHECK_STR(line_of(screen, 1), " 2");
    // «Массив рассматривается как одна строка символов без границ между
    // элементами»: замена перешла через границу элемента.
    CHECK_STR(line_of(screen, 2), "[TUT IVANOV V. I. I IVANOV V. I. ]");
    CHECK_STR(line_of(screen, 3), " 2");
}

// Пример 15.6: `REPLACE K,E¤(),HEX(2020),HEX(20)` в цикле, пока K<>0.
// Цикл обязан сойтись — значит, концевые пробелы поля в замене не
// участвуют, иначе они давали бы пары пробелов без конца.
void test_replace_loop()
{
    std::string screen, error;
    const char * src =
        "10 DIM E\xC2\xA4""20\n"
        "20 E\xC2\xA4=\"A   B  C\"\n"
        "30 REPLACE M,E\xC2\xA4,HEX(2020),HEX(20)\n"
        "40 IF M<>0THEN30\n"
        "50 PRINT \"[\";E\xC2\xA4;\"]\"\n";
    if (!run_text(src, screen, error)) { std::printf("  %s\n", error.c_str()); CHECK(false); return; }
    // Поле постоянной длины: освободившееся место стало концевыми
    // пробелами, «содержимое дополняется необходимым их количеством».
    CHECK_STR(line_of(screen, 1), "[A B C               ]");
}

// «Если последнего параметра нет, то искомая строка удаляется». Пример
// книги: удалить все пробелы, кроме концевых.
void test_replace_delete()
{
    std::string screen, error;
    const char * src =
        "10 DIM B\xC2\xA4""10\n"
        "20 B\xC2\xA4=\"A B C\"\n"
        "30 REPLACE M,B\xC2\xA4,HEX(20)\n"
        "40 PRINT M;\"[\";B\xC2\xA4;\"]\"\n";
    if (!run_text(src, screen, error)) { std::printf("  %s\n", error.c_str()); CHECK(false); return; }
    // «Останутся только концевые пробелы».
    CHECK_STR(line_of(screen, 1), " 2 [ABC       ]");
}

// --- $TRAN( (разд. 15.3) ----------------------------------------------------

// Пример 15.8: в списковой форме пара байтов это «на что» и «что»,
// `C¤="?*"` меняет звёздочки на вопросительные знаки.
void test_tran_list()
{
    std::string screen, error;
    const char * src =
        "10 DIM A\xC2\xA4(2)5,C\xC2\xA4""4\n"
        "20 A\xC2\xA4()=\"AB*CD*EFGH\"\n"
        "30 C\xC2\xA4=\"?*\"\n"
        "40 \xC2\xA4TRAN(A\xC2\xA4(),C\xC2\xA4)R\n"
        "50 PRINT \"[\";A\xC2\xA4();\"]\"\n";
    if (!run_text(src, screen, error)) { std::printf("  %s\n", error.c_str()); CHECK(false); return; }
    CHECK_STR(line_of(screen, 1), "[AB?CD?EFGH]");
}

// Пример 15.9: `B¤=HEX(850A)` меняет HEX(0A) на HEX(85).
void test_tran_list_hex()
{
    std::string screen, error;
    const char * src =
        "10 DIM B\xC2\xA4""4,D\xC2\xA4""4\n"
        "20 B\xC2\xA4=HEX(850A)\n"
        "30 D\xC2\xA4=HEX(0A0A0A0A)\n"
        "40 \xC2\xA4TRAN(D\xC2\xA4,B\xC2\xA4)R:HEXPRINT D\xC2\xA4\n";
    if (!run_text(src, screen, error)) { std::printf("  %s\n", error.c_str()); CHECK(false); return; }
    CHECK_STR(line_of(screen, 1), "85858585");
}

// Табличная форма (без `R`): «код преобразуется в число, к которому
// прибавляется единица; этот результат определяет номер байта в таблице».
// Пример 15.10 берёт `T¤="0123456789ABCDEF"`; маска `hh` в потоке не
// кодируется, поэтому проверяются коды, которые и так меньше длины таблицы.
void test_tran_table()
{
    std::string screen, error;
    const char * src =
        "10 DIM T\xC2\xA4""16,D\xC2\xA4""4\n"
        "20 T\xC2\xA4=\"0123456789ABCDEF\"\n"
        "30 D\xC2\xA4=HEX(00010F03)\n"
        "40 \xC2\xA4TRAN(D\xC2\xA4,T\xC2\xA4):PRINT \"[\";D\xC2\xA4;\"]\"\n";
    if (!run_text(src, screen, error)) { std::printf("  %s\n", error.c_str()); CHECK(false); return; }
    CHECK_STR(line_of(screen, 1), "[01F3]");
}

// «Если в таблице содержится меньше байтов, чем полученный номер, то байт
// не изменяется» (при незаданной маске).
void test_tran_short_table()
{
    std::string screen, error;
    const char * src =
        "10 DIM T\xC2\xA4""2,D\xC2\xA4""2\n"
        "20 T\xC2\xA4=HEX(4142)\n"
        "30 D\xC2\xA4=HEX(0099)\n"
        "40 \xC2\xA4TRAN(D\xC2\xA4,T\xC2\xA4):HEXPRINT D\xC2\xA4\n";
    if (!run_text(src, screen, error)) { std::printf("  %s\n", error.c_str()); CHECK(false); return; }
    // 00 попало в таблицу и стало 41, 99 длиннее таблицы и осталось как был.
    CHECK_STR(line_of(screen, 1), "4199");
}

// --- оттранслированная форма ------------------------------------------------

// `MAT` — двухбайтовый глагол `06 01`; массив назван байтом `E0 <индекс>`
// без скобок, знак равенства — `D9`, `ZER` — `EF` (STAT03 240).
class TokenBuilder
{
public:
    void add_numeric_array(unsigned elements)
    {
        flags_.push_back(0x11);                 // действительная + дескриптор
        t1_.push_back(0x00); t1_.push_back(0x00);
        t1_.push_back(0x00); t1_.push_back(0x08);
        t1_.push_back(elements & 0xFF); t1_.push_back(elements >> 8);
        t1_.push_back(0x21); t1_.push_back(0x00);
    }

    void add_line(unsigned number, const int * body, unsigned n)
    {
        if (!lines_.empty()) lines_.push_back(0xFE);
        lines_.push_back(static_cast<uint8_t>(((number / 1000) % 10) * 16 + (number / 100) % 10));
        lines_.push_back(static_cast<uint8_t>(((number / 10) % 10) * 16 + number % 10));
        lines_.push_back(static_cast<uint8_t>(n + 1));
        for (unsigned i = 0; i < n; ++i) lines_.push_back(static_cast<uint8_t>(body[i]));
    }

    std::vector<uint8_t> file() const
    {
        std::vector<uint8_t> stream;
        const unsigned L1 = static_cast<unsigned>(t1_.size());
        const unsigned L2 = static_cast<unsigned>(flags_.size()) * 4;
        stream.push_back(L1 >> 8); stream.push_back(L1 & 0xFF);
        stream.push_back(L2 >> 8); stream.push_back(L2 & 0xFF);
        stream.push_back(0); stream.push_back(0);
        stream.insert(stream.end(), t1_.begin(), t1_.end());
        for (unsigned i = flags_.size(); i-- > 0; ) {
            stream.push_back(0); stream.push_back(0);
            stream.push_back(flags_[i]);
            stream.push_back(0);
        }
        stream.insert(stream.end(), lines_.begin(), lines_.end());

        std::vector<uint8_t> file(256, 0);
        file[0] = 1;
        file[9] = 0x21;
        for (std::size_t p = 0; p < stream.size(); p += 254) {
            file.push_back(0x00);
            file.push_back(0x80);
            for (unsigned i = 0; i < 254; ++i)
                file.push_back(p + i < stream.size() ? stream[p + i] : 0);
        }
        return file;
    }

private:
    std::vector<uint8_t> t1_;
    std::vector<uint8_t> flags_;
    std::vector<uint8_t> lines_;
};

void test_tokenized()
{
    TokenBuilder b;
    b.add_numeric_array(3);                  // 0: A(3)
    b.add_numeric_array(3);                  // 1: B(3)

    // B(2)=5. У индекса массива своей открывающей скобки в потоке нет:
    // за именем сразу идёт выражение, закрывает его D0 (STAT09 250).
    static const int l10[] = { 0x36, 0x07, 0x01, 0xE8, 0x02, 0xD0, 0xD9, 0xE8, 0x05 };
    b.add_line(10, l10, 9);

    // 06 01 05 | E0 00 D9 E0 01 — MAT A=B
    static const int l20[] = { 0x06, 0x01, 0x05, 0xE0, 0x00, 0xD9, 0xE0, 0x01 };
    b.add_line(20, l20, 8);

    // PRINT A(2)
    static const int l30[] = { 0x4C, 0x04, 0x00, 0xE8, 0x02, 0xD0 };
    b.add_line(30, l30, 6);

    // 06 01 04 | E0 00 D9 EF — MAT A=ZER
    static const int l40[] = { 0x06, 0x01, 0x04, 0xE0, 0x00, 0xD9, 0xEF };
    b.add_line(40, l40, 7);

    static const int l50[] = { 0x4C, 0x04, 0x00, 0xE8, 0x02, 0xD0 };
    b.add_line(50, l50, 6);

    ProgramImage img;
    std::string error;
    if (!img.load_file(b.file(), error)) {
        std::printf("  разбор: %s\n", error.c_str());
        CHECK(false);
        return;
    }
    CHECK_EQ(img.line_count(), 5u);

    std::string screen;
    if (!run_program(img, screen, error)) {
        std::printf("  исполнение: %s\n", error.c_str());
        CHECK(false);
        return;
    }
    CHECK_STR(line_of(screen, 1), " 5");
    CHECK_STR(line_of(screen, 2), " 0");
}

} // namespace

int main()
{
    test_mat_assign();
    test_mat_assign_redim();
    test_mat_zer();
    test_mat_copy_book();
    test_mat_copy_overlap();
    test_mat_search_book();
    test_mat_search_nothing();
    test_mat_search_relations();
    test_hexprint_book();
    test_hexprint_separators();
    test_replace_book();
    test_replace_loop();
    test_replace_delete();
    test_tran_list();
    test_tran_list_hex();
    test_tran_table();
    test_tran_short_table();
    test_tokenized();
    return test::summary("матричные операторы");
}
