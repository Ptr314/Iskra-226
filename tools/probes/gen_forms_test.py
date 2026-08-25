# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Mikhail Revzin <p3.141592653589793238462643@gmail.com>
# Part of the Iskra-226 project: https://github.com/Ptr314/Iskra-226
# Описание: генератор tests/test_forms.cpp из матрицы форм.
#
# Байты берутся у самого транслятора и должны быть сверены глазами с
# docs/format.md — проба только избавляет от переписывания их руками.
# Перегенерировать имеет смысл лишь тогда, когда матрица пополнилась.

import os
import sys

sys.path.insert(0, os.path.dirname(__file__))
import forms                                                # noqa: E402

OUT = os.path.join(forms.ROOT, 'tests', 'test_forms.cpp')

HEAD = '''// SPDX-License-Identifier: GPL-3.0-or-later
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
        std::printf("  %s: %s\\n", f.text, error.c_str());
        CHECK(false);
        return;
    }
    if (hexs(body) != f.bytes) {
        std::printf("  %s (%s)\\n    ждали  %s\\n    вышло  %s\\n",
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
        std::printf("  %s: обратно — %s\\n", f.text, error.c_str());
        CHECK(false);
        return;
    }
    NameTable again = names;
    unsigned n2 = 0;
    std::vector<uint8_t> body2;
    if (!tokenize_line(back, again, n2, body2, error)) {
        std::printf("  %s: круг — %s\\n", f.text, error.c_str());
        CHECK(false);
        return;
    }
    if (body2 != body) {
        std::printf("  %s (%s): круг не сошёлся\\n    было  %s\\n    стало %s\\n",
                    f.text, f.note, hexs(body).c_str(), hexs(body2).c_str());
        CHECK(false);
    }
}

const Form FORMS[] = {
'''

TAIL = '''};

} // namespace

int main()
{
    for (unsigned i = 0; i < sizeof(FORMS) / sizeof(FORMS[0]); ++i)
        check(FORMS[i]);
    std::printf("  форм проверено: %u\\n",
                static_cast<unsigned>(sizeof(FORMS) / sizeof(FORMS[0])));
    return test::summary("матрица форм языка");
}
'''


def cstr(s):
    """Строка для исходника на C++: ¤ отдельным литералом, чтобы шестнадцатеричная
    последовательность не съела следующую цифру."""
    out = []
    cur = ''
    for ch in s:
        if ch == forms.C:
            if cur:
                out.append('"%s"' % cur)
                cur = ''
            out.append('"\\xC2\\xA4"')
        elif ch == '"':
            cur += '\\"'
        elif ch == '\\':
            cur += '\\\\'
        else:
            cur += ch
    if cur or not out:
        out.append('"%s"' % cur)
    return ' '.join(out)


def main():
    rows = []
    skipped = []
    for text, note in forms.FORMS:
        b = forms.run(text)
        if b.startswith('ОТКАЗ'):
            skipped.append((text, b))
            continue
        rows.append('    { %s, "%s", "%s" },' % (cstr(text), b, note))
    with open(OUT, 'w', encoding='utf-8', newline='') as f:
        f.write(HEAD)
        f.write('\n'.join(rows))
        f.write('\n')
        f.write(TAIL)
    print('форм записано: %d' % len(rows))
    for t, why in skipped:
        print('  пропущено: %-34s %s' % (t, why))


if __name__ == '__main__':
    main()
