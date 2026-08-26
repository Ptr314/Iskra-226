// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Mikhail Revzin <p3.141592653589793238462643@gmail.com>
// Part of the Iskra-226 project: https://github.com/Ptr314/Iskra-226
// Description: исполнение графических операторов: буфер, координаты, вывод

#include <cmath>

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

// Приращение в пользовательских единицах — в дискреты. Переносу оно не
// подлежит: сдвинуть надо на столько же, а не встать в другую точку.
// Отсюда отдельный ход — тот же `map_axis`, но по разности.
bool Interp::gmap_delta(unsigned var, const Number & ux, const Number & uy,
                        long & x, long & y)
{
    GBox f = gwin_;
    std::map<unsigned, GBox>::const_iterator it = gframe_.find(var);
    if (it != gframe_.end()) f = it->second;

    const Number zero = Number::from_int(0);
    Number fw, fh, ww, wh;
    if (!Number::sub(f.x1, f.x0, fw) || !Number::sub(f.y1, f.y0, fh) ||
        !Number::sub(gwin_.x1, gwin_.x0, ww) ||
        !Number::sub(gwin_.y1, gwin_.y0, wh))
        return fail("приращение не переводится в дискреты экрана");
    if (!map_axis(ux, zero, fw, zero, ww, x) ||
        !map_axis(uy, zero, fh, zero, wh, y))
        return fail("приращение не переводится в дискреты экрана");
    return true;
}

// `NPLOT`, `DRAW` и `DOT`: буфер и две координаты. Все три кладут в поток
// пятибайтовую запись — код и точку старшим байтом вперёд. `DDRAW` — то же
// самое, но точка задана приращением от текущей: `SIG` 1560 рисует им
// прямоугольник (`DDRAW ,0,4*Y2`, `4*X2,0`, `0,-4*Y2`, `-4*X2,0`), и
// отрицательные значения там не координаты, а сдвиги.
bool Interp::do_gpoint(Stream & st, uint8_t op, const char * who, bool relative)
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
    if (relative) {
        long dx = 0, dy = 0;
        if (!gmap_delta(var, ux, uy, dx, dy)) return false;
        GBuffer cur(*tgt.data, tgt.off, tgt.len);
        if (!cur.looks_open())
            return fail(std::string(who) + ": буфер не открыт оператором ¤OPEN");
        x = cur.x() + dx;
        y = cur.y() + dy;
    } else if (!gmap(var, ux, uy, x, y)) {
        return false;
    }

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

// --- преобразования картинки ------------------------------------------------
//
// `¤MOVE`, `STRETCH` и `TURN` переписывают уже лежащие в буфере записи, а
// новых не кладут. Что они делают, известно из подсказок самого `SLIDE`
// (5880, 5910, 5930): «ВВЕДИТЕ СДВИГ ПО X И ПО Y», «ВВЕДИ КООРДИНАТЫ ТОЧКИ
// (X,Y) И МАСШТАБ (X,Y)», «ВВЕДИ КООРДИНАТЫ ТОЧКИ (X,Y) И УГОЛ ПОВОРОТА
// (ГРАД)». Число операндов сходится с парой `SLIDE`/`SL2` (`docs/format.md`,
// разд. 5): 2, 4 и 3 сверх буфера.

// Список числовых операндов через запятую. Разделитель после выражения
// читается в позиции операции, как везде.
bool Interp::gnums(Stream & st, const char * who, Number * out, unsigned n)
{
    for (unsigned i = 0; i < n; ++i) {
        if (!st.ev.number(out[i])) return fail(st.ev.error());
        if (i + 1 == n) break;
        Tok t;
        if (!st.ev.parser().peek(t, false)) return fail(st.ev.error());
        if (t.t != Tok::COMMA)
            return fail(std::string(who) + ": операндов меньше, чем нужно");
        st.ev.parser().consume();
    }
    return true;
}

// Буфер, запятая, ровно n чисел — общее начало всех трёх преобразований.
bool Interp::gxform_head(Stream & st, const char * who, unsigned n,
                         unsigned & var, Evaluator::Target & tgt, Number * u)
{
    if (!gbuf_operand(st, who, var, tgt)) return false;
    if (!raw_comma(st)) return fail(std::string(who) + " без операндов");
    return gnums(st, who, u, n);
}

