// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Mikhail Revzin <p3.141592653589793238462643@gmail.com>
// Part of the Iskra-226 project: https://github.com/Ptr314/Iskra-226
// Description: хост с окном на чистом Win32 — ни одной сторонней библиотеки

#include "host_win32/win32_host.h"

#include <windows.h>

#include "core/koi8.h"
#include "host_common/fileio.h"

namespace iskra {

namespace {

const wchar_t * CLASS_NAME = L"IskraBosgi";

// Полмига курсора. Точного значения для «Искры» нет, взято привычное для
// знакоместных дисплеев — примерно два мигания в секунду.
const unsigned BLINK_MS = 500;

LRESULT CALLBACK wnd_proc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    Win32Host * self = reinterpret_cast<Win32Host *>(
        GetWindowLongPtr(hwnd, GWLP_USERDATA));
    if (self) {
        bool done = false;
        const long long r = self->handle(hwnd, msg, wp, lp, done);
        if (done) return static_cast<LRESULT>(r);
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

// На мониторе с высокой плотностью система растянула бы окно сама и с
// размытием. Вызов появился в Vista, поэтому берём его по имени: на XP его
// просто нет, и там растягивать нечего.
// Клиентская область при данном увеличении. `DOT_TALL` держит отношение
// сторон точки: сейчас она квадратная, и множитель по осям один.
void client_rect(unsigned w, unsigned h, unsigned scale, RECT & rc)
{
    rc.left = 0;
    rc.top = 0;
    rc.right = static_cast<LONG>(w * scale);
    rc.bottom = static_cast<LONG>(h * scale * DOT_TALL);
}

// Наибольшее увеличение, при котором окно ещё влезает в рабочую область
// экрана. Без этого окно с умолчанием открывалось бы за краем у всякого,
// чей монитор меньше, чем был у выбиравшего умолчание.
unsigned fitting_scale(unsigned w, unsigned h)
{
    RECT work;
    if (!SystemParametersInfoW(SPI_GETWORKAREA, 0, &work, 0)) return 1;

    for (unsigned k = 8; k > 1; --k) {
        RECT rc;
        client_rect(w, h, k, rc);
        AdjustWindowRect(&rc, WS_OVERLAPPEDWINDOW, FALSE);
        if (rc.right - rc.left <= work.right - work.left &&
            rc.bottom - rc.top <= work.bottom - work.top)
            return k;
    }
    return 1;
}

void ask_for_real_pixels()
{
    HMODULE user32 = GetModuleHandleW(L"user32.dll");
    if (!user32) return;
    typedef BOOL (WINAPI * SetDpiAware)(void);
    SetDpiAware fn = reinterpret_cast<SetDpiAware>(
        reinterpret_cast<void *>(GetProcAddress(user32, "SetProcessDPIAware")));
    if (fn) fn();
}

} // namespace

Win32Host::Win32Host()
    : key_pos_(0), hwnd_(0), closed_(false), cursor_on_(false),
      start_ms_(GetTickCount())
{
}

Win32Host::~Win32Host()
{
    close();
}

void Win32Host::resize_frame()
{
    frame_.assign(render_.pixels(), 0);
}

bool Win32Host::open(const std::string & title_utf8, unsigned scale,
                     std::string & error)
{
    error.clear();
    ask_for_real_pixels();

    HINSTANCE inst = GetModuleHandleW(0);

    WNDCLASSEXW wc;
    ZeroMemory(&wc, sizeof(wc));
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = wnd_proc;
    wc.hInstance = inst;
    wc.hCursor = LoadCursor(0, IDC_ARROW);
    wc.hIcon = LoadIcon(0, IDI_APPLICATION);
    wc.lpszClassName = CLASS_NAME;
    // Фон не нужен: поля вокруг экрана закрашиваются при рисовании кадра,
    // а иначе система мигала бы белым при каждом изменении размера.
    wc.hbrBackground = 0;
    if (!RegisterClassExW(&wc) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) {
        error = "не удалось зарегистрировать класс окна";
        return false;
    }

    resize_frame();
    const unsigned fw = render_.width(), fh = render_.height();
    if (!scale) scale = fitting_scale(fw, fh);

    RECT rc;
    client_rect(fw, fh, scale, rc);
    AdjustWindowRect(&rc, WS_OVERLAPPEDWINDOW, FALSE);

    std::vector<wchar_t> title;
    utf8_to_utf16(title_utf8.c_str(), title);

    HWND hwnd = CreateWindowExW(0, CLASS_NAME, &title[0], WS_OVERLAPPEDWINDOW,
                                CW_USEDEFAULT, CW_USEDEFAULT,
                                rc.right - rc.left, rc.bottom - rc.top,
                                0, 0, inst, 0);
    if (!hwnd) { error = "не удалось создать окно"; return false; }

    SetWindowLongPtr(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(this));
    hwnd_ = hwnd;
    closed_ = false;

    redraw();
    ShowWindow(hwnd, SW_SHOW);
    UpdateWindow(hwnd);
    return true;
}

void Win32Host::close()
{
    if (!hwnd_) return;
    HWND hwnd = static_cast<HWND>(hwnd_);
    SetWindowLongPtr(hwnd, GWLP_USERDATA, 0);
    hwnd_ = 0;
    DestroyWindow(hwnd);
}

void Win32Host::redraw()
{
    if (frame_.size() != render_.pixels()) resize_frame();
    render_.set_cursor(cursor_on_);
    render_.draw(screen_, &frame_[0], render_.width());
    screen_.clear_dirty();
    if (hwnd_) InvalidateRect(static_cast<HWND>(hwnd_), 0, FALSE);
}

void Win32Host::paint(void * hdc_raw)
{
    HDC hdc = static_cast<HDC>(hdc_raw);
    const int w = static_cast<int>(render_.width());
    const int h = static_cast<int>(render_.height());

    RECT rc;
    GetClientRect(static_cast<HWND>(hwnd_), &rc);

    // Увеличение только целое: экран знакоместный, дробное растяжение съело
    // бы ровность знаков — однопиксельные штрихи вышли бы разной толщины.
    // Кадр — растр трубки 512x256, и показывается он как 512k x 256k.
    // Что не поместилось — поля, они закрашиваются.
    const int tall = static_cast<int>(DOT_TALL);
    int k = rc.right / w;
    const int kv = rc.bottom / (h * tall);
    if (kv < k) k = kv;
    if (k < 1) k = 1;

    const int dw = w * k, dh = h * k * tall;
    const int dx = (rc.right - dw) / 2, dy = (rc.bottom - dh) / 2;

    BITMAPINFO bi;
    ZeroMemory(&bi, sizeof(bi));
    bi.bmiHeader.biSize = sizeof(bi.bmiHeader);
    bi.bmiHeader.biWidth = w;
    bi.bmiHeader.biHeight = -h;          // сверху вниз, как лежит кадр
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 32;
    bi.bmiHeader.biCompression = BI_RGB;

    SetStretchBltMode(hdc, COLORONCOLOR);   // ближайший сосед, без сглаживания
    StretchDIBits(hdc, dx, dy, dw, dh, 0, 0, w, h,
                  &frame_[0], &bi, DIB_RGB_COLORS, SRCCOPY);

    HBRUSH black = static_cast<HBRUSH>(GetStockObject(BLACK_BRUSH));
    RECT m;
    if (dy > 0) {
        m.left = 0; m.top = 0; m.right = rc.right; m.bottom = dy;
        FillRect(hdc, &m, black);
        m.top = dy + dh; m.bottom = rc.bottom;
        FillRect(hdc, &m, black);
    }
    if (dx > 0) {
        m.left = 0; m.top = dy; m.right = dx; m.bottom = dy + dh;
        FillRect(hdc, &m, black);
        m.left = dx + dw; m.right = rc.right;
        FillRect(hdc, &m, black);
    }
}

long long Win32Host::handle(void * hwnd_raw, unsigned msg,
                            unsigned long long wp, long long lp, bool & done)
{
    HWND hwnd = static_cast<HWND>(hwnd_raw);
    done = true;

    switch (msg) {
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hwnd, &ps);
        paint(hdc);
        EndPaint(hwnd, &ps);
        return 0;
    }

