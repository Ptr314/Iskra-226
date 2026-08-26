// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Mikhail Revzin <p3.141592653589793238462643@gmail.com>
// Part of the Iskra-226 project: https://github.com/Ptr314/Iskra-226
// Description: хранилище переменных: числа, числовые массивы, символьные поля

#include "core/vars.h"

namespace iskra {

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

namespace {

bool err(std::string & error, const char * m)
{
    if (error.empty()) error = m;
    return false;
}

// Смещение элемента: индексы с единицы, порядок по строкам.
bool offset(unsigned d1, unsigned d2, const long * idx, unsigned n,
            std::size_t & off, std::string & error)
{
    for (unsigned k = 0; k < n; ++k)
        if (idx[k] < 1) return err(error, "индекс массива меньше единицы");

    if (d2) {
        if (n != 2) return err(error, "двумерному массиву нужны два индекса");
        if (static_cast<unsigned long>(idx[0]) > d1
            || static_cast<unsigned long>(idx[1]) > d2)
            return err(error, "индекс за границей массива");
        off = static_cast<std::size_t>(idx[0] - 1) * d2 + (idx[1] - 1);
        return true;
    }
    if (n != 1) return err(error, "одномерному массиву нужен один индекс");
    if (static_cast<unsigned long>(idx[0]) > d1)
        return err(error, "индекс за границей массива");
    off = static_cast<std::size_t>(idx[0] - 1);
    return true;
}

} // namespace

bool VarStore::array_alloc(unsigned var, unsigned dim1, unsigned dim2,
                           std::string & error)
{
    if (dim1 == 0) dim1 = 10;                  // размерность по умолчанию
    const unsigned long total = static_cast<unsigned long>(dim1) *
                                (dim2 ? dim2 : 1);
    if (total > 1000000UL) return err(error, "слишком большой массив");

    Array & a = arrays_[var];
    a.dim1 = dim1;
    a.dim2 = dim2;
    a.cells.assign(static_cast<std::size_t>(total), Number());
    return true;
}

void VarStore::clear_non_common()
{
    for (std::map<unsigned, Number>::iterator it = nums_.begin();
         it != nums_.end(); ) {
        if (is_common(it->first)) ++it;
        else nums_.erase(it++);
    }
    for (std::map<unsigned, Array>::iterator it = arrays_.begin();
         it != arrays_.end(); ) {
        if (is_common(it->first)) ++it;
        else arrays_.erase(it++);
    }
    for (std::map<unsigned, std::string>::iterator it = strs_.begin();
         it != strs_.end(); ) {
        if (is_common(it->first)) ++it;
        else strs_.erase(it++);
    }
    for (std::map<unsigned, StrDim>::iterator it = str_dims_.begin();
         it != str_dims_.end(); ) {
        if (is_common(it->first)) ++it;
        else str_dims_.erase(it++);
    }
}

bool VarStore::array_grow(unsigned var, unsigned dim1, unsigned dim2,
                          std::string & error)
{
    if (dim1 == 0) dim1 = 10;
    const unsigned long total = static_cast<unsigned long>(dim1) *
                                (dim2 ? dim2 : 1);
    if (total > 1000000UL) return err(error, "слишком большой массив");

    Array & a = arrays_[var];
    a.dim1 = dim1;
    a.dim2 = dim2;
    a.cells.resize(static_cast<std::size_t>(total), Number());
    return true;
}

bool VarStore::array_dims(unsigned var, unsigned & dim1, unsigned & dim2,
                          std::string & error)
{
    std::map<unsigned, Array>::iterator it = arrays_.find(var);
    if (it == arrays_.end()) {
        unsigned d1 = 0, d2 = 0;
        if (var < vars_.size()) { d1 = vars_[var].dim1; d2 = vars_[var].dim2; }
        if (!array_alloc(var, d1, d2, error)) return false;
        it = arrays_.find(var);
    }
    dim1 = it->second.dim1;
    dim2 = it->second.dim2;
    return true;
}

bool VarStore::array_count(unsigned var, unsigned & n, std::string & error)
{
    unsigned d1 = 0, d2 = 0;
    if (!array_dims(var, d1, d2, error)) return false;
    n = d1 * (d2 ? d2 : 1);
    return true;
}

bool VarStore::slot(unsigned var, const long * idx, unsigned n, Number *& out,
                    std::string & error)
{
    if (n == 0) {
        out = &nums_[var];
        return true;
    }

    std::map<unsigned, Array>::iterator it = arrays_.find(var);
    if (it == arrays_.end()) {
        // Массив, к которому обратились без DIM: размеры из таблиц, если они
        // есть, иначе десять элементов.
        unsigned d1 = 0, d2 = 0;
        if (var < vars_.size()) { d1 = vars_[var].dim1; d2 = vars_[var].dim2; }
        if (!array_alloc(var, d1, d2, error)) return false;
        it = arrays_.find(var);
    }
    Array & a = it->second;

    std::size_t off = 0;
    if (!offset(a.dim1, a.dim2, idx, n, off, error)) return false;
    out = &a.cells[off];
    return true;
}

unsigned VarStore::str_len(unsigned var) const
{
    unsigned len = 16;                          // длина по умолчанию
    if (var < vars_.size() && vars_[var].str_len) len = vars_[var].str_len;
    // `MAT REDIM` перекроил — его слово главнее описания из таблиц.
    std::map<unsigned, StrDim>::const_iterator it = str_dims_.find(var);
    if (it != str_dims_.end() && it->second.len) len = it->second.len;
    if (len < 1) len = 1;
    if (len > 253) len = 253;                   // предел из разд. 4.2
    return len;
}

// Действующие размерности символьного массива: сперва то, что перекроил
// `MAT REDIM`, потом описание из таблиц образа.
void VarStore::str_dims(unsigned var, unsigned & dim1, unsigned & dim2) const
{
    dim1 = 10;
    dim2 = 0;
    if (var < vars_.size()) {
        if (vars_[var].dim1) dim1 = vars_[var].dim1;
        dim2 = vars_[var].dim2;
    }
    std::map<unsigned, StrDim>::const_iterator it = str_dims_.find(var);
    if (it != str_dims_.end()) {
        if (it->second.dim1) dim1 = it->second.dim1;
        dim2 = it->second.dim2;
    }
}

void VarStore::live_dims(unsigned var, unsigned & d1, unsigned & d2) const
{
    std::map<unsigned, StrDim>::const_iterator s = str_dims_.find(var);
    if (s != str_dims_.end()) {
        if (s->second.dim1) d1 = s->second.dim1;
        d2 = s->second.dim2 ? s->second.dim2 : 1;
        return;
    }
    std::map<unsigned, Array>::const_iterator a = arrays_.find(var);
    if (a != arrays_.end()) {
        if (a->second.dim1) d1 = a->second.dim1;
        d2 = a->second.dim2 ? a->second.dim2 : 1;
    }
}

bool VarStore::str_redim(unsigned var, unsigned dim1, unsigned dim2,
                         unsigned len, std::string & error)
{
    if (!dim1) dim1 = 10;
    StrDim & d = str_dims_[var];
    d.dim1 = dim1;
    d.dim2 = dim2;
    if (len) d.len = len;

    const std::size_t total = static_cast<std::size_t>(str_len(var)) * dim1 *
                              (dim2 ? dim2 : 1);
    if (total > 64u * 1024u) return err(error, "слишком большой массив");
    // Содержимое сохраняется: «MAT REDIM меняет размерности уже
    // существующего массива» (руководство, разд. 12.2.4).
    str_field(var).resize(total, ' ');
    return true;
}

std::string & VarStore::str_field(unsigned var)
{
    std::map<unsigned, std::string>::iterator it = strs_.find(var);
    if (it != strs_.end()) return it->second;

    unsigned count = 1;
    if ((var < vars_.size() && vars_[var].is_array) ||
        str_dims_.find(var) != str_dims_.end()) {
        unsigned d1 = 10, d2 = 0;
        str_dims(var, d1, d2);
        count = d1 * (d2 ? d2 : 1);
    }
    // «Начальное значение символьных переменных — пробел» (разд. 4.3).
    strs_[var] = std::string(static_cast<std::size_t>(str_len(var)) * count, ' ');
    return strs_[var];
}

bool VarStore::str_element(unsigned var, const long * idx, unsigned n,
                           StrLoc & loc, std::string & error)
{
    std::string & f = str_field(var);
    if (n == 0) {
        loc.data = &f;
        loc.off = 0;
        loc.len = static_cast<unsigned>(f.size());
        return true;
    }

    unsigned d1 = 10, d2 = 0;
    str_dims(var, d1, d2);
    std::size_t at = 0;
    if (!offset(d1, d2, idx, n, at, error)) return false;

    // Поле при необходимости растёт. По таблицам «строка-скаляр или массив
    // строк» не различается (docs/format.md, разд. 6), так что описание
    // может занижать число элементов; обрывать из-за этого исполнение
    // нечестно — это наша неуверенность, а не ошибка программы.
    const unsigned len = str_len(var);
    const std::size_t off = at * len;
    if (off + len > f.size()) {
        if (off + len > 64u * 1024u) return err(error, "слишком большой символьный массив");
        f.resize(off + len, ' ');
    }
    loc.data = &f;
    loc.off = static_cast<unsigned>(off);
    loc.len = len;
    return true;
}

} // namespace iskra