// Применить преобразование. Выход за растр — ошибка машины, а не наше
// ограничение: `SLIDE` 5070 ставит вокруг `¤MOVE` свой `ON ERROR` и на
// отказе печатает «СДВИГ НЕВОЗМОЖЕН (ВЫХОД ЗА ЭКРАН)». Кода у неё мы не
// знаем, поэтому `ON ERROR` получает `??` — как у отказов `COPY`.
bool Interp::gxform(Evaluator::Target & tgt, const GAffine & m, const char * who)
{
    GBuffer g(*tgt.data, tgt.off, tgt.len);
    std::string err;
    bool unsupported = false;
    if (gbuf_transform(g, m, err, &unsupported)) return true;
    if (unsupported) return fail(std::string(who) + ": " + err);
    return machine_error(err::UNKNOWN, std::string(who) + ": " + err);
}

// `¤MOVE <буфер>,<dx>,<dy>` — сдвинуть картинку целиком.
bool Interp::do_gmove(Stream & st)
{
    unsigned var = 0;
    Evaluator::Target tgt;
    Number u[2];
    if (!gxform_head(st, "¤MOVE", 2, var, tgt, u)) return false;

    long dx = 0, dy = 0;
    if (!gmap_delta(var, u[0], u[1], dx, dy)) return false;

    GAffine m;
    m.cx = static_cast<double>(dx);
    m.cy = static_cast<double>(dy);
    return gxform(tgt, m, "¤MOVE");
}

// `STRETCH <буфер>,<x>,<y>,<kx>,<ky>` — растянуть относительно точки.
// Что точка неподвижна, видно из `SLIDE` 5950: `STRETCH A¤(),0,0,C,1/S`,
// поворот, и обратное `STRETCH A¤(),0,0,1/C,S` возвращает картинку на
// место. С точкой-«поплавком» такая пара не сошлась бы.
bool Interp::do_gstretch(Stream & st)
{
    unsigned var = 0;
    Evaluator::Target tgt;
    Number u[4];
    if (!gxform_head(st, "STRETCH", 4, var, tgt, u)) return false;

    long px = 0, py = 0;
    if (!gmap(var, u[0], u[1], px, py)) return false;
    const double kx = u[2].to_double(), ky = u[3].to_double();

    GAffine m;
    m.ax = kx; m.cx = static_cast<double>(px) * (1.0 - kx);
    m.by = ky; m.cy = static_cast<double>(py) * (1.0 - ky);
    return gxform(tgt, m, "STRETCH");
}

// `TURN <буфер>,<x>,<y>,<угол>` — повернуть вокруг точки. Единица угла —
// та же, что у тригонометрии: «аргументы тригонометрических функций
// воспринимаются в радианах… SELECT G… SELECT D» (руководство, разд. 13.3).
// `SLIDE` это подтверждает делом: строка 260 у него `SELECT D`, и подсказка
// поворота просит градусы.
bool Interp::do_gturn(Stream & st)
{
    unsigned var = 0;
    Evaluator::Target tgt;
    Number u[3];
    if (!gxform_head(st, "TURN", 3, var, tgt, u)) return false;

    long px = 0, py = 0;
    if (!gmap(var, u[0], u[1], px, py)) return false;

    const double PI = 3.14159265358979323846;
    double a = u[2].to_double();
    if (dev_.angle() == ANG_DEG) a *= PI / 180.0;
    else if (dev_.angle() == ANG_GRAD) a *= PI / 200.0;
    const double c = std::cos(a), s = std::sin(a);
    const double x = static_cast<double>(px), y = static_cast<double>(py);

    GAffine m;
    m.ax = c; m.bx = -s; m.cx = x - x * c + y * s;
    m.ay = s; m.by =  c; m.cy = y - x * s - y * c;
    return gxform(tgt, m, "TURN");
}

