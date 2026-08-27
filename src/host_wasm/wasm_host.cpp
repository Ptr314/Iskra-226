// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Mikhail Revzin <p3.141592653589793238462643@gmail.com>
// Part of the Iskra-226 project: https://github.com/Ptr314/Iskra-226
// Description: хост на канве браузера — четвёртый и последний

#include "host_wasm/wasm_host.h"

#include <emscripten/emscripten.h>

#include <cstdint>

#include "core/koi8.h"

namespace iskra {

namespace {

// Кадр обновляется не чаще, чем раз в 16 мс. Ядро зовёт `present()` на
// каждом операторе, которому есть что показать, и рисовать по-настоящему
// столько раз незачем: 143 тысячи точек на кадр, а глаз всё равно берёт
// шестьдесят кадров в секунду.
const double FRAME_MS = 16.0;

// Мигание курсора: два раза в секунду. Сам вид курсора книгой описан
// (подстрочная черта, разд. 2.1), а частота — наша, как и у трёх прочих
// хостов.
const double BLINK_MS = 500.0;

// Сколько ждать между опросами клавиатуры, когда ждать больше нечего.
// Это единственное место, где уступка браузеру видна: `wait_input()`
// возвращает управление странице, и та успевает разобрать нажатия.
const int IDLE_MS = 10;

// Цвет одним словом в том порядке, в каком его берёт `ImageData` канвы:
// байты идут R, G, B, A, то есть в машинном слове это A<<24|B<<16|G<<8|R.
// Растеризатор цветов не толкует вовсе (`host_common/renderer.h`), и хост
// складывает их по-своему — ровно как X11 складывает их по маскам визуала.
inline uint32_t canvas_rgba(uint32_t packed)
{
    uint8_t r = 0, g = 0, b = 0;
    unpack_rgb(packed, r, g, b);
    return 0xFF000000u | (static_cast<uint32_t>(b) << 16)
                       | (static_cast<uint32_t>(g) << 8)
                       |  static_cast<uint32_t>(r);
}

} // namespace

// --- граница со страницей ---------------------------------------------------
//
// Всё, что хост говорит странице, идёт через эти четыре вызова, и больше
// ничего он о ней не знает. `Iskra` — объект страницы; пока его нет
// (например, модуль подняли из node), вызовы молча ничего не делают.

EM_JS(void, js_frame, (int pane, uintptr_t px, int w, int h, int changed), {
    if (typeof Iskra === 'undefined') return;
    Iskra.frame(pane, HEAPU8.subarray(px, px + w * h * 4), w, h, !!changed);
});

EM_JS(void, js_pane, (int pane, int open), {
    if (typeof Iskra === 'undefined') return;
    Iskra.pane(pane, !!open);
});

EM_JS(void, js_tape, (const char * utf8), {
    if (typeof Iskra === 'undefined') return;
    Iskra.tape(UTF8ToString(utf8));
});

EM_JS(void, js_bell, (), {
    if (typeof Iskra === 'undefined') return;
    Iskra.bell();
});

// ---------------------------------------------------------------------------

WasmHost::WasmHost()
    : tape_sent_(0),
      key_pos_(0),
      special_(false),
      control_(CK_NONE),
      reset_(false),
      cursor_on_(false),
      last_paint_(0.0),
      tape_open_(false)
{
    // Увеличение держит канва, а не растеризатор: у неё есть
    // `image-rendering: pixelated`, то есть ближайший сосед без сглаживания.
    // Кадр поэтому собирается один к одному, 560x256.
    render_.set_scale(1, 1);
    render_.set_colors(canvas_rgba(render_.fg()), canvas_rgba(render_.bg()));
    render_.set_paper_colors(canvas_rgba(render_.ink()),
                             canvas_rgba(render_.paper()));

    const std::size_t n = render_.width() * render_.height();
    for (unsigned p = 0; p < PANES; ++p) {
        frame_[p].assign(n, 0);
        open_[p] = false;
        need_[p] = true;
        content_[p] = true;
    }
    open_[PANE_SCREEN] = true;
}

uint32_t WasmHost::ticks_ms() const
{
    return static_cast<uint32_t>(emscripten_get_now());
}

void WasmHost::begin_session()
{
    reset_ = false;
    keys_.clear();
    keys_sf_.clear();
    key_pos_ = 0;
    special_ = false;
    control_ = CK_NONE;

    // Сброс — это включение машины заново: экран чист, растр пуст, лист
    // графопостроителя убран. Лента при этом остаётся: она бумага, а не
    // машина, и от перезагрузки не исчезает.
    screen_.put(CC_CLEAR);
    raster_.clear();
    plotter_.clear();
    close_pane(PANE_PLOT);
    need_[PANE_SCREEN] = true;
    content_[PANE_SCREEN] = true;
    paint(true);
}

void WasmHost::open_pane(unsigned pane)
{
    if (pane >= PANES || open_[pane]) return;
    open_[pane] = true;
    need_[pane] = true;
    content_[pane] = true;
    js_pane(static_cast<int>(pane), 1);
}

void WasmHost::close_pane(unsigned pane)
{
    // Лента кадра не имеет, и в `open_[]` её нет — свой признак.
    if (pane == PANE_TAPE) {
        if (!tape_open_) return;
        tape_open_ = false;
        js_pane(PANE_TAPE, 0);
        return;
    }
    // Экран убрать нельзя: это сама машина, а не бумага.
    if (pane == PANE_SCREEN || pane >= PANES || !open_[pane]) return;
    open_[pane] = false;
    js_pane(static_cast<int>(pane), 0);
}

Raster * WasmHost::plot_surface(uint8_t addr)
{
    if (addr == 0x14) open_pane(PANE_PLOT);
    return Host::plot_surface(addr);
}

void WasmHost::repaint(unsigned pane)
{
    need_[pane] = false;
    const bool changed = content_[pane];
    content_[pane] = false;
    const unsigned w = render_.width();
    const unsigned h = render_.height();
    uint32_t * out = &frame_[pane][0];

    if (pane == PANE_SCREEN) {
        // Трубка одна, устройств два: графика накладывается поверх
        // знакомест. Ноль значит «графики нет» — тогда растеризатору не
        // придётся ходить по растру вовсе.
        render_.draw(screen_, out, w, raster_.empty() ? 0 : &raster_);
    } else {
        render_.draw_raster(plotter_, out, w);
    }

    js_frame(static_cast<int>(pane), reinterpret_cast<uintptr_t>(out),
             static_cast<int>(w), static_cast<int>(h), changed ? 1 : 0);
}

// Перерисовать то, чему есть что показать. Ничем не блокирует: уступка
// браузеру делается отдельно, и делается она не всегда.
void WasmHost::paint(bool force)
{
    const double now = emscripten_get_now();

    // Мигание курсора — не событие, а состояние часов, поэтому проверяется
    // здесь, а не по таймеру.
    const bool on = static_cast<unsigned long>(now / BLINK_MS) % 2 == 0;
    if (on != cursor_on_) {
        cursor_on_ = on;
        render_.set_cursor(on);
        need_[PANE_SCREEN] = true;    // но не content_: это не вывод программы
    }

    if (!force && now - last_paint_ < FRAME_MS) return;
    last_paint_ = now;

    for (unsigned p = 0; p < PANES; ++p)
        if (open_[p] && need_[p]) repaint(p);
}

bool WasmHost::present()
{
    if (reset_) return false;               // «Перезагрузить»: счёт кончился

    if (screen_.dirty() || raster_.dirty()) {
        screen_.clear_dirty();
        raster_.clear_dirty();
        need_[PANE_SCREEN] = true;
        content_[PANE_SCREEN] = true;
    }
    if (plotter_.dirty()) {
        plotter_.clear_dirty();
        need_[PANE_PLOT] = true;
        content_[PANE_PLOT] = true;
    }
    if (screen_.take_bells()) js_bell();

    paint(false);

    // Уступка браузеру. Без неё страница не перерисуется и не примет
    // нажатий: главный цикл здесь чужой. Ноль значит «верни управление и
    // позови обратно при первой возможности», а не «спи».
    emscripten_sleep(0);
    return !reset_;
}

bool WasmHost::poll_key(uint8_t & code)
{
    if (key_pos_ >= keys_.size()) {
        keys_.clear();
        keys_sf_.clear();
        key_pos_ = 0;
        return false;
    }
    special_ = keys_sf_[key_pos_] != 0;
    code = keys_[key_pos_++];
    return true;
}

ControlKey WasmHost::take_control_key()
{
    const ControlKey c = control_;
    control_ = CK_NONE;
    return c;
}

// Ждёт хост, а не ядро. Здесь ожидание — это возврат управления странице:
// пока мы не вернёмся, ни одно нажатие до нас не дойдёт.
bool WasmHost::wait_input(uint8_t & code, ControlKey & ck)
{
    for (;;) {
        if (reset_) return false;
        ck = take_control_key();
        if (ck != CK_NONE) { code = 0; return true; }
        if (poll_key(code)) return true;

        // Кадр обязателен: человек смотрит на мигающий курсор, а не на
        // счёт, и пропустить его мигание нельзя.
        paint(true);
        emscripten_sleep(IDLE_MS);
    }
}

void WasmHost::push_key(uint8_t code, bool special)
{
    keys_.push_back(code);
    keys_sf_.push_back(special ? 1 : 0);
}

void WasmHost::push_word(const char * koi8)
{
    if (!koi8) return;
    for (const char * p = koi8; *p; ++p)
        // **Приведение к прописным — здесь, а не на экране.** Строчных букв
        // у машины нет вовсе, и все три прочих хоста зовут `koi8_upper`
        // прямо на нажатии (`win32_host.cpp:364`, `x11_host.cpp:613`,
        // `cocoa_host.mm:544`). У страницы это единственный путь знака
        // внутрь, значит приводить надо тут.
        //
        // Пропустить это незаметно: `Screen::put` всё равно высветит знак
        // прописным (`koi8_to_koi7`), а вот в текст программы он ляжет
        // строчным — и транслятор ответит «оператор не реализован» на том,
        // что на экране выглядит правильно.
        push_key(koi8_upper(static_cast<uint8_t>(*p)), false);
}

void WasmHost::push_control(ControlKey ck)
{
    control_ = ck;
}

// АЦПУ. Лента копится целиком, а странице уходит только прибавка: печать
// длиной в тысячу строк иначе обошлась бы в квадрат от своей длины.
void WasmHost::print_char(uint8_t ch)
{
    tape_.put(ch);
    if (!tape_open_) {
        tape_open_ = true;
        js_pane(PANE_TAPE, 1);
    }

    const std::vector<uint8_t> & t = tape_.tape();
    if (tape_sent_ >= t.size()) return;
    const std::string add = koi8_to_utf8(&t[tape_sent_],
                                         static_cast<unsigned>(t.size() - tape_sent_));
    tape_sent_ = t.size();
    js_tape(add.c_str());
}

const std::string & WasmHost::tape_utf8()
{
    tape_all_ = tape_.utf8();
    return tape_all_;
}

} // namespace iskra
