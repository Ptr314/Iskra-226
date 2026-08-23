// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Mikhail Revzin <p3.141592653589793238462643@gmail.com>
// Part of the Iskra-226 project: https://github.com/Ptr314/Iskra-226
// Description: исполнение промежуточного представления

#include "core/interp.h"

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

bool Program::find(unsigned number, unsigned & index) const
{
    for (unsigned i = 0; i < lines.size(); ++i)
        if (lines[i].number == number) { index = i; return true; }
    return false;
}

Interp::Interp(const Program & prog, Host & host)
    : prog_(prog), host_(host), labels_ready_(false), li_(0), si_(0),
      jumped_(false), stopped_(false), max_steps_(2000000)
{
}

bool Interp::fail(const std::string & m)
{
    if (error_.empty()) {
        error_ = m;
        if (li_ < prog_.lines.size())
            error_ = "строка " + num_str(prog_.lines[li_].number) + ": " + m;
    }
    return false;
}

bool Interp::variable(unsigned index, Number & out) const
{
    std::map<unsigned, Number>::const_iterator it = vars_.find(index);
    if (it == vars_.end()) return false;
    out = it->second;
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

// Массивы «Искры» индексируются с единицы: «А(5) означает 5-й элемент
// массива А()», а DIM A(120) отводит 120 элементов (руководство, разд. 7.1).
bool Interp::array_alloc(unsigned var, unsigned dim1, unsigned dim2)
{
    if (dim1 == 0) dim1 = 10;                  // размерность по умолчанию
    const unsigned long total = static_cast<unsigned long>(dim1) *
                                (dim2 ? dim2 : 1);
    if (total > 1000000UL) return fail("слишком большой массив");

    Array & a = arrays_[var];
    a.dim1 = dim1;
    a.dim2 = dim2;
    a.cells.assign(static_cast<std::size_t>(total), Number());
    return true;
}

bool Interp::slot(const Expr & e, Number *& out)
{
    if (e.kind == EX_VAR) {
        out = &vars_[e.var];
        return true;
    }
    if (e.kind != EX_ELEM) return fail("сюда нельзя присвоить значение");

    std::map<unsigned, Array>::iterator it = arrays_.find(e.var);
    if (it == arrays_.end()) {
        // Массив, к которому обратились без DIM: размеры берём из таблиц
        // переменных, если они есть, иначе десять элементов.
        unsigned d1 = 0, d2 = 0;
        if (e.var < prog_.vars.size()) {
            d1 = prog_.vars[e.var].dim1;
            d2 = prog_.vars[e.var].dim2;
        }
        if (!array_alloc(e.var, d1, d2)) return false;
        it = arrays_.find(e.var);
    }
    Array & a = it->second;

    // «Если арифметические выражения состоят из целой и дробной частей,
    // используется только их целая часть» (разд. 7.1).
    unsigned idx[2] = { 0, 0 };
    for (unsigned k = 0; k < e.a.size(); ++k) {
        Number n;
        if (!eval_num(e.a[k], n)) return false;
        long v = 0;
        if (!Number::from_double(std::floor(n.to_double())).to_int(v))
            return fail("индекс массива не целое число");
        if (v < 1) return fail("индекс массива меньше единицы");
        idx[k] = static_cast<unsigned>(v);
    }

    std::size_t off;
    if (a.dim2) {
        if (e.a.size() != 2) return fail("двумерному массиву нужны два индекса");
        if (idx[0] > a.dim1 || idx[1] > a.dim2) return fail("индекс за границей массива");
        off = static_cast<std::size_t>(idx[0] - 1) * a.dim2 + (idx[1] - 1);
    } else {
        if (e.a.size() != 1) return fail("одномерному массиву нужен один индекс");
        if (idx[0] > a.dim1) return fail("индекс за границей массива");
        off = idx[0] - 1;
    }

    out = &a.cells[off];
    return true;
}

// --- Символьные данные ------------------------------------------------------

// Поле переменной: длина элемента на число элементов. Заводится при первом
// обращении и заполняется пробелами — «начальное значение символьных
// переменных — пробел» (руководство, разд. 4.3).
std::string & Interp::str_field(unsigned var)
{
    std::map<unsigned, std::string>::iterator it = strs_.find(var);
    if (it != strs_.end()) return it->second;

    unsigned len = 16;                      // длина по умолчанию
    unsigned count = 1;
    if (var < prog_.vars.size()) {
        const VarInfo & v = prog_.vars[var];
        if (v.str_len) len = v.str_len;
        if (v.is_array) count = (v.dim1 ? v.dim1 : 10) * (v.dim2 ? v.dim2 : 1);
    }
    if (len < 1) len = 1;
    if (len > 253) len = 253;               // предел из разд. 4.2

    strs_[var] = std::string(static_cast<std::size_t>(len) * count, ' ');
    return strs_[var];
}

bool Interp::is_string_expr(const Expr & e) const
{
    switch (e.kind) {
        case EX_STR:
        case EX_HEX:
        case EX_SUBSTR:
        case EX_ARRAY:
            return true;
        case EX_VAR:
        case EX_ELEM:
            return e.var < prog_.vars.size() && prog_.vars[e.var].is_string;
        default:
            return false;
    }
}

// Место в поле символьной переменной: сама переменная, элемент массива,
// массив целиком или подстрока STR( от любого из них.
bool Interp::str_loc(const Expr & e, StrLoc & loc)
{
    switch (e.kind) {
        case EX_VAR:
        case EX_ARRAY: {
            loc.data = &str_field(e.var);
            loc.off = 0;
            loc.len = static_cast<unsigned>(loc.data->size());
            return true;
        }

        case EX_ELEM: {
            std::string & f = str_field(e.var);
            unsigned len = 16, d1 = 10, d2 = 0;
            if (e.var < prog_.vars.size()) {
                const VarInfo & v = prog_.vars[e.var];
                if (v.str_len) len = v.str_len;
                if (v.dim1) d1 = v.dim1;
                d2 = v.dim2;
            }

            unsigned idx[2] = { 0, 0 };
            for (unsigned k = 0; k < e.a.size() && k < 2; ++k) {
                Number n;
                if (!eval_num(e.a[k], n)) return false;
                long v = 0;
                if (!Number::from_double(std::floor(n.to_double())).to_int(v))
                    return fail("индекс массива не целое число");
                if (v < 1) return fail("индекс массива меньше единицы");
                idx[k] = static_cast<unsigned>(v);
            }

            unsigned n;
            if (d2) {
                if (e.a.size() != 2) return fail("двумерному массиву нужны два индекса");
                if (idx[0] > d1 || idx[1] > d2) return fail("индекс за границей массива");
                n = (idx[0] - 1) * d2 + (idx[1] - 1);
            } else {
                if (e.a.size() != 1) return fail("одномерному массиву нужен один индекс");
                if (idx[0] > d1) return fail("индекс за границей массива");
                n = idx[0] - 1;
            }

            // Поле при необходимости растёт. По таблицам «строка-скаляр или
            // массив строк» не различается (разд. 6), так что описание может
            // занижать число элементов; обрывать из-за этого разбор нечестно.
            const std::size_t off = static_cast<std::size_t>(n) * len;
            if (off + len > f.size()) {
                if (off + len > 64u * 1024u) return fail("слишком большой символьный массив");
                f.resize(off + len, ' ');
            }
            loc.data = &f;
            loc.off = static_cast<unsigned>(off);
            loc.len = len;
            return true;
        }

        case EX_SUBSTR: {
            StrLoc base;
            if (!str_loc(e.a[0], base)) return false;

            Number n;
            if (!eval_num(e.a[1], n)) return false;
            long start = 0;
            if (!Number::from_double(std::floor(n.to_double())).to_int(start))
                return fail("STR(: начало не целое число");
            if (start < 1) return fail("STR(: начало меньше единицы");
            if (static_cast<unsigned long>(start) > base.len)
                return fail("STR(: начало за границей строки");

            unsigned off = static_cast<unsigned>(start) - 1;
            unsigned len = base.len - off;          // по умолчанию до конца

            if (e.a.size() > 2) {
                if (!eval_num(e.a[2], n)) return false;
                long want = 0;
                if (!Number::from_double(std::floor(n.to_double())).to_int(want))
                    return fail("STR(: длина не целое число");
                if (want < 1) return fail("STR(: длина меньше единицы");
                if (static_cast<unsigned long>(want) > len)
                    return fail("STR(: длина за границей строки");
                len = static_cast<unsigned>(want);
            }

            loc.data = base.data;
            loc.off = base.off + off;
            loc.len = len;
            return true;
        }

        default:
            return fail("здесь ожидалась символьная переменная");
    }
}

// «Если длина присваиваемого значения больше размерности символьной
// переменной, то крайние справа символы игнорируются» (разд. 4.3);
// остаток поля добивается пробелами.
bool Interp::assign_string(const Expr & target, const std::string & value)
{
    StrLoc loc;
    if (!str_loc(target, loc)) return false;

    for (unsigned i = 0; i < loc.len; ++i)
        (*loc.data)[loc.off + i] = (i < value.size()) ? value[i] : ' ';
    return true;
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

bool Interp::do_convert(const Stmt & s)
{
    if (s.targets.size() != 1) return fail("CONVERT ждёт одну цель");

    if (is_string_expr(s.targets[0])) {
        // Число в символьное представление: нужен образ.
        Number v;
        if (!eval_num(s.e, v)) return false;

        std::string text;
        if (s.has_prompt) {
            std::string error;
            if (!format_by_image(v, s.prompt, text, error)) return fail(error);
        } else {
            // Образ не задан. Книга такой формы не описывает, но в корпусе
            // она встречается (BAM*: CONVERT V0E TO STR(V0D¤,12,4)).
            // Допущение: число прижимается вправо к длине приёмника.
            StrLoc loc;
            if (!str_loc(s.targets[0], loc)) return false;
            text = v.to_display();
            if (!v.is_negative() && !text.empty() && text[0] == ' ')
                text = text.substr(1);
            while (text.size() < loc.len) text = " " + text;
        }
        return assign_string(s.targets[0], text);
    }

    // Символьное представление в число: «преобразуемое значение должно
    // представлять собой правильную запись числа» (разд. 13.6).
    std::string text;
    if (!eval_str(s.e, text)) return false;

    Number n;
    if (!Number::parse(text, n)) return fail("CONVERT: «" + text + "» не число");
    Number * cell = 0;
    if (!slot(s.targets[0], cell)) return false;
    *cell = n;
    return true;
}

// MAT REDIM меняет размерности уже существующего массива; содержимое
// памяти при этом сохраняется.
bool Interp::do_redim(const Stmt & s)
{
    for (unsigned i = 0; i < s.dims.size(); ++i) {
        const DimEntry & d = s.dims[i];

        unsigned dim[2] = { 0, 0 };
        for (unsigned k = 0; k < d.sizes.size() && k < 2; ++k) {
            Number n;
            if (!eval_num(d.sizes[k], n)) return false;
            long v = 0;
            if (!Number::from_double(std::floor(n.to_double())).to_int(v) || v < 1)
                return fail("MAT REDIM: размерность не положительное целое");
            dim[k] = static_cast<unsigned>(v);
        }
        if (!dim[0]) return fail("MAT REDIM без размерности");

        const bool is_string = d.var < prog_.vars.size() && prog_.vars[d.var].is_string;
        if (is_string) {
            unsigned len = d.str_len;
            if (!len && d.var < prog_.vars.size()) len = prog_.vars[d.var].str_len;
            if (!len) len = 16;
            const std::size_t total = static_cast<std::size_t>(len) * dim[0] *
                                      (dim[1] ? dim[1] : 1);
            if (total > 64u * 1024u) return fail("MAT REDIM: слишком большой массив");
            str_field(d.var).resize(total, ' ');
            continue;
        }

        const unsigned long total = static_cast<unsigned long>(dim[0]) *
                                    (dim[1] ? dim[1] : 1);
        if (total > 1000000UL) return fail("MAT REDIM: слишком большой массив");

        Array & a = arrays_[d.var];
        a.dim1 = dim[0];
        a.dim2 = dim[1];
        a.cells.resize(static_cast<std::size_t>(total), Number());
    }
    return true;
}

bool Interp::do_dim(const Stmt & s)
{
    for (unsigned i = 0; i < s.dims.size(); ++i) {
        const DimEntry & d = s.dims[i];
        const bool is_string = d.var < prog_.vars.size() && prog_.vars[d.var].is_string;
        if (is_string) {
            // Поле заводится по описанию из таблицы переменных; повторное
            // объявление очищает его.
            strs_.erase(d.var);
            str_field(d.var);
            continue;
        }
        if (!array_alloc(d.var, d.dim1, d.dim2)) return false;
    }
    return true;
}

bool Interp::eval_num(const Expr & e, Number & n)
{
    Value v;
    if (!eval(e, v)) return false;
    if (v.is_str) return fail("здесь ожидалось число, а не строка");
    n = v.num;
    return true;
}

bool Interp::eval_str(const Expr & e, std::string & out)
{
    Value v;
    if (!eval(e, v)) return false;
    if (!v.is_str) return fail("здесь ожидалась строка, а не число");
    out = v.str;
    return true;
}

namespace {

// LEN: «количество символов от крайнего левого байта и до последнего не
// равного пробелу включительно; если строка из всех пробелов — 1».
unsigned str_len_value(const std::string & s)
{
    for (std::size_t i = s.size(); i-- > 0; )
        if (s[i] != ' ') return static_cast<unsigned>(i + 1);
    return 1;
}

// NUM: длина ведущей правильной записи числа. Пробелы до и после числа
// входят в счёт, но только если дальше ничего, кроме пробелов, нет —
// это единственное прочтение, при котором сходятся все три примера
// руководства (разд. 13.6): «+ 1.2   -14 …» → 5, «98.1E+10» → 16, «0..» → 2.
unsigned str_num_value(const std::string & s)
{
    std::size_t p = 0;
    while (p < s.size() && s[p] == ' ') ++p;

    // Между знаком и цифрами пробел допускается: в примере руководства
    // «+ 1.2   -14   +1.2587» ответом служит 5, то есть «+ 1.2» целиком.
    if (p < s.size() && (s[p] == '+' || s[p] == '-')) {
        ++p;
        while (p < s.size() && s[p] == ' ') ++p;
    }

    std::size_t digits = 0;
    while (p < s.size() && s[p] >= '0' && s[p] <= '9') { ++p; ++digits; }
    if (p < s.size() && s[p] == '.') {
        ++p;
        while (p < s.size() && s[p] >= '0' && s[p] <= '9') { ++p; ++digits; }
    }
    if (!digits) return 0;

    if (p < s.size() && (s[p] == 'E' || s[p] == 'e')) {
        const std::size_t save = p;
        ++p;
        if (p < s.size() && (s[p] == '+' || s[p] == '-')) ++p;
        std::size_t ed = 0;
        while (p < s.size() && s[p] >= '0' && s[p] <= '9') { ++p; ++ed; }
        if (!ed) p = save;
    }

    // Хвост из одних пробелов входит в длину, иначе — нет.
    std::size_t tail = p;
    while (tail < s.size() && s[tail] == ' ') ++tail;
    if (tail == s.size()) return static_cast<unsigned>(s.size());
    return static_cast<unsigned>(p);
}

} // namespace

bool Interp::eval(const Expr & e, Value & v)
{
    v = Value();

    // Символьные значения: переменная, элемент, массив целиком, подстрока.
    if (e.kind == EX_SUBSTR || e.kind == EX_ARRAY ||
        ((e.kind == EX_VAR || e.kind == EX_ELEM) && is_string_expr(e))) {
        StrLoc loc;
        if (!str_loc(e, loc)) return false;
        v.is_str = true;
        v.str = loc.data->substr(loc.off, loc.len);
        return true;
    }

    switch (e.kind) {
        case EX_LEN: {
            std::string s;
            if (!eval_str(e.a[0], s)) return false;
            v.num = Number::from_int(static_cast<long>(str_len_value(s)));
            return true;
        }

        case EX_NUMF: {
            std::string s;
            if (!eval_str(e.a[0], s)) return false;
            v.num = Number::from_int(static_cast<long>(str_num_value(s)));
            return true;
        }

        case EX_VAL: {
            // «Преобразует двоичное значение содержимого первого байта или
            // первых двух байтов» (разд. 14.2). Старший байт первый — так
            // велит тождество из книги:
            // VAL(X¤,2) = VAL(X¤)*256 + VAL(STR(X¤,2)).
            std::string s;
            if (!eval_str(e.a[0], s)) return false;
            if (s.empty()) return fail("VAL( от пустой строки");
            unsigned long r = static_cast<unsigned char>(s[0]);
            if (e.a.size() > 1) {
                if (s.size() < 2) return fail("VAL( с двумя байтами от строки короче двух");
                r = r * 256 + static_cast<unsigned char>(s[1]);
            }
            v.num = Number::from_int(static_cast<long>(r));
            return true;
        }

        case EX_POS: {
            // «Поиск проводится с начала поисковой переменной, и за одну
            // операцию находится только одна величина» (разд. 15.1).
            std::string s;
            if (!eval_str(e.a[0], s)) return false;
            if (e.a.size() < 2) return fail("POS( без искомого значения");

            int want;
            if (is_string_expr(e.a[1])) {
                std::string p;
                if (!eval_str(e.a[1], p)) return false;
                if (p.empty()) return fail("POS( с пустым образцом");
                want = static_cast<unsigned char>(p[0]);
            } else {
                Number n;
                if (!eval_num(e.a[1], n)) return false;
                long b = 0;
                n.to_int(b);
                want = static_cast<int>(b);
            }

            unsigned found = 0;
            for (std::size_t i = 0; i < s.size(); ++i) {
                const int c = static_cast<unsigned char>(s[i]);
                bool hit = false;
                switch (e.rel) {
                    case EX_EQ: hit = (c == want); break;
                    case EX_NE: hit = (c != want); break;
                    case EX_LT: hit = (c <  want); break;
                    case EX_LE: hit = (c <= want); break;
                    case EX_GT: hit = (c >  want); break;
                    default:    hit = (c >= want); break;
                }
                if (hit) { found = static_cast<unsigned>(i + 1); break; }
            }
            v.num = Number::from_int(static_cast<long>(found));
            return true;
        }

        default: break;
    }

    // Сравнение строк — побайтовое; более короткая дополняется пробелами.
    if (e.kind >= EX_EQ && e.kind <= EX_GE &&
        (is_string_expr(e.a[0]) || is_string_expr(e.a[1]))) {
        std::string x, y;
        if (!eval_str(e.a[0], x)) return false;
        if (!eval_str(e.a[1], y)) return false;
        while (x.size() < y.size()) x += ' ';
        while (y.size() < x.size()) y += ' ';

        const int c = x.compare(y);
        bool r = false;
        switch (e.kind) {
            case EX_EQ: r = (c == 0); break;
            case EX_NE: r = (c != 0); break;
            case EX_LT: r = (c <  0); break;
            case EX_LE: r = (c <= 0); break;
            case EX_GT: r = (c >  0); break;
            default:    r = (c >= 0); break;
        }
        v.num = Number::from_int(r ? 1 : 0);
        return true;
    }

    switch (e.kind) {
        case EX_NUM: v.num = e.num; return true;
        case EX_PI:  v.num = Number::pi(); return true;

        case EX_STR:
        case EX_HEX:
            v.is_str = true;
            v.str = e.str;
            return true;

        case EX_VAR: {
            std::map<unsigned, Number>::const_iterator it = vars_.find(e.var);
            // Неинициализированная переменная равна нулю — как на машине.
            if (it != vars_.end()) v.num = it->second;
            return true;
        }

        case EX_ELEM: {
            Number * cell = 0;
            if (!slot(e, cell)) return false;
            v.num = *cell;
            return true;
        }

        case EX_AT:
        case EX_TAB:
            return fail("AT( и TAB( допустимы только в PRINT");

        default: break;
    }

    Number a, b;
    if (!eval_num(e.a[0], a)) return false;

    if (e.kind == EX_NEG)  { v.num = a.negated(); return true; }
    if (e.kind == EX_ABS)  { v.num = a; if (v.num.is_negative()) v.num = v.num.negated(); return true; }
    if (e.kind == EX_SGN)  {
        const int c = a.compare(Number());
        v.num = Number::from_int(c > 0 ? 1 : (c < 0 ? -1 : 0));
        return true;
    }
    if (e.kind == EX_INT) {
        // Отбрасывание дробной части вниз, как INT в Бейсике.
        v.num = Number::from_double(std::floor(a.to_double()));
        return true;
    }
    if (e.kind == EX_SQR) {
        if (!Number::sqrt(a, v.num)) return fail("корень из отрицательного числа");
        return true;
    }
    if (e.kind == EX_LOG) {
        if (a.compare(Number()) <= 0) return fail("логарифм неположительного числа");
        v.num = Number::from_double(std::log(a.to_double()));
        return true;
    }
    if (e.kind == EX_EXP) {
        v.num = Number::from_double(std::exp(a.to_double()));
        return true;
    }

    if (!eval_num(e.a[1], b)) return false;

    bool ok = true;
    switch (e.kind) {
        case EX_ADD: ok = Number::add(a, b, v.num); break;
        case EX_SUB: ok = Number::sub(a, b, v.num); break;
        case EX_MUL: ok = Number::mul(a, b, v.num); break;
        case EX_DIV:
            if (b.is_zero()) return fail("деление на нуль");
            ok = Number::div(a, b, v.num);
            break;
        case EX_POW: ok = Number::pow(a, b, v.num); break;

        case EX_EQ: case EX_NE: case EX_LT:
        case EX_LE: case EX_GT: case EX_GE: {
            const int c = a.compare(b);
            bool r = false;
            switch (e.kind) {
                case EX_EQ: r = (c == 0); break;
                case EX_NE: r = (c != 0); break;
                case EX_LT: r = (c < 0);  break;
                case EX_LE: r = (c <= 0); break;
                case EX_GT: r = (c > 0);  break;
                default:    r = (c >= 0); break;
            }
            v.num = Number::from_int(r ? 1 : 0);
            return true;
        }

        default:
            return fail("не реализованная операция");
    }

    if (!ok) return fail("переполнение порядка");
    return true;
}

bool Interp::do_print(const Stmt & s)
{
    bool last_was_at = false;

    for (unsigned i = 0; i < s.items.size(); ++i) {
        const PrintItem & item = s.items[i];

        if (item.e.kind == EX_TAB) {
            // «позиции строки нумеруются с нуля» (разд. 4.4), и курсор
            // двигается только вправо.
            Number n;
            if (!eval_num(item.e.a[0], n)) return false;
            long v = 0;
            n.to_int(v);
            const unsigned col = static_cast<unsigned>(v < 0 ? 0 : v) + 1;
            if (col > host_.screen().col() && col <= SCREEN_COLS)
                host_.screen().at(host_.screen().row(), col);
            last_was_at = false;
        } else if (item.e.kind == EX_AT) {
            Number r, c;
            if (!eval_num(item.e.a[0], r)) return false;
            if (!eval_num(item.e.a[1], c)) return false;
            long rv = 0, cv = 0;
            r.to_int(rv);
            c.to_int(cv);
            host_.screen().at(static_cast<unsigned>(rv < 1 ? 1 : rv),
                              static_cast<unsigned>(cv < 1 ? 1 : cv));
            if (item.e.a.size() > 2) {
                Number n;
                if (!eval_num(item.e.a[2], n)) return false;
                long nv = 0;
                n.to_int(nv);
                if (nv > 0) host_.screen().erase(static_cast<unsigned>(nv));
            }
            last_was_at = true;
        } else {
            Value v;
            if (!eval(item.e, v)) return false;
            if (v.is_str) {
                emit(v.str);
            } else {
                // «с учетом знака перед числом и пробела после числа»
                emit(v.num.to_display());
                emit(" ");
            }
            last_was_at = false;
        }

        if (item.sep == SEP_ZONE) emit_zone();
    }

    // PRINT AT(...) курсор только ставит и перевода строки не делает:
    // «курсор устанавливается в тридцатую позицию восьмой строки экрана»,
    // а печать следующего оператора идёт с этой позиции (пример 17.5).
    if (s.newline && !last_was_at) emit_newline();
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
bool Interp::do_linput(const Stmt & s)
{
    std::string line;
    if (!read_line(s.prompt, s.has_prompt, line)) return false;
    if (s.targets.size() != 1) return fail("LINPUT ждёт один приёмник");
    if (!is_string_expr(s.targets[0])) return fail("LINPUT ждёт символьный приёмник");
    return assign_string(s.targets[0], line);
}

bool Interp::do_input(const Stmt & s)
{
    std::string line;
    if (!read_line(s.prompt, s.has_prompt, line)) return false;

    unsigned p = 0;
    for (unsigned i = 0; i < s.targets.size(); ++i) {
        std::string field;
        if (s.targets.size() == 1 && is_string_expr(s.targets[i])) {
            // Единственный символьный приёмник получает строку целиком:
            // запятая в ней — обычный символ.
            field = line;
            p = static_cast<unsigned>(line.size());
        } else {
            while (p < line.size() && line[p] != ',') field += line[p++];
            if (p < line.size()) ++p;
        }

        if (is_string_expr(s.targets[i])) {
            if (!assign_string(s.targets[i], field)) return false;
            continue;
        }

        Number n;
        if (!Number::parse(field, n)) return fail("INPUT: не число «" + field + "»");
        Number * cell = 0;
        if (!slot(s.targets[i], cell)) return false;
        *cell = n;
    }
    return true;
}

bool Interp::do_for(const Stmt & s)
{
    Number start, limit, step;
    if (!eval_num(s.e, start)) return false;
    if (!eval_num(s.limit, limit)) return false;
    if (s.has_step) {
        if (!eval_num(s.step, step)) return false;
    } else {
        step = Number::from_int(1);
    }
    if (step.is_zero()) return fail("FOR с нулевым шагом");

    vars_[s.var] = start;

    // Уже открытый цикл по той же переменной перезапускается.
    for (unsigned i = 0; i < loops_.size(); ++i) {
        if (loops_[i].var == s.var) { loops_.resize(i); break; }
    }

    Frame f;
    f.var = s.var;
    f.limit = limit;
    f.step = step;
    f.line = li_;
    f.stmt = si_ + 1;
    loops_.push_back(f);
    return true;
}

bool Interp::do_next(const Stmt & s)
{
    while (!loops_.empty() && loops_.back().var != s.var) loops_.pop_back();
    if (loops_.empty()) return fail("NEXT без FOR");

    Frame & f = loops_.back();
    Number v = vars_[f.var];
    if (!Number::add(v, f.step, v)) return fail("переполнение счётчика цикла");
    vars_[f.var] = v;

    const bool up = !f.step.is_negative();
    const bool go_on = up ? (v.compare(f.limit) <= 0) : (v.compare(f.limit) >= 0);

    if (go_on) {
        li_ = f.line;
        si_ = f.stmt;
        jumped_ = true;
    } else {
        loops_.pop_back();
    }
    return true;
}

void Interp::build_labels()
{
    labels_ready_ = true;
    for (unsigned l = 0; l < prog_.lines.size(); ++l) {
        const std::vector<Stmt> & st = prog_.lines[l].stmts;
        for (unsigned i = 0; i < st.size(); ++i) {
            if (st[i].kind != ST_DEFFN) continue;
            // Определение клавиши специальных функций подпрограммой не
            // является — на его метку GOSUB' не переходит.
            if (st[i].has_prompt) continue;
            // Машина просматривает текст сверху вниз, поэтому при повторе
            // имени побеждает первое определение.
            if (labels_.find(st[i].label) != labels_.end()) continue;
            labels_[st[i].label] = std::make_pair(l, i);
        }
    }
}

bool Interp::do_gosubq(const Stmt & s)
{
    if (!labels_ready_) build_labels();

    std::map<unsigned, std::pair<unsigned, unsigned> >::const_iterator it =
        labels_.find(s.label);
    if (it == labels_.end())
        return fail("нет подпрограммы с именем " + num_str(s.label));

    const Stmt & def = prog_.lines[it->second.first].stmts[it->second.second];
    if (def.params.size() != s.args.size())
        return fail("подпрограмме " + num_str(s.label) + " передано " +
                    num_str(static_cast<unsigned>(s.args.size())) +
                    " параметров, а описано " +
                    num_str(static_cast<unsigned>(def.params.size())));

    // Все фактические параметры вычисляются до первого присваивания:
    // подпрограмму зовут и через её же формальные переменные, например
    // GOSUB '100(L3,A%,1) при DEFFN '100(L1,L4,L3).
    std::vector<Value> vals(s.args.size());
    for (unsigned i = 0; i < s.args.size(); ++i)
        if (!eval(s.args[i], vals[i])) return false;

    for (unsigned i = 0; i < def.params.size(); ++i) {
        const unsigned v = def.params[i];
        const bool want_str = v < prog_.vars.size() && prog_.vars[v].is_string;
        if (want_str != vals[i].is_str)
            return fail("параметр " + num_str(i + 1) + " подпрограммы " +
                        num_str(s.label) + ": не совпадают типы");

        Expr target;
        target.kind = EX_VAR;
        target.var = v;
        if (want_str) {
            if (!assign_string(target, vals[i].str)) return false;
        } else {
            Number * cell = 0;
            if (!slot(target, cell)) return false;
            *cell = vals[i].num;
        }
    }

    if (calls_.size() > 1000) return fail("слишком глубокая вложенность GOSUB");
    calls_.push_back(std::make_pair(li_, si_ + 1));

    li_ = it->second.first;
    si_ = it->second.second + 1;      // первый оператор после DEFFN'
    jumped_ = true;
    return true;
}

bool Interp::jump(unsigned line_number)
{
    unsigned idx = 0;
    if (!prog_.find(line_number, idx))
        return fail("нет строки " + num_str(line_number));
    li_ = idx;
    si_ = 0;
    jumped_ = true;
    return true;
}

bool Interp::exec(const Stmt & s)
{
    switch (s.kind) {
        case ST_REM:
            return true;

        case ST_PRINT:
            return do_print(s);

        case ST_INPUT:
            return do_input(s);

        case ST_LET: {
            // Символьное присваивание: и цель, и значение — строки.
            bool to_string = false;
            for (unsigned i = 0; i < s.targets.size(); ++i)
                if (is_string_expr(s.targets[i])) to_string = true;

            if (to_string) {
                std::string value;
                if (!eval_str(s.e, value)) return false;
                for (unsigned i = 0; i < s.targets.size(); ++i) {
                    if (!is_string_expr(s.targets[i]))
                        return fail("в одном присваивании смешаны символьная и числовая цели");
                    if (!assign_string(s.targets[i], value)) return false;
                }
                return true;
            }

            Number n;
            if (!eval_num(s.e, n)) return false;
            for (unsigned i = 0; i < s.targets.size(); ++i) {
                Number * cell = 0;
                if (!slot(s.targets[i], cell)) return false;
                *cell = n;
            }
            return true;
        }

        case ST_DIM:
            return do_dim(s);

        case ST_REDIM:
            return do_redim(s);

        case ST_LINPUT:
            return do_linput(s);

        case ST_CONVERT:
            return do_convert(s);

        case ST_GOTO:
            return jump(s.line);

        case ST_GOSUB:
            if (calls_.size() > 1000) return fail("слишком глубокая вложенность GOSUB");
            calls_.push_back(std::make_pair(li_, si_ + 1));
            return jump(s.line);

        case ST_ON: {
            // Значение 1 ведёт на первый номер, 2 — на второй и т. д.
            // Ноль и всё, что за списком, просто передаёт управление дальше.
            Number n;
            if (!eval_num(s.e, n)) return false;
            long v = 0;
            if (!Number::from_double(std::floor(n.to_double())).to_int(v)) return true;
            if (v < 1 || static_cast<unsigned long>(v) > s.lines.size()) return true;

            const unsigned target = s.lines[static_cast<unsigned>(v) - 1];
            if (s.is_gosub) {
                if (calls_.size() > 1000) return fail("слишком глубокая вложенность GOSUB");
                calls_.push_back(std::make_pair(li_, si_ + 1));
            }
            return jump(target);
        }

        case ST_DEFFN:
            // Помеченный вход сам по себе ничего не делает: встреченный по
            // ходу исполнения, он «не влияет на ход выполнения программы»
            // (руководство, разд. 10.4).
            return true;

        case ST_GOSUBQ:
            return do_gosubq(s);

        case ST_RETURN: {
            if (calls_.empty()) return fail("RETURN без GOSUB");
            li_ = calls_.back().first;
            si_ = calls_.back().second;
            calls_.pop_back();
            jumped_ = true;
            return true;
        }

        case ST_IF: {
            Number c;
            if (!eval_num(s.e, c)) return false;
            if (!c.is_zero()) return jump(s.line);
            return true;                       // иначе — следующий оператор
        }

        case ST_FOR:
            return do_for(s);

        case ST_NEXT:
            return do_next(s);

        case ST_STOP:
        case ST_END:
            stopped_ = true;
            return true;
    }
    return fail("неизвестный оператор");
}

bool Interp::run(std::string & error)
{
    error_.clear();
    li_ = 0;
    si_ = 0;
    stopped_ = false;

    unsigned long steps = 0;

    while (!stopped_) {
        if (li_ >= prog_.lines.size()) break;
        if (si_ >= prog_.lines[li_].stmts.size()) { ++li_; si_ = 0; continue; }

        if (max_steps_ && ++steps > max_steps_) {
            error = "превышено число шагов: похоже на зацикливание";
            return false;
        }

        jumped_ = false;
        if (!exec(prog_.lines[li_].stmts[si_])) { error = error_; return false; }
        if (!jumped_) ++si_;
    }

    host_.present();
    return true;
}

} // namespace iskra