// `¤LET <буфер>=<буфер>` и `¤LET <буфер>=0`. В корпусе форм две и обе
// вырожденные: четыре раза буфер присваивается сам себе (`EDITOR` 1236,
// 1335; `SIG` 645, 7590) и один раз обнуляется (`SIG` 7470). Присваивание
// самому себе у нас не делает ничего, и это видно по `EDITOR`: строки 1235
// и 1236 — две ветки одного разветвления, `STRETCH S¤(),0,0,1/1.2,1` либо
// `¤LET S¤()=S¤()`, то есть «растянуть» либо «оставить как есть».
//
// **Побочное действие оператора неизвестно.** У машины он, судя по всему,
// пересчитывает байты заголовка 40–43 — `SLIDE` 5900 читает оттуда пару
// чисел и проверяет `их+сдвиг<0`, то есть похоже на левый нижний угол
// картинки. Байты эти не разобраны, выдумывать их нельзя, и мы их не
// трогаем; `EDITOR` их всё равно тут же переписывает своими (1240, 1340).
bool Interp::do_glet(Stream & st)
{
    unsigned var = 0;
    Evaluator::Target tgt;
    if (!gbuf_operand(st, "¤LET", var, tgt)) return false;

    uint8_t b = 0;
    if (!st.src.peek_raw_byte(b) || b != 0xD9)
        return fail("¤LET без знака равенства");
    st.src.skip(1);

    if (st.src.peek_raw_byte(b) && b == 0xE0) {
        unsigned svar = 0;
        Evaluator::Target from;
        if (!gbuf_operand(st, "¤LET", svar, from)) return false;
        GBuffer s(*from.data, from.off, from.len);
        if (!s.looks_open())
            return fail("¤LET: буфер справа не открыт оператором ¤OPEN");
        const std::string copy(s.stream(), s.stream_len());
        const long x = s.x(), y = s.y();

        GBuffer d(*tgt.data, tgt.off, tgt.len);
        std::string err;
        if (!d.set_stream(copy.data(), static_cast<unsigned>(copy.size()), err))
            return machine_error(err::UNKNOWN, "¤LET: " + err);
        d.set_point(x, y);
        return true;
    }

    Number n;
    if (!st.ev.number(n)) return fail(st.ev.error());
    if (!n.is_zero()) return fail("¤LET: справа ожидался буфер либо ноль");
    GBuffer d(*tgt.data, tgt.off, tgt.len);
    if (!d.fits()) return fail("¤LET: поле короче заголовка буфера в 43 байта");
    d.open();
    return true;
}

