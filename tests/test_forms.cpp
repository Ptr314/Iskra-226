// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Mikhail Revzin <p3.141592653589793238462643@gmail.com>
// Part of the Iskra-226 project: https://github.com/Ptr314/Iskra-226
// Description: матрица форм языка — текст, байты и круговая проверка

// Собрано генератором tools/probes/gen_forms_test.py. Каждая форма
// проверяется дважды:
//
//   * текст → токены: байты обязаны совпасть с записанными здесь;
//   * токены → текст → токены: те же байты обязаны получиться снова.
//
// Таблица имён у каждой формы своя, поэтому индексы переменных в ней
// раздаются с нуля по первому появлению.

#include <cstdio>
#include <string>
#include <vector>

#include "check.h"
#include "core/detokenize.h"
#include "core/koi8.h"
#include "core/names.h"
#include "core/program.h"
#include "core/tokenize.h"

using namespace iskra;

namespace {

struct Form {
    const char * text;      // текст оператора без номера строки
    const char * bytes;     // ожидаемые байты тела строки, через пробел
    const char * note;
};

std::string hexs(const std::vector<uint8_t> & v)
{
    std::string s;
    for (std::size_t i = 0; i < v.size(); ++i) {
        char b[4];
        std::sprintf(b, "%02X", v[i]);
        if (i) s += ' ';
        s += b;
    }
    return s;
}

// Одна форма: перевести, сверить байты, вернуть в текст и перевести снова.
void check(const Form & f)
{
    std::string koi8;
    utf8_to_koi8(std::string("10 ") + f.text, koi8);

    NameTable names;
    unsigned number = 0;
    std::vector<uint8_t> body;
    std::string error;
    if (!tokenize_line(koi8, names, number, body, error)) {
        std::printf("  %s: %s\n", f.text, error.c_str());
        CHECK(false);
        return;
    }
    if (hexs(body) != f.bytes) {
        std::printf("  %s (%s)\n    ждали  %s\n    вышло  %s\n",
                    f.text, f.note, f.bytes, hexs(body).c_str());
        CHECK(false);
        return;
    }

    // Обратно в текст той же таблицей имён — и снова в токены.
    ProgramLine line;
    line.number = number;
    line.body = body;
    std::string back;
    if (!detokenize_line(line, names, back, error)) {
        std::printf("  %s: обратно — %s\n", f.text, error.c_str());
        CHECK(false);
        return;
    }
    NameTable again = names;
    unsigned n2 = 0;
    std::vector<uint8_t> body2;
    if (!tokenize_line(back, again, n2, body2, error)) {
        std::printf("  %s: круг — %s\n", f.text, error.c_str());
        CHECK(false);
        return;
    }
    if (body2 != body) {
        std::printf("  %s (%s): круг не сошёлся\n    было  %s\n    стало %s\n",
                    f.text, f.note, hexs(body).c_str(), hexs(body2).c_str());
        CHECK(false);
    }
}

const Form FORMS[] = {
    { "A=1", "36 04 00 D9 E8 01", "целая константа" },
    { "A=0", "36 04 00 D9 E8 00", "ноль" },
    { "A=1.5", "36 05 00 D9 E5 12 15", "дробная" },
    { "A=255", "36 05 00 D9 E7 02 55", "двухбайтовая BCD" },
    { "A=1E6", "36 06 00 D9 E6 11 10 06", "положительный порядок" },
    { "A=-3", "36 05 00 D9 E9 E8 03", "унарный минус" },
    { "A=2^3", "36 07 00 D9 E8 02 E0 E8 03", "степень" },
    { "A=(1+2)*3/4-5", "36 12 00 D9 EB E8 01 EA E8 02 D0 DF E8 03 DC E8 04 E9 E8 05", "скобки и приоритеты" },
    { "A=B+C*D^E", "36 09 00 D9 01 EA 02 DF 03 E0 04", "приоритеты без скобок" },
    { "A=#PI", "36 03 00 D9 F1", "системная константа" },
    { "A=SQR(2*#PI)", "36 08 00 D9 F6 E8 02 DF F1 D0", "SQR(" },
    { "A=ABS(-1)", "36 07 00 D9 F2 E9 E8 01 D0", "ABS(" },
    { "A=INT(1.5)", "36 07 00 D9 F3 E5 12 15 D0", "INT(" },
    { "A=SGN(B)", "36 05 00 D9 F5 01 D0", "SGN(" },
    { "A=LOG(B)", "36 05 00 D9 F7 01 D0", "LOG(" },
    { "A=EXP(B)", "36 05 00 D9 F8 01 D0", "EXP(" },
    { "A=ROUND(B,2)", "36 08 00 D9 D8 01 DE E8 02 D0", "ROUND( — вторая запятая кодируется" },
    { "A=RND(1)", "36 06 00 D9 F4 E8 01 D0", "RND(" },
    { "A=SQR(ABS(INT(B)))", "36 09 00 D9 F6 F2 F3 01 D0 D0 D0", "вложенные функции" },
    { "A,B=1", "36 05 00 01 D9 E8 01", "несколько приёмников" },
    { "A(1)=2", "36 07 00 E8 01 D0 D9 E8 02", "элемент массива" },
    { "A(1,2)=3", "36 0A 00 E8 01 DE E8 02 D0 D9 E8 03", "два измерения" },
    { "IF A=B THEN 20", "24 06 00 D9 01 D3 00 20", "IF =" },
    { "IF A<>B THEN 20", "24 06 00 D5 01 D3 00 20", "IF <>" },
    { "IF A<B THEN 20", "24 06 00 D7 01 D3 00 20", "IF <" },
    { "IF A<=B THEN 20", "24 06 00 D6 01 D3 00 20", "IF <=" },
    { "IF A>B THEN 20", "24 06 00 D4 01 D3 00 20", "IF >" },
    { "IF A>=B THEN 20", "24 06 00 D8 01 D3 00 20", "IF >=" },
    { "IF A=1ANDB=2THEN 20", "24 0C 00 D9 E8 01 E7 01 D9 E8 02 D3 00 20", "связка AND" },
    { "IF A=1ORB=2THEN 20", "24 0C 00 D9 E8 01 E6 01 D9 E8 02 D3 00 20", "связка OR" },
    { "IF A=1THEN 20:GOTO 30", "24 07 00 D9 E8 01 D3 00 20 21 02 00 30", "IF и хвост строки" },
    { "GOTO 100", "21 02 01 00", "GOTO" },
    { "GOSUB 100", "22 02 01 00", "GOSUB" },
    { "RETURN", "5E 00", "RETURN" },
    { "ON A GOTO 10,20,30", "26 08 00 CD 00 10 00 20 00 30", "ON GOTO" },
    { "ON A GOSUB 10,20", "26 06 00 CC 00 10 00 20", "ON GOSUB" },
    { "FOR I=1TO10", "57 06 00 E8 01 D1 E8 10", "FOR без шага" },
    { "FOR I=1TO10STEP2", "57 09 00 E8 01 D1 E8 10 D2 E8 02", "FOR с шагом" },
    { "FOR I=10TO1STEP-1", "57 0A 00 E8 10 D1 E8 01 D2 E9 E8 01", "отрицательный шаг" },
    { "NEXT I", "52 01 00", "NEXT" },
    { "END", "59 00", "END" },
    { "STOP", "42 00", "STOP" },
    { "STOP \"ЖДУ\"", "42 05 E3 03 F6 E4 F5", "STOP с сообщением" },
    { "STOP #", "42 01 DB", "STOP # — форма вне книги" },
    { "REM ТЕКСТ", "56 06 20 F4 E5 EB F3 F4", "REM" },
    { "%ОБРАЗЕЦ", "3F 07 EF E2 F2 E1 FA E5 E3", "оператор образа" },
    { "ON ERROR GOTO 100", "34 03 CD 01 00", "ON ERROR GOTO" },
    { "ON ERROR THEN 100", "34 03 D3 01 00", "ON ERROR THEN" },
    { "ON ERROR GOSUB 100", "34 03 CC 01 00", "ON ERROR GOSUB" },
    { "ON ERROR ", "34 00", "ON ERROR — отмена" },
    { "DEFFN '15", "27 05 0F 00 00 00 00", "DEFFN'" },
    { "DEFFN '15(A,B)", "27 07 0F 00 00 00 00 00 01", "DEFFN' с параметрами" },
    { "DEFFN '15\"LIST\"", "3A 0B 0F 00 00 00 00 E3 04 4C 49 53 54", "DEFFN' с текстом клавиши" },
    { "GOSUB '15", "23 01 0F", "GOSUB'" },
    { "GOSUB '15(A+1,B)", "23 07 0F 00 EA E8 01 DE 01", "GOSUB' с выражениями" },
    { "RETURN CLEAR", "30 00", "RETURN CLEAR" },
    { "RETURN CLEAR ALL", "30 01 CB", "RETURN CLEAR ALL" },
    { "DEFFN A(H)=H+1", "5A 08 41 00 00 00 00 EA E8 01", "функция пользователя" },
    { "B=FNA(2)", "36 07 00 D9 F0 41 E8 02 D0", "обращение к функции" },
    { "B=FNA(FNA(1))", "36 0A 00 D9 F0 41 F0 41 E8 01 D0 D0", "функция от функции" },
    { "PRINT ", "4C 00", "PRINT без операндов" },
    { "PRINT \"A\"", "4C 03 E3 01 41", "литерал" },
    { "PRINT A;B", "4C 03 00 DD 01", "точка с запятой" },
    { "PRINT A,B", "4C 03 00 DE 01", "запятая — зоны" },
    { "PRINT ,\"A\"", "4C 04 DE E3 01 41", "пустая зона в начале" },
    { "PRINT \"A\",,B", "4C 06 E3 01 41 DE DE 00", "пустая зона в середине" },
    { "PRINT \"A\",;;B", "4C 07 E3 01 41 DE DD DD 00", "запятая и две точки с запятой" },
    { "PRINT A;", "4C 02 00 DD", "хвостовая точка с запятой" },
    { "PRINT AT(5,14)", "4C 06 D5 E8 05 DE E8 14", "AT( с двумя аргументами" },
    { "PRINT AT(10,20,15)", "4C 09 D5 E8 10 DE E8 20 DE E8 15", "AT( с тремя" },
    { "PRINT TAB(10);\"A\"", "4C 08 DF E8 10 D0 DD E3 01 41", "TAB(" },
    { "PRINT HEX(0D0A)", "4C 04 E2 02 0D 0A", "HEX(" },
    { "PRINT /05,\"A\"", "4C 07 DC DE 05 DE E3 01 41", "адрес устройства" },
    { "PRINT #3,\"A\"", "4C 07 DB E8 03 DE E3 01 41", "строка таблицы устройств" },
    { "PRINTUSING 500,A,B", "28 07 E7 05 00 DE 00 DE 01", "PRINTUSING по номеру строки" },
    { "PRINTUSING \"###\",A", "28 07 E3 03 23 23 23 DE 00", "PRINTUSING с образом-константой" },
    { "HEXPRINT A" "\xC2\xA4", "50 01 00", "HEXPRINT" },
    { "HEXPRINT A" "\xC2\xA4" ";", "50 02 00 DD", "HEXPRINT без перевода строки" },
    { "DIM A" "\xC2\xA4" "16", "46 01 00", "описание строки" },
    { "DIM A" "\xC2\xA4" "(10)8", "46 01 00", "массив строк" },
    { "DIM A(5),B(2,3)", "46 02 00 01", "числовые массивы" },
    { "COM A,B" "\xC2\xA4" "16", "4E 02 00 01", "общие переменные" },
    { "COM CLEAR", "37 00", "COM CLEAR без операнда" },
    { "COM CLEAR A()", "37 02 E0 00", "COM CLEAR с массивом" },
    { "COM CLEAR A", "37 01 00", "COM CLEAR с переменной" },
    { "A" "\xC2\xA4" "=\"X\"", "36 05 00 D9 E3 01 58", "присваивание строки" },
    { "A" "\xC2\xA4" "(2)=\"X\"", "36 08 00 E8 02 D0 D9 E3 01 58", "элемент массива строк" },
    { "A" "\xC2\xA4" "=HEX(0D)", "36 05 00 D9 E2 01 0D", "HEX( справа" },
    { "STR(A" "\xC2\xA4" ",1,2)=\"X\"", "36 0C E1 00 E8 01 DE E8 02 D0 D9 E3 01 58", "STR( слева" },
    { "B=LEN(A" "\xC2\xA4" ")", "36 04 00 D9 ED 01", "LEN" },
    { "B=NUM(A" "\xC2\xA4" ")", "36 04 00 D9 EE 01", "NUM" },
    { "B=VAL(A" "\xC2\xA4" ")", "36 04 00 D9 EF 01", "VAL" },
    { "B=VAL(A" "\xC2\xA4" ",2)", "36 06 00 D9 EF 01 DE DB", "VAL с числом байт" },
    { "B=POS(A" "\xC2\xA4" "=\"X\")", "36 08 00 D9 EC 01 D9 E3 01 58", "POS с условием" },
    { "B" "\xC2\xA4" "=STR(A" "\xC2\xA4" ",1,2)", "36 0A 00 D9 E1 01 E8 01 DE E8 02 D0", "STR( справа" },
    { "B" "\xC2\xA4" "=STR(A" "\xC2\xA4" ",3)", "36 07 00 D9 E1 01 E8 03 D0", "STR( без длины" },
    { "BIN(A" "\xC2\xA4" ")=B", "4B 02 00 01", "BIN(" },
    { "BIN(A" "\xC2\xA4" ",2)=B", "4B 04 00 DE DB 01", "BIN( в два байта" },
    { "INIT(20)A" "\xC2\xA4" ",B" "\xC2\xA4", "64 04 DE 20 00 01", "INIT" },
    { "CONVERT A TO B" "\xC2\xA4", "47 03 00 D1 01", "CONVERT число в строку" },
    { "CONVERT A" "\xC2\xA4" " TO B", "47 03 00 D1 01", "CONVERT строка в число" },
    { "CONVERT A TO B" "\xC2\xA4" ",(###.##)", "47 0B 00 D1 01 E3 06 23 23 23 2E 23 23", "CONVERT с образом" },
    { "LINPUT A" "\xC2\xA4", "06 24 01 00", "LINPUT" },
    { "LINPUT \"ВВОД\",A" "\xC2\xA4", "06 24 07 E3 04 F7 F7 EF E4 00", "LINPUT с приглашением" },
    { "REPLACE K,A" "\xC2\xA4" ",\"X\",\"Y\"", "06 26 0B 00 DE 01 DE E3 01 58 DE E3 01 59", "REPLACE" },
    { "REPLACE K,A" "\xC2\xA4" ",\"X\"", "06 26 07 00 DE 01 DE E3 01 58", "REPLACE без замены" },
    { "\xC2\xA4" "TRAN(A" "\xC2\xA4" ",B" "\xC2\xA4" ")", "06 0C 04 00 DE 01 D0", "TRAN табличный" },
    { "\xC2\xA4" "TRAN(A" "\xC2\xA4" ",B" "\xC2\xA4" ")R", "06 0C 06 00 DE 01 D0 DE 00", "TRAN списковый" },
    { "AND(A" "\xC2\xA4" ",B" "\xC2\xA4" ")", "43 02 00 01", "AND( с переменной" },
    { "AND(A" "\xC2\xA4" ",DF)", "43 03 00 DE DF", "AND( с маской" },
    { "OR(A" "\xC2\xA4" ",FF)", "61 03 00 DE FF", "OR(" },
    { "XOR(A" "\xC2\xA4" ",0F)", "62 03 00 DE 0F", "XOR(" },
    { "BOOL 8(A" "\xC2\xA4" ",B" "\xC2\xA4" ")", "45 03 08 00 01", "BOOL" },
    { "ADD(A" "\xC2\xA4" ",B" "\xC2\xA4" ")", "4A 02 00 01", "ADD" },
    { "ADD(A" "\xC2\xA4" ",02)", "4A 03 00 DE 02", "ADD с маской" },
    { "ADD C(A" "\xC2\xA4" ",B" "\xC2\xA4" ")", "63 02 00 01", "ADD C — свой глагол 63" },
    { "ADDC(A" "\xC2\xA4" ",FF)", "63 03 00 DE FF", "ADD C слитно, с маской" },
    { "ROTATE(A" "\xC2\xA4" ",4)", "4D 06 EB 00 DE E8 04 D0", "ROTATE" },
    { "ROTATE C(A" "\xC2\xA4" ",4)", "4D 07 D4 EB 00 DE E8 04 D0", "ROTATE C" },
    { "PACK(##.##)A" "\xC2\xA4" "FROM B", "48 0A E3 05 23 23 2E 23 23 00 CA 01", "PACK(" },
    { "UNPACK(##.##)A" "\xC2\xA4" "TO B", "5D 0A E3 05 23 23 2E 23 23 00 D1 01", "UNPACK(" },
    { "MAT A=ZER", "06 01 04 E0 00 D9 EF", "MAT =ZER" },
    { "MAT A=B", "06 01 05 E0 00 D9 E0 01", "MAT присваивание" },
    { "MAT REDIM A(5)", "06 02 06 E0 00 EB E8 05 D0", "MAT REDIM" },
    { "MAT READ A", "06 03 02 E0 00", "MAT READ" },
    { "MAT READ A" "\xC2\xA4", "06 03 02 E0 00", "MAT READ символьного массива" },
    { "MAT READ A,B(2,3)", "06 03 0C E0 00 DE E0 01 EB E8 02 DE E8 03 D0", "MAT READ списком и с новой размерностью" },
    { "MAT INPUT A", "06 04 02 E0 00", "MAT INPUT" },
    { "MAT INPUT A,B(2),C(2,4)", "06 04 13 E0 00 DE E0 01 EB E8 02 D0 DE E0 02 EB E8 02 DE E8 04 D0", "MAT INPUT списком" },
    { "MAT INPUT A" "\xC2\xA4" "(3)8", "06 04 08 E0 00 EB E8 03 D0 E8 08", "MAT INPUT с длиной элемента" },
    { "MAT COPY A" "\xC2\xA4" "TO B" "\xC2\xA4", "06 06 03 00 D1 01", "MAT COPY" },
    { "MAT COPY -A" "\xC2\xA4" "TO B" "\xC2\xA4", "06 06 04 E9 00 D1 01", "MAT COPY с обратным порядком" },
    { "MAT SEARCH A" "\xC2\xA4" ",=\"X\"TO B" "\xC2\xA4", "06 0A 08 00 DE D9 E3 01 58 D1 01", "MAT SEARCH" },
    { "DATA 1,2,3", "29 08 E8 01 E8 02 E8 03 00 00", "DATA числа" },
    { "DATA \"A\",\"B\"", "29 08 E3 01 41 E3 01 42 00 00", "DATA строки" },
    { "READ A,B" "\xC2\xA4", "44 02 00 01", "READ" },
    { "RESTORE", "51 00", "RESTORE голый" },
    { "RESTORE 3", "51 02 E8 03", "RESTORE с числом" },
    { "RESTORE ,100", "51 03 DE 01 00", "RESTORE со строки" },
    { "RESTORE 3,100", "51 05 E8 03 DE 01 00", "RESTORE с числом и строкой" },
    { "INPUT A", "41 01 00", "INPUT" },
    { "INPUT \"ЧТО\",A,B", "41 07 E3 03 FE F4 EF 00 01", "INPUT с приглашением" },
    { "KEYIN A" "\xC2\xA4" ",100,200", "25 05 00 01 00 02 00", "KEYIN" },
    { "SELECT PRINT 05", "54 02 07 05", "SELECT группы" },
    { "SELECT PRINT 0C(132)", "54 05 07 0C EB 00 84", "SELECT с шириной" },
    { "SELECT #3 34", "54 03 00 03 34", "SELECT строки таблицы" },
    { "SELECT DISK 18F", "54 03 0A 18 00", "SELECT DISK" },
    { "SELECT P0", "54 02 05 00", "SELECT паузы" },
    { "CLEAR", "2C 00", "CLEAR" },
    { "CLEAR P", "2C 01 14", "CLEAR P" },
    { "CLEAR P 100,200", "2C 06 14 01 00 DE 02 00", "CLEAR P с диапазоном" },
    { "CLEAR V", "2C 01 11", "CLEAR V" },
    { "CLEAR N", "2C 01 12", "CLEAR N" },
    { "RUN", "2F 00", "RUN" },
    { "RUN 100", "2F 02 01 00", "RUN с номера" },
    { "LIST", "2E 00", "LIST" },
    { "LIST 100", "2E 02 01 00", "LIST с номера" },
    { "LIST 100,200", "2E 05 01 00 DE 02 00", "LIST диапазон" },
    { "SCRATCH F\"ИМЯ\"", "81 06 00 E3 03 E9 ED F1", "SCRATCH" },
    { "SCRATCH F/1C,\"ИМЯ\"", "81 0A 00 DC DE 1C DE E3 03 E9 ED F1", "SCRATCH с адресом" },
    { "SAVE DC F\"ИМЯ\"", "80 06 00 E3 03 E9 ED F1", "SAVE DC" },
    { "SAVE DC F" "\xC2\xA4" "T(\"СТАР\")\"НОВ\"", "80 10 00 D6 D2 EB E3 04 F3 F4 E1 F2 D0 E3 03 EE EF F7", "SAVE DC полный" },
    { "SAVE DC F(\"СТАР\")\"НОВ\"9000,9090", "80 13 00 EB E3 04 F3 F4 E1 F2 D0 E3 03 EE EF F7 90 00 DE 90 90", "SAVE DC с диапазоном строк" },
    { "LOAD DC F\"ИМЯ\"", "7D 06 00 E3 03 E9 ED F1", "LOAD DC" },
    { "LOAD DC F\"ИМЯ\"100,200,100", "7D 0E 00 E3 03 E9 ED F1 01 00 DE 02 00 DE 01 00", "LOAD DC с тремя номерами" },
    { "LOAD DC F\"ИМЯ\"1000,,53", "7D 0C 00 E3 03 E9 ED F1 10 00 DE DE 00 53", "LOAD DC с пропущенным номером" },
    { "LOAD DA F(100)", "72 06 00 EB E7 01 00 D0", "LOAD DA" },
    { "LOAD DA R(100,A)", "72 08 01 EB E7 01 00 DE 00 D0", "LOAD DA с приёмником" },
    { "LOAD DA T#1,(A)1000,,53", "72 0E 02 DB E8 01 DE EB 00 D0 10 00 DE DE 00 53", "LOAD DA сегментом" },
    { "LIST DC F", "7C 01 00", "LIST DC" },
    { "LIST DC F\"ИМЯ\"", "7C 06 00 E3 03 E9 ED F1", "LIST DC с именем" },
    { "LIST DC F/1C,\"ИМЯ\"", "7C 0A 00 DC DE 1C DE E3 03 E9 ED F1", "LIST DC с адресом" },
    { "DATA LOAD DC OPEN T#1,\"ИМЯ\"", "75 0A 02 DB E8 01 DE E3 03 E9 ED F1", "открытие файла на чтение" },
    { "DATA SAVE DC OPEN F(100)\"ИМЯ\"", "78 0B 00 EB E7 01 00 D0 E3 03 E9 ED F1", "создание файла" },
    { "DATA SAVE DC A,B" "\xC2\xA4", "76 03 00 DE 01", "запись логической записи" },
    { "DATA SAVE DC END", "76 01 D7", "концевая запись" },
    { "DATA SAVE DC CLOSE", "77 00", "закрытие" },
    { "DATA LOAD DC A,B" "\xC2\xA4", "74 02 00 01", "чтение записи" },
    { "IF END THEN 100", "1E 02 01 00", "IF END THEN" },
    { "DSKIP 3", "7A 02 E8 03", "DSKIP" },
    { "DBACKSPACE 2", "79 02 E8 02", "DBACKSPACE" },
    { "LIMITS T#1,\"ИМЯ\",A,B,C", "7B 0D 02 DB E8 01 DE E3 03 E9 ED F1 00 01 02", "LIMITS" },
    { "VERIFY F", "83 01 00", "VERIFY" },
    { "VERIFY F(10,20)", "83 06 00 E8 10 DE E8 20", "VERIFY с границами" },
    { "COPY F TO R", "6D 03 00 D1 01", "COPY" },
    { "COPY F(10,20) TO R(30)", "6D 0A 00 E8 10 DE E8 20 D1 01 E8 30", "COPY с границами" },
    { "DATA SAVE BT /34,A" "\xC2\xA4", "68 05 DC DE 34 DE 00", "BT с адресом" },
    { "DATA SAVE BT A" "\xC2\xA4", "68 01 00", "BT без приставки" },
    { "DATA LOAD BT #2,A" "\xC2\xA4", "66 05 DB E8 02 DE 00", "BT по строке таблицы" },
    { "DATA SAVE BA F(100)A" "\xC2\xA4" "()", "6E 08 00 EB E7 01 00 D0 E0 00", "BA" },
    { "DATA LOAD BA F(100)A" "\xC2\xA4" "()", "70 08 00 EB E7 01 00 D0 E0 00", "BA чтение" },
    { "DATA SAVE DA F(100)A,B", "6F 09 00 EB E7 01 00 D0 00 DE 01", "DA" },
    { "DATA LOAD DA F(100)A,B", "71 08 00 EB E7 01 00 D0 00 01", "DA чтение" },
};

} // namespace

int main()
{
    for (unsigned i = 0; i < sizeof(FORMS) / sizeof(FORMS[0]); ++i)
        check(FORMS[i]);
    std::printf("  форм проверено: %u\n",
                static_cast<unsigned>(sizeof(FORMS) / sizeof(FORMS[0])));
    return test::summary("матрица форм языка");
}
