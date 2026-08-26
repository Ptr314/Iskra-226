// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Mikhail Revzin <p3.141592653589793238462643@gmail.com>
// Part of the Iskra-226 project: https://github.com/Ptr314/Iskra-226
// Description: хост с окном на AppKit — ни одной сторонней библиотеки

#include "host_cocoa/cocoa_host.h"

#import <Cocoa/Cocoa.h>

#include <time.h>

#include "core/koi8.h"
#include "host_common/keywords.h"

namespace iskra {

namespace {

// Полмига курсора. Точного значения для «Искры» нет, взято привычное для
// знакоместных дисплеев — примерно два мигания в секунду.
const unsigned BLINK_MS = 500;

// Часы монотонные: настенные могут прыгнуть назад при переводе времени, а у
// нас на них мигает курсор и стоит оператор TIME.
uint32_t now_ms()
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return static_cast<uint32_t>(ts.tv_sec * 1000u + ts.tv_nsec / 1000000u);
}

// Значения `keyCode` в AppKit не меняются с раскладкой — это положение
// физической клавиши, аналог `VK_`-кодов Windows и таблицы `XGetKeyboardMapping`
// у X11. Официальных имён без Carbon.framework у нас нет, поэтому здесь те же
// числа, что и в `HIToolbox/Events.h` (`kVK_*`), под своими именами — тянуть
// ради констант ещё один системный фреймворк незачем.
enum {
    KC_ANSI_E        = 0x0E,
    KC_ANSI_N        = 0x2D,
    KC_ANSI_R        = 0x0F,
    KC_RETURN        = 0x24,
    KC_BACKSPACE     = 0x33,   // клавиша со стрелкой влево рядом с Return
    KC_ESCAPE        = 0x35,
    KC_PERIOD        = 0x2F,
    KC_KP_ENTER      = 0x4C,
    KC_KP_MULTIPLY   = 0x43,
    KC_KP_PLUS       = 0x45,
    KC_KP_MINUS      = 0x4E,
    KC_KP_DIVIDE     = 0x4B,
    KC_HELP          = 0x72,  // «Insert» на внешней клавиатуре PC-раскладки
    KC_FORWARD_DELETE= 0x75,  // «Delete» на внешней клавиатуре; своя ⌫ — KC_BACKSPACE
    KC_END           = 0x77,
    KC_LEFT          = 0x7B,
    KC_RIGHT         = 0x7C,
    KC_DOWN          = 0x7D,
    KC_UP            = 0x7E
};

// Номер функциональной клавиши 1…12, 0 — не она. Коды F1…F12 в AppKit не
// идут подряд, поэтому только таблицей.
int fkey_number(unsigned short kc)
{
    switch (kc) {
    case 0x7A: return 1;  case 0x78: return 2;  case 0x63: return 3;
    case 0x76: return 4;  case 0x60: return 5;  case 0x61: return 6;
    case 0x62: return 7;  case 0x64: return 8;  case 0x65: return 9;
    case 0x6D: return 10; case 0x67: return 11; case 0x6F: return 12;
    default: return 0;
    }
}

// Наибольшее увеличение, при котором окно ещё влезает в видимую область
// экрана (за вычетом дока и строки меню — `visibleFrame` уже их убрала).
// Запас по высоте — на заголовок окна, который AppKit добавляет поверх
// содержимого.
unsigned fitting_scale(unsigned fw, unsigned fh)
{
    NSScreen * screen = [NSScreen mainScreen];
    NSRect avail = screen ? screen.visibleFrame : NSMakeRect(0, 0, 1024, 768);
    CGFloat aw = avail.size.width;
    CGFloat ah = avail.size.height;
    if (ah > 60) ah -= 60;

    for (unsigned k = 8; k > 1; --k)
        if (fw * k <= aw && fh * k <= ah) return k;
    return 1;
}

// Минимальное меню — ради Cmd+Q: без него, как и без Info.plist, голый
// исполняемый файл не получает системных сочетаний вовсе. Дальше меню не
// растёт — файлового диалога и прочего Бейсику всё равно взять неоткуда.
void setup_menu()
{
    NSMenu * bar = [[NSMenu alloc] init];
    NSMenuItem * appItem = [[NSMenuItem alloc] init];
    [bar addItem:appItem];

    NSMenu * appMenu = [[NSMenu alloc] init];
    NSString * quitTitle = @"Завершить Искра 226";
    NSMenuItem * quit = [[NSMenuItem alloc] initWithTitle:quitTitle
                                                    action:@selector(terminate:)
                                             keyEquivalent:@"q"];
    [appMenu addItem:quit];
    [quit release];
    appItem.submenu = appMenu;
    [appMenu release];
    [appItem release];

    [NSApp setMainMenu:bar];
    [bar release];
}

} // namespace

} // namespace iskra

