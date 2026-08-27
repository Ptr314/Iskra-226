// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Mikhail Revzin <p3.141592653589793238462643@gmail.com>
// Part of the Iskra-226 project: https://github.com/Ptr314/Iskra-226
// Description: хост на канве браузера — четвёртый и последний

#pragma once

#include <string>
#include <vector>

#include "core/host.h"
#include "host_common/disk_files.h"
#include "host_common/printer.h"
#include "host_common/renderer.h"

namespace iskra {

// Канва браузера. От трёх готовых хостов отличается тремя вещами, и все три
// вынуждены средой, а не вкусом (`docs/DECISIONS.md`, разд. 14):
//
// 1. **Блокировать нельзя вовсе.** У окна главный цикл наш, а в браузере он
//    чужой: пока мы не вернём ему управление, страница не перерисуется и не
//    примет нажатия. Ядро при этом блокирует — `Console::read_line()` ждёт
//    клавишу, `Interp::loop()` считает, — и переписывать его конечным
//    автоматом значило бы переделать диалог и исполнитель ради одной среды.
//    Поэтому взят `-sASYNCIFY`: стек разматывается и складывается обратно
//    вокруг `emscripten_sleep()`, а ядро об этом не знает. Уступки браузеру
//    сидят ровно в двух местах — `present()` и `wait_input()`.
//
// 2. **Второго окна завести нельзя.** Лист графопостроителя и лента АЦПУ у
//    трёх прочих хостов — отдельные окна, а `window.open` посреди работы
//    программы гасит блокировщик всплывающих окон: вывод пропал бы молча.
//    Поэтому оба показываются вкладками над той же областью кадра, а
//    «окно завелось» выражается появлением вкладки.
//
// 3. **Лента показывается текстом, а не знакоместами.** У трёх хостов она
//    рисуется `Renderer::draw_paper`, и видно последние 24 строки; здесь
//    страница держит всю ленту и отдаёт её прокруткой, поиском и
//    сохранением в файл — то, что на десктопе делает ключ `--printer`.
//    Знакогенератора у печати нет и взять его неоткуда, так что своего
//    вида лента не теряет.
//
// Растягивает **система**, как у Win32 и Cocoa: кадр собирается 560x256 и
// растягивается канвой с `image-rendering: pixelated` — тот же ближайший
// сосед, что `StretchDIBits` с `COLORONCOLOR`. Поэтому `Renderer` держит
// увеличение 1x1.
class WasmHost : public Host
{
public:
    // Поверхностей две: экран и лист графопостроителя. Лента третья, но она
    // не кадр, а текст, и канвы ей не надо — поэтому её номер идёт сразу за
    // ними и в `frame_[]`/`open_[]` места не занимает.
    enum Pane { PANE_SCREEN = 0, PANE_PLOT, PANES, PANE_TAPE = PANES };

    WasmHost();

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

    // Печать на АЦПУ: лента копится целиком, а страница получает прибавку.
    void print_char(uint8_t ch);

    // Лист графопостроителя заводится лениво, по первому `¤COPY /14`, —
    // как второе окно у трёх прочих хостов.
    Raster * plot_surface(uint8_t addr);

    // --- Своё ------------------------------------------------------------
    DiskFiles & disks() { return disks_; }
    Printer & tape() { return tape_; }
    Renderer & renderer() { return render_; }

    void push_key(uint8_t code, bool special);
    // Клавиша верхнего регистра вводит целое слово Бейсика, и своего кода у
    // слова нет: машина показывает его, как набранное по буквам.
    void push_word(const char * koi8);
    void push_control(ControlKey ck);

    // «Перезагрузить». Своего цикла исполнения тут заводить нельзя, а
    // прервать чужой можно только оттуда, откуда ядро само спрашивает хост:
    // `present()` и `wait_input()` начинают отвечать «больше ничего не
    // будет», и счёт с диалогом разматываются сами. Сеанс собирает заново
    // `main`.
    void request_reset() { reset_ = true; }
    bool reset_requested() const { return reset_; }
    void begin_session();

    // Убрать вкладку: лист и лента закрываются, как окна у трёх прочих
    // хостов, и следующий вывод заводит их снова.
    void close_pane(unsigned pane);

    // Лента в UTF-8 целиком — для «Сохранить ленту».
    const std::string & tape_utf8();

private:
    // Перерисовать поверхности, которым есть что показать, и выложить кадр
    // в канву. Ничем не блокирует.
    void paint(bool force);
    void repaint(unsigned pane);
    void open_pane(unsigned pane);

    Screen screen_;
    Renderer render_;
    DiskFiles disks_;
    Printer tape_;

    // Что из ленты страница уже видела: шлём прибавку, а не всю ленту, —
    // иначе печать длиной в тысячу строк обошлась бы в квадрат.
    std::size_t tape_sent_;
    std::string tape_all_;

    std::vector<uint32_t> frame_[PANES];
    bool open_[PANES];
    bool need_[PANES];
    // Перерисовать надо и по миганию курсора, а это не вывод программы.
    // Страница разводит их: на невыбранной вкладке содержимое помечается, а
    // мигание — нет, иначе метка «тут новое» горела бы всегда.
    bool content_[PANES];

    std::vector<uint8_t> keys_;
    std::vector<uint8_t> keys_sf_;
    std::size_t key_pos_;
    bool special_;
    ControlKey control_;

    bool reset_;
    bool cursor_on_;
    double last_paint_;
    // Лента заводится так же лениво, как лист, но кадра у неё нет, и в
    // `open_[]` ей места не досталось.
    bool tape_open_;

    WasmHost(const WasmHost &);
    WasmHost & operator=(const WasmHost &);
};

} // namespace iskra
