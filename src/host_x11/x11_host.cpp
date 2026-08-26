// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Mikhail Revzin <p3.141592653589793238462643@gmail.com>
// Part of the Iskra-226 project: https://github.com/Ptr314/Iskra-226
// Description: хост с окном на чистом Xlib — ни одной сторонней библиотеки

#include "host_x11/x11_host.h"

#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/Xatom.h>
#include <X11/keysym.h>

#include <sys/select.h>
#include <time.h>

#include <cstring>

#include "core/koi8.h"
#include "host_common/keywords.h"

namespace iskra {

namespace {

// Полмига курсора. Точного значения для «Искры» нет, взято привычное для
// знакоместных дисплеев — примерно два мигания в секунду.
const unsigned BLINK_MS = 500;

// Часы монотонные: настенные могут прыгнуть назад при переводе времени, а
// у нас на них мигает курсор и стоит оператор TIME.
uint32_t now_ms()
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return static_cast<uint32_t>(ts.tv_sec * 1000u + ts.tv_nsec / 1000000u);
}

// Сдвиг младшего значащего разряда маски.
unsigned mask_shift(unsigned long m)
{
    unsigned s = 0;
    if (!m) return 0;
    while (!(m & 1)) { m >>= 1; ++s; }
    return s;
}

// Слово растеризатора (0x00RRGGBB) → слово визуала. У X11 порядок байт
// задан масками, а не соглашением: на обычном экране они дают тот же
// 0x00RRGGBB, но полагаться на это нельзя.
struct VisualPack
{
    unsigned long rm, gm, bm;
    unsigned rs, gs, bs;

    VisualPack(unsigned long r, unsigned long g, unsigned long b)
        : rm(r), gm(g), bm(b),
          rs(mask_shift(r)), gs(mask_shift(g)), bs(mask_shift(b)) {}

    uint32_t operator()(uint32_t c) const
    {
        uint8_t r = 0, g = 0, b = 0;
        unpack_rgb(c, r, g, b);
        return static_cast<uint32_t>(((static_cast<unsigned long>(r) << rs) & rm) |
                                     ((static_cast<unsigned long>(g) << gs) & gm) |
                                     ((static_cast<unsigned long>(b) << bs) & bm));
    }
};

// Значение клавиши → код КОИ-8. Внутри эмулятора текст живёт в КОИ-8, и
// кириллице X11 повезло: устаревшие значения `XK_Cyrillic_*` (0x6C0–0x6FF)
// совпадают с КОИ-8 байт в байт — `XK_Cyrillic_a` = 0x6C1, а «а» в КОИ-8 =
// 0xC1. Поэтому кириллица приходит с любой раскладки без всякого XIM.
bool keysym_to_koi8(unsigned long ks, uint8_t & out)
{
    if (ks >= 0x20 && ks <= 0x7E) {           // ASCII, он же нижняя половина
        out = static_cast<uint8_t>(ks);
        return true;
    }
    if (ks >= 0x06C0 && ks <= 0x06FF) {       // кириллица == КОИ-8
        out = static_cast<uint8_t>(ks & 0xFF);
        return true;
    }
    uint32_t cp = 0;
    if (ks >= 0xA0 && ks <= 0xFF) cp = static_cast<uint32_t>(ks);  // Latin-1
    else if ((ks & 0xFF000000UL) == 0x01000000UL) cp = static_cast<uint32_t>(ks & 0x00FFFFFF);
    else return false;
    return unicode_to_koi8(cp, out);
}

} // namespace

X11Host::X11Host()
    : key_pos_(0), special_(false), control_(CK_NONE),
      dpy_(0), visual_(0), gc_(0), wm_delete_(0),
      screen_num_(0), depth_(0), closed_(false),
      key_min_(0), key_per_(0),
      cursor_on_(false), start_ms_(now_ms())
{
}

X11Host::~X11Host()
{
    close();
}