// ---------------------------------------------------------------------------
// Вид: знает только «нарисуй кадр» и «разбери нажатие», всё остальное — у
// CocoaHost. Он же — делегат своего окна, разводить их по отдельным классам
// незачем.
// ---------------------------------------------------------------------------

@interface IskraView : NSView <NSWindowDelegate>
- (instancetype)initWithFrame:(NSRect)frame
                          host:(iskra::CocoaHost *)host
                          pane:(unsigned)pane;
@end

@implementation IskraView {
    iskra::CocoaHost * _host;
    unsigned _pane;
}

- (instancetype)initWithFrame:(NSRect)frame
                          host:(iskra::CocoaHost *)host
                          pane:(unsigned)pane
{
    self = [super initWithFrame:frame];
    if (self) {
        _host = host;
        _pane = pane;
    }
    return self;
}

// Вид нарочно НЕ перевёрнутый (isFlipped остаётся NO, умолчание AppKit).
// `CGContextDrawImage` сам кладёт нулевую строку CGImage к верху
// назначенного прямоугольника в текущей системе координат — ровно то, что
// нужно для кадра, собранного сверху вниз, как знакоместа экрана. А вот
// если перевернуть вид (isFlipped → YES), AppKit переворачивает и матрицу
// холста под него, и тогда `CGContextDrawImage` переворачивает кадр
// ещё раз поверх этого — картинка выходит вверх ногами. Проверено на живой
// машине: с `isFlipped → YES` экран был перевёрнут.
- (BOOL)acceptsFirstResponder { return YES; }
- (BOOL)canBecomeKeyView { return YES; }

- (void)drawRect:(NSRect)dirtyRect
{
    (void)dirtyRect;
    CGContextRef ctx = [NSGraphicsContext currentContext].CGContext;
    const NSRect b = self.bounds;
    _host->bridge_paint(_pane, ctx, b.size.width, b.size.height);
}

- (void)keyDown:(NSEvent *)event
{
    _host->bridge_key_down((void *)event);
}

// Клавиши-модификаторы (голый Ctrl, Option…) сами по себе не разбираются:
// «Искре» они без знака не нужны, а системе они пригодятся для чего-то
// своего. Не переопределяем flagsChanged: нарочно.

- (BOOL)windowShouldClose:(NSWindow *)sender
{
    (void)sender;
    if (_pane != iskra::CocoaHost::PANE_SCREEN) return YES;
    // Экран закрывается логически, а не физически — так же, как у Win32 и
    // X11: present() заметит closed_ и остановит программу, а настоящее
    // окно закроет CocoaHost::close() при выходе.
    _host->bridge_will_close(_pane);
    return NO;
}

- (void)windowWillClose:(NSNotification *)notification
{
    (void)notification;
    // Закрытые лист графопостроителя и лента эмулятор не останавливают: это
    // бумага, а не машина. Заведутся заново по следующему выводу.
    if (_pane != iskra::CocoaHost::PANE_SCREEN) _host->bridge_will_close(_pane);
}

