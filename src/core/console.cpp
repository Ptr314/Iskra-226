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
    : img_(img), host_(host), interp_(img, host),
      edit_row_(1), edit_col_(1), shown_len_(0), editing_(false)
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

// Приглашение набора стоит слева от строки: `:` в обычном режиме, `*` в
// режиме правки — «в начале строки экрана вместо сообщения : появится
// сообщение *, означающее, что система находится в режиме редактирования
// строк» (руководство, разд. 3.3).
void Console::show_mark()
{
    if (edit_col_ < 2) return;
    Screen & sc = host_.screen();
    sc.at(edit_row_, edit_col_ - 1);
    sc.put(editing_ ? '*' : ':');
}

// Набранное выводится целиком при каждом нажатии: строка живёт на экране, и
// вставка со сдвигом иначе не покажется. Хвост прошлой отрисовки затирается
// пробелами, а начало строки едет вверх, если экран прокрутился.
void Console::render(const std::string & buf, unsigned cur)
{
    Screen & sc = host_.screen();
    const unsigned len = static_cast<unsigned>(buf.size());
    const unsigned pad = shown_len_ > len ? shown_len_ - len : 0;
    const unsigned total = len + pad;

    show_mark();
    sc.at(edit_row_, edit_col_);
    for (unsigned i = 0; i < len; ++i) sc.put(static_cast<uint8_t>(buf[i]));
    for (unsigned i = 0; i < pad; ++i) sc.put(' ');
    shown_len_ = len;

    // Сколько строк проехали от начала набора — столько же надо отнять от
    // строки, где курсор оказался. Если экран прокручивался, начало уехало
    // вверх ровно на это число.
    const unsigned advanced = edit_col_ - 1 + total;
    const unsigned rows = advanced / SCREEN_COLS;
    edit_row_ = sc.row() > rows ? sc.row() - rows : 1;

    const unsigned at = edit_col_ - 1 + cur;
    sc.at(edit_row_ + at / SCREEN_COLS, at % SCREEN_COLS + 1);
}

bool Console::line_text(unsigned number, std::string & out) const
{
    unsigned idx = 0;
    if (!img_.find(number, idx)) return false;
    std::string err;
    return detokenize_line(img_.line(idx), names_, out, err);
}

// Что делать после прогона: остановленную программу можно продолжить, и
// диалог обязан сказать, что она стоит, а не кончилась.
void Console::after_run(bool ok, const std::string & error)
{
    if (!ok) {
        // Код есть только у ошибки машины; у ограничения эмулятора его нет,
        // и выдумывать его нельзя.
        if (!interp_.error_code().empty())
            report("ERR " + interp_.error_code());
        report(error);
        return;
    }
    if (interp_.stop_reason() == Interp::SR_HALT)
        report("ОСТАНОВ В СТРОКЕ " + dec(interp_.current_line()));
}

// Клавиши управления машиной во время набора. Кодов у них нет вовсе, идут
// они отдельным ящиком (`core/keys.h`), и набранное переживает нажатие —
// кроме сброса, который стирает экран.
bool Console::control(ControlKey ck, std::string & buf, unsigned & cur)
{
    std::string error;
    switch (ck) {
    case CK_STMT_NUMBER: {
        // «Автоматически набирается номер строки на 10 больше максимального
        // номера строки программы, содержащейся в памяти» (разд. 3.2).
        unsigned max = 0;
        for (unsigned i = 0; i < img_.line_count(); ++i)
            if (img_.line(i).number > max) max = img_.line(i).number;
        const std::string num = dec(max + 10);
        buf.insert(cur, num);
        cur += static_cast<unsigned>(num.size());
        return true;
    }

    case CK_CONTINUE:
        if (!interp_.can_continue()) { report("продолжать нечего"); return false; }
        newline();
        after_run(interp_.resume(error), error);
        return false;

    case CK_HALT:
        // Программа не идёт — значит это вторая половина клавиши, шаг.
        if (!interp_.can_continue()) return true;
        newline();
        after_run(interp_.step(error), error);
        return false;

    case CK_RESET:
        // «Прекращение выполнения машиной операций и очистки экрана»
        // (разд. 2.1). Продолжать после неё нельзя (разд. 11.1).
        interp_.forget_continue();
        host_.screen().put(CC_CLEAR);
        return false;

    default:
        return true;
    }
}