    case WM_ERASEBKGND:
        return 1;                        // фон закрашивается вместе с кадром

    case WM_SIZE:
        InvalidateRect(hwnd, 0, FALSE);
        return 0;

    case WM_CHAR: {
        // Окно заведено широким, поэтому wParam — единица UTF-16. Кириллица
        // приходит сюда с любой раскладки, и это единственный путь, которым
        // знаки попадают в машину: переводить нажатия по положению клавиш
        // «Искре» незачем, у неё своя клавиатура.
        const uint32_t cp = static_cast<uint32_t>(wp);
        if (cp == 0x1B || cp == 0x09) return 0;    // отмена и табуляция — не наши
        uint8_t code = 0;
        // Строчных букв у машины нет ни одной: клавиша выдаёт прописную
        // (core/koi8.h, «семибитная граница машины»).
        if (unicode_to_koi8(cp, code)) keys_.push_back(koi8_upper(code));
        return 0;
    }

    case WM_CLOSE:
        closed_ = true;
        return 0;

    case WM_DESTROY:
        closed_ = true;
        hwnd_ = 0;
        return 0;
    }

    done = false;
    return 0;
}

void Win32Host::pump()
{
    MSG msg;
    while (PeekMessageW(&msg, 0, 0, 0, PM_REMOVE)) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
}

bool Win32Host::present()
{
    pump();
    if (closed_) return false;

    // Мигание курсора: не событие, а состояние часов, поэтому проверяется
    // здесь, а не по таймеру окна.
    const bool on = ((GetTickCount() - start_ms_) / BLINK_MS) % 2 == 0;
    if (screen_.dirty() || on != cursor_on_) {
        cursor_on_ = on;
        redraw();
    }

    if (screen_.take_bells()) MessageBeep(MB_OK);
    return true;
}

bool Win32Host::poll_key(uint8_t & code)
{
    pump();
    if (key_pos_ >= keys_.size()) {
        keys_.clear();
        key_pos_ = 0;
        return false;
    }
    code = keys_[key_pos_++];
    return true;
}

bool Win32Host::wait_key(uint8_t & code)
{
    for (;;) {
        if (!present()) return false;
        if (poll_key(code)) return true;
        // Ждём события системы, но не дольше полумига: иначе курсор замрёт,
        // пока никто не трогает клавиатуру.
        MsgWaitForMultipleObjects(0, 0, FALSE, BLINK_MS / 4, QS_ALLINPUT);
    }
}

uint32_t Win32Host::ticks_ms() const
{
    return static_cast<uint32_t>(GetTickCount() - start_ms_);
}

} // namespace iskra
