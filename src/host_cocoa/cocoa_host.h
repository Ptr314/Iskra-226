// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Mikhail Revzin <p3.141592653589793238462643@gmail.com>
// Part of the Iskra-226 project: https://github.com/Ptr314/Iskra-226
// Description: хост с окном на AppKit — ни одной сторонней библиотеки

#pragma once

#include <string>
#include <vector>

#include "core/host.h"
#include "host_common/disk_files.h"
#include "host_common/printer.h"
#include "host_common/renderer.h"

namespace iskra {

// Окно средствами самой системы: AppKit есть на любой macOS, и доставлять с
// эмулятором нечего. Экран знакоместный, кадр рисуется только при
// изменении, поэтому ускорения тут не нужно вовсе — растяжение делает сама
// Quartz (`CGContextDrawImage`), как у Win32 `StretchDIBits`: растеризатор
// держит кадр в масштабе 1x1, а целое увеличение — уже дело окна.
//
// NSWindow, NSView и NSEvent держим за void *: включать Cocoa в заголовок,
// который увидят другие файлы, — верный способ поссориться с их именами
// (`id`, `BOOL` и прочее). Objective-C живёт только в cocoa_host.mm.
class CocoaHost : public Host
{
public:
    CocoaHost();
    ~CocoaHost();

    // Завести окно. scale сейчас не используется Cocoa-хостом впрямую —
    // окно можно тянуть мышью, и увеличение подбирается под него само,
    // как и у прочих хостов; параметр оставлен ради общего main().
    bool open(const std::string & title_utf8, unsigned scale,
              std::string & error);
    void close();

    // --- Host ------------------------------------------------------------
    Screen & screen() { return screen_; }
    bool present();
    bool poll_key(uint8_t & code);
    bool key_was_special() const { return special_; }
    ControlKey take_control_key();
    bool wait_input(uint8_t & code, ControlKey & ck);

    unsigned disk_sectors(unsigned drive) const { return disks_.sectors(drive); }
    bool disk_read(unsigned drive, unsigned sector, uint8_t * buf)
    { return disks_.read(drive, sector, buf); }
    bool disk_write(unsigned drive, unsigned sector, const uint8_t * buf)
    { return disks_.write(drive, sector, buf); }

    uint32_t ticks_ms() const;

    // Печать на АЦПУ. Лента показывается своим окном и, если задан ключ
    // `--printer`, уходит ещё и в файл.
    void print_char(uint8_t ch);

    // --- Своё ------------------------------------------------------------
    DiskFiles & disks() { return disks_; }
    Printer & tape() { return tape_; }
    Renderer & renderer() { return render_; }

    // Лист графопостроителя показывается своим окном, и заводится оно
    // лениво — по первому же `¤COPY /14`. Пока программа не рисует на
    // бумагу, второго окна на экране нет.
    Raster * plot_surface(uint8_t addr);

    // --- Мост к Objective-C ------------------------------------------------
    // Вызывается из IskraView и оконного делегата (cocoa_host.mm); ядру и
    // прочим потребителям заголовка это видеть незачем.
    enum { PANE_SCREEN = 0, PANE_PLOT, PANE_PAPER, PANES };

    void bridge_key_down(void * nsevent);
    void bridge_will_close(unsigned pane);
    // ctx — CGContextRef, vw/vh — размер клиентской области вида в точках.
    void bridge_paint(unsigned pane, void * ctx, double vw, double vh);

private:
    bool open_pane(unsigned idx, const std::string & title_utf8, unsigned scale);
    void close_pane(unsigned idx);
    void redraw(unsigned idx);       // пересобрать кадр из знакомест
    void mark_dirty(unsigned idx);   // попросить систему перерисовать окно

    void push_key(uint8_t code, bool special);
    void push_word(const char * word);

    void pump();                      // разобрать накопившиеся события
    void wait_event(unsigned ms);     // подождать событие, но не дольше

    struct Pane {
        void * window;                // NSWindow *; 0 — окна нет
        void * view;                  // IskraView *
        std::vector<uint32_t> frame;  // кадр 1x1, тот же формат, что у Renderer
        Pane() : window(0), view(0) {}
    };

    Screen screen_;
    Renderer render_;
    DiskFiles disks_;

    // Лента АЦПУ — те же знакоместа: у печати свой знакогенератор нам
    // взять неоткуда, а перевод строки и прокрутку `Screen` уже умеет.
    // Видно последние 24 строки; вся лента целиком — в файле `--printer`.
    Screen paper_;
    Printer tape_;

    Pane pane_[PANES];

    std::vector<uint8_t> keys_;
    std::vector<uint8_t> keys_sf_;     // признак «клавиша спецфункций»
    std::size_t key_pos_;
    bool special_;                     // была ли ею последняя прочитанная
    ControlKey control_;               // ящик на одну клавишу управления

    bool closed_;

    // Курсор мигает сам: у хоста часы, ядру про это знать незачем.
    bool cursor_on_;
    uint32_t start_ms_;

    CocoaHost(const CocoaHost &);
    CocoaHost & operator=(const CocoaHost &);
};

} // namespace iskra
