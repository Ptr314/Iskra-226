// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Mikhail Revzin <p3.141592653589793238462643@gmail.com>
// Part of the Iskra-226 project: https://github.com/Ptr314/Iskra-226
// Description: диалоговый режим — приглашение, ввод строк, RUN, LIST, CLEAR

#include "core/console.h"

#include <cstdio>

#include "core/detokenize.h"
#include "core/koi8.h"
#include "core/tokenize.h"

namespace iskra {

namespace {

// Кадр покадрового просмотра: «на экран сначала выдаются первый кадр (в 23
// строки экрана) … и сообщение „:“» (руководство, разд. 3.4).
const unsigned PAGE_ROWS = 23;

std::string dec(unsigned v)
{
    char b[16];
    std::sprintf(b, "%u", v);
    return b;
}

bool is_digit(char c) { return c >= '0' && c <= '9'; }

void skip_spaces(const std::string & s, unsigned & p)
{
    while (p < s.size() && s[p] == ' ') ++p;
}

// Ключевое слово в начале строки. За ним не должно идти буквы или цифры —
// иначе `LISTV` разберётся как `LIST`, а `RUNNER` как `RUN`.
bool take_word(const std::string & s, unsigned & p, const char * word)
{
    unsigned q = p;
    skip_spaces(s, q);
    unsigned k = 0;
    while (word[k] && q < s.size() && s[q] == word[k]) { ++q; ++k; }
    if (word[k]) return false;
    if (q < s.size() && ((s[q] >= 'A' && s[q] <= 'Z') || is_digit(s[q])))
        return false;
    p = q;
    return true;
}

bool take_number(const std::string & s, unsigned & p, unsigned & out)
{
    unsigned q = p;
    skip_spaces(s, q);
    if (q >= s.size() || !is_digit(s[q])) return false;
    out = 0;
    while (q < s.size() && is_digit(s[q])) out = out * 10 + (s[q++] - '0');
    p = q;
    return true;
}

bool at_end(const std::string & s, unsigned p)
{
    skip_spaces(s, p);
    return p >= s.size();
}

// Диапазон номеров: «[<номер 1>][,[<номер 2>]]». Ноль значит «без границы».
// Такой хвост общий у LIST и у CLEAR P.
bool take_range(const std::string & s, unsigned & p, unsigned & from,
                unsigned & to)
{
    from = 0;
    to = 0;
    unsigned n = 0;
    if (take_number(s, p, n)) from = n;
    skip_spaces(s, p);
    if (p < s.size() && s[p] == ',') {
        ++p;
        if (take_number(s, p, n)) to = n;
    } else if (from) {
        // Один номер без запятой книгой не описан. Принято «с него и до
        // конца» — как у формы с запятой; см. CLAUDE.md, «Допущения».
        to = 0;
    }
    return at_end(s, p);
}

} // namespace

Console::Console(ProgramImage & img, Host & host)
    : img_(img), host_(host), interp_(img, host)
{
    // Если образ пришёл с диска, имён у него нет вовсе — их придумывает
    // обратная трансляция. Делаем это сразу, чтобы LIST и правка строк
    // говорили об одних и тех же переменных.
    std::string text, error;
    detokenize(img_, names_, text, error);
}

void Console::emit(const std::string & koi8)
{
    if (koi8.empty()) return;
    host_.screen().write(reinterpret_cast<const uint8_t *>(koi8.data()),
                         static_cast<unsigned>(koi8.size()));
}

void Console::newline()
{
    host_.screen().put(CC_CR);
    host_.screen().put(CC_DOWN);
}

void Console::prompt()
{
    if (host_.screen().col() != 1) newline();
    emit(":");
}

// Сообщения эмулятора набраны в UTF-8, как и весь исходный текст, а экран
// живёт в КОИ-8 — перекодировка нужна ровно здесь, на границе.
void Console::report(const std::string & message)
{
    std::string koi8;
    utf8_to_koi8(message, koi8);
    if (host_.screen().col() != 1) newline();
    emit(koi8);
    newline();
}

void Console::banner()
{
    host_.screen().put(CC_CLEAR);
    emit("READY BASIC 02 05.10.84");
    newline();
}

bool Console::read_line(std::string & out)
{
    out.clear();
    // Приглашение должно быть видно до того, как хост начнёт ждать.
    host_.present();
    for (;;) {
        uint8_t code = 0;
        if (!host_.wait_key(code)) return false;
        if (code == 0x0D || code == 0x0A) break;      // ВК — конец набора
        if (code == 0x08) {                           // ВШ — забой
            if (!out.empty()) {
                out.resize(out.size() - 1);
                host_.screen().put(CC_LEFT);
                host_.screen().put(0x20);
                host_.screen().put(CC_LEFT);
            }
            continue;
        }
        out += static_cast<char>(code);
        host_.screen().put(code);
    }
    newline();
    return true;
}

bool Console::list_lines(unsigned from, unsigned to, bool paged)
{
    unsigned shown = 0;
    for (unsigned i = 0; i < img_.line_count(); ++i) {
        const unsigned num = img_.line(i).number;
        if (from && num < from) continue;
        if (to && num > to) continue;

        std::string text, err;
        if (!detokenize_line(img_.line(i), names_, text, err)) {
            // Строку, которую обратная трансляция ещё не умеет, не прячем:
            // видно и номер, и причину. Это ограничение эмулятора, а не
            // поведение машины.
            std::string why;
            utf8_to_koi8(err, why);
            emit(dec(num) + " ??? " + why);
        } else {
            emit(text);
        }
        newline();

        if (paged && ++shown % PAGE_ROWS == 0) {
            // «После этого для просмотра каждого следующего кадра следует
            // нажимать на клавишу CR/LF» (разд. 3.4).
            emit(":");
            std::string ignored;
            // Нажатий больше нет — досматривать некому, выдача обрывается.
            if (!read_line(ignored)) return true;
        }
    }
    return true;
}

bool Console::do_list(const std::string & tail)
{
    unsigned p = 0;
    const bool paged = take_word(tail, p, "S");
    unsigned from = 0, to = 0;
    if (!take_range(tail, p, from, to)) return false;
    return list_lines(from, to, paged);
}

bool Console::do_run(const std::string & tail)
{
    unsigned p = 0, n = 0;
    const bool has_line = take_number(tail, p, n);
    if (!at_end(tail, p)) return false;

    std::string error;
    const bool ok = has_line ? interp_.run_from(n, error) : interp_.run(error);
    if (!ok) {
        // Код есть только у ошибки машины; у ограничения эмулятора его нет,
        // и выдумывать его нельзя.
        if (!interp_.error_code().empty())
            report("ERR " + interp_.error_code());
        report(error);
    }
    return true;
}

// CLEAR — всё; CLEAR P [n1][,n2] — только текст; CLEAR V — все переменные;
// CLEAR N — только необщие (руководство, разд. 8.3 и 19.1).
bool Console::do_clear(const std::string & tail)
{
    unsigned p = 0;
    if (take_word(tail, p, "P")) {
        unsigned from = 0, to = 0;
        if (!take_range(tail, p, from, to)) return false;
        img_.erase_range(from, to);
        sync_vars();
        return true;
    }
    if (take_word(tail, p, "V")) {
        if (!at_end(tail, p)) return false;
        interp_.vars().clear();
        return true;
    }
    if (take_word(tail, p, "N")) {
        if (!at_end(tail, p)) return false;
        interp_.vars().clear_non_common();
        return true;
    }
    if (!at_end(tail, p)) return false;

    // «Экран и память машины очистятся, и на экране снова появится сообщение
    // о готовности» (разд. 3.2). Заодно сбрасывается и выбор устройств.
    img_.clear();
    names_ = NameTable();
    interp_.clear_all();
    interp_.clear_devices();
    banner();
    return true;
}

bool Console::command(const std::string & koi8, bool & handled)
{
    unsigned p = 0;
    handled = true;
    if (take_word(koi8, p, "LIST"))  { if (do_list(koi8.substr(p))) return true; }
    else if (take_word(koi8, p, "RUN"))   { if (do_run(koi8.substr(p))) return true; }
    else if (take_word(koi8, p, "CLEAR")) { if (do_clear(koi8.substr(p))) return true; }
    // Слово наше, а хвост нет: `LIST DC`, `LIST V`, `CLEAR ALL`. Пусть об
    // этом говорит транслятор — у него и разбор, и честное сообщение.
    handled = false;
    return true;
}

void Console::sync_vars()
{
    img_.vars() = names_.vars();
    img_.rebuild_tables();
}

// Программу мог сменить LOAD DC: индексы переменных стали чужими, а имён под
// них нет вовсе — в потоке имён не бывает. Признак подмены — рассогласование
// числа имён с числом переменных образа; в остальное время оно держится
// равным через sync_vars().
void Console::refresh_names()
{
    if (names_.count() == img_.vars().size()) return;
    names_ = NameTable();
    std::string text, error;
    detokenize(img_, names_, text, error);
}

bool Console::line(const std::string & koi8)
{
    refresh_names();

    unsigned p = 0;
    skip_spaces(koi8, p);
    if (p >= koi8.size()) return true;

    // Строка с номером правит текст программы: строка с тем же номером
    // заменяется, один номер без операторов её удаляет (разд. 3.3).
    if (is_digit(koi8[p])) {
        unsigned q = p, num = 0;
        take_number(koi8, q, num);
        if (at_end(koi8, q)) {
            if (!img_.erase_line(num)) report("нет строки " + dec(num));
            return true;
        }
        const unsigned mark = names_.count();
        unsigned number = 0;
        std::vector<uint8_t> body;
        std::string err;
        if (!tokenize_line(koi8, names_, number, body, err)) {
            // Имена, набранные при разборе того, что разобрать не удалось, —
            // выдумка: они сдвинули бы индексы всех дальнейших переменных.
            names_.truncate(mark);
            report(err);
            return true;
        }
        img_.put_line(number, body.empty() ? 0 : &body[0],
                      static_cast<unsigned>(body.size()));
        sync_vars();
        return true;
    }

    bool handled = false;
    if (!command(koi8, handled)) return false;
    if (handled) return true;

    // Иначе это оператор режима непосредственного счёта. Транслятор ждёт
    // номер строки — подставляем нулевой; в программу такая строка не идёт.
    const unsigned mark = names_.count();
    unsigned number = 0;
    std::vector<uint8_t> body;
    std::string err;
    if (!tokenize_line("0 " + koi8, names_, number, body, err)) {
        names_.truncate(mark);
        report(err);
        return true;
    }
    sync_vars();

    std::string error;
    if (!interp_.execute(body.empty() ? 0 : &body[0],
                         static_cast<unsigned>(body.size()), error)) {
        if (!interp_.error_code().empty())
            report("ERR " + interp_.error_code());
        report(error);
    }
    return true;
}

bool Console::run(std::string & error)
{
    error.clear();
    banner();
    for (;;) {
        prompt();
        std::string text;
        if (!read_line(text)) break;          // нажатий больше не будет
        if (!line(text)) { error = "сбой хоста"; return false; }
        host_.present();
    }
    host_.present();
    return true;
}

} // namespace iskra