// Рабочая область экрана: у окна с умолчанием нет права открываться за
// краем у всякого, чей монитор меньше, чем был у выбиравшего умолчание.
// `_NET_WORKAREA` — то же, что `SPI_GETWORKAREA` у Windows: экран за
// вычетом панелей. Нет его — берём весь экран.
namespace {

void work_area(Display * dpy, int scr, unsigned & w, unsigned & h)
{
    w = static_cast<unsigned>(DisplayWidth(dpy, scr));
    h = static_cast<unsigned>(DisplayHeight(dpy, scr));

    Atom net = XInternAtom(dpy, "_NET_WORKAREA", True);
    if (net == None) return;

    Atom type = None;
    int fmt = 0;
    unsigned long items = 0, rest = 0;
    unsigned char * data = 0;
    if (XGetWindowProperty(dpy, RootWindow(dpy, scr), net, 0, 4, False,
                           XA_CARDINAL, &type, &fmt, &items, &rest, &data)
        == Success && data) {
        if (fmt == 32 && items >= 4) {
            const long * a = reinterpret_cast<const long *>(data);
            if (a[2] > 0 && a[3] > 0) {
                w = static_cast<unsigned>(a[2]);
                h = static_cast<unsigned>(a[3]);
            }
        }
        XFree(data);
    }
}

// Наибольшее увеличение, при котором окно ещё влезает. Рамку и заголовок
// оконный распорядитель рисует сам и размеров их до отображения окна не
// сообщает, поэтому на них отводится запас на глаз.
unsigned fitting_scale(Display * dpy, int scr, unsigned fw, unsigned fh)
{
    unsigned aw = 0, ah = 0;
    work_area(dpy, scr, aw, ah);
    if (ah > 80) ah -= 80;               // заголовок и рамка
    if (aw > 20) aw -= 20;

    for (unsigned k = 8; k > 1; --k)
        if (fw * k <= aw && fh * k * DOT_TALL <= ah) return k;
    return 1;
}

} // namespace

bool X11Host::open(const std::string & title_utf8, unsigned scale,
                   std::string & error)
{
    error.clear();

    Display * dpy = XOpenDisplay(0);
    if (!dpy) {
        error = "не удалось открыть дисплей X11: проверьте DISPLAY";
        return false;
    }
    dpy_ = dpy;
    screen_num_ = DefaultScreen(dpy);
    depth_ = DefaultDepth(dpy, screen_num_);
    visual_ = DefaultVisual(dpy, screen_num_);

    // Кадр выкладывается словами по 32 бита, как его собрал растеризатор.
    // Экранов с палитрой и 16-битных мы не разбираем: перекладывать кадр
    // точка за точкой ради машин, которых давно нет, незачем — а молча
    // показать мусор нельзя.
    int bpp = 0;
    int nfmt = 0;
    XPixmapFormatValues * fmt = XListPixmapFormats(dpy, &nfmt);
    if (fmt) {
        for (int i = 0; i < nfmt; ++i)
            if (fmt[i].depth == depth_) bpp = fmt[i].bits_per_pixel;
        XFree(fmt);
    }

    XVisualInfo want;
    std::memset(&want, 0, sizeof(want));
    want.visualid = XVisualIDFromVisual(static_cast<Visual *>(visual_));
    int nvi = 0;
    XVisualInfo * vi = XGetVisualInfo(dpy, VisualIDMask, &want, &nvi);
    bool truecolor = false;
    unsigned long rmask = 0, gmask = 0, bmask = 0;
    if (vi && nvi > 0) {
        truecolor = vi->c_class == TrueColor || vi->c_class == DirectColor;
        rmask = vi->red_mask;
        gmask = vi->green_mask;
        bmask = vi->blue_mask;
    }
    if (vi) XFree(vi);

    if (bpp != 32 || !truecolor) {
        error = "нужен экран TrueColor в 32 бита на точку";
        close();
        return false;
    }

    // Растеризатор цветов не толкует вовсе — он берёт готовые слова, и
    // складывает их хост (`host_common/renderer.h`). Поэтому умолчания
    // растеризатора разбираются и складываются заново, по маскам визуала:
    // вторых констант для цвета люминофора и бумаги тут не заводится.
    const VisualPack pack(rmask, gmask, bmask);
    render_.set_colors(pack(render_.fg()), pack(render_.bg()));
    render_.set_paper_colors(pack(render_.ink()), pack(render_.paper()));

    XGCValues gcv;
    std::memset(&gcv, 0, sizeof(gcv));
    gcv.foreground = BlackPixel(dpy, screen_num_);
    gc_ = XCreateGC(dpy, RootWindow(dpy, screen_num_), GCForeground, &gcv);

    wm_delete_ = XInternAtom(dpy, "WM_DELETE_WINDOW", False);
    read_keymap();

    closed_ = false;
    if (!open_pane(PANE_SCREEN, title_utf8, scale)) {
        error = "не удалось создать окно";
        close();
        return false;
    }
    return true;
}

