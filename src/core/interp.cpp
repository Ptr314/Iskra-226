// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Mikhail Revzin <p3.141592653589793238462643@gmail.com>
// Part of the Iskra-226 project: https://github.com/Ptr314/Iskra-226
// Description: исполнение оттранслированной программы прямо из потока токенов

#include "core/interp.h"

#include "core/catalog.h"
#include "core/detokenize.h"
#include "core/tokenize.h"
#include "core/koi8.h"
#include "core/disk_record.h"
#include "core/image.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

namespace iskra {

namespace {
    const unsigned ZONE = 16;      // «каждая строка условно делится на 5 зон»

    std::string num_str(unsigned v)
    {
        char b[16];
        std::sprintf(b, "%u", v);
        return b;
    }

    // ФАУ устройства пишут двумя шестнадцатеричными цифрами.
    std::string hex2_str(unsigned v)
    {
        char b[8];
        std::sprintf(b, "%02X", v & 0xFF);
        return b;
    }
}

// Образ CONVERT — одно описание формата и ничего кроме него
// (руководство, разд. 13.6). Разбор и подстановка общие с PRINTUSING,
// расходятся эти операторы в двух местах: CONVERT заполняет незанятые
// разряды целой части нулями, а не пробелами, и не помещающееся число
// считает ошибкой, а не печатает сам образ.
bool format_by_image(const Number & value, const std::string & image,
                     std::string & out, std::string & error)
{
    ImageField f;
    if (!image_single_field(image, f)) {
        error = "непонятный образ CONVERT: " + image;
        return false;
    }
    if (!f.ip && !f.fp) { error = "в образе CONVERT нет ни одного знака #"; return false; }
    if (!image_number(value, f, true, out)) {
        error = "число не помещается в образ CONVERT";
        return false;
    }
    return true;
}


// Шапка оператора: глагол (один или два байта) и длина операндов.
bool stmt_head(const std::vector<uint8_t> & body, unsigned at,
               unsigned & verb, unsigned & ops_at, unsigned & len)
{
    if (at + 1 >= body.size()) return false;
    unsigned p = at;
    verb = body[p++];
    if (verb == 0x06) {
        if (p >= body.size()) return false;
        verb = 0x0600 | body[p++];
    }
    if (p >= body.size()) return false;
    len = body[p++];
    ops_at = p;
    return ops_at + len <= body.size();
}

Interp::Interp(ProgramImage & img, Host & host)
    : img_(img), host_(host), store_(img.vars()), labels_ready_(false),
      funcs_ready_(false), fn_depth_(0),
      data_ready_(false), data_i_(0), data_off_(0), end_seen_(false),
      li_(0), off_(0), next_off_(0), jumped_(false), stopped_(false),
      max_steps_(0), skip_machine_(false)
{
    fnres_.owner = this;
}

bool Interp::fail(const std::string & m)
{
    if (error_.empty()) {
        error_ = m;
        if (li_ != DIRECT && li_ < img_.line_count())
            error_ = "строка " + num_str(img_.line(li_).number) + ": " + m;
    }
    return false;
}

bool Interp::variable(unsigned index, Number & out) const
{
    // Хранилище отдаёт ячейку только неконстантно; для проверок хватает
    // копии через тот же путь.
    VarStore & s = const_cast<VarStore &>(store_);
    Number * cell = 0;
    std::string err;
    if (!s.slot(index, 0, 0, cell, err)) return false;
    out = *cell;
    return true;
}

void Interp::emit(const std::string & koi8)
{
    if (koi8.empty()) return;
    host_.screen().write(reinterpret_cast<const uint8_t *>(koi8.data()),
                         static_cast<unsigned>(koi8.size()));
}

void Interp::emit_newline()
{
    Screen & s = host_.screen();
    s.put(CC_CR);
    s.put(CC_DOWN);
}

void Interp::emit_zone()
{
    Screen & s = host_.screen();
    const unsigned col = s.col();
    unsigned next = ((col - 1) / ZONE + 1) * ZONE + 1;
    if (next > SCREEN_COLS) {
        // «если предыдущее значение печаталось в последней зоне, следующее
        // будет печататься в первой зоне следующей строки»
        emit_newline();
        return;
    }
    s.at(s.row(), next);
}

// Оператор `%` и оператор `PRINTUSING` печатают на устройство группы
// PRINT, а не на консольное: «PRINT — устройство вывода для операторов
// PRINTUSING, HEXPRINT и MATPRINT» (руководство, разд. 11.5). По умолчанию
// это адрес 05, то есть экран; `SELECT PRINT 0C` уводит вывод на АЦПУ.
void Interp::emit_group(DeviceGroup g, const std::string & koi8)
{
    if (dev_.addr(g) == 0x05) { emit(koi8); return; }
    for (std::size_t i = 0; i < koi8.size(); ++i)
        host_.print_char(static_cast<uint8_t>(koi8[i]));
}

void Interp::emit_group_newline(DeviceGroup g)
{
    if (dev_.addr(g) == 0x05) { emit_newline(); return; }
    host_.print_char(CC_CR);
    host_.print_char(CC_DOWN);
}

void Interp::emit_print(const std::string & koi8) { emit_group(DG_PRINT, koi8); }
void Interp::emit_print_newline() { emit_group_newline(DG_PRINT); }

// --- присваивание -----------------------------------------------------------

bool Interp::assign_string(Stream & st, const Evaluator::Target & t,
                           const std::string & value)
{
    Value v;
    v.is_str = true;
    v.str = value;
    if (!st.ev.store(t, v)) return fail(st.ev.error());
    return true;
}

// --- обработка ошибок ------------------------------------------------------

bool Interp::machine_error(const char * code, const std::string & m)
{
    err_code_ = code;
    return fail(m);
}

bool Interp::handle_error()
{
    if (trap_.mode == EM_OFF || err_code_.empty()) return false;

    const std::string code = err_code_;
    err_code_.clear();
    error_.clear();

    if (trap_.has_targets) {
        // «В переменную 1 заносится код ошибки, в переменную 2 —
        // четырёхзначный номер программной строки» (руководство, разд. 11.6).
        char buf[16];
        std::sprintf(buf, "%04u", (li_ != DIRECT && li_ < img_.line_count())
                                      ? img_.line(li_).number : 0u);
        std::string err;
        VarStore::StrLoc a, b;
        if (!store_.str_element(trap_.target_a, 0, 0, a, err)) return fail(err);
        if (!store_.str_element(trap_.target_b, 0, 0, b, err)) return fail(err);
        for (unsigned i = 0; i < a.len; ++i)
            (*a.data)[a.off + i] = (i < code.size()) ? code[i] : ' ';
        const std::string num = buf;
        for (unsigned i = 0; i < b.len; ++i)
            (*b.data)[b.off + i] = (i < num.size()) ? num[i] : ' ';
    }

    // THEN возвращает на оператор ПОСЛЕ ошибочного, GOSUB — на него самого.
    if (trap_.mode == EM_THEN)
        calls_.push_back(std::make_pair(li_, next_off_));
    else if (trap_.mode == EM_GOSUB)
        calls_.push_back(std::make_pair(li_, off_));

    return jump(trap_.line);
}

// --- дисковая приставка ----------------------------------------------------

bool Interp::disk_prefix(Stream & st, bool with_device, Disk & d,
                         bool allow_verify)
{
    bool has_device = false, removable = false;
    unsigned device = 2;
    uint8_t b = 0;

    if (with_device && st.src.peek_raw_byte(b) && b <= 2) {
        st.src.skip(1);
        has_device = true;
        device = b;
    }
    // У DSKIP и DBACKSPACE байт D6 значит BEG, а не «¤»: приставка с
    // контрольным считыванием у них не встречается.
    if (allow_verify && st.src.peek_raw_byte(b) && b == 0xD6) st.src.skip(1);

    bool has_addr = false;
    unsigned addr = 0;
    if (st.src.peek_raw_byte(b) && b == 0xDC) {
        st.src.skip(1);
        uint8_t de = 0, a = 0;
        if (!st.src.take_raw_byte(de) || de != 0xDE) return fail("после DC нет DE");
        if (!st.src.take_raw_byte(a)) return fail("нет адреса устройства");
        has_addr = true;
        addr = a;
        if (st.src.peek_raw_byte(b) && b == 0xDE) st.src.skip(1);
    }

    d.row = 0;                                  // без `#n` работает строка #0
    if (st.src.peek_raw_byte(b) && b == 0xDB) {
        st.src.skip(1);
        Number n;
        if (!st.ev.number(n)) return fail(st.ev.error());
        long v = 0;
        if (!n.floor_to_int(v) || v < 0 || v >= static_cast<long>(DeviceTable::ROWS))
            return fail("номер строки таблицы устройств вне 0…7");
        d.row = static_cast<unsigned>(v);
        Tok t;
        if (!st.ev.parser().peek(t, false)) return fail(st.ev.error());
        if (t.t == Tok::COMMA) st.ev.parser().consume();
        else st.ev.parser().unpeek();
    }

    DeviceRow & r = dev_.row(d.row);
    if (!has_addr) addr = r.addr;
    if (!addr) addr = dev_.row(0).addr;         // строка не настроена — как #0
    removable = r.removable;
    // «Если в операторе открытия файла указан тип диска, отличный от того,
    // который был назначен по оператору SELECT, то тип диска берётся из
    // оператора открытия файла» (руководство, разд. 18.10). Буква T значит
    // «взять из таблицы устройств», её тип не меняет.
    if (has_device && device < 2) removable = (device == 1);

    if (!DeviceTable::drive_index(static_cast<uint8_t>(addr), removable, d.drive))
        return fail("неизвестный адрес дискового устройства");
    if (!host_.disk_sectors(d.drive)) return fail("дисковода нет");

    r.addr = static_cast<uint8_t>(addr);
    r.removable = removable;
    return true;
}

// --- операторы --------------------------------------------------------------

bool Interp::do_print(Stream & st)
{
    bool last_was_at = false;
    bool newline = true;

    for (;;) {
        Tok t;
        if (!st.ev.parser().peek(t, true)) return fail(st.ev.error());
        if (t.t == Tok::END) break;

        if (t.t == Tok::COMMA) { st.ev.parser().consume(); emit_zone(); newline = false; continue; }
        if (t.t == Tok::SEMI)  { st.ev.parser().consume(); newline = false; continue; }

        if (t.t == Tok::FN_TAB) {
            // «позиции строки нумеруются с нуля» (разд. 4.4), и курсор
            // двигается только вправо.
            st.ev.parser().consume();
            Number n;
            if (!st.ev.number(n)) return fail(st.ev.error());
            Tok c;
            if (!st.ev.parser().take(c, false) || c.t != Tok::RPAR)
                return fail("TAB( не закрыт");
            long v = 0;
            n.to_int(v);
            const unsigned col = static_cast<unsigned>(v < 0 ? 0 : v) + 1;
            if (col > host_.screen().col() && col <= SCREEN_COLS)
                host_.screen().at(host_.screen().row(), col);
            last_was_at = false;
            newline = true;
        } else if (t.t == Tok::FN_AT) {
            // У AT( закрывающей скобки в потоке нет вовсе.
            st.ev.parser().consume();
            Number a[3];
            unsigned n = 0;
            for (;;) {
                if (n >= 3) return fail("у AT( не больше трёх аргументов");
                if (!st.ev.number(a[n++])) return fail(st.ev.error());
                Tok c;
                if (!st.ev.parser().peek(c, false)) return fail(st.ev.error());
                if (c.t != Tok::COMMA) break;
                st.ev.parser().consume();
            }
            if (n < 2) return fail("у AT( меньше двух аргументов");
            long rv = 0, cv = 0;
            a[0].to_int(rv);
            a[1].to_int(cv);
            host_.screen().at(static_cast<unsigned>(rv < 1 ? 1 : rv),
                              static_cast<unsigned>(cv < 1 ? 1 : cv));
            if (n > 2) {
                long nv = 0;
                a[2].to_int(nv);
                if (nv > 0) host_.screen().erase(static_cast<unsigned>(nv));
            }
            last_was_at = true;
            newline = true;
        } else {
            Value v;
            if (!st.ev.expr(v)) return fail(st.ev.error());
            if (v.is_str) {
                emit(v.str);
            } else {
                // «с учетом знака перед числом и пробела после числа»
                emit(v.num.to_display());
                emit(" ");
            }
            last_was_at = false;
            newline = true;
        }

        // Разделитель после значения читается в позиции операции: разбор
        // выражения уже заглянул вперёд именно в ней (CLAUDE.md).
        if (!st.ev.parser().peek(t, false)) return fail(st.ev.error());
        if (t.t == Tok::COMMA) { st.ev.parser().consume(); emit_zone(); newline = false; }
        else if (t.t == Tok::SEMI) { st.ev.parser().consume(); newline = false; }
        else break;
    }

    // PRINT AT(...) курсор только ставит и перевода строки не делает:
    // «курсор устанавливается в тридцатую позицию восьмой строки экрана»,
    // а печать следующего оператора идёт с этой позиции (пример 17.5).
    if (newline && !last_was_at) emit_newline();
    return true;
}

// Образ печати лежит в операторе `%` отдельной строкой: он неисполняемый и
// «может размещаться в любой строке программы» (руководство, разд. 16.1).
// Операнд `%` — сырой текст, без длины и без префикса (docs/format.md,
// разд. 5).
bool Interp::image_of_line(unsigned number, std::string & image) const
{
    unsigned li = 0;
    if (!img_.find(number, li)) return false;
    const std::vector<uint8_t> & body = img_.line(li).body;
    unsigned at = 0;
    while (at < body.size()) {
        unsigned verb = 0, ops = 0, len = 0;
        if (!stmt_head(body, at, verb, ops, len)) return false;
        if (verb == 0x3F) {
            image.assign(reinterpret_cast<const char *>(&body[0]) + ops, len);
            return true;
        }
        at = ops + len;
    }
    return false;
}

// «Оператор PRINTUSING и оператор задания формата % используются совместно
// для управления размещением данных при печати» (руководство, гл. 16).
// Первый операнд — либо номер строки с оператором `%`, либо сам образ
// символьным значением: «формат печати может задаваться также
// непосредственно в операторе PRINTUSING в виде символьной константы или
// символьной переменной» (разд. 16.2).
bool Interp::do_printusing(Stream & st)
{
    Value first;
    if (!st.ev.expr(first)) return fail(st.ev.error());

    std::string image;
    if (first.is_str) {
        image = first.str;
    } else {
        long n = 0;
        if (!first.num.to_int(n) || n < 0)
            return fail("PRINTUSING: номер строки образа не целый");
        // Кода у этой ошибки мы не знаем — выдумывать его нельзя.
        if (!image_of_line(static_cast<unsigned>(n), image))
            return machine_error(err::UNKNOWN,
                                 "PRINTUSING: в строке " + num_str(static_cast<unsigned>(n))
                                 + " нет оператора %");
    }

    // Элементы печати «разделяются запятыми или точкой с запятой»
    // (разд. 16.1). Запоминаем разделитель, стоящий ПЕРЕД элементом: именно
    // он решает, переводить ли строку, когда образ пойдёт по второму разу.
    std::vector<Value> items;
    std::vector<bool> semi;
    bool trailing_semi = false;
    for (;;) {
        Tok t;
        if (!st.ev.parser().peek(t, false)) return fail(st.ev.error());
        bool is_semi;
        if (t.t == Tok::COMMA) is_semi = false;
        else if (t.t == Tok::SEMI) is_semi = true;
        else break;
        st.ev.parser().consume();
        if (st.src.at_end()) { trailing_semi = is_semi; break; }
        Value v;
        if (!st.ev.expr(v)) return fail(st.ev.error());
        items.push_back(v);
        semi.push_back(is_semi);
    }

    // Образ без единого описания формата печатается целиком: «оператор %
    // состоит лишь из символов, отличных от символа #» (пример 16.4).
    ImageField probe;
    if (!image_next_field(image, 0, probe)) {
        emit_print(image);
        if (!trailing_semi) emit_print_newline();
        return true;
    }

    unsigned pos = 0;
    std::size_t next = 0;
    for (;;) {
        ImageField f;
        if (!image_next_field(image, pos, f)) {
            emit_print(image.substr(pos));
            if (next >= items.size()) break;
            // «Список элементов не исчерпан, но в операторе % нет больше
            // описаний формата. Происходит переход на начало следующей
            // строки» (пример 16.11) — если только его не подавили точкой
            // с запятой (пример 16.12).
            if (!semi[next]) emit_print_newline();
            pos = 0;
            continue;
        }
        if (next >= items.size()) {
            // Элементов не хватило на образ. Книга такого случая не
            // разбирает; печатаем остаток как есть, и незаполненные разряды
            // видны знаками #, как и при переполнении.
            emit_print(image.substr(pos));
            break;
        }

        emit_print(image.substr(pos, f.at - pos));
        const Value & v = items[next];
        std::string text;
        if (v.is_str) {
            image_string(v.str, f, text);
        } else if (!image_number(v.num, f, false, text)) {
            // «Если попытаться напечатать число 5555 по формату ###, то
            // вместо числа будет напечатано описание формата» (пример 16.7).
            text = image.substr(f.at, f.len);
        }
        emit_print(text);
        ++next;
        pos = f.at + f.len;
    }

    if (!trailing_semi) emit_print_newline();
    return true;
}

// «В Бейсике „Искры 226“ предусмотрен специальный оператор вывода
// шестнадцатеричных кодов значений символьных переменных HEXPRINT»
// (руководство, разд. 13.5): байты печатаются парами цифр вплотную.
// Точка с запятой ничего не разделяет, запятая переводит строку.
bool Interp::do_hexprint(Stream & st)
{
    static const char * HEXD = "0123456789ABCDEF";
    bool newline = true;
    while (!st.src.at_end()) {
        Value v;
        if (!st.ev.expr(v)) return fail(st.ev.error());
        std::string out;
        if (v.is_str) {
            for (std::size_t i = 0; i < v.str.size(); ++i) {
                const unsigned char b = static_cast<unsigned char>(v.str[i]);
                out += HEXD[b >> 4];
                out += HEXD[b & 15];
            }
        } else {
            // Число в записи файла данных — восемь байт (разд. 2 формата).
            uint8_t buf[8];
            v.num.to_disk8(buf);
            for (unsigned i = 0; i < 8; ++i) {
                out += HEXD[buf[i] >> 4];
                out += HEXD[buf[i] & 15];
            }
        }
        emit_print(out);

        Tok t;
        if (!st.ev.parser().peek(t, false)) return fail(st.ev.error());
        if (t.t == Tok::COMMA) {
            st.ev.parser().consume();
            emit_print_newline();
            newline = false;
        } else if (t.t == Tok::SEMI) {
            st.ev.parser().consume();
            newline = false;
        } else {
            st.ev.parser().unpeek();
            newline = true;
            break;
        }
        if (!st.src.at_end()) newline = true;
    }
    if (newline) emit_print_newline();
    return true;
}

bool Interp::do_select(Stream & st)
{
    for (;;) {
        uint8_t code = 0;
        if (!st.src.take_raw_byte(code)) break;

        bool disk = false;
        unsigned addr = 0, row = 0, width = 0;
        bool has_addr = false;

        if (code == 0x00) {
            uint8_t r = 0, a = 0;
            if (!st.src.take_raw_byte(r) || !st.src.take_raw_byte(a))
                return fail("SELECT #: нет адреса");
            row = r;
            addr = a;
            disk = true;
        } else if (code == 0x05) {
            uint8_t p = 0;
            unsigned pause = 0;
            if (st.src.peek_raw_byte(p) && p <= 9) { st.src.skip(1); pause = p; }
            // `SELECT P` без цифры снимает паузу (руководство, разд. 11.4).
            dev_.set_pause(pause);
        } else {
            uint8_t a = 0;
            if (!st.src.take_raw_byte(a)) return fail("SELECT: нет адреса");
            addr = a;
            has_addr = true;
            disk = (code == 0x0A);
        }

        uint8_t b = 0;
        bool removable = false;
        if (disk) {
            if (st.src.peek_raw_byte(b) && (b == 0x00 || b == 0x01)) {
                st.src.skip(1);
                removable = (b == 0x01);
            }
        } else if (st.src.peek_raw_byte(b) && b == 0xEB) {
            st.src.skip(1);
            uint8_t hi = 0, lo = 0;
            if (!st.src.take_raw_byte(hi) || !st.src.take_raw_byte(lo))
                return fail("SELECT: нет ширины строки");
            width = (static_cast<unsigned>(hi) << 8) | lo;
        }

        if (code == 0x00) {
            if (!DeviceTable::valid_row(row))
                return fail("SELECT #: строки " + num_str(row) + " нет");
            dev_.select_row(row, static_cast<uint8_t>(addr), removable);
        } else if (code == 0x0A) {
            dev_.select_disk(static_cast<uint8_t>(addr), removable);
        } else if (code != 0x05) {
            DeviceGroup g;
            if (!group_of_code(code, g))
                return fail("SELECT: неизвестная группа устройств, код "
                            + num_str(code));
            dev_.select(g, static_cast<uint8_t>(addr), width);
        }
        (void)has_addr;

        if (!st.src.peek_raw_byte(b) || b != 0xDE) break;
        st.src.skip(1);
    }
    return true;
}

bool Interp::do_open(Stream & st, bool with_device)
{
    Disk d;
    if (!disk_prefix(st, with_device, d)) return false;

    std::string name;
    if (!st.ev.text(name)) return fail(st.ev.error());

    uint8_t nm[NAME_LEN];
    Catalog::make_name(name, nm);

    Catalog cat(host_, d.drive);
    CatalogEntry e;
    std::string err;
    if (!cat.find(nm, e, err)) return fail(err);
    if (!e.exists() || e.scratched())
        return machine_error(err::NO_FILE, "файла нет в каталоге");

    DeviceRow & r = dev_.row(d.row);
    r.bound = true;
    r.first = e.first;
    r.current = e.first;
    r.last = e.last;
    return true;
}

bool Interp::store_value(const Evaluator::Target & target, Stream & st,
                         const std::vector<Value> & vals, std::size_t & used)
{
    // Числовой массив целиком — столько значений, сколько в нём элементов,
    // строка за строкой (руководство, разд. 18.3 и 18.6).
    if (target.whole && !target.is_str) {
        const std::vector<VarInfo> & vi = store_.vars();
        const unsigned d1 = (target.var < vi.size() && vi[target.var].dim1)
                                ? vi[target.var].dim1 : 1;
        const unsigned d2 = (target.var < vi.size() && vi[target.var].dim2)
                                ? vi[target.var].dim2 : 1;
        for (unsigned i = 1; i <= d1; ++i)
            for (unsigned j = 1; j <= d2; ++j) {
                if (used >= vals.size()) return fail("в записи меньше значений, чем приёмников");
                const Value & v = vals[used++];
                if (v.is_str) return fail("строке в записи соответствует числовой приёмник");
                long idx[2] = { static_cast<long>(i), static_cast<long>(j) };
                Number * cell = 0;
                if (!store_.slot(target.var, idx, (d2 > 1) ? 2u : 1u, cell, error_))
                    return fail(error_);
                *cell = v.num;
            }
        return true;
    }

    // Массив целиком — столько значений, сколько в нём элементов: «элементы
    // массива записываются построчно» (руководство, разд. 18.6).
    if (target.whole && target.is_str) {
        const std::vector<VarInfo> & vi = store_.vars();
        const unsigned n = (target.var < vi.size() && vi[target.var].dim1)
                               ? vi[target.var].dim1 : 1;
        const unsigned len = store_.str_len(target.var);
        std::string & field = store_.str_field(target.var);
        for (unsigned i = 0; i < n; ++i) {
            if (used >= vals.size()) return fail("в записи меньше значений, чем приёмников");
            const Value & v = vals[used++];
            if (!v.is_str) return fail("числу в записи соответствует символьный приёмник");
            const unsigned off = i * len;
            if (off + len > field.size()) break;
            for (unsigned k = 0; k < len; ++k)
                field[off + k] = (k < v.str.size()) ? v.str[k] : ' ';
        }
        return true;
    }

    if (used >= vals.size()) return fail("в записи меньше значений, чем приёмников");
    const Value & v = vals[used++];
    if (target.is_str != v.is_str)
        return fail("тип значения в записи не совпадает с приёмником");
    if (!st.ev.store(target, v)) return fail(st.ev.error());
    return true;
}

bool Interp::do_dload(Stream & st)
{
    Disk d;
    if (!disk_prefix(st, false, d)) return false;
    DeviceRow & r = dev_.row(d.row);
    if (!r.bound) return fail("файл не открыт");

    // «В Бейсике „Искры 226“ есть специальный оператор для обнаружения факта
    // чтения концевой (или закрывающей) записи файла оператором DATA LOAD DC»
    // (руководство, разд. 18.5). Признак ставится здесь, а проверяет его
    // `IF END THEN`; сама загрузка при этом ничего не делает, и адрес
    // текущего сектора остаётся на концевой записи — на этом стоит
    // пример 18.5.
    unsigned code = 0;
    if (!record_code(host_, d.drive, r.current, code))
        return machine_error(err::UNKNOWN, "не читается сектор " + num_str(r.current));
    end_seen_ = (code == REC_END);
    if (end_seen_) return true;

    std::vector<Value> vals;
    unsigned next = 0;
    std::string err;
    if (!read_record(host_, d.drive, r.current, vals, next, err))
        return machine_error(err::UNKNOWN, err);

    std::size_t used = 0;
    for (;;) {
        Tok t;
        if (!st.ev.parser().peek(t, true)) return fail(st.ev.error());
        if (t.t == Tok::END) break;
        Evaluator::Target target;
        if (!st.ev.target(target, true)) return fail(st.ev.error());
        if (!store_value(target, st, vals, used)) return false;
    }

    // «По окончании операции загрузки адрес текущего сектора устанавливается
    // на первый сектор следующей записи» (руководство, разд. 18.4).
    r.current = (next <= r.last) ? next : r.last;
    return true;
}

// --- запись данных в файл ---------------------------------------------------

// «Оператор DATA SAVE DC OPEN создаёт новый файл и открывает его, т. е.
// заносит имя файла и адреса его граничных секторов в указатель каталога, а
// также записывает адреса начального, конечного и текущего секторов в строку
// таблицы устройств» (руководство, разд. 18.2.1). В скобках либо число
// секторов, либо имя вычеркнутого файла, на место которого файл ложится
// (разд. 18.2.2).
bool Interp::do_dsave_open(Stream & st)
{
    Disk d;
    if (!disk_prefix(st, true, d)) return false;

    Tok t;
    if (!st.ev.parser().take(t, true) || t.t != Tok::LPAR)
        return fail("DATA SAVE DC OPEN без размера");
    Value size;
    if (!st.ev.expr(size)) return fail(st.ev.error());
    if (!st.ev.parser().take(t, false) || t.t != Tok::RPAR)
        return fail("DATA SAVE DC OPEN: скобка не закрыта");

    std::string name;
    if (!st.ev.text(name)) return fail(st.ev.error());

    uint8_t nm[NAME_LEN];
    Catalog::make_name(name, nm);

    Catalog cat(host_, d.drive);
    CatalogEntry e;
    std::string err;
    if (!cat.find(nm, e, err)) return fail(err);
    // «Попытка создать новый файл с именем „АНКЕТА“ приведёт к останову по
    // ошибке, так как в указателе каталога уже есть такое имя» (разд. 18.3).
    if (e.alive()) return machine_error(err::FILE_EXISTS, "файл с таким именем уже есть");

    bool fresh = true;
    if (size.is_str) {
        // На месте вычеркнутого файла: «адреса граничных секторов не
        // изменяются», а «содержимое секторов диска… не изменяется» — значит
        // и служебной записи туда не пишем.
        uint8_t old[NAME_LEN];
        Catalog::make_name(size.str, old);
        CatalogEntry victim;
        if (!cat.find(old, victim, err)) return fail(err);
        if (!victim.exists() || !victim.scratched())
            return machine_error(err::NO_FILE, "вычеркнутого файла с таким именем нет");
        if (!cat.rename_over(victim, nm, false, e, err)) return fail(err);
        fresh = false;
    } else {
        long n = 0;
        if (!size.num.floor_to_int(n) || n < 1)
            return fail("DATA SAVE DC OPEN: размер не целое положительное число");
        if (!cat.create(nm, false, static_cast<unsigned>(n), e, err))
            return machine_error(cat.io_error() ? err::UNKNOWN : err::FILE_BIG, err);
    }

    // «Машина использует последний сектор файла для хранения служебной
    // информации. Никакая другая информация по этому оператору в файл не
    // записывается» (разд. 18.2.1). Это концевая запись со счётчиком 1:
    // «если признак конца данных в файле не записан, то в графе
    // „Использовано“ всегда будет стоять 00001» (разд. 18.4). Подтверждено
    // нетронутым файлом `B0001` на `w001-s2` — 300 секторов нулей, и только
    // в последнем `1C 00 01`.
    if (fresh && !write_end_record(host_, d.drive, e.last, e.last, err))
        return machine_error(err::UNKNOWN, err);

    DeviceRow & r = dev_.row(d.row);
    r.bound = true;
    r.first = e.first;
    r.current = e.first;
    r.last = e.last;
    return true;
}

// Значения списка `DATA SAVE DC`. Массив целиком идёт «строка за строкой»
// (разд. 18.3), поэтому разворачивается в свои элементы — так же, как их
// собирает обратно `store_value()` при чтении.
bool Interp::save_values(Stream & st, std::vector<Value> & vals)
{
    for (;;) {
        Tok t;
        if (!st.ev.parser().peek(t, true)) return fail(st.ev.error());
        if (t.t == Tok::END) break;

        if (t.t == Tok::ARRAY) {
            st.ev.parser().consume();
            const unsigned var = t.var;
            const std::vector<VarInfo> & vi = store_.vars();
            const unsigned d1 = (var < vi.size() && vi[var].dim1) ? vi[var].dim1 : 1;
            const unsigned d2 = (var < vi.size() && vi[var].dim2) ? vi[var].dim2 : 1;
            for (unsigned i = 1; i <= d1; ++i) {
                for (unsigned j = 1; j <= d2; ++j) {
                    long idx[2] = { static_cast<long>(i), static_cast<long>(j) };
                    const unsigned ni = (d2 > 1) ? 2u : 1u;
                    Value v;
                    if (store_.is_string(var)) {
                        VarStore::StrLoc loc;
                        if (!store_.str_element(var, idx, ni, loc, error_))
                            return fail(error_);
                        v.is_str = true;
                        v.str = loc.data->substr(loc.off, loc.len);
                    } else {
                        Number * cell = 0;
                        if (!store_.slot(var, idx, ni, cell, error_)) return fail(error_);
                        v.num = *cell;
                    }
                    vals.push_back(v);
                }
            }
        } else {
            Value v;
            if (!st.ev.expr(v)) return fail(st.ev.error());
            vals.push_back(v);
        }

        // Разделитель после значения читается в позиции операции.
        if (!st.ev.parser().peek(t, false)) return fail(st.ev.error());
        if (t.t != Tok::COMMA) { st.ev.parser().unpeek(); break; }
        st.ev.parser().consume();
    }
    return true;
}

// «Совокупность значений, записываемых с помощью одного оператора
// DATA SAVE DC, называется логической записью данных… По окончании записи
// адрес текущего сектора изменяется на адрес сектора, следующего за
// последним сектором, занятым под данные» (разд. 18.3). Форма `END` пишет
// односекторную концевую запись в текущий сектор (разд. 18.4).
bool Interp::do_dsave(Stream & st)
{
    Disk d;
    if (!disk_prefix(st, false, d)) return false;
    DeviceRow & r = dev_.row(d.row);
    if (!r.bound) return fail("файл не открыт");

    std::string err;
    uint8_t b = 0;
    if (st.src.peek_raw_byte(b) && b == 0xD7) {
        st.src.skip(1);
        // Концевая запись помечает сектор, «откуда можно записывать данные»,
        // поэтому текущий сектор она не двигает: следующая запись ляжет
        // прямо на неё.
        if (!write_end_record(host_, d.drive, r.first, r.current, err))
            return machine_error(err::UNKNOWN, err);
        return true;
    }

    std::vector<Value> vals;
    if (!save_values(st, vals)) return false;
    if (vals.empty()) return fail("DATA SAVE DC без значений");

    unsigned next = 0;
    if (!write_record(host_, d.drive, r.current, r.last, vals, next, err))
        return machine_error(err::UNKNOWN, err);
    r.current = next;
    return true;
}

// «Оператор DATA SAVE DC CLOSE закрывает файл, записывая нули во все графы
// таблицы устройств» (разд. 18.4). Адрес самого устройства при этом
// остаётся: его назначает SELECT, а не открытие файла.
bool Interp::do_dclose(Stream & st)
{
    Disk d;
    if (!disk_prefix(st, false, d)) return false;
    DeviceRow & r = dev_.row(d.row);
    r.bound = false;
    r.first = 0;
    r.current = 0;
    r.last = 0;
    return true;
}

// «При считывании концевой записи по оператору DATA LOAD DC происходит
// переход к строке с номером, указанным в операторе IF END THEN»
// (разд. 18.5). Номер строки — сырой двухбайтовый BCD, как у GOTO.
bool Interp::do_if_end(Stream & st)
{
    uint8_t a = 0, b = 0;
    if (!st.src.take_raw_byte(a) || !st.src.take_raw_byte(b))
        return fail("IF END THEN без номера строки");
    if (!end_seen_) return true;
    end_seen_ = false;
    return jump(bcd2(a) * 100 + bcd2(b));
}

// --- блочный обмен с устройством: DATA SAVE BT и DATA LOAD BT ---------------

// Устройство задаётся приставкой. Форм три:
//
//   * `/<а.в.>` — байт `DC` и **выражение**: в корпусе там и однобайтовый
//     литерал (`/34` = `DC DE 34`, VICT 2190), и переменная (`DC 0B`,
//     DISSM 7382) — программа вычисляет адрес сама;
//   * `#<а.в.>` — байт `DB`, номер строки таблицы устройств;
//   * ничего — тогда берётся группа `TAPE`: «TAPE — устройство ввода и
//     вывода для операторов DATA LOAD BT и DATA SAVE BT» (разд. 11.5).
bool Interp::bt_prefix(Stream & st, unsigned & addr)
{
    uint8_t b = 0;
    if (st.src.peek_raw_byte(b) && (b == 0xDC || b == 0xDB)) {
        st.src.skip(1);
        Number n;
        if (!st.ev.number(n)) return fail(st.ev.error());
        long v = 0;
        if (!n.floor_to_int(v) || v < 0 || v > 255)
            return fail("BT: адрес устройства вне 0…255");
        if (b == 0xDC) {
            addr = static_cast<unsigned>(v);
        } else {
            if (!DeviceTable::valid_row(static_cast<unsigned>(v)))
                return fail("BT: строки " + num_str(static_cast<unsigned>(v)) +
                            " в таблице устройств нет");
            addr = dev_.row(static_cast<unsigned>(v)).addr;
        }
        // Запятая после приставки читается в позиции операции.
        Tok t;
        if (!st.ev.parser().peek(t, false)) return fail(st.ev.error());
        if (t.t == Tok::COMMA) st.ev.parser().consume();
        else st.ev.parser().unpeek();
        return true;
    }
    addr = dev_.addr(DG_TAPE);
    return true;
}

// «TAPE — устройство ввода и вывода для операторов DATA LOAD BT и
// DATA SAVE BT» (руководство, разд. 11.5). Сами операторы книга не
// описывает: шлют и принимают блок байтов, без всякой служебной разметки —
// в отличие от `DATA SAVE DC`, где у записи есть заголовки значений.
bool Interp::do_block_transfer(Stream & st, bool load)
{
    unsigned addr = 0;
    if (!bt_prefix(st, addr)) return false;

    while (!st.src.at_end()) {
        if (load) {
            Evaluator::Target target;
            if (!st.ev.target(target, true)) return fail(st.ev.error());
            if (!target.is_str || !target.data)
                return fail("DATA LOAD BT: приёмник не символьный");
            std::vector<uint8_t> buf(target.len ? target.len : 1, 0);
            if (!host_.device_read(static_cast<uint8_t>(addr), &buf[0], target.len))
                return fail("DATA LOAD BT: устройства /" + hex2_str(addr) +
                            " у хоста нет");
            std::string & field = *target.data;
            for (unsigned i = 0; i < target.len; ++i)
                field[target.off + i] = static_cast<char>(buf[i]);
        } else {
            Value v;
            if (!st.ev.expr(v)) return fail(st.ev.error());
            if (!v.is_str) return fail("DATA SAVE BT: значение не символьное");
            if (!v.str.empty() &&
                !host_.device_write(static_cast<uint8_t>(addr),
                                    reinterpret_cast<const uint8_t *>(v.str.data()),
                                    static_cast<unsigned>(v.str.size())))
                return fail("DATA SAVE BT: устройства /" + hex2_str(addr) +
                            " у хоста нет");
        }

        Tok t;
        if (!st.ev.parser().peek(t, false)) return fail(st.ev.error());
        if (t.t != Tok::COMMA) { st.ev.parser().unpeek(); break; }
        st.ev.parser().consume();
    }
    return true;
}

// --- режим абсолютной адресации секторов (разд. 18.9) -----------------------

// Приставка и номер начального сектора: `<приставка> [EB <а.в.> D0]`.
// «Таблица устройств всё же используется для хранения характерной для режима
// DA адресной информации: адреса начального сектора, указанного в операторе,
// максимально возможного адреса сектора… адреса сектора, следующего за
// последним использованным» (разд. 18.9).
bool Interp::abs_prefix(Stream & st, Disk & d, unsigned & sector,
                        bool & has_target, Evaluator::Target & target)
{
    has_target = false;
    if (!disk_prefix(st, true, d)) return false;

    DeviceRow & r = dev_.row(d.row);
    sector = r.current;

    uint8_t b = 0;
    if (st.src.peek_raw_byte(b) && b == 0xEB) {
        st.src.skip(1);
        Number n;
        if (!st.ev.number(n)) return fail(st.ev.error());
        Tok t;
        if (!st.ev.parser().peek(t, false)) return fail(st.ev.error());
        if (t.t == Tok::COMMA) {
            // «Значение этого адреса можно считать в числовую или
            // символьную переменную, которая может быть указана после
            // адреса начального сектора» (разд. 18.9.1).
            st.ev.parser().consume();
            if (!st.ev.target(target, true)) return fail(st.ev.error());
            has_target = true;
            if (!st.ev.parser().peek(t, false)) return fail(st.ev.error());
        }
        if (t.t != Tok::RPAR) return fail("обмен по адресу: скобка не закрыта");
        st.ev.parser().consume();
        long v = 0;
        if (!n.floor_to_int(v) || v < 0)
            return fail("номер сектора не целое неотрицательное число");
        sector = static_cast<unsigned>(v);
    }

    const unsigned total = host_.disk_sectors(d.drive);
    if (sector >= total)
        return machine_error(err::UNKNOWN,
                             "сектора " + num_str(sector) + " на диске нет");
    r.bound = true;
    r.first = sector;
    r.current = sector;
    r.last = total - 1;
    return true;
}

// «Операторы этой группы… используются для записи и загрузки с диска
// содержимого одного сектора размером в 256 байт. По оператору DATA SAVE BA
// в заданный сектор записывается содержимое символьного массива. Если массив
// содержит больше 256 байт, то записываются первые 256. Если массив содержит
// меньше 256 байт, то оставшиеся байты сектора заполняются кодами HEX(00)»
// (разд. 18.9.4).
// «После выполнения оператора машина запоминает адрес сектора, следующего за
// последним сектором, использованным в данной операции… В случае символьной
// переменной используется двоичное значение первых двух байтов»
// (разд. 18.9.1).
bool Interp::store_next(Stream & st, bool has_target,
                        const Evaluator::Target & target, unsigned next)
{
    if (!has_target) return true;
    Value v;
    if (target.is_str) {
        v.is_str = true;
        v.str.push_back(static_cast<char>((next >> 8) & 0xFF));
        v.str.push_back(static_cast<char>(next & 0xFF));
    } else {
        v.num = Number::from_int(static_cast<long>(next));
    }
    if (!st.ev.store(target, v)) return fail(st.ev.error());
    return true;
}

bool Interp::do_block(Stream & st, bool load)
{
    Disk d;
    unsigned sector = 0;
    bool has_target = false;
    Evaluator::Target addr;
    if (!abs_prefix(st, d, sector, has_target, addr)) return false;

    uint8_t sec[Host::SECTOR_SIZE];
    if (load) {
        if (!host_.disk_read(d.drive, sector, sec))
            return machine_error(err::UNKNOWN, "не читается сектор " + num_str(sector));
        Evaluator::Target target;
        if (!st.ev.target(target, true)) return fail(st.ev.error());
        if (!target.is_str) return fail("DATA LOAD BA: приёмник не символьный");
        if (!assign_string(st, target,
                           std::string(reinterpret_cast<const char *>(sec),
                                       Host::SECTOR_SIZE)))
            return false;
    } else {
        Value v;
        if (!st.ev.expr(v)) return fail(st.ev.error());
        if (!v.is_str) return fail("DATA SAVE BA: значение не символьное");
        std::memset(sec, 0, Host::SECTOR_SIZE);
        const std::size_t n = (v.str.size() < Host::SECTOR_SIZE)
                                  ? v.str.size() : Host::SECTOR_SIZE;
        for (std::size_t i = 0; i < n; ++i)
            sec[i] = static_cast<uint8_t>(v.str[i]);
        if (!host_.disk_write(d.drive, sector, sec))
            return machine_error(err::UNKNOWN, "не пишется сектор " + num_str(sector));
    }

    dev_.row(d.row).current = sector + 1;
    return store_next(st, has_target, addr, sector + 1);
}

// «В режиме DA информация записывается в тех же форматах, что и в режиме
// каталога» (разд. 18.9) — те же логические записи, только начальный сектор
// задаётся прямо в операторе, а не берётся из каталога.
bool Interp::do_abs_record(Stream & st, bool load)
{
    Disk d;
    unsigned sector = 0;
    bool has_target = false;
    Evaluator::Target addr;
    if (!abs_prefix(st, d, sector, has_target, addr)) return false;
    DeviceRow & r = dev_.row(d.row);
    std::string err;

    if (!load) {
        uint8_t b = 0;
        if (st.src.peek_raw_byte(b) && b == 0xD7) {
            st.src.skip(1);
            if (!write_end_record(host_, d.drive, r.first, sector, err))
                return machine_error(err::UNKNOWN, err);
            r.current = sector + 1;
            return store_next(st, has_target, addr, sector + 1);
        }
        std::vector<Value> vals;
        if (!save_values(st, vals)) return false;
        if (vals.empty()) return fail("DATA SAVE DA без значений");
        unsigned next = 0;
        if (!write_record(host_, d.drive, sector, r.last, vals, next, err))
            return machine_error(err::UNKNOWN, err);
        r.current = next;
        return store_next(st, has_target, addr, next);
    }

    unsigned code = 0;
    if (!record_code(host_, d.drive, sector, code))
        return machine_error(err::UNKNOWN, "не читается сектор " + num_str(sector));
    end_seen_ = (code == REC_END);
    if (end_seen_) {
        r.current = sector + 1;
        return store_next(st, has_target, addr, sector + 1);
    }

    std::vector<Value> vals;
    unsigned next = 0;
    if (!read_record(host_, d.drive, sector, vals, next, err))
        return machine_error(err::UNKNOWN, err);

    std::size_t used = 0;
    while (!st.src.at_end()) {
        Evaluator::Target target;
        if (!st.ev.target(target, true)) return fail(st.ev.error());
        if (!store_value(target, st, vals, used)) return false;
    }
    r.current = next;
    return store_next(st, has_target, addr, next);
}

// «Оператор VERIFY предназначен для контроля правильности записи информации
// в заданной области диска… Если границы проверяемой области не заданы, то
// проверяются все секторы диска» (разд. 18.9.5).
bool Interp::do_verify(Stream & st)
{
    Disk d;
    if (!disk_prefix(st, true, d)) return false;

    const unsigned total = host_.disk_sectors(d.drive);
    unsigned from = 0, to = total ? total - 1 : 0;
    if (!st.src.at_end()) {
        Number a;
        if (!st.ev.number(a)) return fail(st.ev.error());
        Tok t;
        if (!st.ev.parser().peek(t, false)) return fail(st.ev.error());
        if (t.t != Tok::COMMA) return fail("VERIFY: нет второй границы");
        st.ev.parser().consume();
        Number b;
        if (!st.ev.number(b)) return fail(st.ev.error());
        long x = 0, y = 0;
        if (!a.floor_to_int(x) || !b.floor_to_int(y) || x < 0 || y < 0)
            return fail("VERIFY: границы не целые неотрицательные");
        // «Значение адреса начального сектора должно быть меньше адреса
        // конечного сектора, иначе выдаётся сообщение об ошибке».
        if (x >= y) return machine_error(err::UNKNOWN, "VERIFY: границы наоборот");
        from = static_cast<unsigned>(x);
        to = static_cast<unsigned>(y);
    }
    if (to >= total) return machine_error(err::UNKNOWN, "VERIFY: за концом диска");

    uint8_t sec[Host::SECTOR_SIZE];
    for (unsigned s = from; s <= to; ++s)
        if (!host_.disk_read(d.drive, s, sec)) {
            // «на экран дисплея выводится сообщение о номере ошибочного
            // сектора: ERROR IN SECTOR 200».
            emit("ERROR IN SECTOR " + num_str(s));
            emit_newline();
        }
    return true;
}

bool Interp::do_dskip(Stream & st, bool backwards)
{
    Disk d;
    if (!disk_prefix(st, false, d, false)) return false;
    DeviceRow & r = dev_.row(d.row);
    if (!r.bound) return fail("файл не открыт");

    std::string err;
    uint8_t b = 0;
    if (st.src.peek_raw_byte(b) && b == 0xD6) { st.src.skip(1); r.current = r.first; return true; }
    if (st.src.peek_raw_byte(b) && b == 0xD7) {
        st.src.skip(1);
        unsigned sector = 0, used = 0;
        if (!find_end_record(host_, d.drive, r.first, r.last, sector, used))
            return machine_error(err::UNKNOWN, "в файле нет концевой записи");
        r.current = sector;
        return true;
    }

    Number n;
    if (!st.ev.number(n)) return fail(st.ev.error());
    long count = 0;
    if (!n.floor_to_int(count)) return fail("DSKIP/DBACKSPACE: не целое число");
    if (count < 0) return fail("DSKIP/DBACKSPACE: отрицательное число");
    // Признак «в секторах» — байт 05 за разобранным выражением. Разбор
    // выражения его уже заглянул, поэтому сперва возвращаем источник.
    st.ev.parser().unpeek();
    bool sectors = false;
    if (st.src.peek_raw_byte(b) && b == 0x05) { st.src.skip(1); sectors = true; }

    unsigned cur = r.current;
    if (sectors) {
        // «Целая часть указанного выражения прибавляется к адресу текущего
        // сектора» (руководство, разд. 18.7) — без разбора записей.
        if (backwards)
            cur = (static_cast<unsigned long>(count) > cur - r.first)
                      ? r.first : cur - static_cast<unsigned>(count);
        else
            cur = (cur + static_cast<unsigned>(count) > r.last)
                      ? r.last : cur + static_cast<unsigned>(count);
    } else {
        for (long i = 0; i < count; ++i) {
            if (backwards) {
                if (cur <= r.first) { cur = r.first; break; }
                unsigned start = cur - 1;
                if (!record_start(host_, d.drive, r.first, cur - 1, start))
                    return fail("не читается сектор при DBACKSPACE");
                cur = start;
            } else {
                unsigned next = cur;
                if (!record_end(host_, d.drive, cur, next, err)) return fail(err);
                if (next > r.last) { cur = r.last; break; }
                cur = next;
            }
        }
    }
    // «Пользуясь операторами DSKIP и DBACKSPACE, невозможно выйти за
    // границы файла» (руководство, разд. 18.7).
    if (cur < r.first) cur = r.first;
    if (cur > r.last) cur = r.last;
    r.current = cur;
    return true;
}

// «Пометка ненужных файлов в каталоге осуществляется с помощью оператора
// SCRATCH путём указания в кавычках имени файла» (руководство, разд. 5.4).
bool Interp::do_scratch(Stream & st)
{
    Disk d;
    if (!disk_prefix(st, true, d)) return false;

    Catalog cat(host_, d.drive);
    for (;;) {
        std::string name;
        if (!st.ev.text(name)) return fail(st.ev.error());
        uint8_t nm[NAME_LEN];
        Catalog::make_name(name, nm);

        std::string err;
        if (!cat.scratch(nm, err)) return machine_error(err::NO_FILE, err);

        Tok t;
        if (!st.ev.parser().peek(t, false)) return fail(st.ev.error());
        if (t.t != Tok::COMMA) { st.ev.parser().unpeek(); break; }
        st.ev.parser().consume();
    }
    return true;
}

// «Оператор SCRATCH DISK, в котором указываются число секторов в указателе
// каталога и номер последнего сектора, входящего в область каталога»
// (руководство, разд. 5.1).
bool Interp::do_scratch_disk(Stream & st)
{
    Disk d;
    if (!disk_prefix(st, true, d)) return false;

    // «В случае, если число секторов в указателе каталога не задано, оно
    // устанавливается равным 24.»
    long ls = 24;
    uint8_t b = 0;
    if (st.src.peek_raw_byte(b) && b == 0x06) {
        st.src.skip(1);
        uint8_t eq = 0;
        if (!st.src.take_raw_byte(eq) || eq != 0xD9) return fail("SCRATCH DISK: LS без =");
        Number n;
        if (!st.ev.number(n)) return fail(st.ev.error());
        if (!n.floor_to_int(ls)) return fail("SCRATCH DISK: LS не целое число");
        Tok t;
        if (!st.ev.parser().peek(t, false)) return fail(st.ev.error());
        if (t.t == Tok::COMMA) st.ev.parser().consume();
        else st.ev.parser().unpeek();
    }
    if (!st.src.take_raw_byte(b) || b != 0xD7) return fail("SCRATCH DISK без END");
    uint8_t eq = 0;
    if (!st.src.take_raw_byte(eq) || eq != 0xD9) return fail("SCRATCH DISK: END без =");
    Number e;
    if (!st.ev.number(e)) return fail(st.ev.error());
    long end = 0;
    if (!e.floor_to_int(end)) return fail("SCRATCH DISK: END не целое число");
    if (ls < 1 || ls > 255) return machine_error(err::UNKNOWN,
                                                 "SCRATCH DISK: LS вне 1…255");
    if (end < ls) return machine_error(err::UNKNOWN,
                                       "SCRATCH DISK: END раньше указателя");

    Catalog cat(host_, d.drive);
    std::string msg;
    if (!cat.format(static_cast<unsigned>(ls), static_cast<unsigned>(end), msg))
        return machine_error(err::UNKNOWN, msg);

    // Каталог создан заново — все строки таблицы устройств, смотревшие на
    // этот диск, больше ни на что не указывают.
    for (unsigned i = 0; i < DeviceTable::ROWS; ++i) {
        DeviceRow & r = dev_.row(i);
        unsigned dr = 0;
        if (r.bound && DeviceTable::drive_index(r.addr, r.removable, dr)
            && dr == d.drive) {
            r.bound = false;
            r.first = r.current = r.last = 0;
        }
    }
    return true;
}

// Номер сектора в выдаче LIST DC — всегда пять цифр с нулями слева
// («INDEX SECTORS=00006», руководство, разд. 5.1).
static std::string sector_no(unsigned v)
{
    char b[16];
    std::sprintf(b, "%05u", v > 99999 ? 99999 : v);
    return b;
}

// Столбцы выдачи восстановлены, а не прочитаны: в скане книги три примера
// LIST DC, и в каждом разбивка пробелами своя — распознавание их потеряло.
// Взято так, чтобы заголовки стояли над своими полями (CLAUDE.md,
// «Допущения»). Ширины полей из данных сомнений не вызывают: имя — восемь
// байт, номер сектора — пять цифр.
static std::string catalog_row(const std::string & name, const char * type,
                               unsigned first, unsigned last, unsigned used)
{
    std::string s = name;
    s.resize(NAME_LEN, ' ');
    s += ' ';
    std::string t = type;
    t.resize(4, ' ');
    s += t;
    s += ' ';
    s += sector_no(first);
    s += ' ';
    s += sector_no(last);
    s += ' ';
    s += sector_no(used);
    return s;
}

// LIST DC — выдача указателя каталога (руководство, разд. 5.1). Операнд
// один и тот же, что у прочих дисковых глаголов, только короче: буква
// устройства и ничего больше.
// «При выполнении оператора RETURN CLEAR список адресов возврата
// уменьшается на один адрес, соответствующий последнему обращению к данной
// подпрограмме… Переход к оператору, следующему за оператором GOSUB, не
// производится, а выполняется следующий за оператором RETURN CLEAR
// оператор» (руководство, разд. 10.3). `ALL` — байт `CB` — стирает список
// целиком; в книге эта форма не описана, но в корпусе встречается.
bool Interp::do_return_clear(Stream & st, unsigned len)
{
    if (len) {
        uint8_t b = 0;
        if (!st.src.take_raw_byte(b) || b != 0xCB)
            return fail("RETURN CLEAR: не ALL");
        calls_.clear();
        return true;
    }
    if (!calls_.empty()) calls_.pop_back();
    return true;
}

// «Оператор KEYIN в данной книге не рассматривается» (руководство,
// разд. 18.1) — семантика восстановлена по корпусу и совпадает с той, что
// была у Wang 2200:
//
//   * клавиша не нажата — исполнение идёт **следующим оператором**;
//   * нажата обычная — код в приёмник, управление на первую строку;
//   * нажата клавиша специальных функций — код в приёмник, на вторую.
//
// Неблокирующий опрос виден прямо в тексте: `EDITOR` 244 это
// `KEYIN A¤,246,246:GOTO 244` — пустой цикл ожидания, а `EDITOR` 3312 —
// `FOR I=1TO100:KEYIN A¤,3314,3500:NEXT I`. Разные строки у двух видов
// клавиш: `EDITOR` 2505 (`KEYIN B¤,2510,2700`) и 6310.
bool Interp::do_keyin(Stream & st)
{
    // Приёмник индексируется строго по таблицам: за ним сразу идут сырые
    // байты номеров строк (CLAUDE.md, ловушка 3).
    Evaluator::Target target;
    if (!st.ev.target(target, true)) return fail(st.ev.error());
    if (!target.is_str) return fail("KEYIN: приёмник не символьный");

    unsigned ln[2] = { 0, 0 };
    for (unsigned k = 0; k < 2; ++k) {
        uint8_t a = 0, b = 0;
        if (!st.src.take_raw_byte(a) || !st.src.take_raw_byte(b))
            return fail("KEYIN без номеров строк");
        ln[k] = bcd2(a) * 100 + bcd2(b);
    }

    uint8_t code = 0;
    if (!host_.poll_key(code)) return true;      // не нажата — дальше по тексту
    const bool special = host_.key_was_special();

    if (!assign_string(st, target, std::string(1, static_cast<char>(code))))
        return false;
    return jump(ln[special ? 1 : 0]);
}

// --- обмен программой через символьный буфер --------------------------------

// В буфере лежит **программа в текстовом виде**, строки разделены байтом
// `85` — тем же, что разделяет их в текстовом файле на дискете
// (core/tokenize.h). Доказательства из корпуса:
//
//   * `EDITOR` 5195–5210 сохраняет строку 5215 в `Z¤`, дописывает в него
//     набранное с клавиатуры оператором `LINPUT STR(Z¤,6)` — то есть прямо
//     поверх текста, начиная с шестого байта, — ставит `HEX(85)` в конец и
//     грузит обратно. Шестой байт: четыре цифры номера, пробел, текст;
//   * `ASMBBAS` 9048 ищет в буфере `85`, чтобы напечатать очередную строку
//     оператором `PRINT STR(A4¤(),A0,A1-1)` — значит, там текст;
//   * `ASMBBAS` 9056–9058 сохраняет собственную строку 9066
//     (`LOAD A4¤()9008,9008,9068`) и правит её `STR(A2¤,21)=A3¤`. Двадцать
//     первый байт — начало второго номера, если считать «четыре цифры,
//     пробел, `LOAD`, пробел, `A4¤()`, номера». Номер там собран
//     оператором `CONVERT A TO A3¤,(####)`.
const char BUF_EOL = '\x85';

// Номера строк у обмена через буфер: до трёх сырых пар BCD через `DE`.
unsigned Interp::buf_lines(Stream & st, unsigned * out, unsigned max)
{
    unsigned n = 0;
    for (;;) {
        uint8_t a = 0, b = 0;
        if (n && (!st.src.peek_raw_byte(a) || a != 0xDE)) break;
        if (n) st.src.skip(1);
        if (!st.src.take_raw_byte(a) || !st.src.take_raw_byte(b)) break;
        if (n < max) out[n] = bcd2(a) * 100 + bcd2(b);
        ++n;
        if (n >= max) break;
    }
    return n;
}

// `SAVE <буфер><строка1>,<строка2>` — листинг строк диапазона в символьную
// переменную, каждая строка заканчивается байтом `85`.
bool Interp::do_save_buf(Stream & st)
{
    uint8_t b = 0;
    if (!st.src.take_raw_byte(b) || b != 0xDD)
        return fail("SAVE: нет признака буфера");

    Evaluator::Target dst;
    if (!st.ev.target(dst, true)) return fail(st.ev.error());
    if (!dst.is_str || !dst.data) return fail("SAVE: буфер не символьный");

    unsigned ln[3] = { 0, 0, 0 };
    const unsigned n = buf_lines(st, ln, 3);
    if (!n) return fail("SAVE: нет номеров строк");
    const unsigned from = ln[0];
    const unsigned to = (n > 1) ? ln[1] : ln[0];

    // Имён переменных в потоке нет: листинг называет их сам.
    NameTable names;
    std::string whole, err;
    detokenize(img_, names, whole, err);

    std::string out;
    for (unsigned i = 0; i < img_.line_count(); ++i) {
        const unsigned num = img_.line(i).number;
        if (num < from || num > to) continue;
        std::string text, why;
        if (!detokenize_line(img_.line(i), names, text, why))
            return fail("SAVE: строка " + num_str(num) + " не разбирается: " + why);
        out += text;
        out += BUF_EOL;
    }

    std::string & field = *dst.data;
    for (unsigned i = 0; i < dst.len; ++i)
        field[dst.off + i] = (i < out.size()) ? out[i] : ' ';
    return true;
}

// `LOAD <буфер><строка1>,<строка2>[,<строка3>]` — обратно в текст программы.
// Третий номер — куда продолжать исполнение: `EDITOR` 5210 грузит строку
// 5215 и уходит на 5225, чтобы не исполнить только что заменённую строку
// тут же; `ASMBBAS` 9066 и `DATABAS` 1283 уходят на 9068, где `STOP`.
bool Interp::do_load_buf(Stream & st)
{
    uint8_t b = 0;
    if (!st.src.take_raw_byte(b) || b != 0xDD)
        return fail("LOAD: нет признака буфера");

    // Буфер индексируется строго по таблицам: за ним сразу идут сырые байты
    // номеров строк, и заглядывание приняло бы их за список индексов
    // (CLAUDE.md, ловушка 3).
    Evaluator::Target buf;
    if (!st.ev.target(buf, true)) return fail(st.ev.error());
    if (!buf.is_str || !buf.data) return fail("LOAD: буфер не символьный");
    Value src;
    if (!st.ev.load(buf, src)) return fail(st.ev.error());

    unsigned ln[3] = { 0, 0, 0 };
    const unsigned n = buf_lines(st, ln, 3);
    if (!n) return fail("LOAD: нет номеров строк");
    const unsigned from = ln[0];
    const unsigned to = (n > 1) ? ln[1] : ln[0];

    // Номер текущей строки надо запомнить до правки: индексы съедут.
    const bool direct = (li_ == DIRECT);
    const unsigned here = direct ? 0 : img_.line(li_).number;

    NameTable names;
    std::string whole, err;
    detokenize(img_, names, whole, err);

    std::size_t at = 0;
    while (at < src.str.size()) {
        const std::size_t e = src.str.find(BUF_EOL, at);
        std::string chunk = (e == std::string::npos) ? src.str.substr(at)
                                                     : src.str.substr(at, e - at);
        at = (e == std::string::npos) ? src.str.size() : e + 1;
        // Поле символьной переменной добито пробелами — это не текст.
        while (!chunk.empty() && chunk[chunk.size() - 1] == ' ')
            chunk.resize(chunk.size() - 1);
        if (chunk.empty()) continue;

        // Неразобранная строка откатывает таблицу имён: иначе выдуманные
        // имена сдвинут индексы всех дальнейших переменных (CLAUDE.md).
        const unsigned mark = names.count();
        unsigned number = 0;
        std::vector<uint8_t> body;
        std::string why;
        if (!tokenize_line(chunk, names, number, body, why)) {
            names.truncate(mark);
            return fail("LOAD: " + why);
        }
        if (number < from || number > to) { names.truncate(mark); continue; }
        img_.put_line(number, body.empty() ? 0 : &body[0],
                      static_cast<unsigned>(body.size()));
    }

    img_.vars() = names.vars();
    img_.rebuild_tables();
    rescan();

    if (n > 2) return jump(ln[2]);

    // Текст программы поменялся — индексы строк съехали, и текущую надо
    // найти заново по номеру (та же беда, что у CLEAR P).
    if (!direct) {
        unsigned idx = 0;
        if (!img_.find(here, idx)) { stopped_ = true; return true; }
        li_ = idx;
    }
    return true;
}

// --- команды диалога внутри программы ---------------------------------------

// Номера строк у этих операторов лежат сырыми парами BCD, как у GOTO.
// Возвращает, сколько номеров прочитано (0, 1 или 2).
unsigned Interp::line_range(Stream & st, unsigned & from, unsigned & to)
{
    from = 0;
    to = 0;
    uint8_t a = 0, b = 0;
    if (!st.src.take_raw_byte(a) || !st.src.take_raw_byte(b)) return 0;
    from = bcd2(a) * 100 + bcd2(b);
    if (!st.src.peek_raw_byte(a) || a != 0xDE) return 1;
    st.src.skip(1);
    if (!st.src.take_raw_byte(a) || !st.src.take_raw_byte(b)) return 1;
    to = bcd2(a) * 100 + bcd2(b);
    return 2;
}

// `CLEAR` — всё; `CLEAR P [n1][,n2]` — только текст программы; `CLEAR V` —
// все переменные, `CLEAR N` — только необщие (руководство, разд. 8.3).
//
// Коды в потоке: `14` это `P` — подтверждено диапазоном строк за ним
// (`DASB2` 790 = `2C 05 14 95 00 DE 99 20`); без операндов — голый `CLEAR`
// (`UDAW` 363). Коды `11` и `12` это `V` и `N`, но какой какой — корпус не
// различает (CLAUDE.md, «Допущения»).
bool Interp::do_clear(Stream & st)
{
    uint8_t code = 0;
    if (!st.src.peek_raw_byte(code)) {
        // «Экран и память машины очистятся» (разд. 3.2) — исполнять дальше
        // нечего, программы больше нет.
        img_.clear();
        rescan();
        clear_all();
        dev_ = DeviceTable();
        host_.screen().put(CC_CLEAR);
        stopped_ = true;
        return true;
    }
    st.src.skip(1);

    if (code == 0x11) { store_.clear(); return true; }
    if (code == 0x12) { store_.clear_non_common(); return true; }
    if (code != 0x14) return fail("CLEAR: неизвестный вид, код " + num_str(code));

    // «При выполнении оператора CLEAR P из памяти машины стирается только
    // текст программы, и никаких других изменений не происходит».
    unsigned from = 0, to = 0;
    line_range(st, from, to);

    // Строку, из которой стирают, может стереть и саму себя: после правки
    // индексы съезжают, и текущую надо найти заново по номеру.
    const bool direct = (li_ == DIRECT);
    const unsigned here = direct ? 0 : img_.line(li_).number;
    img_.erase_range(from, to);
    rescan();
    if (!direct) {
        unsigned idx = 0;
        if (!img_.find(here, idx)) { stopped_ = true; return true; }
        li_ = idx;
    }
    return true;
}

// «Оператор RUN без указания номера строки обнуляет переменные», а с
// номером «переменные сохраняют значения, присвоенные им ранее»
// (руководство, разд. 4.1). Изнутри программы это перезапуск: своего цикла
// исполнения тут заводить нельзя, поэтому просто передаём управление.
bool Interp::do_run(Stream & st)
{
    unsigned from = 0, to = 0;
    const unsigned n = line_range(st, from, to);
    if (!n) {
        clear_all();
        if (!img_.line_count()) { stopped_ = true; return true; }
        li_ = 0;
        off_ = 0;
        jumped_ = true;
        return true;
    }
    return jump(from);
}

// `LIST [<устройство>] [<строка1>[,<строка2>]]` — тот же листинг, что в
// диалоге, но на устройство группы LIST: «LIST — устройство вывода для
// операторов LIST» (руководство, разд. 11.5).
bool Interp::do_list(Stream & st)
{
    // Приставка устройства у этого LIST — только адрес: строки таблицы и
    // дисковода тут ни при чём (`DASB2` 448 = `2E 06 DC DE 05 DE 95 02`).
    uint8_t b = 0;
    if (st.src.peek_raw_byte(b) && b == 0xDC) {
        st.src.skip(1);
        uint8_t de = 0, addr = 0;
        if (!st.src.take_raw_byte(de) || de != 0xDE) return fail("LIST: после / нет DE");
        if (!st.src.take_raw_byte(addr)) return fail("LIST: нет адреса устройства");
        dev_.select(DG_LIST, addr, 0);
        if (st.src.peek_raw_byte(b) && b == 0xDE) st.src.skip(1);
    }

    unsigned from = 0, to = 0;
    const unsigned n = line_range(st, from, to);
    if (n == 1) to = from;                      // одна строка — она и есть весь диапазон

    // Имён переменных в потоке нет вовсе: детокенизация придумывает их сама
    // (CLAUDE.md, «Обратная трансляция»).
    NameTable names;
    std::string whole, err;
    detokenize(img_, names, whole, err);

    for (unsigned i = 0; i < img_.line_count(); ++i) {
        const unsigned num = img_.line(i).number;
        if (from && num < from) continue;
        if (to && num > to) continue;
        std::string text, why;
        if (!detokenize_line(img_.line(i), names, text, why)) {
            // Ограничение эмулятора не прячем: видно и номер, и причину.
            std::string koi;
            utf8_to_koi8(why, koi);
            emit_group(DG_LIST, num_str(num) + " ??? " + koi);
        } else {
            emit_group(DG_LIST, text);
        }
        emit_group_newline(DG_LIST);
    }
    return true;
}

bool Interp::do_list_dc(Stream & st)
{
    Disk d;
    if (!disk_prefix(st, true, d)) return false;

    Catalog cat(host_, d.drive);
    std::string err;
    if (!cat.open(err)) return machine_error(err::UNKNOWN, err);

    // Вычеркнутые файлы в выдаче остаются — их-то и помечают SP и SD.
    std::vector<CatalogEntry> files;
    if (!cat.list(files, true, err)) return machine_error(err::UNKNOWN, err);

    if (host_.screen().col() != 1) emit_newline();
    emit(dev_.row(d.row).removable ? "REMOVABLE CATALOG" : "FIXED CATALOG");
    emit_newline();
    emit("INDEX SECTORS=" + sector_no(cat.index_sectors()));
    emit_newline();
    emit("END CAT.AREA=" + sector_no(cat.area_end()));
    emit_newline();
    emit("CURRENT END=" + sector_no(cat.current_end()));
    emit_newline();
    emit("NAME     TYPE START END   USED");
    emit_newline();

    for (std::size_t i = 0; i < files.size(); ++i) {
        const CatalogEntry & e = files[i];
        const char * type = e.is_program()
            ? (e.scratched() ? "SP" : "P")
            : (e.scratched() ? "SD" : "D");

        // «Число использованных секторов, т. е. секторов, реально занятых в
        // файле». Оно живёт не в указателе, а в самом файле — концевой
        // записью; у сырых блоков от DATA SAVE BA её нет вовсе.
        unsigned sector = 0, used = 0;
        if (!find_end_record(host_, d.drive, e.first, e.last, sector, used))
            used = 0;

        emit(catalog_row(e.name_str(), type, e.first, e.last, used));
        emit_newline();
    }
    return true;
}

bool Interp::do_limits(Stream & st)
{
    Disk d;
    if (!disk_prefix(st, true, d)) return false;
    DeviceRow & r = dev_.row(d.row);

    // Форма с именем файла отличается тем, что первый операнд символьный
    // (руководство, разд. 18.8.3).
    Tok t;
    if (!st.ev.parser().peek(t, true)) return fail(st.ev.error());
    const bool named = (t.t == Tok::STR)
        || ((t.t == Tok::VAR || t.t == Tok::ARRAY) && store_.is_string(t.var));

    unsigned code = 0;
    if (named) {
        std::string name;
        if (!st.ev.text(name)) return fail(st.ev.error());
        uint8_t nm[NAME_LEN];
        Catalog::make_name(name, nm);

        Catalog cat(host_, d.drive);
        CatalogEntry e;
        std::string err;
        if (!cat.find(nm, e, err)) return fail(err);
        st.ev.parser().unpeek();
        code = limits_code(e);
        if (e.exists()) {
            r.bound = true;
            r.first = e.first;
            r.current = e.first;
            r.last = e.last;
        } else {
            r.bound = false;
            r.first = r.current = r.last = 0;
        }
    } else {
        // Форма 2: обращений к диску нет вовсе, всё берётся из строки.
        code = r.bound ? 2u : 0u;
    }

    unsigned sector = 0, used = 0;
    if (!r.bound || !find_end_record(host_, d.drive, r.first, r.last, sector, used))
        used = 0;

    const unsigned vals[4] = { r.first, r.last, used, code };
    for (unsigned i = 0; i < 4; ++i) {
        if (!st.ev.parser().peek(t, true)) return fail(st.ev.error());
        if (t.t == Tok::END) break;
        Evaluator::Target target;
        if (!st.ev.target(target, true)) return fail(st.ev.error());
        if (target.is_str) return fail("LIMITS: приёмники числовые");
        Value v;
        v.num = Number::from_int(static_cast<long>(vals[i]));
        if (!st.ev.store(target, v)) return fail(st.ev.error());
    }
    return true;
}

bool Interp::do_onerr(Stream & st)
{
    trap_ = ErrorTrap();
    if (st.src.at_end()) return true;

    Tok t;
    if (!st.ev.parser().peek(t, true)) return fail(st.ev.error());
    if (t.t == Tok::VAR) {
        // Приёмники идут парой, разделителя между ними в потоке нет.
        for (unsigned k = 0; k < 2; ++k) {
            if (!st.ev.parser().take(t, true) || t.t != Tok::VAR)
                return fail("ON ERROR: ждали символьную переменную");
            if (k == 0) trap_.target_a = t.var; else trap_.target_b = t.var;
        }
        trap_.has_targets = true;
    } else {
        // Приёмников нет — заглянутую в позиции операнда лексему надо
        // вернуть: GOTO и THEN читаются в позиции операции.
        st.ev.parser().unpeek();
    }
    if (!st.ev.parser().take(t, false)) return fail(st.ev.error());
    long ln = 0;
    if (t.t == Tok::KW_THEN) {
        trap_.mode = EM_THEN;
        if (!t.num.to_int(ln)) return fail("ON ERROR: неверный номер строки");
    } else if (t.t == Tok::KW_GOTO || t.t == Tok::KW_GOSUB) {
        trap_.mode = (t.t == Tok::KW_GOTO) ? EM_GOTO : EM_GOSUB;
        uint8_t a = 0, b = 0;
        if (!st.src.take_raw_byte(a) || !st.src.take_raw_byte(b))
            return fail("ON ERROR без номера строки");
        ln = bcd2(a) * 100 + bcd2(b);
    } else {
        return fail("ON ERROR без GOTO, THEN или GOSUB");
    }
    trap_.line = static_cast<unsigned>(ln);
    return true;
}

bool Interp::do_dim(Stream & st, unsigned len, const uint8_t * ops, bool common)
{
    (void)st;
    (void)common;
    for (unsigned i = 0; i < len; ++i) {
        const unsigned var = ops[i];
        std::string err;
        if (store_.is_string(var)) {
            // Поле заводится по описанию из таблицы переменных; повторное
            // объявление очищает его.
            store_.reset_string(var);
            store_.str_field(var);
            continue;
        }
        unsigned d1 = 0, d2 = 0;
        if (var < store_.vars().size()) {
            d1 = store_.vars()[var].dim1;
            d2 = store_.vars()[var].dim2;
        }
        if (!store_.array_alloc(var, d1, d2, err)) return fail(err);
    }
    return true;
}

// MAT REDIM меняет размерности уже существующего массива; содержимое
// памяти при этом сохраняется.
// --- замена и перекодировка символьных данных (руководство, разд. 15.3) -----

// «Оператор REPLACE позволяет находить и заменять определённые цепочки
// символов на другие в содержимом символьной переменной и подсчитывать
// количество совершённых замен. При замене в содержимом символьного массива
// массив рассматривается как одна строка символов без границ между
// элементами» (разд. 15.3).
//
// В потоке: `<счётчик> DE <переменная> DE <искомая> [DE <заменяющая>]`
// (EDITOR 462).
bool Interp::do_replace(Stream & st)
{
    Evaluator::Target counter;
    if (!st.ev.target(counter)) return fail(st.ev.error());
    if (counter.is_str) return fail("REPLACE: счётчик не числовой");

    Tok t;
    if (!st.ev.parser().take(t, false) || t.t != Tok::COMMA)
        return fail("REPLACE без запятой");

    Evaluator::Target where;
    if (!st.ev.target(where)) return fail(st.ev.error());
    if (!where.is_str || !where.data) return fail("REPLACE: где менять — не строка");

    if (!st.ev.parser().take(t, false) || t.t != Tok::COMMA)
        return fail("REPLACE без запятой");

    Value what;
    if (!st.ev.expr(what)) return fail(st.ev.error());
    if (!what.is_str) return fail("REPLACE: искомая строка не символьная");

    // «Этот параметр необязателен. Если его нет, то искомая строка удаляется
    // из содержимого символьной переменной».
    std::string with;
    if (!st.ev.parser().peek(t, false)) return fail(st.ev.error());
    if (t.t == Tok::COMMA) {
        st.ev.parser().consume();
        Value v;
        if (!st.ev.expr(v)) return fail(st.ev.error());
        if (!v.is_str) return fail("REPLACE: заменяющая строка не символьная");
        with = v.str;
    } else {
        st.ev.parser().unpeek();
    }

    if (what.str.empty()) return fail("REPLACE: искомая строка пуста");

    // Концевые пробелы поля в замене не участвуют: иначе пример 15.6
    // (`REPLACE K,E¤(),HEX(2020),HEX(20)` в цикле, пока K<>0) не сошёлся бы
    // никогда — хвост поля давал бы пары пробелов без конца.
    std::string & field = *where.data;
    std::string work = field.substr(where.off, where.len);
    work.resize(str_len_value(work));

    unsigned count = 0;
    std::size_t at = 0;
    for (;;) {
        const std::size_t p = work.find(what.str, at);
        if (p == std::string::npos) break;
        work.replace(p, what.str.size(), with);
        at = p + with.size();            // на подставленное заново не смотрим
        ++count;
    }

    // «Содержимое переменной дополняется необходимым количеством символов
    // пробела»; выросшее сверх поля обрезается — поле постоянной длины.
    work.resize(where.len, ' ');
    for (unsigned i = 0; i < where.len; ++i) field[where.off + i] = work[i];

    Value n;
    n.num = Number::from_int(static_cast<long>(count));
    if (!st.ev.store(counter, n)) return fail(st.ev.error());
    return true;
}

// «Оператор $TRAN позволяет производить быструю перекодировку всего
// содержимого символьной переменной в соответствии с задаваемой таблицей
// символов» (разд. 15.3). Форм две:
//
//   * табличная (без `R`): код байта — это номер байта в таблице, считая с
//     единицы. Если таблица короче, байт остаётся как был;
//   * списковая (с `R`): таблица это пары байтов, второй в паре — что
//     заменять, первый — на что. Список кончается парой пробелов.
//
// В потоке: `<переменная> DE <таблица> D0 [DE 00]`, где хвост и означает
// `R` (EDITOR 290 против EDITOR 4760). Маску `hh` книга описывает, но в
// корпусе её нет, и как она кодируется — неизвестно.
bool Interp::do_tran(Stream & st)
{
    Evaluator::Target where;
    if (!st.ev.target(where)) return fail(st.ev.error());
    if (!where.is_str || !where.data) return fail("$TRAN: не символьная переменная");

    Tok t;
    if (!st.ev.parser().take(t, false) || t.t != Tok::COMMA)
        return fail("$TRAN без запятой");

    Value table;
    if (!st.ev.expr(table)) return fail(st.ev.error());
    if (!table.is_str) return fail("$TRAN: таблица не символьная");

    if (!st.ev.parser().take(t, false) || t.t != Tok::RPAR)
        return fail("$TRAN: скобка не закрыта");

    const bool list_form = !st.src.at_end();
    if (list_form) {
        uint8_t b = 0, mode = 0;
        if (!st.src.take_raw_byte(b) || b != 0xDE || !st.src.take_raw_byte(mode))
            return fail("$TRAN: непонятный хвост");
    }

    std::string & field = *where.data;
    for (unsigned i = 0; i < where.len; ++i) {
        const unsigned char src = static_cast<unsigned char>(field[where.off + i]);
        unsigned char out = src;
        if (list_form) {
            for (std::size_t p = 0; p + 1 < table.str.size(); p += 2) {
                const unsigned char to = static_cast<unsigned char>(table.str[p]);
                const unsigned char from = static_cast<unsigned char>(table.str[p + 1]);
                if (to == ' ' && from == ' ') break;      // конец списка
                if (from == src) { out = to; break; }
            }
        } else if (src < table.str.size()) {
            // «Код преобразуется в число, к которому прибавляется единица.
            // Этот результат определяет номер байта в таблице».
            out = static_cast<unsigned char>(table.str[src]);
        }
        field[where.off + i] = static_cast<char>(out);
    }
    return true;
}

// --- матричные операторы ----------------------------------------------------

// `MAT <массив>=ZER` и `MAT <массив>=<массив>`: `E0 <индекс> D9 <EF | E0
// <индекс>>` (STAT03 240). Других форм в корпусе нет вовсе — ни `CON`, ни
// `IDN`, ни арифметики, ни `INV`/`TRN`: их байты не установлены, и
// транслятор их кодировать отказывается.
bool Interp::do_mat(Stream & st)
{
    uint8_t b = 0, dst = 0;
    if (!st.src.take_raw_byte(b) || b != 0xE0) return fail("MAT без массива");
    if (!st.src.take_raw_byte(dst)) return fail("MAT без массива");
    if (!st.src.take_raw_byte(b) || b != 0xD9) return fail("MAT без знака равенства");
    if (!st.src.take_raw_byte(b)) return fail("MAT: нечего присваивать");

    std::string err;
    if (b == 0xEF) {
        // «MAT ZER — каждый элемент матрицы = 0» (руководство, разд. 12.1).
        if (store_.is_string(dst)) return fail("MAT =ZER: массив не числовой");
        unsigned d1 = 0, d2 = 0;
        if (!store_.array_dims(dst, d1, d2, err)) return fail(err);
        if (!store_.array_alloc(dst, d1, d2, err)) return fail(err);
        return true;
    }

    if (b != 0xE0) return fail("MAT: справа не массив");
    uint8_t src = 0;
    if (!st.src.take_raw_byte(src)) return fail("MAT: справа не массив");

    if (store_.is_string(dst) != store_.is_string(src))
        return fail("MAT: массивы разного типа");
    if (store_.is_string(dst)) {
        // Массив строк — одно непрерывное поле (разд. 13.2), и копируется
        // оно целиком.
        store_.str_field(dst) = store_.str_field(src);
        return true;
    }

    // «Размерность массива А изменяется в соответствии с размерностью
    // массива В» (разд. 12.2.4).
    unsigned d1 = 0, d2 = 0;
    if (!store_.array_dims(src, d1, d2, err)) return fail(err);
    if (!store_.array_grow(dst, d1, d2, err)) return fail(err);
    const unsigned rows = d1, cols = d2 ? d2 : 1;
    for (unsigned i = 1; i <= rows; ++i)
        for (unsigned j = 1; j <= cols; ++j) {
            long idx[2] = { static_cast<long>(i), static_cast<long>(j) };
            const unsigned n = d2 ? 2u : 1u;
            Number * from = 0;
            if (!store_.slot(src, idx, n, from, err)) return fail(err);
            const Number v = *from;
            Number * to = 0;
            if (!store_.slot(dst, idx, n, to, err)) return fail(err);
            *to = v;
        }
    return true;
}

// Место символьного значения: сама переменная, элемент массива, массив
// целиком или вырезка `STR(`. Знак минус перед ним значит «в обратном
// порядке» (разд. 15.2), и вычислителю его показывать нельзя — он ругнётся
// на минус перед строкой.
bool Interp::str_place(Stream & st, bool & reverse, Evaluator::Target & out)
{
    reverse = false;
    Tok t;
    if (!st.ev.parser().peek(t, true)) return fail(st.ev.error());
    if (t.t == Tok::MINUS) { st.ev.parser().consume(); reverse = true; }
    if (!st.ev.target(out, true)) return fail(st.ev.error());
    if (!out.is_str || !out.data) return fail("ожидалось символьное значение");
    return true;
}

// «Оператор MAT COPY переписывает данные из входной символьной переменной
// или её части в выходную символьную переменную или её часть. Данные
// переписываются последовательно по байтам. При переписи границы элементов
// массива игнорируются» (руководство, разд. 15.2).
bool Interp::do_mat_copy(Stream & st)
{
    bool rev_src = false;
    Evaluator::Target src;
    if (!str_place(st, rev_src, src)) return false;

    Tok t;
    if (!st.ev.parser().take(t, false) || t.t != Tok::KW_TO)
        return fail("MAT COPY без TO");

    bool rev_dst = false;
    Evaluator::Target dst;
    if (!str_place(st, rev_dst, dst)) return false;

    // Байты источника снимаются заранее: вход и выход бывают одной и той же
    // переменной, и подпрограммы вставки из примера 15.5 на этом стоят.
    std::string bytes = src.data->substr(src.off, src.len);
    if (rev_src) std::reverse(bytes.begin(), bytes.end());

    // «Операция заканчивается, когда заполняется вся выходная переменная или
    // её часть. Если переписываемых байтов недостаточно, в оставшиеся байты
    // записываются символы пробела».
    bytes.resize(dst.len, ' ');
    if (rev_dst) std::reverse(bytes.begin(), bytes.end());

    for (unsigned i = 0; i < dst.len; ++i)
        (*dst.data)[dst.off + i] = bytes[i];
    return true;
}

// «MAT SEARCH — оператор группового поиска данных, удовлетворяющих
// заданному условию по всему содержимому поисковой переменной… Результатом
// операции поиска является список порядковых номеров начальных байтов
// найденных строк» (руководство, разд. 15.1).
//
// Поток: `<где> DE <знак> <что> D1 <куда> [D2 <шаг>]` (EDITOR 346).
bool Interp::do_mat_search(Stream & st)
{
    Evaluator::Target where;
    if (!st.ev.target(where, true)) return fail(st.ev.error());
    if (!where.is_str || !where.data) return fail("MAT SEARCH: где искать — не строка");

    Tok t;
    if (!st.ev.parser().take(t, false) || t.t != Tok::COMMA)
        return fail("MAT SEARCH без запятой");

    uint8_t rel = 0;
    if (!st.src.take_raw_byte(rel)) return fail("MAT SEARCH без знака");
    // `*` (по маске), `%` (с отождествлением) и `#` (диапазон) книга
    // описывает, но их байты в корпусе не встречаются и не установлены.
    if (rel != 0xD9 && rel != 0xD5 && rel != 0xD6 && rel != 0xD7 &&
        rel != 0xD8 && rel != 0xD4)
        return fail("MAT SEARCH: знак условия не опознан");

    Value what;
    if (!st.ev.expr(what)) return fail(st.ev.error());
    if (!what.is_str) return fail("MAT SEARCH: искомое значение не строка");
    // «При сравнении концевые пробелы искомой величины не входят в
    // сравниваемое значение».
    std::string key = what.str;
    while (!key.empty() && key[key.size() - 1] == ' ') key.resize(key.size() - 1);
    if (key.empty()) return fail("MAT SEARCH: искомое значение пусто");

    if (!st.ev.parser().take(t, false) || t.t != Tok::KW_TO)
        return fail("MAT SEARCH без TO");

    Evaluator::Target list;
    if (!st.ev.target(list, true)) return fail(st.ev.error());
    if (!list.is_str || !list.data) return fail("MAT SEARCH: список номеров не строка");

    long step = 1;
    if (!st.src.at_end()) {
        uint8_t b = 0;
        if (st.src.peek_raw_byte(b) && b == 0xD2) {
            st.src.skip(1);
            Number n;
            if (!st.ev.number(n)) return fail(st.ev.error());
            if (!n.floor_to_int(step) || step == 0)
                return fail("MAT SEARCH: шаг не целое ненулевое число");
            const long a = step < 0 ? -step : step;
            if (a > 255) return fail("MAT SEARCH: шаг больше 255");
        }
    }

    const std::string hay = where.data->substr(where.off, where.len);
    const unsigned klen = static_cast<unsigned>(key.size());
    const unsigned slots = list.len / 2;      // номер занимает два байта

    std::vector<unsigned> found;
    if (hay.size() >= klen) {
        const long last = static_cast<long>(hay.size() - klen);   // 0-я позиция
        const long k = step < 0 ? -step : step;
        // «Если значение выражения положительно или параметр STEP опущен,
        // поисковая переменная просматривается начиная с первого байта. Если
        // значение отрицательно — начиная с её последнего байта».
        for (long p = (step > 0 ? 0 : last); p >= 0 && p <= last; p += (step > 0 ? k : -k)) {
            const int c = hay.compare(static_cast<std::size_t>(p), klen, key);
            bool hit = false;
            switch (rel) {
                case 0xD9: hit = (c == 0); break;
                case 0xD5: hit = (c != 0); break;
                case 0xD6: hit = (c <= 0); break;
                case 0xD7: hit = (c < 0);  break;
                case 0xD8: hit = (c >= 0); break;
                case 0xD4: hit = (c > 0);  break;
                default: break;
            }
            if (!hit) continue;
            found.push_back(static_cast<unsigned>(p) + 1);   // нумерация с единицы
            if (found.size() >= slots) break;
        }
    }

    // «Если операция закончилась потому, что проверены все строки, то в
    // конец списка записываются два байта HEX(0000)».
    std::string & out = *list.data;
    unsigned at = 0;
    for (std::size_t i = 0; i < found.size(); ++i, at += 2) {
        out[list.off + at] = static_cast<char>((found[i] >> 8) & 0xFF);
        out[list.off + at + 1] = static_cast<char>(found[i] & 0xFF);
    }
    if (found.size() < slots) {
        out[list.off + at] = 0;
        out[list.off + at + 1] = 0;
    }
    return true;
}

bool Interp::do_redim(Stream & st)
{
    for (;;) {
        Tok t;
        if (!st.ev.parser().take(t, true)) return fail(st.ev.error());
        if (t.t != Tok::ARRAY) return fail("MAT REDIM без массива");
        const unsigned var = t.var;
        if (!st.ev.parser().take(t, true) || t.t != Tok::LPAR)
            return fail("MAT REDIM без размерностей");

        unsigned dim[2] = { 0, 0 };
        unsigned n = 0;
        for (;;) {
            Number v;
            if (!st.ev.number(v)) return fail(st.ev.error());
            long k = 0;
            if (!v.floor_to_int(k) || k < 1)
                return fail("MAT REDIM: размерность не положительное целое");
            if (n < 2) dim[n] = static_cast<unsigned>(k);
            ++n;
            if (!st.ev.parser().take(t, false)) return fail(st.ev.error());
            if (t.t == Tok::COMMA) continue;
            if (t.t != Tok::RPAR) return fail("MAT REDIM: скобка не закрыта");
            break;
        }
        if (!dim[0]) return fail("MAT REDIM без размерности");

        // За скобкой у символьного массива может стоять длина элемента.
        // Заглядываем в позиции операции: там же стоит запятая между
        // записями, а в позиции операнда DE значит совсем другое.
        unsigned str_len = 0;
        if (!st.ev.parser().peek(t, false)) return fail(st.ev.error());
        if (t.t != Tok::END && t.t != Tok::COMMA) {
            st.ev.parser().unpeek();
            Number v;
            if (!st.ev.number(v)) return fail(st.ev.error());
            long k = 0;
            if (!v.floor_to_int(k) || k < 1)
                return fail("MAT REDIM: длина элемента не положительна");
            str_len = static_cast<unsigned>(k);
        }

        std::string err;
        if (store_.is_string(var)) {
            unsigned len = str_len ? str_len : store_.str_len(var);
            const std::size_t total = static_cast<std::size_t>(len) * dim[0] *
                                      (dim[1] ? dim[1] : 1);
            if (total > 64u * 1024u) return fail("MAT REDIM: слишком большой массив");
            store_.str_field(var).resize(total, ' ');
        } else {
            if (!store_.array_grow(var, dim[0], dim[1], err)) return fail(err);
        }

        if (!st.ev.parser().peek(t, false)) return fail(st.ev.error());
        if (t.t != Tok::COMMA) break;
        st.ev.parser().consume();
    }
    return true;
}

bool Interp::read_line(const std::string & prompt, bool has_prompt,
                       std::string & out)
{
    if (has_prompt) emit(prompt);
    emit("?");

    out.clear();
    for (;;) {
        uint8_t code = 0;
        if (!host_.wait_key(code)) return fail("нет данных на клавиатуре");
        if (code == 0x0D || code == 0x0A) break;
        if (code == 0x08) {                       // ВШ — забой
            if (!out.empty()) {
                out.resize(out.size() - 1);
                host_.screen().put(CC_LEFT);
                host_.screen().put(0x20);
                host_.screen().put(CC_LEFT);
            }
            continue;
        }
        out += static_cast<char>(code);
        host_.screen().put(code);
    }
    emit_newline();
    return true;
}

// LINPUT принимает строку целиком, без разбора на поля.
bool Interp::do_linput(Stream & st)
{
    std::string prompt;
    bool has_prompt = false;
    Tok t;
    if (!st.ev.parser().peek(t, true)) return fail(st.ev.error());
    if (t.t == Tok::STR) {
        st.ev.parser().consume();
        prompt = t.s;
        has_prompt = true;
        if (!st.ev.parser().peek(t, true)) return fail(st.ev.error());
    }
    // Минус перед приёмником назначения пока не имеет.
    if (t.t == Tok::MINUS) st.ev.parser().consume();

    Evaluator::Target target;
    if (!st.ev.target(target, true)) return fail(st.ev.error());
    if (!target.is_str) return fail("LINPUT ждёт символьный приёмник");

    std::string line;
    if (!read_line(prompt, has_prompt, line)) return false;
    return assign_string(st, target, line);
}

bool Interp::do_input(Stream & st)
{
    std::string prompt;
    bool has_prompt = false;
    Tok t;
    if (!st.ev.parser().peek(t, true)) return fail(st.ev.error());
    if (t.t == Tok::STR) {
        st.ev.parser().consume();
        prompt = t.s;
        has_prompt = true;
    }

    // Приёмники идут вплотную, разделителей в потоке нет.
    std::vector<Evaluator::Target> targets;
    for (;;) {
        if (!st.ev.parser().peek(t, true)) return fail(st.ev.error());
        if (t.t == Tok::END) break;
        Evaluator::Target target;
        if (!st.ev.target(target, true)) return fail(st.ev.error());
        targets.push_back(target);
    }

    std::string line;
    if (!read_line(prompt, has_prompt, line)) return false;
    if (targets.empty()) return true;

    unsigned p = 0;
    for (unsigned i = 0; i < targets.size(); ++i) {
        std::string field;
        if (targets.size() == 1 && targets[i].is_str) {
            // Единственный символьный приёмник получает строку целиком:
            // запятая в ней — обычный символ.
            field = line;
            p = static_cast<unsigned>(line.size());
        } else {
            while (p < line.size() && line[p] != ',') field += line[p++];
            if (p < line.size()) ++p;
        }

        if (targets[i].is_str) {
            if (!assign_string(st, targets[i], field)) return false;
            continue;
        }
        Value v;
        if (!Number::parse(field, v.num)) return fail("INPUT: не число «" + field + "»");
        if (!st.ev.store(targets[i], v)) return fail(st.ev.error());
    }
    return true;
}

// --- упаковка чисел (руководство, разд. 13.7) -------------------------------

// `PACK(<образ>)<приёмник>FROM<список>` и
// `UNPACK(<образ>)<источник>TO<приёмники>`. В потоке образ — обычный
// литерал, `CA` это `FROM`, `D1` — `TO` (EDITOR 344, 378).
//
// «С помощью одного оператора можно задать упаковку и распаковку сразу для
// всех числовых переменных и массивов, перечисленных в списке. При этом
// упакованные значения записываются вплотную друг к другу».
bool Interp::do_pack(Stream & st, bool unpack)
{
    Tok t;
    if (!st.ev.parser().take(t, true) || t.t != Tok::STR)
        return fail(unpack ? "UNPACK без образа" : "PACK без образа");

    ImageField f;
    if (!image_single_field(t.s, f))
        return fail("непонятный образ упаковки: " + t.s);
    if (!f.ip && !f.fp) return fail("в образе упаковки нет ни одного знака #");
    const unsigned size = image_packed_size(f);

    if (unpack) {
        Value from;
        if (!st.ev.expr(from)) return fail(st.ev.error());
        if (!from.is_str) return fail("UNPACK: источник не символьный");
        if (!st.ev.parser().take(t, false) || t.t != Tok::KW_TO)
            return fail("UNPACK без TO");

        // Значений ровно столько, сколько групп помещается в источник, но
        // не дальше первой неупакованной: хвост поля обычно занят чем
        // придётся, а сколько чисел нужно — решают приёмники.
        std::vector<Value> vals;
        for (std::size_t at = 0; at + size <= from.str.size(); at += size) {
            Value v;
            if (!image_unpack(from.str.substr(at, size), f, v.num)) break;
            vals.push_back(v);
        }
        if (vals.empty()) return fail("UNPACK: в источнике не упакованное число");

        std::size_t used = 0;
        while (!st.src.at_end()) {
            Evaluator::Target target;
            if (!st.ev.target(target, true)) return fail(st.ev.error());
            if (target.is_str) return fail("UNPACK: приёмник не числовой");
            if (!store_value(target, st, vals, used)) return false;
            uint8_t b = 0;
            if (st.src.peek_raw_byte(b) && b == 0xDE) st.src.skip(1);
        }
        return true;
    }

    Evaluator::Target target;
    if (!st.ev.target(target, true)) return fail(st.ev.error());
    if (!target.is_str || !target.data) return fail("PACK: приёмник не символьный");

    uint8_t b = 0;
    if (!st.src.take_raw_byte(b) || b != 0xCA) return fail("PACK без FROM");

    // Массивы разворачиваются в элементы тем же кодом, что у DATA SAVE DC.
    std::vector<Value> vals;
    if (!save_values(st, vals)) return false;
    if (vals.empty()) return fail("PACK без значений");

    std::string out;
    for (std::size_t i = 0; i < vals.size(); ++i) {
        if (vals[i].is_str) return fail("PACK: упаковывать можно только числа");
        std::string one;
        if (!image_pack(vals[i].num, f, one))
            return fail("PACK: число не помещается в образ");
        out += one;
    }
    if (out.size() > target.len)
        return fail("PACK: упакованное не помещается в приёмник");

    std::string & field = *target.data;
    for (std::size_t i = 0; i < out.size(); ++i)
        field[target.off + i] = out[i];
    return true;
}

bool Interp::do_convert(Stream & st)
{
    // `CONVERT <а.в.> TO <приёмник>[,<образ>]`; знак равенства и скобки
    // вокруг образа в потоке не кодируются.
    Value from;
    if (!st.ev.expr(from)) return fail(st.ev.error());
    Tok t;
    if (!st.ev.parser().take(t, false) || t.t != Tok::KW_TO)
        return fail("CONVERT без TO");

    Evaluator::Target target;
    if (!st.ev.target(target, true)) return fail(st.ev.error());

    if (target.is_str) {
        // Число в символьное представление: нужен образ.
        if (from.is_str) return fail("CONVERT: слева ожидалось число");
        std::string image;
        bool has_image = false;
        if (!st.ev.parser().peek(t, false)) return fail(st.ev.error());
        if (t.t == Tok::COMMA) st.ev.parser().consume();
        else st.ev.parser().unpeek();
        if (!st.ev.parser().peek(t, true)) return fail(st.ev.error());
        if (t.t == Tok::STR) {
            st.ev.parser().consume();
            image = t.s;
            has_image = true;
        } else {
            st.ev.parser().unpeek();
        }

        std::string text;
        if (has_image) {
            std::string error;
            if (!format_by_image(from.num, image, text, error)) return fail(error);
        } else {
            // Образ не задан. Книга такой формы не описывает, но в корпусе
            // она встречается (BAM*: CONVERT V0E TO STR(V0D¤,12,4)).
            // Допущение: число прижимается вправо к длине приёмника.
            text = from.num.to_display();
            if (!from.num.is_negative() && !text.empty() && text[0] == ' ')
                text = text.substr(1);
            while (text.size() < target.len) text = " " + text;
        }
        return assign_string(st, target, text);
    }

    // Символьное представление в число: «преобразуемое значение должно
    // представлять собой правильную запись числа» (разд. 13.6).
    if (!from.is_str) return fail("CONVERT: слева ожидалась строка");
    Value v;
    if (!Number::parse(from.str, v.num))
        return fail("CONVERT: «" + from.str + "» не число");
    if (!st.ev.store(target, v)) return fail(st.ev.error());
    return true;
}

// «Преобразует целую часть арифметического выражения в двоичное число и
// записывает это число в первом байте или в первых двух байтах символьной
// переменной» (руководство, разд. 14.2). Операция, обратная функции VAL(.
bool Interp::do_bin(Stream & st)
{
    Evaluator::Target target;
    if (!st.ev.target(target, true)) return fail(st.ev.error());
    if (!target.is_str) return fail("BIN( записывает в символьную переменную");

    // «,2» кодируется парой DE DB. Смотрим сырой байт, а не лексему: за
    // приёмником может сразу стоять индекс переменной, и в позиции операции
    // он выглядит как индексация символьной переменной.
    unsigned bytes = 1;
    uint8_t sep = 0;
    if (st.src.peek_raw_byte(sep) && sep == 0xDE) {
        st.src.skip(1);
        uint8_t db = 0;
        if (!st.src.take_raw_byte(db) || db != 0xDB)
            return fail("BIN(: второй аргумент может быть только 2");
        bytes = 2;
    }

    Number n;
    if (!st.ev.number(n)) return fail(st.ev.error());
    long v = 0;
    if (!n.floor_to_int(v)) return fail("BIN(: значение не помещается в целое");

    // «Если значение арифметического выражения превысит соответствующие
    // пределы, то при выполнении оператора возникнет ошибка».
    const long limit = (bytes == 2) ? 65535 : 255;
    if (v < 0 || v > limit)
        return fail("BIN(: значение вне пределов 0…" + num_str(static_cast<unsigned>(limit)));
    if (target.len < bytes)
        return fail("BIN(: приёмник короче " + num_str(bytes) + " байт");

    // Пишем только эти байты: остаток поля BIN не трогает — в примере 14.4
    // BIN(A¤)=1 при A¤ из двух нулевых байт даёт 0100, а не 01.
    unsigned long u = static_cast<unsigned long>(v);
    if (bytes == 2) {
        (*target.data)[target.off]     = static_cast<char>((u >> 8) & 0xFF);
        (*target.data)[target.off + 1] = static_cast<char>(u & 0xFF);
    } else {
        (*target.data)[target.off] = static_cast<char>(u & 0xFF);
    }
    return true;
}

// «Для задания одинаковых значений во все байты символьных переменных или их
// подстрок» (руководство, разд. 13.3).
// --- операции над байтами (руководство, гл. 14) -----------------------------

namespace {
    // «Значение х, представляемое в двоичном виде четырьмя разрядами,
    // является результатом выполняемой операции» для пар аргументов
    // (1,1), (1,0), (0,1), (0,0) — таблица 14.5. То есть разряд результата
    // это просто разряд x под номером «аргумент1*2 + аргумент2».
    uint8_t bool_byte(unsigned x, uint8_t a, uint8_t b)
    {
        uint8_t r = 0;
        for (unsigned k = 0; k < 8; ++k) {
            const unsigned i = (((a >> k) & 1u) << 1) | ((b >> k) & 1u);
            if ((x >> i) & 1u) r |= static_cast<uint8_t>(1u << k);
        }
        return r;
    }

