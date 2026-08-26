# -*- coding: utf-8 -*-
"""Отдельный детокенизатор: шесть пар файлов, без транслятора.

Собирает из src/core/ то и только то, что нужно обратной трансляции
«токены → текст», чтобы её можно было взять в другой проект. Текстовая
форма программы при этом оглушается: за ней тянутся tokenize.* и
text_lexer.*, а детокенизатору транслятор не нужен вовсе.

    py tools/detok_min.py [КУДА] [--prefix ПУТЬ]

КУДА по умолчанию build/detok-min. Без --prefix выходит отдельный пакет:
файлы в core/, включаются как "core/detokenize.h", рядом пример сборки.
С --prefix они ложатся прямо в КУДА, а включаются как "ПУТЬ/detokenize.h" —
так их принимает чужой проект со своей раскладкой каталогов:

    py tools/detok_min.py …/src/viewers/iskra226 --prefix viewers/iskra226

Единственный источник правды — сам эмулятор: скрипт копирует его файлы, а
не держит их вторую редакцию. Всякое несовпадение с ожидаемым он считает
отказом, а не поводом собрать что попало.
"""
import io
import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SRC = os.path.join(ROOT, 'src')

# Шесть пар. Порядок — снизу вверх по зависимостям, для читаемости списка.
UNITS = ['number', 'names', 'program', 'expr', 'byte_source', 'detokenize']

# Что вырезаем из program.cpp: единственная связь с транслятором.
CUT_INCLUDE = '#include "core/tokenize.h"\n'
CUT_FROM = 'bool ProgramImage::load_text_file'
CUT_TO = 'bool ProgramImage::load_stream'

STUB = u'''// Текстовая форма здесь оглушена: она тянет за собой транслятор
// (core/tokenize.* и core/text_lexer.*), а обратной трансляции он не нужен
// вовсе. В самом эмуляторе этот метод собирает листинг из секторов —
// строки там разделены байтом 85 — и зовёт tokenize().
bool ProgramImage::load_text_file(const std::vector<uint8_t> &,
                                  std::string & error)
{
    error = "текстовая форма программы здесь не поддерживается";
    return false;
}

'''

EXAMPLE = r'''// Пример: файл, снятый с дискеты, в текстовый листинг.
//     g++ -std=c++11 -I. -o detok example.cpp core/*.cpp
//     ./detok STAT04.bin

#include <cstdio>
#include <string>
#include <vector>

#include "core/detokenize.h"

int main(int argc, char ** argv)
{
    if (argc < 2) { std::printf("укажите файл\n"); return 1; }

    std::FILE * f = std::fopen(argv[1], "rb");
    if (!f) { std::printf("нет файла\n"); return 1; }
    std::vector<unsigned char> bytes;
    char buf[4096];
    std::size_t n;
    while ((n = std::fread(buf, 1, sizeof(buf), f)) > 0)
        bytes.insert(bytes.end(), buf, buf + n);
    std::fclose(f);

    iskra::ProgramImage img;
    std::string error;
    if (!img.load_file(bytes, error)) {
        std::printf("разбор: %s\n", error.c_str());
        return 1;
    }

    // Имена переменных придумывает сама детокенизация: в потоке их нет.
    iskra::NameTable names;
    std::string koi8;
    if (!iskra::detokenize(img, names, koi8, error))
        std::printf("детокенизация: %s\n", error.c_str());

    // Листинг в КОИ-8; перекодировка — забота вызывающего.
    std::fwrite(koi8.data(), 1, koi8.size(), stdout);
    return 0;
}
'''

