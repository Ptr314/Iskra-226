// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Mikhail Revzin <p3.141592653589793238462643@gmail.com>
// Part of the Iskra-226 project: https://github.com/Ptr314/Iskra-226
// Description: точка входа хоста на канве браузера и вся его граница со страницей

#include <emscripten/emscripten.h>

#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

#include "core/console.h"
#include "core/detokenize.h"
#include "core/keys.h"
#include "core/koi8.h"
#include "core/program.h"
#include "core/tokenize.h"
#include "core/version.h"
#include "host_common/keywords.h"
#include "host_wasm/wasm_host.h"

namespace {

// Хост живёт весь сеанс работы страницы: дискеты вставлены, лента копится,
// очередь нажатий цела. Перезагрузка машины пересобирает не его, а то, что
// над ним, — образ программы и диалог.
iskra::WasmHost * g_host = 0;

// Текст программы, который надо положить в память при следующем запуске.
// «Загрузить программу» — это перезагрузка с листингом, ровно как ключ
// `--text` у трёх прочих хостов: подменять образ программы под работающим
// исполнителем нельзя, а спрашивать человека «остановите сперва счёт»
// значит перекладывать на него нашу заботу.
std::string g_pending_listing;

// Текст программы для «Сохранить программу»: листинг в UTF-8 и причина, по
// которой его не вышло собрать. Держатся до следующего запроса — страница
// читает их сразу же, пока стек ядра разложен.
std::string g_program_text;
std::string g_program_error;

// Ключ `-i`: пропускать то, чего здесь нет и не будет (`ASMB`, `$GIO`,
// вывод на устройство, которого у хоста нет). Командной строки у страницы
// нет, поэтому ключ приходит из адреса — `?i=1`.
bool g_skip_machine = false;

// Сеанс: образ программы и диалог над ним. Пересобирается «Перезагрузить».
struct Session {
    iskra::ProgramImage img;
    iskra::Console console;
    Session(iskra::WasmHost & host) : console(img, host) {}
};

Session * g_session = 0;

// Отчёт о беде — в консоль браузера: своего места на экране «Искры» для
// него нет, а молча ронять нельзя.
EM_JS(void, js_fail, (const char * utf8), {
    if (typeof console !== 'undefined') console.error(UTF8ToString(utf8));
});

// То же, но человеку: беда, которую он сам себе устроил, выбрав не тот файл.
// В консоль браузера он не смотрит, а машина после неудачной трансляции
// просто окажется пустой — и почему, узнать будет неоткуда.
EM_JS(void, js_note, (const char * utf8), {
    if (typeof Iskra !== 'undefined' && Iskra.note) Iskra.note(UTF8ToString(utf8));
});

EM_JS(void, js_ready, (const char * version), {
    if (typeof Iskra === 'undefined') return;
    Iskra.ready(UTF8ToString(version));
});

// Положить листинг в память до приглашения — как если бы его набрали с
// клавиатуры. Дальше человек говорит `RUN`.
void load_listing(Session & s, const std::string & utf8)
{
    std::string koi8;
    iskra::utf8_to_koi8(utf8, koi8);

    std::string error;
    if (!iskra::tokenize(koi8, s.img, s.console.names(), error)) {
        const std::string msg = "трансляция: " + error;
        js_fail(msg.c_str());
        js_note(msg.c_str());
    }
}

} // namespace

// --- то, что страница зовёт у нас -------------------------------------------
//
// Все эти вызовы приходят, пока стек ядра разложен `-sASYNCIFY`, то есть
// снаружи его исполнения. Опасного в этом ничего нет: остановка стека
// случается ровно в двух местах, `present()` и `wait_input()`, а там ни
// диск, ни образ программы не тронуты. Единственное, что могло бы помешать
// исполнителю, — подмена текста программы, и она нарочно отложена до
// перезагрузки.