void X11Host::close()
{
    if (!dpy_) return;
    Display * dpy = static_cast<Display *>(dpy_);
    for (unsigned i = 0; i < PANES; ++i) close_pane(i);
    if (gc_) { XFreeGC(dpy, static_cast<GC>(gc_)); gc_ = 0; }
    XCloseDisplay(dpy);
    dpy_ = 0;
}

bool X11Host::open_pane(unsigned idx, const std::string & title_utf8,
                        unsigned scale)
{
    if (!dpy_) return false;
    Pane & p = pane_[idx];
    if (p.win) return true;

    Display * dpy = static_cast<Display *>(dpy_);
    const unsigned fw = render_.frame_width(), fh = render_.frame_height();
    if (!scale) scale = fitting_scale(dpy, screen_num_, fw, fh);

    p.scale = scale;
    p.cw = fw * scale;
    p.ch = fh * scale * DOT_TALL;

    const unsigned long black = BlackPixel(dpy, screen_num_);
    Window w = XCreateSimpleWindow(dpy, RootWindow(dpy, screen_num_),
                                   0, 0, p.cw, p.ch, 0, black, black);
    if (!w) return false;

    XSelectInput(dpy, w, KeyPressMask | ExposureMask | StructureNotifyMask);
    Atom del = static_cast<Atom>(wm_delete_);
    XSetWMProtocols(dpy, w, &del, 1);

    // Заголовок кириллический, поэтому имя ставится свойством `_NET_WM_NAME`
    // в UTF-8: старое `WM_NAME` по стандарту восьмибитное в Latin-1, и
    // «Искра» в нём не выражается вовсе. Распорядители читают новое имя
    // уже лет двадцать; старое ставим тем же байтами — на крайний случай.
    Atom net_name = XInternAtom(dpy, "_NET_WM_NAME", False);
    Atom utf8 = XInternAtom(dpy, "UTF8_STRING", False);
    const unsigned char * title =
        reinterpret_cast<const unsigned char *>(title_utf8.c_str());
    XChangeProperty(dpy, w, net_name, utf8, 8, PropModeReplace,
                    title, static_cast<int>(title_utf8.size()));
    XChangeProperty(dpy, w, XA_WM_NAME, XA_STRING, 8, PropModeReplace,
                    title, static_cast<int>(title_utf8.size()));

    // Меньше кадра окно делать бессмысленно: увеличение целое, и ниже
    // единицы ему падать некуда.
    XSizeHints hints;
    std::memset(&hints, 0, sizeof(hints));
    hints.flags = PMinSize;
    hints.min_width = static_cast<int>(fw);
    hints.min_height = static_cast<int>(fh * DOT_TALL);
    XSetWMNormalHints(dpy, w, &hints);

    p.win = w;
    fit(idx);
    XMapWindow(dpy, w);
    redraw(idx);
    XFlush(dpy);
    return true;
}

void X11Host::close_pane(unsigned idx)
{
    Pane & p = pane_[idx];
    if (!dpy_) { p.win = 0; p.img = 0; return; }
    Display * dpy = static_cast<Display *>(dpy_);
    if (p.img) {
        XImage * im = static_cast<XImage *>(p.img);
        im->data = 0;                    // кадр наш, XDestroyImage его не тронет
        XDestroyImage(im);
        p.img = 0;
    }
    if (p.win) { XDestroyWindow(dpy, static_cast<Window>(p.win)); p.win = 0; }
    p.frame.clear();
}

unsigned X11Host::pane_of(unsigned long win) const
{
    for (unsigned i = 0; i < PANES; ++i)
        if (pane_[i].win == win) return i;
    return PANES;
}