@end

namespace iskra {

CocoaHost::CocoaHost()
    : key_pos_(0), special_(false), control_(CK_NONE),
      closed_(false), cursor_on_(false), start_ms_(now_ms())
{
}

CocoaHost::~CocoaHost()
{
    close();
}

bool CocoaHost::open(const std::string & title_utf8, unsigned scale,
                     std::string & error)
{
    error.clear();

    // Голый исполняемый файл без .app и Info.plist не активируется сам:
    // без этого окно не становится фронтальным и клавиатура мимо него.
    [NSApplication sharedApplication];
    [NSApp setActivationPolicy:NSApplicationActivationPolicyRegular];
    setup_menu();
    [NSApp finishLaunching];
    [NSApp activateIgnoringOtherApps:YES];

    closed_ = false;
    if (!open_pane(PANE_SCREEN, title_utf8, scale)) {
        error = "не удалось создать окно";
        return false;
    }
    return true;
}

void CocoaHost::close()
{
    for (unsigned i = 0; i < PANES; ++i) close_pane(i);
}

bool CocoaHost::open_pane(unsigned idx, const std::string & title_utf8,
                          unsigned scale)
{
    Pane & p = pane_[idx];
    if (p.window) return true;

    const unsigned fw = render_.width();
    const unsigned fh = render_.height() * DOT_TALL;
    if (!scale) scale = fitting_scale(fw, fh);

    NSRect rect = NSMakeRect(0, 0, fw * scale, fh * scale);
    NSWindow * win = [[NSWindow alloc]
        initWithContentRect:rect
                  styleMask:(NSWindowStyleMaskTitled | NSWindowStyleMaskClosable |
                             NSWindowStyleMaskMiniaturizable |
                             NSWindowStyleMaskResizable)
                    backing:NSBackingStoreBuffered
                      defer:NO];
    if (!win) return false;

    win.releasedWhenClosed = YES;
    win.title = [NSString stringWithUTF8String:title_utf8.c_str()];
    // Меньше кадра окно делать бессмысленно: увеличение целое, ниже единицы
    // ему падать некуда.
    win.contentMinSize = NSMakeSize(fw, fh);

    IskraView * view = [[IskraView alloc] initWithFrame:win.contentView.bounds
                                                     host:this
                                                     pane:idx];
    view.autoresizingMask = NSViewWidthSizable | NSViewHeightSizable;
    win.contentView = view;
    win.delegate = view;
    [view release];

    p.window = win;
    p.view = view;

    [win center];
    [win makeKeyAndOrderFront:nil];
    redraw(idx);
    return true;
}

void CocoaHost::close_pane(unsigned idx)
{
    Pane & p = pane_[idx];
    if (!p.window) { p.view = 0; return; }
    NSWindow * win = (NSWindow *)p.window;
    win.delegate = nil;             // не звать windowWillClose: — чистим сами
    [win close];
    p.window = 0;
    p.view = 0;
    p.frame.clear();
}

void CocoaHost::redraw(unsigned idx)
{
    Pane & p = pane_[idx];
    if (!p.window) return;
    if (p.frame.size() != render_.pixels()) p.frame.assign(render_.pixels(), 0);

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
    mark_dirty(idx);
}

void CocoaHost::mark_dirty(unsigned idx)
{
    Pane & p = pane_[idx];
    if (!p.view) return;
    ((NSView *)p.view).needsDisplay = YES;
}

// Бумаги у нас нет, и разрешения графопостроителя мы не знаем: книга
// называет его «планшетным» и больше о нём не говорит, а размер картинки не
// задан ни в буфере, ни в его заголовке. Поэтому лист берётся в тех же
// координатах, что и буфер (docs/DECISIONS.md, разд. 15).
Raster * CocoaHost::plot_surface(uint8_t addr)
{
    if (addr == 0x14 &&
        !open_pane(PANE_PLOT, "Искра 226 — графопостроитель", 0))
        return 0;
    return Host::plot_surface(addr);
}

// АЦПУ. Знакогенератора у печати мы не знаем и взять его неоткуда, поэтому
// лента показывается теми же знакоместами, что и экран: перевод строки,
// возврат каретки и прокрутку `Screen` уже умеет, а больше печать ничего и
// не шлёт. Видно последние 24 строки; вся лента — в файле `--printer`.
void CocoaHost::print_char(uint8_t ch)
{
    tape_.put(ch);
    open_pane(PANE_PAPER, "Искра 226 — АЦПУ", 0);
    paper_.put(ch);
}

void CocoaHost::push_key(uint8_t code, bool special)
{
    keys_.push_back(code);
    keys_sf_.push_back(special ? 1 : 0);
}

// Клавиша верхнего регистра вводит целое слово Бейсика, и своего кода у
// слова нет: машина показывает его на экране, как набранное по буквам. Так
// и кладём — знаками в ту же очередь.
void CocoaHost::push_word(const char * word)
{
    if (!word) return;
    for (const char * p = word; *p; ++p)
        push_key(static_cast<uint8_t>(*p), false);
}

void CocoaHost::bridge_paint(unsigned idx, void * ctx_raw, double vw_d,
                             double vh_d)
{
    if (idx >= PANES) return;
    Pane & p = pane_[idx];
    CGContextRef ctx = (CGContextRef)ctx_raw;
    const int vw = static_cast<int>(vw_d);
    const int vh = static_cast<int>(vh_d);

    CGContextSetRGBFillColor(ctx, 0, 0, 0, 1);
    CGContextFillRect(ctx, CGRectMake(0, 0, vw_d, vh_d));

    if (p.frame.size() != render_.pixels()) return;   // ещё нечего показывать

    const int w = static_cast<int>(render_.width());
    const int h = static_cast<int>(render_.height());
    const int tall = static_cast<int>(DOT_TALL);

    // Увеличение только целое: экран знакоместный, дробное растяжение съело
    // бы ровность знаков — однопиксельные штрихи вышли бы разной толщины.
    // Растягивает здесь система (`CGContextDrawImage`), как у Win32
    // `StretchDIBits` с `COLORONCOLOR`: кадр держится в масштабе 1x1, а
    // целое увеличение — уже дело окна (docs/DECISIONS.md, разд. 14).
    int k = w > 0 ? vw / w : 1;
    const int kv = (h * tall) > 0 ? vh / (h * tall) : 1;
    if (kv < k) k = kv;
    if (k < 1) k = 1;

    const int dw = w * k, dh = h * k * tall;
    const int dx = (vw - dw) / 2, dy = (vh - dh) / 2;

    CGColorSpaceRef cs = CGColorSpaceCreateDeviceRGB();
    CGDataProviderRef prov = CGDataProviderCreateWithData(
        0, &p.frame[0], p.frame.size() * sizeof(uint32_t), 0);
    // Слово кадра — 0x00RRGGBB в порядке хоста (`host_common/renderer.h`,
    // `pack_rgb`); `kCGBitmapByteOrder32Host` + `kCGImageAlphaNoneSkipFirst`
    // читают ровно такое слово, без второго прохода за перепаковкой цвета.
    CGImageRef img = CGImageCreate(
        static_cast<size_t>(w), static_cast<size_t>(h), 8, 32,
        static_cast<size_t>(w) * 4, cs,
        kCGImageAlphaNoneSkipFirst | kCGBitmapByteOrder32Host,
        prov, 0, false, kCGRenderingIntentDefault);

    CGContextSetInterpolationQuality(ctx, kCGInterpolationNone);
    CGContextDrawImage(ctx, CGRectMake(dx, dy, dw, dh), img);

    CGImageRelease(img);
    CGDataProviderRelease(prov);
    CGColorSpaceRelease(cs);
}

void CocoaHost::bridge_will_close(unsigned pane)
{
    if (pane == PANE_SCREEN) {
        closed_ = true;
        return;
    }
    Pane & p = pane_[pane];
    p.window = 0;
    p.view = 0;
    p.frame.clear();
}

// Клавиатура «Искры» — восемь зон (руководство, разд. 2.1), и три из них на
// обычной клавиатуре взять неоткуда: 32 клавиши специальных функций, шесть
// клавиш перемещения курсора и клавиши управления машиной.
//
// Зона 8 ложится на верхний ряд один в один: Esc и двенадцать функциональных
// — тринадцать клавиш, а Shift даёт верхний банк, как и на машине. Номера
// при этом совпадают: F5 — это клавиша 5. У Mac нет PrtScr/ScrLk/Pause, и
// клавиши 13–15 держатся только на дублях Ctrl+F1…Ctrl+F3 — как у Windows
// 11, где PrtScr забирает себе система.
void CocoaHost::bridge_key_down(void * nsevent)
{
    NSEvent * ev = (NSEvent *)nsevent;
    const unsigned short kc = ev.keyCode;
    const NSEventModifierFlags mods = ev.modifierFlags;
    const bool shift = (mods & NSEventModifierFlagShift) != 0;
    const bool ctrl  = (mods & NSEventModifierFlagControl) != 0;
    const bool opt   = (mods & NSEventModifierFlagOption) != 0;
    const bool cmd   = (mods & NSEventModifierFlagCommand) != 0;

    // Клавиши управления машиной идут первыми: у них кодов нет вовсе, и
    // ложатся они в отдельный ящик. Cmd+. — обычный на Mac жест «отмена»,
    // им и заведён HALT; сброс сделан нарочно неудобным, он стирает экран.
    if (cmd && kc == KC_PERIOD) {
        control_ = opt ? CK_RESET : CK_HALT;
        return;
    }
    if (ctrl && !opt && !cmd && (kc == KC_RETURN || kc == KC_KP_ENTER)) {
        control_ = CK_CONTINUE;
        return;
    }
    if (ctrl && !opt && !cmd && kc == KC_ANSI_N) {
        control_ = CK_STMT_NUMBER;
        return;
    }

    // С Option дальше идут только слова Бейсика верхнего регистра зоны 1:
    // «нажатием каждой клавиши вводится то или иное ключевое слово Бейсика»
    // (разд. 2.1). Ключ — знак, а не положение, и берётся он через
    // charactersIgnoringModifiers: Option на Mac составляет диакритику
    // («ç», «å»…), а нам нужен знак базовой клавиши, как если бы Option не
    // было вовсе (Apple, `NSEvent.charactersIgnoringModifiers`).
    if (opt) {
        PadFunc pf = PAD_MUL;
        bool pad = true;
        switch (kc) {
        case KC_KP_MULTIPLY: pf = PAD_MUL; break;
        case KC_KP_PLUS:     pf = PAD_ADD; break;
        case KC_KP_MINUS:    pf = PAD_SUB; break;
        case KC_KP_DIVIDE:   pf = PAD_DIV; break;
        default: pad = false;
        }
        if (pad) { push_word(keyword_for_pad(pf)); return; }

        NSString * s = ev.charactersIgnoringModifiers;
        if (s.length) {
            uint8_t ch = 0;
            if (unicode_to_koi8([s characterAtIndex:0], ch))
                push_word(keyword_for_char(koi8_upper(ch)));
        }
        return;
    }

    const unsigned bank = shift ? 16 : 0;
    int sf = -1;                    // номер клавиши спецфункций, -1 — не она

    if (kc == KC_ESCAPE) {
        sf = 0;
    } else {
        const int num = fkey_number(kc);
        if (num > 0) {
            if (ctrl) { if (num <= 3) sf = num + 12; }
            else sf = num;
        }
    }

    if (sf >= 0) {
        push_key(static_cast<uint8_t>(sf + bank), true);
        return;
    }

    uint8_t code = 0;
    switch (kc) {
    case KC_LEFT:            code = ctrl ? KEY_LEFT5  : KEY_LEFT;  break;
    case KC_RIGHT:           code = ctrl ? KEY_RIGHT5 : KEY_RIGHT; break;
    case KC_UP:               code = KEY_UP;     break;
    case KC_DOWN:             code = KEY_DOWN;   break;
    case KC_HELP:             code = KEY_INSERT; break;
    case KC_FORWARD_DELETE:   code = KEY_DELETE; break;
    case KC_END:              code = KEY_ERASE;  break;
    case KC_BACKSPACE:        code = ctrl ? KEY_LINE_ERASE : KEY_BACKSPACE; break;
    case KC_RETURN: case KC_KP_ENTER: code = KEY_CR; break;
    default: break;
    }
    // Буквы берём по положению, а не по знаку: иначе Ctrl+Е отвалился бы на
    // русской раскладке. Физические позиции ЙЦУКЕН на «маковской» русской
    // раскладке те же, что у QWERTY, поэтому код клавиши общий для обеих.
    if (!code && ctrl) {
        if (kc == KC_ANSI_E) code = KEY_EDIT;
        else if (kc == KC_ANSI_R) code = KEY_RECALL;
    }
    if (code) { push_key(code, false); return; }

    // Всё прочее — знаки. Это единственный путь, которым они попадают в
    // машину: переводить их по положению «Искре» незачем, у неё своя
    // раскладка. С Ctrl знаков не бывает — там управляющие коды.
    if (ctrl) return;

    NSString * s = ev.characters;
    if (!s.length) return;
    const unichar u = [s characterAtIndex:0];
    if (u < 0x20 || u == 0x7F) return;
    uint8_t ch = 0;
    if (!unicode_to_koi8(static_cast<uint32_t>(u), ch)) return;
    // Строчных букв у машины нет ни одной: клавиша выдаёт прописную
    // (core/koi8.h, «семибитная граница машины»).
    push_key(koi8_upper(ch), false);
}

void CocoaHost::pump()
{
    NSApplication * app = [NSApplication sharedApplication];
    for (;;) {
        NSEvent * ev = [app nextEventMatchingMask:NSEventMaskAny
                                         untilDate:[NSDate distantPast]
                                            inMode:NSDefaultRunLoopMode
                                           dequeue:YES];
        if (!ev) break;
        [app sendEvent:ev];
    }
}

// Ждём события системы, но не дольше выдержки: иначе курсор замрёт, пока
// никто не трогает клавиатуру.
void CocoaHost::wait_event(unsigned ms)
{
    NSApplication * app = [NSApplication sharedApplication];
    NSDate * until = [NSDate dateWithTimeIntervalSinceNow:
                      static_cast<double>(ms) / 1000.0];
    NSEvent * ev = [app nextEventMatchingMask:NSEventMaskAny
                                     untilDate:until
                                        inMode:NSDefaultRunLoopMode
                                       dequeue:YES];
    if (ev) [app sendEvent:ev];
}

ControlKey CocoaHost::take_control_key()
{
    pump();
    const ControlKey c = control_;
    control_ = CK_NONE;
    return c;
}

bool CocoaHost::present()
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

    if (screen_.take_bells()) NSBeep();
    return true;
}

bool CocoaHost::poll_key(uint8_t & code)
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

bool CocoaHost::wait_input(uint8_t & code, ControlKey & ck)
{
    for (;;) {
        if (!present()) return false;
        ck = take_control_key();
        if (ck != CK_NONE) { code = 0; return true; }
        if (poll_key(code)) return true;
        wait_event(BLINK_MS / 4);
    }
}

uint32_t CocoaHost::ticks_ms() const
{
    return now_ms() - start_ms_;
}

} // namespace iskra