extern "C" {

EMSCRIPTEN_KEEPALIVE
void iskra_key(int code, int special)
{
    if (g_host) g_host->push_key(static_cast<uint8_t>(code & 0xFF), special != 0);
}

// Слово Бейсика верхнего регистра зоны 1: своего кода у него нет, машина
// показывает его знак за знаком.
EMSCRIPTEN_KEEPALIVE
void iskra_word(const char * utf8)
{
    if (!g_host || !utf8) return;
    std::string koi8;
    iskra::utf8_to_koi8(utf8, koi8);
    g_host->push_word(koi8.c_str());
}

// Клавиша управления машиной: HALT/STEP, CONTINUE, RESET, STMT NUMBER.
// Кодов у них нет вовсе, и программе они не достаются.
EMSCRIPTEN_KEEPALIVE
void iskra_control(int ck)
{
    if (g_host) g_host->push_control(static_cast<iskra::ControlKey>(ck));
}

EMSCRIPTEN_KEEPALIVE
void iskra_reset(void)
{
    g_pending_listing.clear();
    if (g_host) g_host->request_reset();
}

EMSCRIPTEN_KEEPALIVE
void iskra_load_program(const char * utf8)
{
    if (!g_host || !utf8) return;
    g_pending_listing = utf8;
    g_host->request_reset();
}

// Текст программы, лежащей в памяти, — то же, что показал бы `LIST`, только
// целиком и в UTF-8. Имён переменных в потоке нет вовсе, их придумывает
// обратная трансляция, и берётся тут **таблица имён сеанса** — та самая, по
// которой говорит `LIST` на экране: иначе сохранённый листинг называл бы
// переменные не так, как их только что видел человек.
//
// Пустая строка значит неудачу, и причина её лежит в iskra_program_error().
// Строку, которой обратная трансляция ещё не умеет, пропустить нельзя: файл
// разошёлся бы с программой в памяти незаметно, а загрузить его обратно уже
// не вышло бы вовсе.
EMSCRIPTEN_KEEPALIVE
const char * iskra_program_text(void)
{
    g_program_text.clear();
    g_program_error.clear();

    if (!g_session) {
        g_program_error = "машина ещё не запущена";
        return "";
    }

    const iskra::ProgramImage & img = g_session->img;
    std::string koi8;
    for (unsigned i = 0; i < img.line_count(); ++i) {
        std::string text, err;
        if (!iskra::detokenize_line(img.line(i), g_session->console.names(),
                                    text, err)) {
            char num[16];
            std::sprintf(num, "%u", img.line(i).number);
            g_program_error = "строка " + std::string(num) + ": " + err;
            g_program_text.clear();
            return "";
        }
        koi8 += text;
        // Конец строки — CRLF: листинг уходит в файл, и открывать его будут
        // чем попало, в том числе блокнотом Windows.
        koi8 += "\r\n";
    }

    iskra::koi8_to_utf8(reinterpret_cast<const uint8_t *>(koi8.data()),
                        static_cast<unsigned>(koi8.size()), g_program_text);
    return g_program_text.c_str();
}

EMSCRIPTEN_KEEPALIVE
const char * iskra_program_error(void)
{
    return g_program_error.c_str();
}

// Сколько строк в программе. «Сохранить» на пустой машине выдало бы пустой
// файл, и человек узнал бы об этом, только открыв его.
EMSCRIPTEN_KEEPALIVE
int iskra_program_lines(void)
{
    return g_session ? static_cast<int>(g_session->img.line_count()) : 0;
}

EMSCRIPTEN_KEEPALIVE
int iskra_mount(int drive, uintptr_t data, int size, const char * name,
                int writable)
{
    if (!g_host) return 0;
    std::string error;
    const bool ok = g_host->disks().mount_bytes(
        static_cast<unsigned>(drive), reinterpret_cast<const uint8_t *>(data),
        static_cast<std::size_t>(size), name ? name : "", writable != 0, error);
    if (!ok) js_fail(error.c_str());
    return ok ? 1 : 0;
}

EMSCRIPTEN_KEEPALIVE
void iskra_unmount(int drive)
{
    if (g_host) g_host->disks().unmount(static_cast<unsigned>(drive));
}

EMSCRIPTEN_KEEPALIVE
int iskra_disk_mounted(int drive)
{
    return g_host && g_host->disks().mounted(static_cast<unsigned>(drive)) ? 1 : 0;
}

// Писала ли программа на эту дискету. В браузере запись идёт только в
// память, и «Извлечь диск» без этого вопроса терял бы её молча.
EMSCRIPTEN_KEEPALIVE
int iskra_disk_modified(int drive)
{
    return g_host && g_host->disks().modified(static_cast<unsigned>(drive)) ? 1 : 0;
}

EMSCRIPTEN_KEEPALIVE
const char * iskra_disk_name(int drive)
{
    if (!g_host) return "";
    return g_host->disks().path(static_cast<unsigned>(drive)).c_str();
}

// Образ вместе с тем, что записала программа: запись на дискету в браузере
// идёт только в память, и вынуть её оттуда — единственный способ сохранить
// сделанное.
EMSCRIPTEN_KEEPALIVE
uintptr_t iskra_disk_data(int drive)
{
    if (!g_host) return 0;
    const std::vector<uint8_t> & img =
        g_host->disks().image(static_cast<unsigned>(drive));
    return img.empty() ? 0 : reinterpret_cast<uintptr_t>(&img[0]);
}

EMSCRIPTEN_KEEPALIVE
int iskra_disk_size(int drive)
{
    if (!g_host) return 0;
    return static_cast<int>(
        g_host->disks().image(static_cast<unsigned>(drive)).size());
}

// Лента целиком в UTF-8 — для «Сохранить ленту».
EMSCRIPTEN_KEEPALIVE
const char * iskra_tape(void)
{
    return g_host ? g_host->tape_utf8().c_str() : "";
}

// Убрать вкладку листа графопостроителя: это бумага, а не машина, и
// эмулятор от этого не останавливается.
EMSCRIPTEN_KEEPALIVE
void iskra_close_pane(int pane)
{
    if (g_host) g_host->close_pane(static_cast<unsigned>(pane));
}

EMSCRIPTEN_KEEPALIVE
void iskra_skip_machine(int on)
{
    g_skip_machine = on != 0;
    if (g_session) g_session->console.interp().set_skip_machine(g_skip_machine);
}

// Слово Бейсика, которое вводит клавиша зоны 1 с этим знаком. Страница
// берёт его отсюда, а не держит свою таблицу: таблица одна на все хосты
// (`host_common/keywords.*`, снята с рис. 2.2).
EMSCRIPTEN_KEEPALIVE
const char * iskra_keyword(const char * utf8_char)
{
    if (!utf8_char) return "";
    std::string koi8;
    iskra::utf8_to_koi8(utf8_char, koi8);
    if (koi8.empty()) return "";
    const char * w = iskra::keyword_for_char(
        iskra::koi8_upper(static_cast<uint8_t>(koi8[0])));
    return w ? w : "";
}

} // extern "C"

// ---------------------------------------------------------------------------

int main()
{
    iskra::WasmHost host;
    g_host = &host;

    js_ready(iskra::version());

    // Машина включается заново столько раз, сколько нажали «Перезагрузить».
    // Своего цикла исполнения тут нет: диалог разматывается сам, когда хост
    // перестаёт отдавать нажатия.
    for (;;) {
        host.begin_session();

        Session session(host);
        g_session = &session;
        session.console.interp().set_skip_machine(g_skip_machine);

        if (!g_pending_listing.empty()) {
            load_listing(session, g_pending_listing);
            g_pending_listing.clear();
        }

        std::string error;
        if (!session.console.run(error) && !error.empty())
            js_fail(error.c_str());

        g_session = 0;
    }
}