// Увеличение подгоняется под окно: экран знакоместный, и дробное растяжение
// съело бы ровность знаков — однопиксельные штрихи вышли бы разной толщины.
// Что не поместилось целым увеличением, становится полями.
//
// **Растягивает здесь растеризатор, а не система.** У GDI для этого есть
// StretchDIBits с COLORONCOLOR, у голого Xlib — ничего: XPutImage кладёт
// точку в точку. Поэтому кадр сразу собирается нужного размера.
void X11Host::fit(unsigned idx)
{
    Pane & p = pane_[idx];
    if (!p.win) return;

    const unsigned fw = render_.frame_width(), fh = render_.frame_height();
    unsigned k = p.cw / fw;
    const unsigned kv = p.ch / (fh * DOT_TALL);
    if (kv < k) k = kv;
    if (k < 1) k = 1;
    p.scale = k;

    render_.set_scale(k, k * DOT_TALL);
    const std::size_t need = render_.pixels();
    if (p.frame.size() == need && p.img) return;

    Display * dpy = static_cast<Display *>(dpy_);
    if (p.img) {
        XImage * im = static_cast<XImage *>(p.img);
        im->data = 0;
        XDestroyImage(im);
        p.img = 0;
    }
    p.frame.assign(need, 0);
    p.img = XCreateImage(dpy, static_cast<Visual *>(visual_),
                         static_cast<unsigned>(depth_), ZPixmap, 0,
                         reinterpret_cast<char *>(&p.frame[0]),
                         render_.width(), render_.height(), 32, 0);
}

void X11Host::redraw(unsigned idx)
{
    Pane & p = pane_[idx];
    if (!p.win) return;

    // Растеризатор один на три окна, а увеличение у каждого своё, поэтому
    // fit() ставит его прямо перед проходом. Проход синхронный, делить тут
    // нечего.
    fit(idx);
    if (p.frame.empty()) return;

    switch (idx) {
    case PANE_SCREEN:
        render_.set_cursor(cursor_on_);
        // Трубка одна: графический растр ложится поверх знакомест. Пока на
        // нём ни одной точки, накладывать нечего — и лишнего прохода не будет.
        render_.draw(screen_, &p.frame[0], render_.width(),
                     raster_.empty() ? 0 : &raster_);
        screen_.clear_dirty();
        raster_.clear_dirty();
        break;
    case PANE_PLOT:
        // Лист графопостроителя — тот же растр, только без знакомест: своих
        // знаков у бумаги нет.
        render_.draw_raster(plotter_, &p.frame[0], render_.width());
        plotter_.clear_dirty();
        break;
    case PANE_PAPER:
        // Знакоместа цветами бумаги и без курсора: печатает АЦПУ, а не трубка.
        render_.draw_paper(paper_, &p.frame[0], render_.width());
        paper_.clear_dirty();
        break;
    }
    flush(idx);
}

void X11Host::flush(unsigned idx)
{
    Pane & p = pane_[idx];
    if (!p.win || !p.img || p.frame.empty()) return;

    Display * dpy = static_cast<Display *>(dpy_);
    XImage * im = static_cast<XImage *>(p.img);
    const int dw = im->width, dh = im->height;
    const int dx = (static_cast<int>(p.cw) - dw) / 2;
    const int dy = (static_cast<int>(p.ch) - dh) / 2;

    XPutImage(dpy, static_cast<Window>(p.win), static_cast<GC>(gc_), im,
              0, 0, dx > 0 ? dx : 0, dy > 0 ? dy : 0, dw, dh);
    fill_margins(p, dx, dy, dw, dh);
    XFlush(dpy);
}

// Поля закрашиваются сами: фона у окна нет вовсе — иначе система мигала бы
// им при каждом изменении размера.
void X11Host::fill_margins(const Pane & p, int dx, int dy, int dw, int dh)
{
    Display * dpy = static_cast<Display *>(dpy_);
    Window w = static_cast<Window>(p.win);
    GC gc = static_cast<GC>(gc_);
    const int cw = static_cast<int>(p.cw), ch = static_cast<int>(p.ch);

    if (dy > 0) {
        XFillRectangle(dpy, w, gc, 0, 0, cw, dy);
        XFillRectangle(dpy, w, gc, 0, dy + dh, cw, ch - dy - dh);
    }
    if (dx > 0) {
        XFillRectangle(dpy, w, gc, 0, dy, dx, dh);
        XFillRectangle(dpy, w, gc, dx + dw, dy, cw - dx - dw, dh);
    }
}