bool Console::read_line(std::string & out)
{
    out.clear();
    std::string buf;
    unsigned cur = 0;

    editing_ = false;
    shown_len_ = 0;
    edit_row_ = host_.screen().row();
    edit_col_ = host_.screen().col();

    // Приглашение должно быть видно до того, как хост начнёт ждать.
    host_.present();
    for (;;) {
        uint8_t code = 0;
        ControlKey ck = CK_NONE;
        if (!host_.wait_input(code, ck)) return false;

        if (ck != CK_NONE) {
            if (control(ck, buf, cur)) { render(buf, cur); continue; }
            return true;                  // набор начнётся заново, с приглашения
        }

        // Клавиша специальных функций: спрашивать хост надо сразу, до
        // разбора кода. Код у неё — её же номер, 0…31.
        if (host_.key_was_special()) {
            std::string text;
            if (interp_.sf_text(code, text)) {
                // «Символы … станут частью вводимой строки текста»
                // (разд. 10.6): это не переход к подпрограмме.
                buf.insert(cur, text);
                cur += static_cast<unsigned>(text.size());
                render(buf, cur);
                continue;
            }
            if (interp_.sf_armed()) {
                newline();
                std::string err;
                after_run(interp_.sf_call(code, err), err);
                return true;
            }
            // «Нажатие клавиши специальных функций, не определённой
            // оператором DEFFN', вызовет индикацию соответствующей ошибки»
            // (разд. 10.6). Набранное при этом не теряется.
            host_.screen().put(CC_BELL);
            render(buf, cur);
            continue;
        }

        if (code == KEY_CR) break;

        switch (code) {
        case KEY_EDIT:
            editing_ = true;
            break;

        case KEY_LINE_ERASE:
            // «Стирает всю редактируемую строку. При этом символ * в начале
            // строки заменяется на :, т. е. система выходит из режима
            // редактирования» (разд. 3.3).
            buf.clear();
            cur = 0;
            editing_ = false;
            break;

        case KEY_BACKSPACE:
            // В обычном наборе забой стирает знак, в режиме правки — только
            // двигает курсор влево (разд. 3.3).
            if (editing_) { if (cur) --cur; }
            else if (cur) { buf.erase(--cur, 1); }
            break;

        case KEY_LEFT:   if (cur) --cur; break;
        case KEY_LEFT5:  cur = cur > 5 ? cur - 5 : 0; break;
        case KEY_RIGHT:  if (cur < buf.size()) ++cur; break;
        case KEY_RIGHT5:
            cur += 5;
            if (cur > buf.size()) cur = static_cast<unsigned>(buf.size());
            break;
        case KEY_UP:
            cur = cur > SCREEN_COLS ? cur - SCREEN_COLS : 0;
            break;
        case KEY_DOWN:
            cur += SCREEN_COLS;
            if (cur > buf.size()) cur = static_cast<unsigned>(buf.size());
            break;

        case KEY_INSERT:
            // «Вставить пробел в текущую позицию строки, сдвинув вправо
            // правую часть строки» (разд. 3.3).
            buf.insert(cur, 1, ' ');
            break;

        case KEY_DELETE:
            if (cur < buf.size()) buf.erase(cur, 1);
            break;

        case KEY_ERASE:
            buf.resize(cur);
            break;

        case KEY_RECALL: {
            // Три случая: пусто — последняя введённая строка; один номер —
            // строка программы; хвост «:<номер>» — присоединение строки, и
            // её собственный номер при этом отбрасывается (разд. 3.3).
            std::string text;
            std::string::size_type colon = buf.find_last_of(':');
            unsigned tail = colon == std::string::npos ? 0
                                                       : static_cast<unsigned>(colon + 1);
            bool digits = tail < buf.size();
            for (unsigned i = tail; i < buf.size() && digits; ++i)
                digits = is_digit(buf[i]);

            if (buf.empty()) {
                buf = last_line_;
            } else if (digits && colon == std::string::npos) {
                unsigned p = 0, n = 0;
                take_number(buf, p, n);
                if (!line_text(n, text)) { host_.screen().put(CC_BELL); break; }
                buf = text;
            } else if (digits) {
                unsigned p = tail, n = 0;
                take_number(buf, p, n);
                if (!line_text(n, text)) { host_.screen().put(CC_BELL); break; }
                // Номер присоединяемой строки в тексте не нужен: за
                // двоеточием идут её операторы.
                unsigned q = 0;
                take_number(text, q, n);
                buf.resize(tail);
                buf += text.substr(q);
            } else {
                host_.screen().put(CC_BELL);
                break;
            }
            cur = static_cast<unsigned>(buf.size());
            break;
        }

        default:
            // Всё прочее из диапазона `80`–`BF` машина тоже считает
            // не-символом (`EDITOR` 2520), но что там за клавиши, корпус не
            // показывает. Молчаливо пропускаем: набирать ими нечего.
            if (is_edit_key(code)) break;
            if (code < 0x20) break;
            // Набор поверх: курсор стоит под знаком, и знак заменяется.
            // В обычном наборе курсор всегда в конце, и выходит дописывание.
            if (cur < buf.size()) buf[cur] = static_cast<char>(code);
            else buf += static_cast<char>(code);
            ++cur;
            break;
        }
        render(buf, cur);
    }

    render(buf, static_cast<unsigned>(buf.size()));
    newline();
    out = buf;
    if (!buf.empty()) last_line_ = buf;
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
    after_run(has_line ? interp_.run_from(n, error) : interp_.run(error), error);
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
        interp_.forget_continue();
        interp_.forget_sf();
        return true;
    }
    if (take_word(tail, p, "V")) {
        if (!at_end(tail, p)) return false;
        interp_.vars().clear();
        interp_.forget_continue();
        return true;
    }
    if (take_word(tail, p, "N")) {
        if (!at_end(tail, p)) return false;
        interp_.vars().clear_non_common();
        interp_.forget_continue();
        return true;
    }
    if (!at_end(tail, p)) return false;

    // «Экран и память машины очистятся, и на экране снова появится сообщение
    // о готовности» (разд. 3.2). Заодно сбрасывается и выбор устройств.
    img_.clear();
    names_ = NameTable();
    interp_.forget_continue();
    interp_.forget_sf();
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
            interp_.forget_continue();
            interp_.forget_sf();
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
        // «Иногда дальнейшее выполнение программы не может быть продолжено
        // из-за того, что … изменён текст программы» (разд. 11.1). По той же
        // причине перестают работать и клавиши спецфункций (разд. 10.5).
        interp_.forget_continue();
        interp_.forget_sf();
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
    after_run(interp_.execute(body.empty() ? 0 : &body[0],
                              static_cast<unsigned>(body.size()), error), error);
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