    // Циклический сдвиг поля на |k| разрядов: влево при k > 0, вправо при
    // k < 0 (руководство, разд. 14.4). Границы байтов не учитываются.
    void rotate_bits(std::string & f, unsigned off, unsigned len, long k)
    {
        if (!len || !k) return;
        const bool left = k > 0;
        const unsigned n = static_cast<unsigned>(left ? k : -k);
        std::string in(f, off, len);
        for (unsigned i = 0; i < len; ++i) {
            const unsigned prev = (i + len - 1) % len;
            const unsigned next = (i + 1) % len;
            unsigned v;
            if (n == 8) {
                v = static_cast<unsigned char>(in[left ? next : prev]);
            } else if (left) {
                v = (static_cast<unsigned char>(in[i]) << n) |
                    (static_cast<unsigned char>(in[next]) >> (8 - n));
            } else {
                v = (static_cast<unsigned char>(in[i]) >> n) |
                    (static_cast<unsigned char>(in[prev]) << (8 - n));
            }
            f[off + i] = static_cast<char>(v & 0xFF);
        }
    }
}

// Второй аргумент поразрядных операторов: либо код байта, либо вторая
// символьная переменная. Разделителя между ними в потоке нет вовсе —
// `DE hh` это и есть однобайтовый литерал (`AND(B¤,DF)` = `43 03 23 DE DF`,
// EDITOR 3469; `AND(A¤,B¤)` = `43 02 1E 15`, DISSM 23571).
bool Interp::byte_arg(Stream & st, Value & out)
{
    if (!st.ev.operand(out)) return fail(st.ev.error());
    if (out.is_str) return true;
    long n = 0;
    if (!out.num.floor_to_int(n) || n < 0 || n > 255)
        return fail("код байта вне 0…255");
    return true;
}

// «Логическая операция проводится отдельно над каждым разрядом одной
// символьной переменной (аргументом 1) и соответствующим разрядом другой
// символьной переменной либо константы. Результаты записываются в
// соответствующие разряды <символьной переменной 1>» (разд. 14.3).
//
// `AND`, `OR` и `XOR` — частные случаи `BOOL`: «операторы BOOL 8 и AND,
// BOOL Е и OR, BOOL 6 и XOR соответственно эквивалентны друг другу».
bool Interp::do_bitop(Stream & st, unsigned x, bool from_stream)
{
    if (from_stream) {
        uint8_t d = 0;
        if (!st.src.take_raw_byte(d)) return fail("BOOL без кода операции");
        if (d > 0x0F) return fail("BOOL: код операции больше F");
        x = d;
    }

    Evaluator::Target dst;
    if (!st.ev.target(dst, true)) return fail(st.ev.error());
    if (!dst.is_str || !dst.data) return fail("поразрядная операция: приёмник не символьный");

    Value arg;
    if (!byte_arg(st, arg)) return false;

    std::string & f = *dst.data;
    if (arg.is_str) {
        // Книга не говорит, что делать с хвостом более длинного приёмника;
        // принято оставлять его как есть.
        unsigned n = dst.len;
        if (arg.str.size() < n) n = static_cast<unsigned>(arg.str.size());
        for (unsigned i = 0; i < n; ++i)
            f[dst.off + i] = static_cast<char>(
                bool_byte(x, static_cast<unsigned char>(f[dst.off + i]),
                          static_cast<unsigned char>(arg.str[i])));
    } else {
        // «В последнем случае логическая операция производится с содержимым
        // разрядов всех байтов <символьной переменной 1>».
        long v = 0;
        arg.num.floor_to_int(v);
        const uint8_t b = static_cast<uint8_t>(v);
        for (unsigned i = 0; i < dst.len; ++i)
            f[dst.off + i] = static_cast<char>(
                bool_byte(x, static_cast<unsigned char>(f[dst.off + i]), b));
    }
    return true;
}

// «Содержимое аргументов складывается по правилам сложения двоичных чисел,
// а результат заносится в содержимое первого аргумента» (разд. 14.1).
// Без `C` переноса между байтами нет; с `C` границы байтов игнорируются.
bool Interp::do_add(Stream & st)
{
    bool carry_mode = false;
    uint8_t b = 0;
    // Признак `C` — тот же байт `D4`, что у `ROTATE C`. В корпусе форма с
    // `C` не встречается, байт взят по аналогии (CLAUDE.md, «Допущения»).
    if (st.src.peek_raw_byte(b) && b == 0xD4) { st.src.skip(1); carry_mode = true; }

    Evaluator::Target dst;
    if (!st.ev.target(dst, true)) return fail(st.ev.error());
    if (!dst.is_str || !dst.data) return fail("ADD: приёмник не символьный");

    Value arg;
    if (!byte_arg(st, arg)) return false;

    std::string & f = *dst.data;
    if (!arg.is_str) {
        long v = 0;
        arg.num.floor_to_int(v);
        const unsigned add = static_cast<unsigned>(v) & 0xFF;
        if (!carry_mode) {
            // «hh складывается с содержимым каждого байта».
            for (unsigned i = 0; i < dst.len; ++i)
                f[dst.off + i] = static_cast<char>(
                    (static_cast<unsigned char>(f[dst.off + i]) + add) & 0xFF);
            return true;
        }
        // «Если параметр С присутствует, hh складывается только с
        // содержимым последнего байта символьной переменной».
        unsigned carry = add;
        for (unsigned i = dst.len; i-- > 0 && carry; ) {
            const unsigned s = static_cast<unsigned char>(f[dst.off + i]) + carry;
            f[dst.off + i] = static_cast<char>(s & 0xFF);
            carry = s >> 8;
        }
        return true;
    }

    // «Если длина символьных переменных различна, то переменная с меньшей
    // длиной выравнивается по длине большей, т. е. складываются сначала
    // последние байты переменных, потом предпоследние».
    const unsigned m = static_cast<unsigned>(arg.str.size());
    unsigned carry = 0;
    for (unsigned k = 0; k < dst.len; ++k) {
        const unsigned i = dst.len - 1 - k;
        const unsigned add = (k < m) ? static_cast<unsigned char>(arg.str[m - 1 - k]) : 0u;
        const unsigned s = static_cast<unsigned char>(f[dst.off + i]) + add + carry;
        f[dst.off + i] = static_cast<char>(s & 0xFF);
        carry = carry_mode ? (s >> 8) : 0u;
    }
    return true;
}

// «Оператор ROTATE предназначен для циклического сдвига содержимого
// символьной переменной на 1—8 разрядов… Отсутствие параметра С обозначает
// проведение операции с содержимым каждого байта; если параметр С задан,
// границы между байтами игнорируются» (разд. 14.4).
bool Interp::do_rotate(Stream & st)
{
    bool whole = false;
    uint8_t b = 0;
    if (st.src.peek_raw_byte(b) && b == 0xD4) { st.src.skip(1); whole = true; }

    Tok t;
    if (!st.ev.parser().take(t, true) || t.t != Tok::LPAR)
        return fail("ROTATE без скобки");

    Evaluator::Target dst;
    if (!st.ev.target(dst, true)) return fail(st.ev.error());
    if (!dst.is_str || !dst.data) return fail("ROTATE: не символьная переменная");

    if (!st.ev.parser().take(t, false) || t.t != Tok::COMMA)
        return fail("ROTATE без запятой");

    Number n;
    if (!st.ev.number(n)) return fail(st.ev.error());
    long k = 0;
    if (!n.floor_to_int(k)) return fail("ROTATE: сдвиг не целое число");
    if (k < -8 || k > 8) return fail("ROTATE: сдвиг вне −8…8");

    if (!st.ev.parser().take(t, false) || t.t != Tok::RPAR)
        return fail("ROTATE: скобка не закрыта");

    std::string & f = *dst.data;
    if (whole) {
        rotate_bits(f, dst.off, dst.len, k);
    } else {
        for (unsigned i = 0; i < dst.len; ++i)
            rotate_bits(f, dst.off + i, 1, k);
    }
    return true;
}

bool Interp::do_init(Stream & st)
{
    // Значение — код из двух шестнадцатеричных цифр, символ в кавычках либо
    // символьная переменная: «в последнем случае для задания значения
    // используется первый байт» (руководство, разд. 13.3).
    Tok t;
    if (!st.ev.parser().peek(t, true)) return fail(st.ev.error());
    Value code;
    if (t.t == Tok::STR || t.t == Tok::NUM) {
        st.ev.parser().consume();
        code.is_str = (t.t == Tok::STR);
        code.str = t.s;
        code.num = t.num;
    } else {
        // Переменная тут читается по таблицам: за ней сразу идут приёмники,
        // и заглядывание приняло бы их за список индексов (CLAUDE.md).
        Evaluator::Target src;
        if (!st.ev.target(src, true)) return fail(st.ev.error());
        if (!st.ev.load(src, code)) return fail(st.ev.error());
    }

    char fill;
    if (code.is_str) {
        if (code.str.empty()) return fail("INIT( от пустой строки");
        fill = code.str[0];
    } else {
        long n = 0;
        if (!code.num.floor_to_int(n) || n < 0 || n > 255)
            return fail("INIT(: значение не байт");
        fill = static_cast<char>(n & 0xFF);
    }

    bool any = false;
    for (;;) {
        if (!st.ev.parser().peek(t, true)) return fail(st.ev.error());
        if (t.t == Tok::END) break;
        Evaluator::Target target;
        if (!st.ev.target(target, true)) return fail(st.ev.error());
        if (!target.is_str) return fail("INIT( заполняет символьные переменные");
        // «Значение присваивается всем байтам»: у массива без STR( — всему
        // полю целиком, оно одна непрерывная строка.
        for (unsigned k = 0; k < target.len; ++k)
            (*target.data)[target.off + k] = fill;
        any = true;
    }
    if (!any) return fail("INIT( без приёмников");
    return true;
}

bool Interp::do_let(Stream & st)
{
    std::vector<Evaluator::Target> targets;
    for (;;) {
        Evaluator::Target t;
        if (!st.ev.target(t, true)) return fail(st.ev.error());
        targets.push_back(t);
        Tok k;
        // Заглядывать надо в позиции операнда: дальше либо `=`, либо
        // очередная цель (CLAUDE.md).
        if (!st.ev.parser().peek(k, true)) return fail(st.ev.error());
        if (k.t == Tok::EQ) { st.ev.parser().consume(); break; }
    }

    Value v;
    if (!st.ev.expr(v)) return fail(st.ev.error());
    for (unsigned i = 0; i < targets.size(); ++i) {
        if (targets[i].is_str != v.is_str)
            return fail("в присваивании не совпадают типы");
        if (!st.ev.store(targets[i], v)) return fail(st.ev.error());
    }
    return true;
}

bool Interp::do_for(Stream & st)
{
    Evaluator::Target t;
    if (!st.ev.target(t, true)) return fail(st.ev.error());
    if (t.is_str || t.nidx) return fail("FOR: счётчиком может быть только простая переменная");

    Number start, limit, step;
    if (!st.ev.number(start)) return fail(st.ev.error());
    Tok k;
    if (!st.ev.parser().take(k, false) || k.t != Tok::KW_TO) return fail("FOR без TO");
    if (!st.ev.number(limit)) return fail(st.ev.error());
    if (!st.ev.parser().peek(k, false)) return fail(st.ev.error());
    if (k.t == Tok::KW_STEP) {
        st.ev.parser().consume();
        if (!st.ev.number(step)) return fail(st.ev.error());
    } else {
        step = Number::from_int(1);
    }
    if (step.is_zero()) return fail("FOR с нулевым шагом");

    Value v;
    v.num = start;
    if (!st.ev.store(t, v)) return fail(st.ev.error());

    // Уже открытый цикл по той же переменной перезапускается.
    for (unsigned i = 0; i < loops_.size(); ++i)
        if (loops_[i].var == t.var) { loops_.resize(i); break; }

    Frame f;
    f.var = t.var;
    f.limit = limit;
    f.step = step;
    f.line = li_;
    f.off = next_off_;
    loops_.push_back(f);
    return true;
}

bool Interp::do_next(Stream & st)
{
    Tok k;
    if (!st.ev.parser().take(k, true) || k.t != Tok::VAR)
        return fail("NEXT без переменной");
    const unsigned var = k.var;

    while (!loops_.empty() && loops_.back().var != var) loops_.pop_back();
    if (loops_.empty()) return fail("NEXT без FOR");

    Frame & f = loops_.back();
    std::string err;
    Number * cell = 0;
    if (!store_.slot(f.var, 0, 0, cell, err)) return fail(err);
    Number v = *cell;
    if (!Number::add(v, f.step, v)) return fail("переполнение счётчика цикла");
    *cell = v;

    const bool up = !f.step.is_negative();
    const bool go_on = up ? (v.compare(f.limit) <= 0) : (v.compare(f.limit) >= 0);

    if (go_on) {
        li_ = f.line;
        off_ = f.off;
        jumped_ = true;
    } else {
        loops_.pop_back();
    }
    return true;
}

bool Interp::do_if(Stream & st)
{
    Value v;
    if (!st.ev.expr(v)) return fail(st.ev.error());
    Tok t;
    if (!st.ev.parser().take(t, false) || t.t != Tok::KW_THEN)
        return fail("IF без THEN");
    if (v.is_str) return fail("условием IF оказалась строка");
    if (v.num.is_zero()) return true;
    long ln = 0;
    if (!t.num.to_int(ln)) return fail("IF: неверный номер строки");
    return jump(static_cast<unsigned>(ln));
}

bool Interp::do_on(Stream & st)
{
    Number n;
    if (!st.ev.number(n)) return fail(st.ev.error());
    Tok t;
    if (!st.ev.parser().take(t, false)) return fail(st.ev.error());
    bool is_gosub;
    if (t.t == Tok::KW_GOSUB) is_gosub = true;
    else if (t.t == Tok::KW_GOTO) is_gosub = false;
    else return fail("ON без GOTO или GOSUB");

    std::vector<unsigned> lines;
    for (;;) {
        uint8_t a = 0, b = 0;
        if (!st.src.take_raw_byte(a)) break;
        if (!st.src.take_raw_byte(b)) return fail("ON: оборванный номер строки");
        lines.push_back(bcd2(a) * 100 + bcd2(b));
    }
    if (lines.empty()) return fail("ON без номеров строк");

    long k = 0;
    if (!n.floor_to_int(k)) return fail("ON: не целое число");
    // «Если значение выражения меньше единицы или больше числа указанных
    // строк, выполняется следующий оператор» (руководство, разд. 10.3).
    if (k < 1 || static_cast<std::size_t>(k) > lines.size()) return true;

    if (is_gosub) {
        if (calls_.size() > 1000) return fail("слишком глубокая вложенность GOSUB");
        calls_.push_back(std::make_pair(li_, next_off_));
    }
    return jump(lines[static_cast<std::size_t>(k) - 1]);
}

void Interp::build_labels()
{
    labels_ready_ = true;
    for (unsigned l = 0; l < img_.line_count(); ++l) {
        const std::vector<uint8_t> & b = img_.line(l).body;
        unsigned at = 0;
        for (;;) {
            unsigned verb = 0, ops_at = 0, len = 0;
            if (!stmt_head(b, at, verb, ops_at, len)) break;
            // Определение клавиши специальных функций подпрограммой не
            // является — на его метку GOSUB' не переходит.
            if (verb == 0x27 && len >= 1) {
                const unsigned label = b[ops_at];
                // Машина просматривает текст сверху вниз, поэтому при
                // повторе имени побеждает первое определение.
                if (labels_.find(label) == labels_.end())
                    labels_[label] = std::make_pair(l, at);
            }
            at = ops_at + len;
            if (at >= b.size()) break;
        }
    }
}

// --- READ, DATA, RESTORE ----------------------------------------------------

// «При выполнении оператора READ отыскивается оператор DATA, независимо от
// того, в каком месте программы он находится» (руководство, разд. 4.9).
// У машины операторы DATA связаны цепочкой прямо в потоке: два последних
// байта операндов каждого — адрес следующего (docs/format.md, разд. 5).
// Здесь тот же порядок восстанавливается просмотром программы.
void Interp::build_data()
{
    data_.clear();
    data_ready_ = true;
    for (unsigned l = 0; l < img_.line_count(); ++l) {
        const std::vector<uint8_t> & b = img_.line(l).body;
        unsigned at = 0;
        for (;;) {
            unsigned verb = 0, ops_at = 0, len = 0;
            if (!stmt_head(b, at, verb, ops_at, len)) break;
            if (verb == 0x29 && len >= 2) {
                DataStmt d;
                d.line = l;
                d.at = ops_at;
                d.len = len - 2;         // хвост — указатель цепочки, не значение
                data_.push_back(d);
            }
            at = ops_at + len;
            if (at >= b.size()) break;
        }
    }
}

bool Interp::next_data(Value & out, bool & exhausted)
{
    exhausted = false;
    if (!data_ready_) build_data();

    while (data_i_ < data_.size()) {
        const DataStmt & d = data_[data_i_];
        if (data_off_ >= d.len) { ++data_i_; data_off_ = 0; continue; }

        const std::vector<uint8_t> & b = img_.line(d.line).body;
        // Значения идут вплотную, без разделителей, поэтому берётся ровно
        // один операнд: полное выражение прочитало бы `E7` следующей
        // константы как `AND`.
        Stream ds(&b[d.at], d.len, &img_.vars(), store_, &fnres_);
        ds.src.set_pos(data_off_);
        if (!ds.ev.operand(out)) return fail(ds.ev.error());
        data_off_ = ds.src.pos();
        return true;
    }
    exhausted = true;
    return false;
}

// «Переменные, которым нужно присвоить значения, содержащиеся в операторе
// DATA, в том же порядке перечисляются в операторе READ» (разд. 4.9).
// Приёмники идут вплотную, без разделителей (VICT 2200).
bool Interp::do_read(Stream & st)
{
    while (!st.src.at_end()) {
        Evaluator::Target t;
        if (!st.ev.target(t, true)) return fail(st.ev.error());

        Value v;
        bool exhausted = false;
        if (!next_data(v, exhausted)) {
            if (!exhausted) return false;
            // «При попытке считывания 13-й пары данных система выдаст
            // сообщение об ошибке (ERR 27), поскольку в операторах DATA
            // нет больше констант» (пример 4.21).
            return machine_error(err::DATA_END, "в операторах DATA больше нет значений");
        }
        if (t.is_str != v.is_str)
            return machine_error(err::UNKNOWN,
                                 "тип значения в DATA не совпадает с приёмником");
        if (!st.ev.store(t, v)) return fail(st.ev.error());
    }
    return true;
}

// «Оператор RESTORE без параметров устанавливает специальный указатель
// начала считывания данных на первую константу первого оператора DATA в
// программе» (разд. 4.9). Формы: `RESTORE`, `RESTORE <а.в.>`,
// `RESTORE ,<строка>` и `RESTORE <а.в.>,<строка>`; в потоке запятая — `DE`,
// номер строки — сырой двухбайтовый BCD.
bool Interp::do_restore(Stream & st)
{
    if (!data_ready_) build_data();

    long n = 1;
    unsigned line = 0;
    bool has_line = false;

    bool comma = false;
    if (!st.src.at_end()) {
        uint8_t b = 0;
        // Запятая формы `RESTORE ,<строка>` читается сырым байтом: в позиции
        // операнда `DE` — не запятая, а однобайтовый литерал, и он съел бы
        // старший байт номера строки (CLAUDE.md, ловушка 2).
        if (st.src.peek_raw_byte(b) && b == 0xDE) {
            st.src.skip(1);
            comma = true;
        } else {
            Number v;
            if (!st.ev.number(v)) return fail(st.ev.error());
            // «Значение арифметического выражения должно быть в пределах
            // от 1 до 9999».
            if (!v.floor_to_int(n) || n < 1 || n > 9999)
                return machine_error(err::UNKNOWN, "RESTORE: номер константы вне 1…9999");
            // А вот после выражения разделитель читается уже в позиции
            // операции — там `DE` запятая и есть.
            Tok t;
            if (!st.ev.parser().peek(t, false)) return fail(st.ev.error());
            if (t.t == Tok::COMMA) { st.ev.parser().consume(); comma = true; }
            else st.ev.parser().unpeek();
        }
    }
    if (comma) {
        uint8_t a = 0, b = 0;
        if (!st.src.take_raw_byte(a) || !st.src.take_raw_byte(b))
            return fail("RESTORE: обрезан номер строки");
        line = bcd2(a) * 100 + bcd2(b);
        has_line = true;
    }

    unsigned start = 0;
    if (has_line) {
        // «отсчёт начинается с первой константы DATA указанной строки»
        for (; start < data_.size(); ++start)
            if (img_.line(data_[start].line).number == line) break;
        if (start >= data_.size())
            return machine_error(err::UNKNOWN,
                                 "RESTORE: в строке " + num_str(line) + " нет оператора DATA");
    }
    restore_data(start);

    // Отсчёт констант с единицы: `RESTORE 4,120` — на четвёртой.
    for (long k = 1; k < n; ++k) {
        Value v;
        bool exhausted = false;
        if (!next_data(v, exhausted)) {
            if (!exhausted) return false;
            return machine_error(err::DATA_END, "RESTORE: в операторах DATA меньше значений");
        }
    }
    return true;
}

bool Interp::do_deffn(Stream & st, unsigned len)
{
    // Само определение исполнения не требует: подпрограмма начинается
    // после него.
    (void)st;
    (void)len;
    return true;
}

// --- функции пользователя (руководство, разд. 4.8) ---------------------------

// «Функция может быть объявлена в любом месте программы, независимо от того,
// где она будет использоваться», поэтому определения ищутся просмотром всего
// текста — тем же, что и метки помеченных подпрограмм.
void Interp::build_functions()
{
    funcs_ready_ = true;
    for (unsigned l = 0; l < img_.line_count(); ++l) {
        const std::vector<uint8_t> & b = img_.line(l).body;
        unsigned at = 0;
        for (;;) {
            unsigned verb = 0, ops_at = 0, len = 0;
            if (!stmt_head(b, at, verb, ops_at, len)) break;
            if (verb == 0x5A && len >= 4) {
                const unsigned name = b[ops_at];
                // Машина ищет просмотром текста сверху вниз, поэтому при
                // повторе имени побеждает первое определение — так же, как
                // у DEFFN'.
                if (funcs_.find(name) == funcs_.end())
                    funcs_[name] = std::make_pair(l, at);
            }
            at = ops_at + len;
            if (at >= b.size()) break;
        }
    }
}

// «Сначала вычисляется значение арифметического выражения, затем это
// значение присваивается формальной переменной и осуществляется вычисление
// значения выражения, заданного в определении функции» (разд. 4.8).
//
// Формальная переменная — обычная глобальная переменная программы, как и у
// помеченных подпрограмм: своего места для неё в машине нет, и прежнее
// значение не восстанавливается.
bool Interp::call_fn(unsigned name, const Value & arg, Value & out,
                     std::string & err)
{
    if (!funcs_ready_) build_functions();

    std::map<unsigned, std::pair<unsigned, unsigned> >::const_iterator it =
        funcs_.find(name);
    if (it == funcs_.end()) return false;      // причина — за вызывающим

    if (arg.is_str) {
        err = std::string("FN ") + static_cast<char>(name) +
              ": аргумент символьный, а функция числовая";
        return false;
    }

    // Тело лежит в программе, и вычисляется оно тем же кодом. Обращение
    // изнутри тела к самому себе программу бы зациклило, а стек — обвалило.
    if (fn_depth_ >= 32) {
        err = std::string("FN ") + static_cast<char>(name) +
              ": слишком глубокая вложенность вызовов";
        return false;
    }

    const std::vector<uint8_t> & b = img_.line(it->second.first).body;
    unsigned verb = 0, ops_at = 0, len = 0;
    if (!stmt_head(b, it->second.second, verb, ops_at, len) || len < 4) {
        err = std::string("FN ") + static_cast<char>(name) + ": DEFFN испорчен";
        return false;
    }
    // Байты: имя, два байта рабочего поля, формальная переменная, тело.
    const unsigned formal = b[ops_at + 3];
    if (store_.is_string(formal)) {
        err = std::string("FN ") + static_cast<char>(name) +
              ": формальная переменная символьная";
        return false;
    }
    std::string serr;
    Number * cell = 0;
    if (!store_.slot(formal, 0, 0, cell, serr)) { err = serr; return false; }
    *cell = arg.num;

    Stream body(&b[ops_at + 4], len - 4, &img_.vars(), store_, &fnres_);
    ++fn_depth_;
    Value v;
    const bool ok = body.ev.expr(v);
    --fn_depth_;
    if (!ok) {
        err = std::string("FN ") + static_cast<char>(name) + ": " +
              body.ev.error();
        return false;
    }
    out = v;
    return true;
}

bool Interp::Functions::call_fn(unsigned name, const Value & arg, Value & out,
                                std::string & err)
{
    return owner->call_fn(name, arg, out, err);
}

bool Interp::do_gosubq(Stream & st)
{
    if (!labels_ready_) build_labels();

    uint8_t label = 0;
    if (!st.src.take_raw_byte(label)) return fail("GOSUB' без метки");

    std::map<unsigned, std::pair<unsigned, unsigned> >::const_iterator it =
        labels_.find(label);
    if (it == labels_.end())
        return fail("нет подпрограммы с именем " + num_str(label));

    // Все фактические параметры вычисляются до первого присваивания:
    // подпрограмму зовут и через её же формальные переменные, например
    // GOSUB '100(L3,A%,1) при DEFFN '100(L1,L4,L3).
    std::vector<Value> vals;
    for (;;) {
        Tok t;
        if (!st.ev.parser().peek(t, true)) return fail(st.ev.error());
        if (t.t == Tok::END) break;
        Value v;
        if (!st.ev.expr(v)) return fail(st.ev.error());
        vals.push_back(v);
        if (!st.ev.parser().peek(t, false)) return fail(st.ev.error());
        if (t.t != Tok::COMMA) break;
        st.ev.parser().consume();
    }

    // Формальные параметры лежат в самом DEFFN': метка, четыре байта
    // адреса возврата, дальше индексы переменных вплотную.
    const std::vector<uint8_t> & db = img_.line(it->second.first).body;
    unsigned verb = 0, ops_at = 0, dlen = 0;
    if (!stmt_head(db, it->second.second, verb, ops_at, dlen))
        return fail("DEFFN' испорчен");
    if (dlen < 5) return fail("DEFFN' без адреса возврата");
    std::vector<unsigned> params;
    for (unsigned i = 5; i < dlen; ++i) params.push_back(db[ops_at + i]);

    if (params.size() != vals.size())
        return fail("подпрограмме " + num_str(label) + " передано " +
                    num_str(static_cast<unsigned>(vals.size())) +
                    " параметров, а описано " +
                    num_str(static_cast<unsigned>(params.size())));

    for (unsigned i = 0; i < params.size(); ++i) {
        const unsigned v = params[i];
        const bool want_str = store_.is_string(v);
        if (want_str != vals[i].is_str)
            return fail("параметр " + num_str(i + 1) + " подпрограммы " +
                        num_str(label) + ": не совпадают типы");
        std::string err;
        if (want_str) {
            VarStore::StrLoc loc;
            if (!store_.str_element(v, 0, 0, loc, err)) return fail(err);
            for (unsigned k = 0; k < loc.len; ++k)
                (*loc.data)[loc.off + k] = (k < vals[i].str.size()) ? vals[i].str[k] : ' ';
        } else {
            Number * cell = 0;
            if (!store_.slot(v, 0, 0, cell, err)) return fail(err);
            *cell = vals[i].num;
        }
    }

    if (calls_.size() > 1000) return fail("слишком глубокая вложенность GOSUB");
    calls_.push_back(std::make_pair(li_, next_off_));

    li_ = it->second.first;
    off_ = ops_at + dlen;             // первый оператор после DEFFN'
    jumped_ = true;
    return true;
}

// «Для записи в каталог программы, находящейся в данный момент в
// оперативной памяти» (руководство, разд. 5.2). Операнды: буква устройства,
// `¤` — контрольное считывание, `T` — оттранслированная форма, дальше
// необязательная скобка и имя.
//
// Скобка двузначна и различается по типу значения (разд. 5.3):
// `("<старое имя>")` — писать на место вычеркнутого файла,
// `(<а.в.>)` — сколько секторов добавить в запас.
bool Interp::do_save_dc(Stream & st)
{
    uint8_t b = 0;
    bool has_device = false;
    unsigned device = 2;
    if (st.src.peek_raw_byte(b) && b <= 2) {
        st.src.skip(1);
        has_device = true;
        device = b;
    }
    if (st.src.peek_raw_byte(b) && b == 0xD6) st.src.skip(1);   // ¤ — контроль
    // `T` (оттранслированная форма) — единственная, которую умеет эмулятор:
    // текстовой записи программы на диск ещё нет.
    if (st.src.peek_raw_byte(b) && b == 0xD2) st.src.skip(1);

    Value extra;
    bool has_extra = false;
    Tok t;
    if (!st.ev.parser().peek(t, true)) return fail(st.ev.error());
    if (t.t == Tok::LPAR) {
        st.ev.parser().consume();
        if (!st.ev.expr(extra)) return fail(st.ev.error());
        if (!st.ev.parser().take(t, false) || t.t != Tok::RPAR)
            return fail("SAVE DC: скобка не закрыта");
        has_extra = true;
    } else {
        st.ev.parser().unpeek();
    }

    std::string name;
    if (!st.ev.text(name)) return fail(st.ev.error());

    // Диск выбирается той же приставкой, что и у прочих дисковых операторов,
    // только строки таблицы у SAVE DC нет — работает строка #0.
    DeviceRow & r0 = dev_.row(0);
    unsigned addr = r0.addr;
    bool removable = r0.removable;
    if (has_device && device < 2) removable = (device == 1);
    unsigned drive = 0;
    if (!DeviceTable::drive_index(static_cast<uint8_t>(addr), removable, drive))
        return fail("неизвестный адрес дискового устройства");
    if (!host_.disk_sectors(drive)) return fail("дисковода нет");

    std::vector<uint8_t> file;
    img_.save_file(name, file);
    const unsigned need = static_cast<unsigned>(file.size() / Host::SECTOR_SIZE);

    Catalog cat(host_, drive);
    uint8_t nm[NAME_LEN];
    Catalog::make_name(name, nm);

    CatalogEntry e;
    std::string err;
    if (!cat.find(nm, e, err)) return fail(err);
    // «Предполагается, что ранее в каталоге файла с таким именем не было,
    // иначе записи не произойдет» (разд. 5.2).
    if (e.alive()) return machine_error(err::FILE_EXISTS, "файл с таким именем уже есть");

    if (has_extra && extra.is_str) {
        // Запись на место вычеркнутого файла: имя старого файла в скобках.
        uint8_t old[NAME_LEN];
        Catalog::make_name(extra.str, old);
        CatalogEntry victim;
        if (!cat.find(old, victim, err)) return fail(err);
        if (!victim.exists() || !victim.scratched())
            return machine_error(err::NO_FILE, "вычеркнутого файла с таким именем нет");
        // «Если программа не помещается на нем полностью, выдается
        // соответствующее сообщение об ошибке» (разд. 5.3).
        if (victim.sectors() < need)
            return machine_error(err::FILE_SMALL, "программа не помещается в старый файл");
        if (!cat.rename_over(victim, nm, true, e, err)) return fail(err);
    } else {
        unsigned reserve = 0;
        if (has_extra) {
            long v = 0;
            if (!extra.num.floor_to_int(v) || v < 0)
                return fail("SAVE DC: запас не целое неотрицательное число");
            reserve = static_cast<unsigned>(v);
        }
        // Не всякий отказ create() — нехватка места: дискета может просто не
        // писаться. Код у этих бед разный, и выдавать «файл слишком велик»
        // там, где виноват диск, значит сбивать с толку обработчик ON ERROR.
        if (!cat.create(nm, true, need + reserve, e, err))
            return machine_error(cat.io_error() ? err::UNKNOWN : err::FILE_BIG,
                                 err);
    }

    for (unsigned i = 0; i < need; ++i)
        if (!host_.disk_write(drive, e.first + i, &file[i * Host::SECTOR_SIZE]))
            return machine_error(err::UNKNOWN, "сбой записи на диск");

    return true;
}

// «Оператор LOAD DC используется в программе для загрузки нового
// программного сегмента» (руководство, разд. 19.1). Последовательность
// оттуда же: остановить исполнение, стереть строки (CLEAR P), стереть
// необщие переменные (CLEAR N), загрузить сегмент, начать со строки 3,
// иначе со строки 1, иначе с наименьшей.
bool Interp::do_load_dc(Stream & st)
{
    Disk d;
    if (!disk_prefix(st, true, d)) return false;

    std::string name;
    if (!st.ev.text(name)) return fail(st.ev.error());
    st.ev.parser().unpeek();
    if (!st.src.at_end())
        return fail("LOAD DC с номерами строк ещё не исполняется");

    uint8_t nm[NAME_LEN];
    Catalog::make_name(name, nm);

    Catalog cat(host_, d.drive);
    CatalogEntry e;
    std::string err;
    if (!cat.find(nm, e, err)) return fail(err);
    if (!e.alive())
        return machine_error(err::NO_FILE, "программы нет в каталоге");
    if (!e.is_program())
        return machine_error(err::NO_FILE, "это не программный файл");

    std::vector<uint8_t> file(static_cast<std::size_t>(e.sectors()) * Host::SECTOR_SIZE, 0);
    for (unsigned i = 0; i < e.sectors(); ++i)
        if (!host_.disk_read(d.drive, e.first + i, &file[i * Host::SECTOR_SIZE]))
            return machine_error(err::UNKNOWN, "сбой чтения с диска");

    ProgramImage next;
    if (!next.load_file(file, err)) return machine_error(err::UNKNOWN, err);

    // Номеров строк в операторе нет, поэтому стирается вся программа
    // целиком, а с ней — циклы и адреса возвратов.
    const bool direct = li_ == DIRECT;
    img_ = next;
    rescan();

    if (direct) {
        // «В режиме непосредственного счёта оператор LOAD DC (LOAD DA) только
        // загружает программу в оперативную память без её предварительной
        // очистки» (руководство, разд. 19.1): ни CLEAR N, ни запуска.
        // Переменные при этом достаются новой программе чужими — потому книга
        // и советует перед загрузкой набирать CLEAR.
        return true;
    }

    store_.clear_non_common();
    loops_.clear();
    calls_.clear();

    if (!img_.line_count()) return fail("загруженный сегмент пуст");
    li_ = 0;
    off_ = 0;
    jumped_ = true;
    return true;
}

bool Interp::jump(unsigned line_number)
{
    unsigned idx = 0;
    if (!img_.find(line_number, idx))
        return fail("нет строки " + num_str(line_number));
    li_ = idx;
    off_ = 0;
    jumped_ = true;
    return true;
}

bool Interp::exec(unsigned verb, const uint8_t * ops, unsigned len)
{
    // REM и % операнды не разбирают вовсе. DATA при исполнении тоже не
    // делает ничего: значения из него забирает READ, а сам он — «оператор
    // задания констант» (руководство, разд. 4.9).
    if (verb == 0x56 || verb == 0x3F || verb == 0x29) return true;

    Stream st(ops, len, &img_.vars(), store_, &fnres_);

    switch (verb) {
        case 0x36: return do_let(st);
        case 0x28: return do_printusing(st);
        case 0x44: return do_read(st);
        case 0x51: return do_restore(st);
        case 0x4C: return do_print(st);
        case 0x50: return do_hexprint(st);
        case 0x41: return do_input(st);
        case 0x0624: return do_linput(st);
        case 0x24: return do_if(st);
        case 0x57: return do_for(st);
        case 0x52: return do_next(st);
        case 0x26: return do_on(st);
        case 0x34: return do_onerr(st);
        case 0x46: return do_dim(st, len, ops, false);
        case 0x4E: return do_dim(st, len, ops, true);
        case 0x0602: return do_redim(st);
        case 0x0601: return do_mat(st);
        case 0x0606: return do_mat_copy(st);
        case 0x060A: return do_mat_search(st);
        case 0x0626: return do_replace(st);
        case 0x060C: return do_tran(st);
        case 0x47: return do_convert(st);
        case 0x48: return do_pack(st, false);
        case 0x5D: return do_pack(st, true);
        case 0x4B: return do_bin(st);
        case 0x64: return do_init(st);
        case 0x43: return do_bitop(st, 0x8, false);   // AND
        case 0x61: return do_bitop(st, 0xE, false);   // OR
        case 0x62: return do_bitop(st, 0x6, false);   // XOR
        case 0x45: return do_bitop(st, 0, true);      // BOOL
        case 0x4A: return do_add(st);
        case 0x4D: return do_rotate(st);
        case 0x54: return do_select(st);
        case 0x27: case 0x3A: return do_deffn(st, len);
        // Определение функции пользователя при исполнении ничего не делает:
        // тело вычисляется по обращению FN<имя>( (руководство, разд. 4.8).
        case 0x5A: return true;
        case 0x23: return do_gosubq(st);
        case 0x80: return do_save_dc(st);
        case 0x7D: return do_load_dc(st);

        case 0x75: return do_open(st, true);
        case 0x78: return do_dsave_open(st);
        case 0x74: return do_dload(st);
        case 0x76: return do_dsave(st);
        case 0x77: return do_dclose(st);
        case 0x1E: return do_if_end(st);
        case 0x68: return do_block_transfer(st, false);
        case 0x66: return do_block_transfer(st, true);
        case 0x6E: return do_block(st, false);
        case 0x70: return do_block(st, true);
        case 0x6F: return do_abs_record(st, false);
        case 0x71: return do_abs_record(st, true);
        case 0x83: return do_verify(st);
        case 0x79: return do_dskip(st, true);
        case 0x7A: return do_dskip(st, false);
        case 0x7B: return do_limits(st);
        case 0x7C: return do_list_dc(st);
        case 0x30: return do_return_clear(st, len);
        case 0x2C: return do_clear(st);
        case 0x2F: return do_run(st);
        case 0x2E: return do_list(st);
        case 0x25: return do_keyin(st);
        case 0x2A: return do_save_buf(st);
        case 0x2D: return do_load_buf(st);
        case 0x81: return do_scratch(st);
        case 0x82: return do_scratch_disk(st);

        case 0x21: case 0x22: {
            uint8_t a = 0, b = 0;
            if (!st.src.take_raw_byte(a) || !st.src.take_raw_byte(b))
                return fail("переход без номера строки");
            if (verb == 0x22) {
                if (calls_.size() > 1000) return fail("слишком глубокая вложенность GOSUB");
                calls_.push_back(std::make_pair(li_, next_off_));
            }
            return jump(bcd2(a) * 100 + bcd2(b));
        }

        case 0x5E: {                                   // RETURN
            if (calls_.empty()) return fail("RETURN без GOSUB");
            li_ = calls_.back().first;
            off_ = calls_.back().second;
            calls_.pop_back();
            jumped_ = true;
            return true;
        }

        case 0x42:                                     // STOP
            // Сообщение, если оно есть, печатается перед остановкой.
            if (len) {
                Value v;
                if (!st.ev.expr(v)) return fail(st.ev.error());
                if (v.is_str) { emit(v.str); emit_newline(); }
            }
            stopped_ = true;
            return true;

        case 0x59:                                     // END
            stopped_ = true;
            return true;

        default: break;
    }

    char b[16];
    std::sprintf(b, "%02X", verb & 0xFF);
    const std::string name = std::string((verb > 0xFF) ? "06 " : "") + b;

    // Машинозависимые операторы — отдельный разговор: они не «ещё не
    // исполняются», а не будут исполняться здесь вовсе.
    if (machine_verb(verb)) {
        if (skip_machine_) return true;
        return fail("оператор " + name + " машинозависим: нужна эмуляция "
                    "процессора. Ключ -i велит такие пропускать");
    }
    // Графика — отдельный проект: всё это работает над буфером, устройство
    // которого не разобрано. Пропускать нельзя — программа выглядела бы
    // рисующей.
    if (graphics_verb(verb))
        return fail("оператор " + name + " графический: устройство буфера "
                    "не разобрано");
    return fail("оператор " + name + " ещё не исполняется");
}

const std::vector<uint8_t> & Interp::body_at(unsigned li) const
{
    return (li == DIRECT) ? direct_ : img_.line(li).body;
}

void Interp::clear_all()
{
    store_.clear();
    loops_.clear();
    calls_.clear();
    rescan();
    // «Оператор RESTORE без параметров устанавливает указатель начала
    // считывания данных на первую константу первого оператора DATA»
    // (руководство, разд. 4.9) — CLEAR и RUN без номера строки делают то же.
    restore_data(0);
    trap_ = ErrorTrap();
    err_code_.clear();
}

bool Interp::loop(std::string & error)
{
    unsigned long steps = 0;

    while (!stopped_) {
        if (li_ != DIRECT && li_ >= img_.line_count()) break;
        const std::vector<uint8_t> & b = body_at(li_);
        if (off_ >= b.size()) {
            // Прямая строка кончилась — возвращаемся к приглашению.
            if (li_ == DIRECT) break;
            ++li_;
            off_ = 0;
            continue;
        }

        unsigned verb = 0, ops_at = 0, len = 0;
        if (!stmt_head(b, off_, verb, ops_at, len)) {
            error = "оператор оборван";
            return false;
        }
        next_off_ = ops_at + len;

        if (max_steps_ && ++steps > max_steps_) {
            error = "превышено число шагов: похоже на зацикливание";
            return false;
        }

        jumped_ = false;
        if (!exec(verb, len ? &b[ops_at] : 0, len)) {
            // Ошибку машины перехватывает ON ERROR; ограничение эмулятора —
            // нет, оно всегда останавливает программу.
            if (!handle_error()) { error = error_; return false; }
            continue;
        }
        if (!jumped_) off_ = next_off_;
    }

    host_.present();
    return true;
}

bool Interp::run(std::string & error)
{
    error_.clear();
    // Без номера строки RUN обнуляет переменные (разд. 4.1, пример 4.2).
    clear_all();
    li_ = 0;
    off_ = 0;
    stopped_ = false;
    return loop(error);
}

bool Interp::run_from(unsigned line_number, std::string & error)
{
    error_.clear();
    err_code_.clear();
    // Программу могли поправить между запусками, а метки и список DATA
    // строятся её просмотром. Указатель начала считывания при этом
    // сохраняется — как сохраняются и значения переменных (разд. 4.1).
    rescan();
    unsigned idx = 0;
    if (!img_.find(line_number, idx)) {
        error = "нет строки " + num_str(line_number);
        return false;
    }
    li_ = idx;
    off_ = 0;
    stopped_ = false;
    return loop(error);
}

bool Interp::execute(const uint8_t * body, unsigned len, std::string & error)
{
    error_.clear();
    err_code_.clear();
    rescan();
    direct_.assign(body, body + len);
    li_ = DIRECT;
    off_ = 0;
    stopped_ = false;
    return loop(error);
}

} // namespace iskra