// Бумаги у нас нет, и разрешения графопостроителя мы не знаем: книга
// называет его «планшетным» и больше о нём не говорит, а размер картинки не
// задан ни в буфере, ни в его заголовке. Поэтому лист берётся в тех же
// координатах, что и буфер (docs/DECISIONS.md, разд. 15).
Raster * X11Host::plot_surface(uint8_t addr)
{
    if (addr == 0x14 && !open_pane(PANE_PLOT, "Искра 226 — графопостроитель", 0))
        return 0;
    return Host::plot_surface(addr);
}

// АЦПУ. Знакогенератора у печати мы не знаем и взять его неоткуда, поэтому
// лента показывается теми же знакоместами, что и экран: перевод строки,
// возврат каретки и прокрутку `Screen` уже умеет, а больше печать ничего и
// не шлёт. Видно последние 24 строки; вся лента — в файле `--printer`.
void X11Host::print_char(uint8_t ch)
{
    tape_.put(ch);
    open_pane(PANE_PAPER, "Искра 226 — АЦПУ", 0);
    paper_.put(ch);
}

void X11Host::push_key(uint8_t code, bool special)
{
    keys_.push_back(code);
    keys_sf_.push_back(special ? 1 : 0);
}

// Клавиша верхнего регистра вводит целое слово Бейсика, и своего кода у
// слова нет: машина показывает его на экране, как набранное по буквам. Так
// и кладём — знаками в ту же очередь.
void X11Host::push_word(const char * word)
{
    if (!word) return;
    for (const char * p = word; *p; ++p)
        push_key(static_cast<uint8_t>(*p), false);
}

void X11Host::read_keymap()
{
    Display * dpy = static_cast<Display *>(dpy_);
    int lo = 0, hi = 0;
    XDisplayKeycodes(dpy, &lo, &hi);
    int per = 0;
    KeySym * ks = XGetKeyboardMapping(dpy, static_cast<KeyCode>(lo),
                                      hi - lo + 1, &per);
    keymap_.clear();
    key_min_ = lo;
    key_per_ = per;
    if (!ks) return;
    keymap_.assign(ks, ks + static_cast<std::size_t>(hi - lo + 1) * per);
    XFree(ks);
}

// Раскладок на PC несколько, и буква под клавишей в каждой своя: на русской
// на месте `E` сидит `У`. Поэтому клавиши, которые «Искра» знает по
// положению (Ctrl+E, Ctrl+R, Ctrl+N), ищутся среди всех значений клавиши —
// берётся первое латинское. Тот же смысл, что у `VK_`-кодов Windows.
uint8_t X11Host::latin_of(unsigned keycode) const
{
    if (key_per_ <= 0) return 0;
    const int i = static_cast<int>(keycode) - key_min_;
    if (i < 0 || static_cast<std::size_t>((i + 1) * key_per_) > keymap_.size())
        return 0;
    for (int j = 0; j < key_per_; ++j) {
        const unsigned long ks = keymap_[static_cast<std::size_t>(i) * key_per_ + j];
        if (ks >= 'A' && ks <= 'Z') return static_cast<uint8_t>(ks);
        if (ks >= 'a' && ks <= 'z') return static_cast<uint8_t>(ks - 'a' + 'A');
    }
    return 0;
}