README = u'''# Детокенизатор BASIC 02 «Искра 226»

Обратная трансляция «токены → текст»: файл программы, снятый с дискеты, в
текстовый листинг. Вынуто из эмулятора
[Iskra-226](https://github.com/Ptr314/Iskra-226) скриптом `tools/detok_min.py`;
править эти файлы здесь незачем — правка потеряется при следующей выемке.

Включаются так:

```cpp
#include "@PREFIX@/detokenize.h"
```

C++11, сторонних библиотек нет вовсе, ни исключений, ни `std::regex`, ни
локалей. Собирается всем, начиная с GCC 4.9. Лицензия — GPL-3.0-or-later,
как у эмулятора.

## Состав

| Файл | Назначение |
|---|---|
| `core/detokenize.*` | сама обратная трансляция |
| `core/byte_source.*` | байты операндов → лексемы; знает двузначность токенов |
| `core/expr.*` | лексемы и их двузначность |
| `core/names.*` | таблица имён переменных |
| `core/number.*` | десятичная арифметика, 13 значащих разрядов |
| `core/program.*` | образ программы: поток строк и таблицы переменных |

## Как звать

```cpp
iskra::ProgramImage img;
std::string error;
img.load_file(bytes, error);        // весь файл с дискеты, с заголовком
                                    // без заголовка — load_stream()

iskra::NameTable names;             // выход, а не вход: см. ниже
std::string koi8;
iskra::detokenize(img, names, koi8, error);
```

Построчно — `detokenize_line()`: одна строка, которую детокенизатор не
разобрал, не должна съедать весь листинг.

## Что надо знать

- **Листинг в КОИ-8.** Перекодировка сюда не входит; в эмуляторе она в
  `core/koi8.*`, и та пара ни от чего не зависит.
- **Имён переменных в потоке нет вовсе**, есть индексы, и `A0` в листинге
  придумано детокенизатором. Обратно в токены тот же листинг сойдётся
  только с той же таблицей имён.
- **Отказ не значит пустой листинг.** `detokenize()` возвращает `false` на
  первой неразобранной строке, а `koi8` заполнен по неё включительно.
- **Текстовая форма программы оглушена.** Младший бит признака в заголовке
  файла различает два представления, и текстовое здесь не грузится:
  `load_text_file()` возвращает отказ. В эмуляторе оно транслируется при
  загрузке — вместе с ним пришлось бы взять весь транслятор.
'''


def cut_text_form(text):
    """Оглушить load_text_file и убрать за ней include транслятора."""
    if text.count(CUT_INCLUDE) != 1:
        sys.exit('program.cpp: не нашёлся ' + CUT_INCLUDE.strip())
    text = text.replace(CUT_INCLUDE, '')

    a = text.find(CUT_FROM)
    b = text.find(CUT_TO)
    if a < 0 or b < a:
        sys.exit('program.cpp: не нашлась load_text_file перед load_stream')
    return text[:a] + STUB + text[b:]


def parse_args(argv):
    out = None
    prefix = None
    i = 0
    while i < len(argv):
        if argv[i] == '--prefix':
            i += 1
            if i >= len(argv):
                sys.exit('--prefix: не задан путь')
            prefix = argv[i].replace(chr(92), '/').strip('/')
        elif argv[i].startswith('--'):
            sys.exit('неизвестный ключ: ' + argv[i])
        elif out is None:
            out = argv[i]
        else:
            sys.exit('лишний довод: ' + argv[i])
        i += 1
    return out, prefix


def write(path, text):
    io.open(path, 'w', encoding='utf-8', newline='\n').write(text)


def main():
    out, prefix = parse_args(sys.argv[1:])
    embed = prefix is not None
    if out is None:
        out = os.path.join(ROOT, 'build', 'detok-min')
    if prefix is None:
        prefix = 'core'

    # Отдельным пакетом файлы лежат в core/, вложенным — прямо в КУДА:
    # чужой проект уже назвал каталог сам, и второй core/ внутри лишний.
    dest = out if embed else os.path.join(out, 'core')
    if not os.path.isdir(dest):
        os.makedirs(dest)

    allowed = set('core/' + unit + '.h' for unit in UNITS)

    for unit in UNITS:
        for ext in ('.h', '.cpp'):
            name = unit + ext
            text = io.open(os.path.join(SRC, 'core', name), encoding='utf-8').read()
            if name == 'program.cpp':
                text = cut_text_form(text)

            # Ничего лишнего: всякий чужой include значит, что состав
            # шестёрки устарел и её надо пересматривать, а не собирать.
            for inc in re.findall(r'#include "([^"]+)"', text):
                if inc not in allowed:
                    sys.exit('%s тянет %s — состав устарел' % (name, inc))

            text = text.replace('#include "core/', '#include "%s/' % prefix)
            write(os.path.join(dest, name), text)

    write(os.path.join(dest, 'README.md'), README.replace('@PREFIX@', prefix))
    if not embed:
        write(os.path.join(out, 'example.cpp'), EXAMPLE)

    print('готово: %s' % dest)
    print('%d пар, включаются как "%s/detokenize.h"' % (len(UNITS), prefix))


main()
