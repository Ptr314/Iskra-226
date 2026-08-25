// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Mikhail Revzin <p3.141592653589793238462643@gmail.com>
// Part of the Iskra-226 project: https://github.com/Ptr314/Iskra-226
// Description: исполнение графических операторов: буфер, координаты, вывод

#include "core/interp.h"

namespace iskra {

namespace {

// Ось отображается по двум отрезкам: пользовательскому и устройства.
bool map_axis(const Number & u, const Number & f0, const Number & f1,
              const Number & w0, const Number & w1, long & out)
{
    Number du, df, dw, t;
    if (!Number::sub(u, f0, du)) return false;
    if (!Number::sub(f1, f0, df)) return false;
    if (df.is_zero()) return false;
    if (!Number::sub(w1, w0, dw)) return false;
    if (!Number::mul(du, dw, t)) return false;
    if (!Number::div(t, df, t)) return false;
    if (!Number::add(t, w0, t)) return false;
    return t.floor_to_int(out);
}

} // namespace

Interp::GBox Interp::screen_box()
{
    GBox b;
    b.x0 = Number::from_int(0);
    b.x1 = Number::from_int(static_cast<long>(RASTER_WIDTH) - 1);
    b.y0 = Number::from_int(0);
    b.y1 = Number::from_int(static_cast<long>(RASTER_HEIGHT) - 1);
    return b;
}

// Разделитель `DE` сразу за операндом, который не заглядывал вперёд. В
// позиции операнда `DE` значит однобайтовый литерал, поэтому спрашивать о
// нём разборщик нельзя — только сырой байт.
bool Interp::raw_comma(Stream & st)
{
    uint8_t b = 0;
    if (!st.src.peek_raw_byte(b) || b != 0xDE) return false;
    st.src.skip(1);
    return true;
}

// Операнд-буфер — символьная переменная целиком. Заглядывания она за собой
// не оставляет: у `Tok::ARRAY` список индексов не читается вовсе, так что
// сразу за ней можно смотреть сырой байт.
bool Interp::gbuf_operand(Stream & st, const char * who, unsigned & var,
                          Evaluator::Target & tgt)
{
    if (!st.ev.target(tgt)) return fail(st.ev.error());
    if (!tgt.is_str || !tgt.data)
        return fail(std::string(who) + ": буфер не символьная переменная");
    var = tgt.var;
    return true;
}

// Пользовательские единицы — в дискреты устройства. Без `FRAME` отображение
// тождественное: `VICT`, `STAT01`, `M2`, `M4`, `P2`, `STAT001` и `FAN01` не
// зовут ни `WINDOW`, ни `FRAME` вовсе и рисуют прямо дискретами.
bool Interp::gmap(unsigned var, const Number & ux, const Number & uy,
                  long & x, long & y)
{
    GBox f = gwin_;
    std::map<unsigned, GBox>::const_iterator it = gframe_.find(var);
    if (it != gframe_.end()) f = it->second;

    if (!map_axis(ux, f.x0, f.x1, gwin_.x0, gwin_.x1, x) ||
        !map_axis(uy, f.y0, f.y1, gwin_.y0, gwin_.y1, y))
        return fail("координата не переводится в дискреты экрана");
    return true;
}

// `¤OPEN <буфер>[,<буфер>]` — объявить массив буфером, то есть записать в
// него пустой заголовок. Первый операнд бывает пропущен (`SLIDE` 220
// `¤OPEN ,B¤()`), и тогда операнды начинаются с `DE`.
bool Interp::do_gopen(Stream & st)
{
    unsigned opened = 0;
    while (!st.src.at_end()) {
        if (raw_comma(st)) continue;

        unsigned var = 0;
        Evaluator::Target tgt;
        if (!gbuf_operand(st, "¤OPEN", var, tgt)) return false;
        GBuffer g(*tgt.data, tgt.off, tgt.len);
        if (!g.fits())
            return fail("¤OPEN: поле короче заголовка буфера в 43 байта");
        g.open();
        // У свежего буфера отображение тождественное, пока не позвали `FRAME`.
        gframe_.erase(var);
        ++opened;
    }
    if (!opened) return fail("¤OPEN без буфера");
    return true;
}

// Четыре границы через запятую — общая часть `WINDOW` и `FRAME`.
bool Interp::do_gbox(Stream & st, GBox & out, const char * who)
{
    Number * dst[4] = { &out.x0, &out.x1, &out.y0, &out.y1 };
    for (unsigned i = 0; i < 4; ++i) {
        if (!st.ev.number(*dst[i])) return fail(st.ev.error());
        if (i == 3) break;
        Tok t;
        if (!st.ev.parser().peek(t, false)) return fail(st.ev.error());
        if (t.t != Tok::COMMA)
            return fail(std::string(who) + ": ожидались четыре границы");
        st.ev.parser().consume();
    }
    return true;
}

// `WINDOW <x0>,<x1>,<y0>,<y1>` — область на устройстве, в дискретах. Буфера
// в операндах нет вовсе: область общая, и `SIG` 7580 открывает два буфера
// сразу, задавая один `WINDOW` на оба.
bool Interp::do_gwindow(Stream & st)
{
    GBox b;
    if (!do_gbox(st, b, "WINDOW")) return false;
    gwin_ = b;
    return true;
}

// `FRAME <буфер>,<x0>,<x1>,<y0>,<y1>` — та же область в пользовательских
// единицах, и она своя у буфера. Рамки не рисует: `SLIDE` 4510 зовёт её
// сразу после чтения чужой картинки с диска, в паре с `ORIGIN A¤(),0,0` —
// это сброс привязки, а не рисование (`docs/format.md`, разд. 5).
bool Interp::do_gframe(Stream & st)
{
    unsigned var = 0;
    Evaluator::Target tgt;
    if (!gbuf_operand(st, "FRAME", var, tgt)) return false;
    if (!raw_comma(st)) return fail("FRAME без границ");

    GBox b;
    if (!do_gbox(st, b, "FRAME")) return false;
    gframe_[var] = b;
    return true;
}

// `NPLOT`, `DRAW` и `DOT`: буфер и две координаты. Все три кладут в поток
// пятибайтовую запись — код и точку старшим байтом вперёд.
bool Interp::do_gpoint(Stream & st, uint8_t op, const char * who)
{
    unsigned var = 0;
    Evaluator::Target tgt;
    if (!gbuf_operand(st, who, var, tgt)) return false;
    if (!raw_comma(st)) return fail(std::string(who) + " без координат");

    Number ux, uy;
    if (!st.ev.number(ux)) return fail(st.ev.error());
    Tok t;
    if (!st.ev.parser().peek(t, false)) return fail(st.ev.error());
    if (t.t != Tok::COMMA)
        return fail(std::string(who) + " без второй координаты");
    st.ev.parser().consume();
    if (!st.ev.number(uy)) return fail(st.ev.error());

    long x = 0, y = 0;
    if (!gmap(var, ux, uy, x, y)) return false;

    uint8_t rec[5];
    if (!gbuf_point_record(op, x, y, rec))
        return fail(std::string(who) + ": точка вне растра");

    GBuffer g(*tgt.data, tgt.off, tgt.len);
    std::string err;
    if (!g.append(rec, 5, err)) return fail(std::string(who) + ": " + err);
    g.set_point(x, y);
    return true;
}

// `LABEL <буфер>[<размер>],<a>,<b>,<текст>`. Множитель размера пишется
// вплотную к имени буфера, без разделителя (`VICT` 6210
// `LABEL B¤()3,,,"Q"`); пропущенный аргумент всё равно даёт `DE`.
bool Interp::do_glabel(Stream & st)
{
    unsigned var = 0;
    Evaluator::Target tgt;
    if (!gbuf_operand(st, "LABEL", var, tgt)) return false;

    unsigned size = 1;
    bool sep = raw_comma(st);
    if (!sep && !st.src.at_end()) {
        Number n;
        if (!st.ev.number(n)) return fail(st.ev.error());
        long k = 0;
        if (!n.floor_to_int(k) || k < 1 || k > 255)
            return fail("LABEL: множитель размера вне 1…255");
        size = static_cast<unsigned>(k);
        Tok t;
        if (!st.ev.parser().peek(t, false)) return fail(st.ev.error());
        if (t.t != Tok::COMMA) return fail("LABEL без текста");
        st.ev.parser().consume();
        sep = true;
    }
    if (!sep) return fail("LABEL без текста");

    // Дальше три аргумента через запятую: два признака и текст. Умолчания
    // взяты с живой картинки — у всех её надписей признаки `01 02 00`.
    uint8_t attr[2] = { 0x02, 0x00 };
    Value text;
    text.is_str = true;
    for (unsigned i = 0; i < 3; ++i) {
        if (st.src.at_end()) break;
        if (i < 2) {
            if (raw_comma(st)) continue;        // аргумент пропущен
            Number n;
            if (!st.ev.number(n)) return fail(st.ev.error());
            long k = 0;
            if (!n.floor_to_int(k) || k < 0 || k > 255)
                return fail("LABEL: признак вне 0…255");
            attr[i] = static_cast<uint8_t>(k);
            Tok t;
            if (!st.ev.parser().peek(t, false)) return fail(st.ev.error());
            if (t.t != Tok::COMMA) return fail("LABEL без текста");
            st.ev.parser().consume();
        } else {
            if (!st.ev.expr(text)) return fail(st.ev.error());
            if (!text.is_str) return fail("LABEL: текст не символьный");
        }
    }

    std::string rec;
    if (!gbuf_label_record(text.str, size, attr[0], attr[1], rec))
        return fail("LABEL: надпись слишком длинна");

    GBuffer g(*tgt.data, tgt.off, tgt.len);
    std::string err;
    if (!g.append(reinterpret_cast<const uint8_t *>(rec.data()),
                  static_cast<unsigned>(rec.size()), err))
        return fail(std::string("LABEL: ") + err);

    // Точка двигается на приращение из самой записи — так его читает и
    // `SLIDE` (`ADD C (N¤,STR(A¤(),J+2,4))`).
    const long dx = (static_cast<unsigned char>(rec[2]) << 8) |
                     static_cast<unsigned char>(rec[3]);
    g.set_point(g.x() + dx, g.y());
    return true;
}

// `¤COPY /<адрес>,<буфер>` — выложить буфер на устройство. Приставка та же,
// что у блочного обмена: `¤COPY /34,S¤()` = `06 1F 06 DC DE 34 DE E0 0C`.
bool Interp::do_gcopy(Stream & st)
{
    unsigned addr = 0;
    if (!bt_prefix(st, addr)) return false;

    unsigned var = 0;
    Evaluator::Target tgt;
    if (!gbuf_operand(st, "¤COPY", var, tgt)) return false;

    GBuffer g(*tgt.data, tgt.off, tgt.len);
    if (!g.looks_open()) return fail("¤COPY: буфер не открыт оператором ¤OPEN");

    Raster * out = host_.plot_surface(static_cast<uint8_t>(addr));
    if (!out)
        return fail("¤COPY: устройства /" + hex2_str(addr) + " у хоста нет");

    // Растр чистится перед выводом. Прямо это нигде не сказано, но без
    // такого правила не работал бы `SLIDE`: стирание элемента у него — сброс
    // бита `04` в коде записи (5240), а следом идёт одна только
    // «ВОССТ.ИНФ.» = `¤COPY /10` (1480). Не чисти он растр, стёртый элемент
    // так и остался бы гореть.
    out->clear();

    std::string err;
    if (!gbuf_draw(g.stream(), g.stream_len(), *out, err))
        return fail("¤COPY: " + err);
    return true;
}

} // namespace iskra
