// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Mikhail Revzin <p3.141592653589793238462643@gmail.com>
// Part of the Iskra-226 project: https://github.com/Ptr314/Iskra-226
// Description: графический буфер: заголовок, поток записей, вывод в растр

#include "core/gbuffer.h"

#include "core/koi8.h"
#include "font/font.h"

namespace iskra {

unsigned GBuffer::get16(unsigned pos) const
{
    if (off_ + pos + 1 >= d_.size()) return 0;
    const unsigned lo = static_cast<uint8_t>(d_[off_ + pos]);
    const unsigned hi = static_cast<uint8_t>(d_[off_ + pos + 1]);
    return lo | (hi << 8);
}

void GBuffer::put16(unsigned pos, unsigned v)
{
    if (off_ + pos + 1 >= d_.size()) return;
    d_[off_ + pos]     = static_cast<char>(v & 0xFF);
    d_[off_ + pos + 1] = static_cast<char>((v >> 8) & 0xFF);
}

void GBuffer::open()
{
    for (unsigned i = 0; i < GBUF_HEADER && off_ + i < d_.size(); ++i)
        d_[off_ + i] = '\0';
    put16(0, GBUF_HEADER);
}

void GBuffer::set_point(long x, long y)
{
    put16(2, static_cast<unsigned>(x) & 0xFFFF);
    put16(4, static_cast<unsigned>(y) & 0xFFFF);
}

bool GBuffer::looks_open() const
{
    const unsigned n = used();
    return n >= GBUF_HEADER && n <= len_;
}

bool GBuffer::append(const uint8_t * rec, unsigned n, std::string & error)
{
    if (!looks_open()) { error = "буфер не открыт оператором ¤OPEN"; return false; }
    const unsigned end = used();
    if (end + n > len_) { error = "в графическом буфере нет места"; return false; }
    for (unsigned i = 0; i < n; ++i)
        d_[off_ + end + i] = static_cast<char>(rec[i]);
    put16(0, end + n);
    return true;
}

const char * GBuffer::stream() const
{
    return d_.data() + off_ + GBUF_HEADER;
}

char * GBuffer::stream_mut()
{
    return &d_[0] + off_ + GBUF_HEADER;
}

bool GBuffer::set_stream(const char * s, unsigned n, std::string & error)
{
    if (!fits()) { error = "поле короче заголовка буфера в 43 байта"; return false; }
    if (GBUF_HEADER + n > len_) { error = "в графическом буфере нет места"; return false; }
    for (unsigned i = 0; i < n; ++i)
        d_[off_ + GBUF_HEADER + i] = s[i];
    put16(0, GBUF_HEADER + n);
    return true;
}

unsigned GBuffer::stream_len() const
{
    const unsigned n = used();
    return (n > GBUF_HEADER) ? n - GBUF_HEADER : 0;
}

bool gbuf_point_record(uint8_t op, long x, long y, uint8_t * out)
{
    if (x < 0 || y < 0 || x > 0xFFFF || y > 0xFFFF) return false;
    out[0] = op;
    out[1] = static_cast<uint8_t>((x >> 8) & 0xFF);
    out[2] = static_cast<uint8_t>(x & 0xFF);
    out[3] = static_cast<uint8_t>((y >> 8) & 0xFF);
    out[4] = static_cast<uint8_t>(y & 0xFF);
    return true;
}

bool gbuf_label_record(const std::string & text, unsigned size,
                       uint8_t a2, uint8_t a3, std::string & out)
{
    // Длина считает сама себя: у надписи 'Y' она 9, у '-1' — 10, то есть
    // 1 + 4 приращения + 3 признака + сам текст.
    const unsigned len = 1 + 4 + 3 + static_cast<unsigned>(text.size());
    if (len > 0xFF) return false;

    const unsigned dx = GBUF_LABEL_CELL * size * static_cast<unsigned>(text.size());
    if (dx > 0xFFFF) return false;

    out.clear();
    out.push_back(static_cast<char>(GOP_LABEL));
    out.push_back(static_cast<char>(len));
    out.push_back(static_cast<char>((dx >> 8) & 0xFF));
    out.push_back(static_cast<char>(dx & 0xFF));
    out.push_back('\0');                 // приращение по Y: надпись идёт вбок
    out.push_back('\0');
    out.push_back(static_cast<char>(size & 0xFF));
    out.push_back(static_cast<char>(a2));
    out.push_back(static_cast<char>(a3));
    out += text;
    return true;
}

namespace {

// Знак надписи рисуется тем же знакогенератором, что и текст: приращение в
// семь дискрет сходится с шириной знакоместа, а своего набора у графической
// половины БОСГИ нам взять неоткуда.
void draw_char(Raster & r, long x, long y, uint8_t code, unsigned size)
{
    const Font & f = Font::standard();
    for (unsigned gy = 0; gy < f.height(); ++gy) {
        for (unsigned gx = 0; gx < f.width(); ++gx) {
            if (!f.dot(code, gx, gy)) continue;
            // Глиф отсчитывается сверху вниз, растр — снизу вверх, и точка
            // надписи стоит у её основания.
            const long px = x + static_cast<long>(gx * size);
            const long py = y + static_cast<long>((f.height() - 1 - gy) * size);
            for (unsigned sy = 0; sy < size; ++sy)
                for (unsigned sx = 0; sx < size; ++sx)
                    r.plot(px + sx, py + sy);
        }
    }
}

unsigned be16(const unsigned char * p) { return (p[0] << 8) | p[1]; }

// Приращение надписи — со знаком, в отличие от координаты точки. Это
// следует из того, как его читает сам `SLIDE`: `ADD C (N¤,STR(A¤(),J+2,4))`
// (1430) — двоичное сложение с переносом, а оно переносит и заём. Иначе
// повёрнутая надпись была бы непредставима, а `TURN` крутит картинку
// целиком, вместе с надписями.
long be16s(const unsigned char * p)
{
    const long v = static_cast<long>(be16(p));
    return (v >= 0x8000) ? v - 0x10000 : v;
}

} // namespace

void gbuf_text(Raster & r, long x, long y, const char * text, unsigned n,
               unsigned size, long dx, long dy)
{
    if (!size) size = 1;
    for (unsigned i = 0; i < n; ++i) {
        draw_char(r, x, y, koi8_to_koi7(static_cast<unsigned char>(text[i])), size);
        x += dx;
        y += dy;
    }
}

namespace {

// Округление к ближайшей дискрете. `std::lround` в C++11 есть, но GCC 4.9.2
// под MinGW отдаёт его не всегда; своя строчка надёжнее.
long round_dot(double v)
{
    return static_cast<long>(v < 0 ? v - 0.5 : v + 0.5);
}

std::string hex_byte(unsigned char v)
{
    char b[3];
    b[0] = "0123456789ABCDEF"[(v >> 4) & 0xF];
    b[1] = "0123456789ABCDEF"[v & 0xF];
    b[2] = 0;
    return std::string(b);
}

// Записать точку старшим байтом вперёд, отказавшись от невместимой.
bool put_be16(unsigned char * p, long v)
{
    if (v < 0 || v > 0xFFFF) return false;
    p[0] = static_cast<unsigned char>((v >> 8) & 0xFF);
    p[1] = static_cast<unsigned char>(v & 0xFF);
    return true;
}

// То же для приращения, у которого есть знак.
bool put_be16s(unsigned char * p, long v)
{
    if (v < -0x8000 || v > 0x7FFF) return false;
    return put_be16(p, v & 0xFFFF);
}

} // namespace

bool gbuf_transform(GBuffer & g, const GAffine & m, std::string & error,
                    bool * unsupported)
{
    if (unsupported) *unsupported = false;
    if (!g.looks_open()) { error = "буфер не открыт оператором ¤OPEN"; return false; }

    unsigned char * s = reinterpret_cast<unsigned char *>(g.stream_mut());
    const unsigned n = g.stream_len();
    unsigned p = 0;

    while (p < n) {
        const unsigned char op = s[p];
        if (op == GOP_NPLOT || op == GOP_NDRAW ||
            op == GOP_DOT   || op == GOP_DRAW) {
            if (p + 5 > n) { error = "запись графики обрывается"; return false; }
            const double x = static_cast<double>(be16(s + p + 1));
            const double y = static_cast<double>(be16(s + p + 3));
            if (!put_be16(s + p + 1, round_dot(m.ax * x + m.bx * y + m.cx)) ||
                !put_be16(s + p + 3, round_dot(m.ay * x + m.by * y + m.cy))) {
                error = "точка выходит за растр";
                return false;
            }
            p += 5;
            continue;
        }
        if (op == GOP_LABEL || op == GOP_NLAB) {
            if (p + 2 > n) { error = "запись графики обрывается"; return false; }
            const unsigned k = 1 + s[p + 1];
            if (k < 9 || p + k > n) { error = "запись графики обрывается"; return false; }
            // В записи надписи лежит приращение точки, а не координата,
            // поэтому переносу оно не подлежит — только линейной части.
            const double dx = static_cast<double>(be16s(s + p + 2));
            const double dy = static_cast<double>(be16s(s + p + 4));
            if (!put_be16s(s + p + 2, round_dot(m.ax * dx + m.bx * dy)) ||
                !put_be16s(s + p + 4, round_dot(m.ay * dx + m.by * dy))) {
                error = "приращение надписи выходит за растр";
                return false;
            }
            p += k;
            continue;
        }
        if (unsupported) *unsupported = true;
        error = "запись графики " + hex_byte(op) + " ещё не разобрана";
        return false;
    }

    // Текущая точка заголовка едет вместе с картинкой.
    const double x = static_cast<double>(g.x());
    const double y = static_cast<double>(g.y());
    const long nx = round_dot(m.ax * x + m.bx * y + m.cx);
    const long ny = round_dot(m.ay * x + m.by * y + m.cy);
    if (nx < 0 || nx > 0xFFFF || ny < 0 || ny > 0xFFFF) {
        error = "текущая точка выходит за растр";
        return false;
    }
    g.set_point(nx, ny);
    return true;
}

bool gbuf_draw(const char * stream, unsigned n, Raster & r, std::string & error)
{
    const unsigned char * s = reinterpret_cast<const unsigned char *>(stream);
    long x = 0, y = 0;
    unsigned p = 0;

    while (p < n) {
        const unsigned char op = s[p];
        if (op == GOP_NPLOT || op == GOP_NDRAW ||
            op == GOP_DOT   || op == GOP_DRAW) {
            if (p + 5 > n) { error = "запись графики обрывается"; return false; }
            const long nx = static_cast<long>(be16(s + p + 1));
            const long ny = static_cast<long>(be16(s + p + 3));
            // `80` и `81` — те же `84` и `85`, только стёртые: рисовать они
            // не должны ничего, лишь перевести точку.
            if (op == GOP_DRAW) r.line(x, y, nx, ny);
            else if (op == GOP_DOT) r.plot(nx, ny);
            x = nx; y = ny;
            p += 5;
            continue;
        }
        if (op == GOP_LABEL || op == GOP_NLAB) {
            if (p + 2 > n) { error = "запись графики обрывается"; return false; }
            const unsigned k = 1 + s[p + 1];
            if (k < 9 || p + k > n) { error = "запись графики обрывается"; return false; }
            const long dx = be16s(s + p + 2);
            const long dy = be16s(s + p + 4);
            unsigned size = s[p + 6];
            if (!size) size = 1;
            // `82` — стёртая надпись: точку двигает, а знаков не рисует.
            if (op == GOP_LABEL)
                gbuf_text(r, x, y, stream + p + 9, k - 9, size,
                          static_cast<long>(GBUF_LABEL_CELL * size), 0);
            // Приращение берётся из самой записи, а не из числа знаков: так
            // его читает и `SLIDE` (`ADD C (N¤,STR(A¤(),J+2,4))`).
            x += dx;
            y += dy;
            p += k;
            continue;
        }
        // Остался `83`: имени у него нет и в самом `SLIDE` — ветка 1410
        // печатает «НЕИЗВ. ГЭ БАЙТ». Пропуск выдал бы неполную картинку за
        // целую, поэтому — отказ.
        char b[8];
        b[0] = "0123456789ABCDEF"[(op >> 4) & 0xF];
        b[1] = "0123456789ABCDEF"[op & 0xF];
        b[2] = 0;
        error = std::string("запись графики ") + b + " ещё не разобрана";
        return false;
    }
    return true;
}

} // namespace iskra
