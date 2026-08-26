// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Mikhail Revzin <p3.141592653589793238462643@gmail.com>
// Part of the Iskra-226 project: https://github.com/Ptr314/Iskra-226
// Description: хост с окном на чистом Xlib — ни одной сторонней библиотеки

#pragma once

#include <string>
#include <vector>

#include "core/host.h"
#include "host_common/disk_files.h"
#include "host_common/printer.h"
#include "host_common/renderer.h"

namespace iskra {

// Окно средствами самой системы: Xlib есть везде, где есть X11, и на
// Wayland работает через XWayland. Ни toolkit'а, ни OpenGL, ни SHM: экран
// знакоместный, кадр перерисовывается только при изменении, и XPutImage на
// 560x256 занимает доли миллисекунды.
//
// Отличие от Win32 в одном месте: **растягивает растеризатор, а не система**.
// У GDI есть StretchDIBits с COLORONCOLOR — ближайший сосед без сглаживания;
// у голого Xlib такого нет вовсе (XRender сглаживает, а тянуть за ним ещё
// одну библиотеку ради целого увеличения незачем). Поэтому здесь
// `Renderer::set_scale` держит настоящее увеличение, а кадр выкладывается
// один в один.
//
// Описатели X держим за `void *` и `unsigned long`: включать Xlib.h в
// заголовок, который увидят другие файлы, — верный способ поссориться с их
// именами. Xlib определяет макросами `Status`, `Bool`, `None` и прочее.
class X11Host : public Host
{
public:
    X11Host();
    ~X11Host();

    // Завести окно. scale — целое увеличение; точка квадратная, поэтому по
    // обеим осям оно одно и то же (`DOT_TALL` = 1). Ноль значит «подобрать
    // наибольшее, которое влезает в рабочую область экрана». Дальше окно
    // можно тянуть мышью, и увеличение подберётся под него само.
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

private:
    // Три окна: экран, лист графопостроителя и лента АЦПУ. Два последних
    // заводятся лениво, по первому выводу, и закрыть их можно — эмулятор
    // не остановится: это бумага, а не машина.
    enum { PANE_SCREEN = 0, PANE_PLOT, PANE_PAPER, PANES };

    struct Pane {
        unsigned long win;              // Window; 0 — окна нет
        void * img;                     // XImage поверх frame
        std::vector<uint32_t> frame;
        unsigned cw, ch;                // размер клиентской области, точек
        unsigned scale;                 // целое увеличение кадра
        Pane() : win(0), img(0), cw(0), ch(0), scale(1) {}
    };

    bool open_pane(unsigned idx, const std::string & title_utf8, unsigned scale);
    void close_pane(unsigned idx);
    unsigned pane_of(unsigned long win) const;

    // Перерисовать кадр из знакомест и выложить его в окно.
    void redraw(unsigned idx);
    // Только выложить уже собранный кадр: приходит по Expose.
    void flush(unsigned idx);
    // Подогнать увеличение под окно и, если надо, кадр под увеличение.
    void fit(unsigned idx);
    // Поля вокруг кадра: что не поместилось целым увеличением.
    void fill_margins(const Pane & p, int dx, int dy, int dw, int dh);

    void push_key(uint8_t code, bool special);
    void push_word(const char * word);

    // Разобрать нажатие. Знаки идут по значению клавиши (кириллица тогда
    // приходит с любой раскладки), а зона 8, правка и управление машиной —
    // по её положению.
    void key_press(void * event);
    // Первая латинская буква у клавиши: Ctrl+E обязан работать и на
    // русской раскладке, поэтому такие клавиши ищутся по положению.
    uint8_t latin_of(unsigned keycode) const;
    void read_keymap();

    void pump();                        // разобрать накопившиеся события
    void wait_event(unsigned ms);       // подождать события, но не дольше

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
    std::vector<uint8_t> keys_sf_;      // признак «клавиша спецфункций»
    std::size_t key_pos_;
    bool special_;                      // была ли ею последняя прочитанная
    ControlKey control_;                // ящик на одну клавишу управления

    void * dpy_;
    void * visual_;
    void * gc_;
    unsigned long wm_delete_;           // Atom WM_DELETE_WINDOW
    int screen_num_;
    int depth_;
    bool closed_;

    // Значения клавиш по положению: список на клавишу, как его отдаёт
    // XGetKeyboardMapping. Обновляется по MappingNotify.
    std::vector<unsigned long> keymap_;
    int key_min_;
    int key_per_;

    // Курсор мигает сам: у хоста часы, ядру про это знать незачем.
    bool cursor_on_;
    uint32_t start_ms_;

    X11Host(const X11Host &);
    X11Host & operator=(const X11Host &);
};

} // namespace iskra
