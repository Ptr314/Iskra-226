// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Mikhail Revzin <p3.141592653589793238462643@gmail.com>
// Part of the Iskra-226 project: https://github.com/Ptr314/Iskra-226
// Description: хост с окном на чистом Win32 — ни одной сторонней библиотеки

#pragma once

#include <string>
#include <vector>

#include "core/host.h"
#include "host_common/disk_files.h"
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
    bool wait_key(uint8_t & code);

    unsigned disk_sectors(unsigned drive) const { return disks_.sectors(drive); }
    bool disk_read(unsigned drive, unsigned sector, uint8_t * buf)
    { return disks_.read(drive, sector, buf); }
    bool disk_write(unsigned drive, unsigned sector, const uint8_t * buf)
    { return disks_.write(drive, sector, buf); }

    uint32_t ticks_ms() const;

    // --- Своё ------------------------------------------------------------
    DiskFiles & disks() { return disks_; }
    Renderer & renderer() { return render_; }

    // Обработчик оконных сообщений; открыт только ради статического моста.
    long long handle(void * hwnd, unsigned msg, unsigned long long wp,
                     long long lp, bool & done);

    // Лист графопостроителя показывается своим окном, и заводится оно
    // лениво — по первому же `¤COPY /14`. Пока программа не рисует на
    // бумагу, второго окна на экране нет.
    Raster * plot_surface(uint8_t addr);

private:
    void pump();                  // разобрать накопившиеся сообщения
    void redraw();                // пересобрать кадр из знакомест
    void redraw_plot();           // пересобрать лист графопостроителя
    void paint(void * hwnd, void * hdc, const std::vector<uint32_t> & frame);
    void resize_frame();
    bool open_plotter();

    Screen screen_;
    Renderer render_;
    DiskFiles disks_;

    std::vector<uint32_t> frame_;
    std::vector<uint32_t> plot_frame_;
    std::vector<uint8_t> keys_;
    std::size_t key_pos_;

    void * hwnd_;
    void * plot_hwnd_;
    bool closed_;

    // Курсор мигает сам: у хоста часы, ядру про это знать незачем.
    bool cursor_on_;
    unsigned long start_ms_;

    Win32Host(const Win32Host &);
    Win32Host & operator=(const Win32Host &);
};

} // namespace iskra