// Клавиатура «Искры» — восемь зон (руководство, разд. 2.1), и три из них на
// обычной клавиатуре взять неоткуда: 32 клавиши специальных функций, шесть
// клавиш перемещения курсора и клавиши управления машиной.
//
// Зона 8 ложится на верхний ряд один в один: Esc, двенадцать функциональных
// и три справа — ровно шестнадцать клавиш, а Shift даёт верхний банк, как и
// на машине. Номера при этом совпадают: F5 — это клавиша 5. У 13–15 есть
// дубли на Ctrl+F1…F3: PrintScreen у многих сред забирает себе снимок экрана.
void X11Host::key_press(void * event)
{
    XKeyEvent & ke = static_cast<XEvent *>(event)->xkey;

    char buf[32];
    KeySym ks = NoSymbol;
    const int n = XLookupString(&ke, buf, sizeof(buf), &ks, 0);

    const bool shift = (ke.state & ShiftMask) != 0;
    const bool ctrl  = (ke.state & ControlMask) != 0;
    const bool alt   = (ke.state & Mod1Mask) != 0;

    // Клавиши управления машиной идут первыми: у них кодов нет вовсе, и
    // ложатся они в отдельный ящик. Ctrl+Break — привычный «останови счёт»;
    // сброс сделан нарочно неудобным, он стирает экран.
    if (ctrl && (ks == XK_Pause || ks == XK_Break)) {
        control_ = alt ? CK_RESET : CK_HALT;
        return;
    }
    if (ctrl && !alt && (ks == XK_Return || ks == XK_KP_Enter)) {
        control_ = CK_CONTINUE;
        return;
    }
    if (ctrl && !alt && latin_of(ke.keycode) == 'N') {
        control_ = CK_STMT_NUMBER;
        return;
    }

    // С Alt дальше идут только слова Бейсика верхнего регистра зоны 1:
    // «нажатием каждой клавиши вводится то или иное ключевое слово Бейсика»
    // (разд. 2.1). Ключ — знак, а не положение: на «Искре» русская и
    // латинская буквы сидят на одной клавише, а на PC разъезжаются по
    // раскладкам, и по знаку они сходятся обратно. На арифметических
    // клавишах цифрового блока сидит зона 5 — её от зоны 1 по знаку не
    // отличить, только по положению.
    if (alt) {
        PadFunc pf = PAD_MUL;
        bool pad = true;
        switch (ks) {
        case XK_KP_Multiply: pf = PAD_MUL; break;
        case XK_KP_Add:      pf = PAD_ADD; break;
        case XK_KP_Subtract: pf = PAD_SUB; break;
        case XK_KP_Divide:   pf = PAD_DIV; break;
        default: pad = false;
        }
        if (pad) { push_word(keyword_for_pad(pf)); return; }

        uint8_t ch = 0;
        if (keysym_to_koi8(ks, ch)) push_word(keyword_for_char(koi8_upper(ch)));
        return;
    }

    const unsigned bank = shift ? 16 : 0;
    int sf = -1;                    // номер клавиши спецфункций, -1 — не она

    if (ks == XK_Escape) sf = 0;
    else if (ks >= XK_F1 && ks <= XK_F12) {
        const int num = static_cast<int>(ks - XK_F1) + 1;
        // Ctrl+F1…F3 — дубли клавиш 13–15; прочие с Ctrl не наши.
        if (ctrl) { if (num > 3) return; sf = num + 12; }
        else sf = num;
    }
    else if (ks == XK_Print || ks == XK_Sys_Req) sf = 13;
    else if (ks == XK_Scroll_Lock) sf = 14;
    else if (ks == XK_Pause || ks == XK_Break) sf = 15;

    if (sf >= 0) {
        push_key(static_cast<uint8_t>(sf + bank), true);
        return;
    }

    uint8_t code = 0;
    switch (ks) {
    case XK_Left:  case XK_KP_Left:  code = ctrl ? KEY_LEFT5  : KEY_LEFT;  break;
    case XK_Right: case XK_KP_Right: code = ctrl ? KEY_RIGHT5 : KEY_RIGHT; break;
    case XK_Up:     case XK_KP_Up:     code = KEY_UP;     break;
    case XK_Down:   case XK_KP_Down:   code = KEY_DOWN;   break;
    case XK_Insert: case XK_KP_Insert: code = KEY_INSERT; break;
    case XK_Delete: case XK_KP_Delete: code = KEY_DELETE; break;
    case XK_End:    case XK_KP_End:    code = KEY_ERASE;  break;
    case XK_BackSpace: code = ctrl ? KEY_LINE_ERASE : KEY_BACKSPACE; break;
    case XK_Return: case XK_KP_Enter:  code = KEY_CR;     break;
    default: break;
    }
    // Буквы берём по положению, а не по знаку: иначе Ctrl+E отвалился бы
    // на русской раскладке.
    if (!code && ctrl) {
        const uint8_t l = latin_of(ke.keycode);
        if (l == 'E') code = KEY_EDIT;
        else if (l == 'R') code = KEY_RECALL;
    }
    if (code) { push_key(code, false); return; }

    // Всё прочее — знаки. Это единственный путь, которым они попадают в
    // машину: переводить их по положению «Искре» незачем, у неё своя
    // раскладка. С Ctrl знаков не бывает — там управляющие коды.
    if (ctrl) return;

    uint8_t ch = 0;
    if (!keysym_to_koi8(ks, ch)) {
        // Цифровой блок и прочее, чему значения клавиши не хватило:
        // XLookupString отдаёт готовый байт в Latin-1.
        if (n <= 0) return;
        const uint8_t b = static_cast<uint8_t>(buf[0]);
        if (b < 0x20 || b == 0x7F) return;
        if (!keysym_to_koi8(b, ch)) return;
    }
    // Строчных букв у машины нет ни одной: клавиша выдаёт прописную
    // (core/koi8.h, «семибитная граница машины»).
    push_key(koi8_upper(ch), false);
}