// --- PLOT -------------------------------------------------------------------
//
// `PLOT <x,y,перо>[,<…>]` — единственный графический оператор **без
// буфера**: он рисует прямо на устройстве группы `PLOT` (по умолчанию
// `10`, трубка). Перья разобраны парой `SLIDE`/`SL2` (`docs/format.md`,
// разд. 5): `E5`…`E9` = `U`, `D`, `R`, `S`, `C`.
//
// **Координаты — приращения, а не точки.** `SLIDE` 6170 берёт соседние
// записи буфера, где точки лежат абсолютными, считает разность
// (`X2%=X1%-X2%`) и подаёт её `PLOT <X2%,Y2%,U>`.
//
// Что значит каждое перо, видно из того, чем его кормят. `SLIDE` 5690
// печатает надпись из записи буфера так:
//
//     PLOT <X3,Y3,S>,<R,,C>,<,,STR(A¤(),J+9,K%-9)>,<X3,,U>
//
// где (по DEFFN' 52, строка 1720) `R` — байт признаков 1, а `X3`, `Y3` —
// байты 2 и 3. Байт 1 идёт у графопостроителя в код размера знака
// (`SLIDE` 7160 `I%=R*F/2.4`), а байт 2 — в приращение на знак (7170,
// `GOSUB ' 35(F*X3)`). Отсюда `C` — размер знака, `S` — шаг знака.
// `R` без координат стоит в начале каждой серии (5650, 6160) — сброс.
//
// **Шаг знака по умолчанию — семь дискрет на знак**, как у `LABEL`: пока
// `S` не задан, брать больше неоткуда.
bool Interp::plot_group(Stream & st, Raster & out)
{
    uint8_t b = 0;
    if (!st.src.take_raw_byte(b) || b != 0xD7)
        return fail("PLOT: группа не открыта");

    Number val[2];
    bool has[2] = { false, false };
    unsigned pen = 0;                    // 0 — пера в группе нет
    Value text;
    bool has_text = false;

    for (unsigned k = 0;;) {
        if (!st.src.peek_raw_byte(b)) return fail("PLOT: группа не закрыта");
        if (b == 0xD4) { st.src.skip(1); break; }
        if (b == 0xDE) { st.src.skip(1); ++k; continue; }

        // Третий элемент — перо. Буква там значит метку, а не переменную:
        // `S` и `C` — законные имена, и в `SLIDE` они есть. Различает их
        // только место в группе и то, что за меткой сразу конец элемента.
        if (k == 2 && b >= 0xE5 && b <= 0xE9) {
            const unsigned save = st.src.pos();
            st.src.skip(1);
            uint8_t nx = 0;
            if (st.src.peek_raw_byte(nx) && (nx == 0xD4 || nx == 0xDE)) {
                pen = b - 0xE4;                     // 1…5 = U D R S C
                continue;
            }
            st.src.set_pos(save);
        }

        st.ev.parser().reset();
        st.ev.set_stop_gt(true);
        Value v;
        const bool ok = st.ev.expr(v);
        st.ev.set_stop_gt(false);
        if (!ok) return fail(st.ev.error());
        st.ev.parser().unpeek();

        if (k > 2) return fail("PLOT: в группе больше трёх элементов");
        if (k == 2) { text = v; has_text = true; }
        else {
            if (v.is_str) return fail("PLOT: координата не число");
            val[k] = v.num;
            has[k] = true;
        }
    }

    long d[2] = { 0, 0 };
    for (unsigned i = 0; i < 2; ++i)
        if (has[i] && !val[i].floor_to_int(d[i]))
            return fail("PLOT: операнд не целое число");

    switch (pen) {
        case 1:                                     // U — перо поднято
        case 2: {                                   // D — перо опущено
            const long nx = plot_x_ + d[0], ny = plot_y_ + d[1];
            if (pen == 2) out.line(plot_x_, plot_y_, nx, ny);
            plot_x_ = nx;
            plot_y_ = ny;
            break;
        }
        case 3:                                     // R — сброс
            plot_x_ = plot_y_ = 0;
            plot_size_ = 1;
            plot_step_x_ = plot_step_y_ = 0;
            break;
        case 4:                                     // S — шаг знака
            plot_step_x_ = d[0];
            plot_step_y_ = d[1];
            break;
        case 5:                                     // C — размер знака
            if (d[0] < 1 || d[0] > 255)
                return fail("PLOT: размер знака вне 1…255");
            plot_size_ = static_cast<unsigned>(d[0]);
            break;
        default:
            if (!has_text) return fail("PLOT: в группе нет ни пера, ни надписи");
            break;
    }

    if (has_text) {
        if (!text.is_str) return fail("PLOT: надпись не символьная");
        const long dx = plot_step_x_ || plot_step_y_
                            ? plot_step_x_
                            : static_cast<long>(GBUF_LABEL_CELL * plot_size_);
        gbuf_text(out, plot_x_, plot_y_, text.str.data(),
                  static_cast<unsigned>(text.str.size()), plot_size_,
                  dx, plot_step_y_);
        plot_x_ += dx * static_cast<long>(text.str.size());
        plot_y_ += plot_step_y_ * static_cast<long>(text.str.size());
    }
    return true;
}

bool Interp::do_plot(Stream & st)
{
    const unsigned addr = dev_.addr(DG_PLOT);
    Raster * out = host_.plot_surface(static_cast<uint8_t>(addr));
    if (!out)
        return fail("PLOT: устройства /" + hex2_str(addr) + " у хоста нет");

    for (;;) {
        if (!plot_group(st, *out)) return false;
        uint8_t b = 0;
        if (!st.src.peek_raw_byte(b) || b != 0xDE) return true;
        st.src.skip(1);
    }
}

} // namespace iskra
