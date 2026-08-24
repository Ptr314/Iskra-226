// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Mikhail Revzin <p3.141592653589793238462643@gmail.com>
// Part of the Iskra-226 project: https://github.com/Ptr314/Iskra-226
// Description: диалоговый режим — приглашение, ввод строк, RUN, LIST, CLEAR

#pragma once

#include <string>

#include "core/host.h"
#include "core/interp.h"
#include "core/names.h"
#include "core/program.h"

namespace iskra {

// «Ввод программных строк в машину может быть осуществлён только после
// сообщения машины „:“ в начале строки дисплея» (руководство, разд. 3.2).
// Набранная строка с номером правит текст программы, без номера —
// исполняется немедленно (режим непосредственного счёта).
//
// Состояние между командами живёт в Interp: переменные, таблица устройств,
// открытые файлы. Поэтому исполнитель тут один на весь сеанс, а не по
// одному на команду.
class Console
{
public:
    Console(ProgramImage & img, Host & host);

    // Ведёт диалог, пока хост отдаёт нажатия. false — только на беде хоста;
    // ошибки самой программы печатаются, и диалог продолжается, как на машине.
    bool run(std::string & error);

    // Одна набранная строка — для проверок и для будущего окна, которое будет
    // отдавать строки по мере набора. false — беда хоста, не ошибка строки.
    bool line(const std::string & koi8);

    // Заставка: «READY BASIC 02 05.10.84» (руководство, разд. 3.2).
    void banner();

    // Таблица имён живёт весь сеанс: индексы переменных раздаются по первому
    // появлению имени, и правка одной строки не должна их сдвигать.
    NameTable & names() { return names_; }

    Interp & interp() { return interp_; }

private:
    void emit(const std::string & koi8);
    void newline();
    void prompt();
    void report(const std::string & message);

    // Ввод строки с эхом и забоем. false — нажатий больше не будет.
    bool read_line(std::string & out);

    // Команды режима непосредственного счёта. false значит «слово не наше»
    // либо «хвост непонятен» — строка уходит транслятору, и об ошибке
    // скажет он, а не мы.
    bool command(const std::string & koi8, bool & handled);
    bool do_list(const std::string & tail);
    bool do_run(const std::string & tail);
    bool do_clear(const std::string & tail);

    // Выдача текста программы; paged — параметр S, «в 23 строки экрана».
    bool list_lines(unsigned from, unsigned to, bool paged);

    // Пересобрать таблицы переменных после правки текста: SAVE DC пишет на
    // диск именно их.
    void sync_vars();
    // Раздать имена заново, если программу сменил LOAD DC.
    void refresh_names();

    ProgramImage & img_;
    Host & host_;
    NameTable names_;
    Interp interp_;
};

} // namespace iskra