void X11Host::pump()
{
    if (!dpy_) return;
    Display * dpy = static_cast<Display *>(dpy_);

    while (XPending(dpy)) {
        XEvent ev;
        XNextEvent(dpy, &ev);
        const unsigned idx = pane_of(ev.xany.window);

        switch (ev.type) {
        case Expose:
            // Последний из череды: перерисовывать кадр на каждый кусок
            // незачем, он и так лежит целиком.
            if (ev.xexpose.count == 0 && idx < PANES) flush(idx);
            break;

        case ConfigureNotify:
            if (idx < PANES) {
                Pane & p = pane_[idx];
                const unsigned w = static_cast<unsigned>(ev.xconfigure.width);
                const unsigned h = static_cast<unsigned>(ev.xconfigure.height);
                if (w != p.cw || h != p.ch) {
                    p.cw = w;
                    p.ch = h;
                    redraw(idx);
                }
            }
            break;

        case KeyPress:
            key_press(&ev);
            break;

        case MappingNotify:
            XRefreshKeyboardMapping(&ev.xmapping);
            if (ev.xmapping.request != MappingPointer) read_keymap();
            break;

        case ClientMessage:
            if (static_cast<unsigned long>(ev.xclient.data.l[0]) == wm_delete_) {
                // Закрытый лист графопостроителя эмулятор не останавливает:
                // это бумага, а не машина. Заведётся заново при следующем
                // выводе.
                if (idx == PANE_SCREEN || idx >= PANES) closed_ = true;
                else close_pane(idx);
            }
            break;

        default:
            break;
        }
    }
}

// Ждём события системы, но не дольше выдержки: иначе курсор замрёт, пока
// никто не трогает клавиатуру. Xlib своего ожидания с выдержкой не даёт —
// ждём на его же сокете.
void X11Host::wait_event(unsigned ms)
{
    Display * dpy = static_cast<Display *>(dpy_);
    XFlush(dpy);
    if (XPending(dpy)) return;

    const int fd = ConnectionNumber(dpy);
    fd_set r;
    FD_ZERO(&r);
    FD_SET(fd, &r);
    struct timeval tv;
    tv.tv_sec = ms / 1000;
    tv.tv_usec = (ms % 1000) * 1000;
    select(fd + 1, &r, 0, 0, &tv);
}

ControlKey X11Host::take_control_key()
{
    pump();
    const ControlKey c = control_;
    control_ = CK_NONE;
    return c;
}

bool X11Host::present()
{
    pump();
    if (closed_) return false;

    // Мигание курсора: не событие, а состояние часов, поэтому проверяется
    // здесь, а не по таймеру.
    const bool on = ((now_ms() - start_ms_) / BLINK_MS) % 2 == 0;
    if (screen_.dirty() || raster_.dirty() || on != cursor_on_) {
        cursor_on_ = on;
        redraw(PANE_SCREEN);
    }
    if (plotter_.dirty()) redraw(PANE_PLOT);
    if (paper_.dirty()) redraw(PANE_PAPER);

    if (screen_.take_bells()) XBell(static_cast<Display *>(dpy_), 0);
    return true;
}

bool X11Host::poll_key(uint8_t & code)
{
    pump();
    if (key_pos_ >= keys_.size()) {
        keys_.clear();
        keys_sf_.clear();
        key_pos_ = 0;
        return false;
    }
    special_ = keys_sf_[key_pos_] != 0;
    code = keys_[key_pos_++];
    return true;
}

bool X11Host::wait_input(uint8_t & code, ControlKey & ck)
{
    for (;;) {
        if (!present()) return false;
        ck = take_control_key();
        if (ck != CK_NONE) { code = 0; return true; }
        if (poll_key(code)) return true;
        wait_event(BLINK_MS / 4);
    }
}

uint32_t X11Host::ticks_ms() const
{
    return now_ms() - start_ms_;
}

} // namespace iskra
