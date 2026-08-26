// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Mikhail Revzin <p3.141592653589793238462643@gmail.com>
// Part of the Iskra-226 project: https://github.com/Ptr314/Iskra-226
// Description: хост с окном на чистом Win32 — ни одной сторонней библиотеки

#pragma once

#include <string>
#include <vector>

#include "core/host.h"
#include "host_common/disk_files.h"
#include "host_common/printer.h"
#include "host_common/renderer.h"

namespace iskra {

// Окно средствами самой системы: user32 и gdi32 есть на любой Windows от XP
// до 11, и доставлять с эмулятором нечего. Экран знакоместный, кадр рисуется
// только при изменении, поэтому ускорения тут не нужно вовсе — хватает
// StretchDIBits.
//
// HWND и прочие описатели держим за void *: включать windows.h в заголовок,
// который увидят другие файлы, — верный способ поссориться с их именами.
class Win32Host : public Host
{
public:
    Win32Host();
    ~Win32Host();

    // Завести окно. scale — целое увеличение; точка квадратная, поэтому по
    // обеим осям оно одно и то же (`DOT_TALL` = 1). Ноль значит «подобрать
    // наибольшее, которое влезает в экран». Дальше окно можно тянуть мышью,
    // и увеличение подберётся под него само.
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

    // Обработчик оконных сообщений; открыт только ради статического моста.
    long long handle(void * hwnd, unsigned msg, unsigned long long wp,
                     long long lp, bool & done);

    // Лист графопостроителя показывается своим окном, и заводится оно
    // лениво — по первому же `¤COPY /14`. Пока программа не рисует на
    // бумагу, второго окна на экране нет.
    Raster * plot_surface(uint8_t addr);

private:
    // Лента заводится так же лениво — по первому напечатанному знаку.
    bool open_paper();
    void redraw_paper();

    // Нажатие по положению клавиши: зона 8, редактирование и управление
    // машиной. Знаки идут другим путём, через WM_CHAR. true — клавиша наша,
    // и системе её отдавать не надо.
    bool key_down(unsigned vk);

    void push_key(uint8_t code, bool special);
    void push_word(const char * word);

    void pump();                  // разобрать накопившиеся сообщения
    void redraw();                // пересобрать кадр из знакомест
    void redraw_plot();           // пересобрать лист графопостроителя
    void paint(void * hwnd, void * hdc, const std::vector<uint32_t> & frame);
    void resize_frame();
    bool open_plotter();

    Screen screen_;
    Renderer render_;
    DiskFiles disks_;

    // Лента АЦПУ — те же знакоместа: у печати свой знакогенератор нам
    // взять неоткуда, а перевод строки и прокрутку Screen уже умеет.
    // Видно последние 24 строки; вся лента целиком — в файле `--printer`.
    Screen paper_;
    Printer tape_;

    std::vector<uint32_t> frame_;
    std::vector<uint32_t> plot_frame_;
    std::vector<uint32_t> paper_frame_;
    std::vector<uint8_t> keys_;
    std::vector<uint8_t> keys_sf_;   // признак «клавиша спецфункций»
    std::size_t key_pos_;
    bool special_;                   // была ли ею последняя прочитанная
    ControlKey control_;             // ящик на одну клавишу управления

    void * hwnd_;
    void * plot_hwnd_;
    void * paper_hwnd_;
    bool closed_;

    // Курсор мигает сам: у хоста часы, ядру про это знать незачем.
    bool cursor_on_;
    unsigned long start_ms_;

    Win32Host(const Win32Host &);
    Win32Host & operator=(const Win32Host &);
};

} // namespace iskra
