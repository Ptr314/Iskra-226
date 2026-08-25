# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Mikhail Revzin <p3.141592653589793238462643@gmail.com>
# Part of the Iskra-226 project: https://github.com/Ptr314/Iskra-226
# Описание: матрица форм языка — текст и ожидаемые байты.
#
# Каждая форма транслируется своей таблицей имён, поэтому индексы переменных
# в ней раздаются с нуля по первому появлению. Проба печатает готовые строки
# для tests/test_forms.cpp; сверять глазами с docs/format.md.

import os
import subprocess
import sys

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), '..', '..'))
ISKRA = os.path.join(ROOT, 'build', 'cmake', 'iskra.exe')
TMP = os.path.join(ROOT, 'build', 'form.txt')

C = '¤'          # ¤ — признак символьной переменной

FORMS = [
    # --- числа и арифметика
    ('A=1',                       'целая константа'),
    ('A=0',                       'ноль'),
    ('A=1.5',                     'дробная'),
    ('A=255',                     'двухбайтовая BCD'),
    ('A=1E6',                     'положительный порядок'),
    ('A=-3',                      'унарный минус'),
    ('A=2^3',                     'степень'),
    ('A=(1+2)*3/4-5',             'скобки и приоритеты'),
    ('A=B+C*D^E',                 'приоритеты без скобок'),
    ('A=#PI',                     'системная константа'),
    ('A=SQR(2*#PI)',              'SQR('),
    ('A=ABS(-1)',                 'ABS('),
    ('A=INT(1.5)',                'INT('),
    ('A=SGN(B)',                  'SGN('),
    ('A=LOG(B)',                  'LOG('),
    ('A=EXP(B)',                  'EXP('),
    ('A=ROUND(B,2)',              'ROUND( — вторая запятая кодируется'),
    ('A=RND(1)',                  'RND('),
    ('A=SQR(ABS(INT(B)))',        'вложенные функции'),
    # --- присваивание
    ('A,B=1',                     'несколько приёмников'),
    ('A(1)=2',                    'элемент массива'),
    ('A(1,2)=3',                  'два измерения'),
    # --- сравнения и связки
    ('IF A=B THEN 20',            'IF ='),
    ('IF A<>B THEN 20',           'IF <>'),
    ('IF A<B THEN 20',            'IF <'),
    ('IF A<=B THEN 20',           'IF <='),
    ('IF A>B THEN 20',            'IF >'),
    ('IF A>=B THEN 20',           'IF >='),
    ('IF A=1ANDB=2THEN 20',       'связка AND'),
    ('IF A=1ORB=2THEN 20',        'связка OR'),
    ('IF A=1THEN 20:GOTO 30',     'IF и хвост строки'),
    # --- управление
    ('GOTO 100',                  'GOTO'),
    ('GOSUB 100',                 'GOSUB'),
    ('RETURN',                    'RETURN'),
    ('ON A GOTO 10,20,30',        'ON GOTO'),
    ('ON A GOSUB 10,20',          'ON GOSUB'),
    ('FOR I=1TO10',               'FOR без шага'),
    ('FOR I=1TO10STEP2',          'FOR с шагом'),
    ('FOR I=10TO1STEP-1',         'отрицательный шаг'),
    ('NEXT I',                    'NEXT'),
    ('END',                       'END'),
    ('STOP',                      'STOP'),
    ('STOP "ЖДУ"',                'STOP с сообщением'),
    ('STOP #',                    'STOP # — форма вне книги'),
    ('REM ТЕКСТ',                 'REM'),
    ('%ОБРАЗЕЦ',                  'оператор образа'),
    # --- ошибки
    ('ON ERROR GOTO 100',         'ON ERROR GOTO'),
    ('ON ERROR THEN 100',         'ON ERROR THEN'),
    ('ON ERROR GOSUB 100',        'ON ERROR GOSUB'),
    ('ON ERROR ',                 'ON ERROR — отмена'),
    # --- помеченные подпрограммы и функции
    ("DEFFN '15",                 "DEFFN'"),
    ("DEFFN '15(A,B)",            "DEFFN' с параметрами"),
    ("DEFFN '15\"LIST\"",         "DEFFN' с текстом клавиши"),
    ("GOSUB '15",                 "GOSUB'"),
    ("GOSUB '15(A+1,B)",          "GOSUB' с выражениями"),
    ('RETURN CLEAR',              'RETURN CLEAR'),
    ('RETURN CLEAR ALL',          'RETURN CLEAR ALL'),
    ('DEFFN A(H)=H+1',            'функция пользователя'),
    ('B=FNA(2)',                  'обращение к функции'),
    ('B=FNA(FNA(1))',             'функция от функции'),
    # --- печать
    ('PRINT ',                    'PRINT без операндов'),
    ('PRINT "A"',                 'литерал'),
    ('PRINT A;B',                 'точка с запятой'),
    ('PRINT A,B',                 'запятая — зоны'),
    ('PRINT ,"A"',                'пустая зона в начале'),
    ('PRINT "A",,B',              'пустая зона в середине'),
    ('PRINT "A",;;B',             'запятая и две точки с запятой'),
    ('PRINT A;',                  'хвостовая точка с запятой'),
    ('PRINT AT(5,14)',            'AT( с двумя аргументами'),
    ('PRINT AT(10,20,15)',        'AT( с тремя'),
    ('PRINT TAB(10);"A"',         'TAB('),
    ('PRINT HEX(0D0A)',           'HEX('),
    ('PRINT /05,"A"',             'адрес устройства'),
    ('PRINT #3,"A"',              'строка таблицы устройств'),
    ('PRINTUSING 500,A,B',        'PRINTUSING по номеру строки'),
    ('PRINTUSING "###",A',        'PRINTUSING с образом-константой'),
    ('HEXPRINT A' + C,            'HEXPRINT'),
    ('HEXPRINT A' + C + ';',      'HEXPRINT без перевода строки'),
    # --- символьные данные
    ('DIM A' + C + '16',          'описание строки'),
    ('DIM A' + C + '(10)8',       'массив строк'),
    ('DIM A(5),B(2,3)',           'числовые массивы'),
    ('COM A,B' + C + '16',        'общие переменные'),
    ('A' + C + '="X"',            'присваивание строки'),
    ('A' + C + '(2)="X"',         'элемент массива строк'),
    ('A' + C + '=HEX(0D)',        'HEX( справа'),
    ('STR(A' + C + ',1,2)="X"',   'STR( слева'),
    ('B=LEN(A' + C + ')',         'LEN'),
    ('B=NUM(A' + C + ')',         'NUM'),
    ('B=VAL(A' + C + ')',         'VAL'),
    ('B=VAL(A' + C + ',2)',       'VAL с числом байт'),
    ('B=POS(A' + C + '="X")',     'POS с условием'),
    ('B' + C + '=STR(A' + C + ',1,2)', 'STR( справа'),
    ('B' + C + '=STR(A' + C + ',3)',   'STR( без длины'),
    ('BIN(A' + C + ')=B',         'BIN('),
    ('BIN(A' + C + ',2)=B',       'BIN( в два байта'),
    ('INIT(20)A' + C + ',B' + C,  'INIT'),
    ('CONVERT A TO B' + C,        'CONVERT число в строку'),
    ('CONVERT A' + C + ' TO B',   'CONVERT строка в число'),
    ('CONVERT A TO B' + C + ',(###.##)', 'CONVERT с образом'),
    ('LINPUT A' + C,              'LINPUT'),
    ('LINPUT "ВВОД",A' + C,       'LINPUT с приглашением'),
    ('REPLACE K,A' + C + ',"X","Y"', 'REPLACE'),
    ('REPLACE K,A' + C + ',"X"',  'REPLACE без замены'),
    (C + 'TRAN(A' + C + ',B' + C + ')', 'TRAN табличный'),
    (C + 'TRAN(A' + C + ',B' + C + ')R', 'TRAN списковый'),
    # --- операции над байтами
    ('AND(A' + C + ',B' + C + ')', 'AND( с переменной'),
    ('AND(A' + C + ',DF)',        'AND( с маской'),
    ('OR(A' + C + ',FF)',         'OR('),
    ('XOR(A' + C + ',0F)',        'XOR('),
    ('BOOL 8(A' + C + ',B' + C + ')', 'BOOL'),
    ('ADD(A' + C + ',B' + C + ')', 'ADD'),
    ('ADD(A' + C + ',02)',        'ADD с маской'),
    ('ADD C(A' + C + ',B' + C + ')', 'ADD C — свой глагол 63'),
    ('ADDC(A' + C + ',FF)',       'ADD C слитно, с маской'),
    ('ROTATE(A' + C + ',4)',      'ROTATE'),
    ('ROTATE C(A' + C + ',4)',    'ROTATE C'),
    ('PACK(##.##)A' + C + 'FROM B', 'PACK('),
    ('UNPACK(##.##)A' + C + 'TO B', 'UNPACK('),
    # --- матричные
    ('MAT A=ZER',                 'MAT =ZER'),
    ('MAT A=B',                   'MAT присваивание'),
    ('MAT REDIM A(5)',            'MAT REDIM'),
    ('MAT COPY A' + C + 'TO B' + C, 'MAT COPY'),
    ('MAT COPY -A' + C + 'TO B' + C, 'MAT COPY с обратным порядком'),
    ('MAT SEARCH A' + C + ',="X"TO B' + C, 'MAT SEARCH'),
    # --- задание констант
    ('DATA 1,2,3',                'DATA числа'),
    ('DATA "A","B"',              'DATA строки'),
    ('READ A,B' + C,              'READ'),
    ('RESTORE',                   'RESTORE голый'),
    ('RESTORE 3',                 'RESTORE с числом'),
    ('RESTORE ,100',              'RESTORE со строки'),
    ('RESTORE 3,100',             'RESTORE с числом и строкой'),
    # --- ввод
    ('INPUT A',                   'INPUT'),
    ('INPUT "ЧТО",A,B',           'INPUT с приглашением'),
    ('KEYIN A' + C + ',100,200',  'KEYIN'),
    # --- SELECT
    ('SELECT PRINT 05',           'SELECT группы'),
    ('SELECT PRINT 0C(132)',      'SELECT с шириной'),
    ('SELECT #3 34',              'SELECT строки таблицы'),
    ('SELECT DISK 18F',           'SELECT DISK'),
    ('SELECT P0',                 'SELECT паузы'),
    # --- диалоговые внутри программы
    ('CLEAR',                     'CLEAR'),
    ('CLEAR P',                   'CLEAR P'),
    ('CLEAR P 100,200',           'CLEAR P с диапазоном'),
    ('CLEAR V',                   'CLEAR V'),
    ('CLEAR N',                   'CLEAR N'),
    ('RUN',                       'RUN'),
    ('RUN 100',                   'RUN с номера'),
    ('LIST',                      'LIST'),
    ('LIST 100',                  'LIST с номера'),
    ('LIST 100,200',              'LIST диапазон'),
    # --- диск
    ('SCRATCH F"ИМЯ"',            'SCRATCH'),
    ('SCRATCH F/1C,"ИМЯ"',        'SCRATCH с адресом'),
    ('SAVE DC F"ИМЯ"',            'SAVE DC'),
    ('SAVE DC F' + C + 'T("СТАР")"НОВ"', 'SAVE DC полный'),
    ('SAVE DC F("СТАР")"НОВ"9000,9090', 'SAVE DC с диапазоном строк'),
    ('LOAD DC F"ИМЯ"',            'LOAD DC'),
    ('LOAD DC F"ИМЯ"100,200,100', 'LOAD DC с тремя номерами'),
    ('LIST DC F',                 'LIST DC'),
    ('LIST DC F"ИМЯ"',            'LIST DC с именем'),
    ('LIST DC F/1C,"ИМЯ"',        'LIST DC с адресом'),
    ('DATA LOAD DC OPEN T#1,"ИМЯ"', 'открытие файла на чтение'),
    ('DATA SAVE DC OPEN F(100)"ИМЯ"', 'создание файла'),
    ('DATA SAVE DC A,B' + C,      'запись логической записи'),
    ('DATA SAVE DC END',          'концевая запись'),
    ('DATA SAVE DC CLOSE',        'закрытие'),
    ('DATA LOAD DC A,B' + C,      'чтение записи'),
    ('IF END THEN 100',           'IF END THEN'),
    ('DSKIP 3',                   'DSKIP'),
    ('DBACKSPACE 2',              'DBACKSPACE'),
    ('LIMITS T#1,"ИМЯ",A,B,C',    'LIMITS'),
    ('VERIFY F',                  'VERIFY'),
    ('VERIFY F(10,20)',           'VERIFY с границами'),
    ('COPY F TO R',               'COPY'),
    ('COPY F(10,20) TO R(30)',    'COPY с границами'),
    ('DATA SAVE BT /34,A' + C,    'BT с адресом'),
    ('DATA SAVE BT A' + C,        'BT без приставки'),
    ('DATA LOAD BT #2,A' + C,     'BT по строке таблицы'),
    ('DATA SAVE BA F(100)A' + C + '()', 'BA'),
    ('DATA LOAD BA F(100)A' + C + '()', 'BA чтение'),
    ('DATA SAVE DA F(100)A,B',    'DA'),
    ('DATA LOAD DA F(100)A,B',    'DA чтение'),
]


def run(text):
    with open(TMP, 'w', encoding='utf-8') as f:
        f.write('10 ' + text + '\n')
    r = subprocess.run([ISKRA, '--tok', TMP, '-b'], capture_output=True)
    out = r.stdout.decode('utf-8', 'replace')
    for line in out.splitlines():
        if line.startswith('10 = '):
            return line[5:]
        if ' ??? ' in line:
            return 'ОТКАЗ: ' + line.split(' ??? ', 1)[1]
    return 'ОТКАЗ: нет вывода'


def main():
    for text, note in FORMS:
        print('%-30s %-46s %s' % (text, run(text), note))


if __name__ == '__main__':
    main()
