// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Mikhail Revzin <p3.141592653589793238462643@gmail.com>
// Part of the Iskra-226 project: https://github.com/Ptr314/Iskra-226
// Description: исполнение оттранслированной программы прямо из потока токенов

#include "core/interp.h"

#include "core/catalog.h"
#include "core/disk_record.h"

#include <cmath>
#include <cstdio>

namespace iskra {

namespace {
    const unsigned ZONE = 16;      // «каждая строка условно делится на 5 зон»

    std::string num_str(unsigned v)
    {
        char b[16];
        std::sprintf(b, "%u", v);
        return b;
    }
}

// Образ CONVERT: [+|-] [###] [.] [###] [^^^^] (руководство, разд. 13.6).
// Знака нет — число пишется без знака; «+» — всегда + или −; «−» — пробел
// или минус. Младшие разряды, не влезшие в образ, отбрасываются, а не
// округляются. Слишком длинная целая часть — ошибка.
bool format_by_image(const Number & value, const std::string & image,
                     std::string & out, std::string & error)
{
    unsigned sign_mode = 0;                   // 0 нет, 1 плюс, 2 минус
    unsigned p = 0;
    if (p < image.size() && (image[p] == '+' || image[p] == '-')) {
        sign_mode = (image[p] == '+') ? 1 : 2;
        ++p;
    }

    unsigned ip = 0, fp = 0;
    while (p < image.size() && image[p] == '#') { ++ip; ++p; }
    if (p < image.size() && image[p] == '.') {
        ++p;
        while (p < image.size() && image[p] == '#') { ++fp; ++p; }
    }

    bool exponential = false;
    // В листингах показатель степени изображается как ^^^^, в книге тот же
    // знак распознан как /\/\/\/\ — принимаем оба написания.
    while (p < image.size() && (image[p] == '^' || image[p] == '\\' ||
                                image[p] == '/')) {
        exponential = true;
        ++p;
    }
    if (p != image.size()) { error = "непонятный образ CONVERT: " + image; return false; }
    if (!ip && !fp) { error = "в образе CONVERT нет ни одного знака #"; return false; }

    Number v = value;
    const bool negative = v.is_negative();
    if (negative) v = v.negated();

    std::string digits;
    int exponent = 0;

    if (exponential) {
        // Мантисса приводится к виду с ip цифрами до точки.
        const std::string d = v.to_display();
        (void)d;
        double x = v.to_double();
        if (x != 0.0) {
            while (x >= std::pow(10.0, static_cast<double>(ip))) { x /= 10.0; ++exponent; }
            while (x < std::pow(10.0, static_cast<double>(ip - 1))) { x *= 10.0; --exponent; }
        }
        char buf[64];
        std::sprintf(buf, "%.*f", static_cast<int>(fp) + 2, x);
        digits = buf;
    } else {
        char buf[64];
        std::sprintf(buf, "%.*f", static_cast<int>(fp) + 2, v.to_double());
        digits = buf;
    }

    // Разделяем на целую и дробную части и отбрасываем лишние разряды.
    std::string whole = digits, frac;
    const std::size_t dot = digits.find('.');
    if (dot != std::string::npos) {
        whole = digits.substr(0, dot);
        frac = digits.substr(dot + 1);
    }
    if (whole.size() > ip) { error = "число не помещается в образ CONVERT"; return false; }
    while (whole.size() < ip) whole = "0" + whole;
    frac.resize(fp, '0');

    out.clear();
    if (sign_mode == 1) out += negative ? '-' : '+';
    else if (sign_mode == 2) out += negative ? '-' : ' ';

    out += whole;
    if (fp) { out += '.'; out += frac; }

    if (exponential) {
        char buf[16];
        std::sprintf(buf, "E%c%02d", exponent < 0 ? '-' : '+',
                     exponent < 0 ? -exponent : exponent);
        out += buf;
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

Interp::Interp(const ProgramImage & img, Host & host)
    : img_(img), host_(host), store_(img.vars()), labels_ready_(false),
      li_(0), off_(0), next_off_(0), jumped_(false), stopped_(false),
      max_steps_(0)
{
}

bool Interp::fail(const std::string & m)
{
    if (error_.empty()) {
        error_ = m;
        if (li_ < img_.line_count())
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
        std::sprintf(buf, "%04u", li_ < img_.line_count()
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
        if (!host_.poll_key(code)) return fail("нет данных на клавиатуре");
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

bool Interp::do_deffn(Stream & st, unsigned len)
{
    // Само определение исполнения не требует: подпрограмма начинается
    // после него.
    (void)st;
    (void)len;
    return true;
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
    // REM и % операнды не разбирают вовсе.
    if (verb == 0x56 || verb == 0x3F) return true;

    Stream st(ops, len, &img_.vars(), store_);

    switch (verb) {
        case 0x36: return do_let(st);
        case 0x4C: return do_print(st);
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
        case 0x47: return do_convert(st);
        case 0x4B: return do_bin(st);
        case 0x64: return do_init(st);
        case 0x54: return do_select(st);
        case 0x27: case 0x3A: return do_deffn(st, len);
        case 0x23: return do_gosubq(st);

        case 0x75: return do_open(st, true);
        case 0x74: return do_dload(st);
        case 0x79: return do_dskip(st, true);
        case 0x7A: return do_dskip(st, false);
        case 0x7B: return do_limits(st);
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
    return fail(std::string("оператор ") + ((verb > 0xFF) ? "06 " : "") + b
                + " ещё не исполняется");
}

bool Interp::run(std::string & error)
{
    error_.clear();
    li_ = 0;
    off_ = 0;
    stopped_ = false;

    unsigned long steps = 0;

    while (!stopped_) {
        if (li_ >= img_.line_count()) break;
        const std::vector<uint8_t> & b = img_.line(li_).body;
        if (off_ >= b.size()) { ++li_; off_ = 0; continue; }

        unsigned verb = 0, ops_at = 0, len = 0;
        if (!stmt_head(b, off_, verb, ops_at, len)) {
            error = "строка " + num_str(img_.line(li_).number) + ": оператор оборван";
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

} // namespace iskra

