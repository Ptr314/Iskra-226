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

    // Ввод строки с правкой — зоны 6 и 7 клавиатуры (руководство, разд. 3.3).
    // false — нажатий больше не будет.
    bool read_line(std::string & out);

    // Перерисовать набираемую строку и поставить курсор на cur-й знак.
    void render(const std::string & buf, unsigned cur);

    // Приглашение набора: `:` в обычном режиме, `*` в режиме правки
    // (руководство, разд. 3.3).
    void show_mark();

    // Текст строки программы для RECALL; false — такой строки нет.
    bool line_text(unsigned number, std::string & out) const;

    // Клавиша управления машиной, нажатая во время набора. Возвращает
    // false, если после неё набор надо начать заново.
    bool control(ControlKey ck, std::string & buf, unsigned & cur);

    // Останов и продолжение счёта — клавиши HALT/STEP и CONTINUE.
    void after_run(bool ok, const std::string & error);

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

    // Состояние набираемой строки: где она начинается на экране, сколько
    // знаков нарисовано и в каком режиме идёт набор.
    unsigned edit_row_;
    unsigned edit_col_;
    unsigned shown_len_;
    bool editing_;

    // «Последняя введённая строка … может быть вновь вызвана на экран для
    // редактирования и повторного ввода» (разд. 3.3).
    std::string last_line_;
};

} // namespace iskra
