# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Mikhail Revzin <p3.141592653589793238462643@gmail.com>
# Part of the Iskra-226 project: https://github.com/Ptr314/Iskra-226

cmake_minimum_required(VERSION 3.16)

file(READ "${CMAKE_CURRENT_SOURCE_DIR}/VERSION" ISKRA_VERSION_RAW)
string(STRIP "${ISKRA_VERSION_RAW}" ISKRA_VERSION)

project(iskra226 VERSION ${ISKRA_VERSION} LANGUAGES CXX)

# Хост на AppKit — Objective-C++, свой язык у CMake. Включается только на
# Apple: в остальных системах компилятора для него нет и не будет.
if(APPLE)
    enable_language(OBJCXX)
endif()

set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "" FORCE)
endif()

# ---------------------------------------------------------------------------
# Параметры сборки
# ---------------------------------------------------------------------------

option(ISKRA_HOST_WINDOW    "Хост с окном средствами самой системы"    ON)
option(ISKRA_BUILD_TESTS    "Сборка автотестов"                        ON)

# Пять наборов работают на корпусе, а он в git не входит (`.gitignore`). На
# машине, где корпуса нет, они падают на отсутствующих файлах, а не на коде,
# и выпуск вставал бы не на своей причине: `deploy/*` прогоняет `ctest`
# перед упаковкой. Поэтому в выпускной сборке их нет, а в отладочной есть.
#
# Ключ разбирается заново при каждой настройке, а не запоминается в кэше:
# сменил тип сборки — сменился и состав наборов. Задать руками:
# `-DISKRA_CORPUS_TESTS=ON`.
if(NOT DEFINED ISKRA_CORPUS_TESTS)
    if(CMAKE_BUILD_TYPE STREQUAL "Debug")
        set(ISKRA_CORPUS_TESTS ON)
    else()
        set(ISKRA_CORPUS_TESTS OFF)
    endif()
endif()

add_library(iskra_warnings INTERFACE)
if(MSVC)
    target_compile_options(iskra_warnings INTERFACE /W4 /utf-8)
else()
    target_compile_options(iskra_warnings INTERFACE -Wall -Wextra -Wno-unused-parameter)
endif()

# ---------------------------------------------------------------------------
# Ядро: ни SDL, ни Qt, ни сторонних библиотек вовсе. Только стандартная.
# ---------------------------------------------------------------------------

add_library(iskra_core STATIC
    src/core/host.h
    src/core/version.h
    src/core/koi8.h             src/core/koi8.cpp
    src/core/number.h           src/core/number.cpp
    src/core/value.h
    src/core/image.h            src/core/image.cpp
    src/core/program.h          src/core/program.cpp
    src/core/disk_record.h      src/core/disk_record.cpp
    src/core/catalog.h          src/core/catalog.cpp
    src/core/devtable.h         src/core/devtable.cpp
    src/core/expr.h             src/core/expr.cpp
    src/core/text_lexer.h       src/core/text_lexer.cpp
    src/core/tokenize.h         src/core/tokenize.cpp
    src/core/names.h            src/core/names.cpp
    src/core/byte_source.h      src/core/byte_source.cpp
    src/core/vars.h             src/core/vars.cpp
    src/core/eval.h             src/core/eval.cpp
    src/core/detokenize.h       src/core/detokenize.cpp
    src/core/raster.h           src/core/raster.cpp
    src/core/gbuffer.h          src/core/gbuffer.cpp
    src/core/interp.h           src/core/interp.cpp
    src/core/interp_graphics.cpp
    src/core/console.h          src/core/console.cpp
    src/core/screen.h           src/core/screen.cpp

    src/font/font.h             src/font/font.cpp
    src/font/font_data.cpp      # генерируется tools/font2c.py из koi7-5x8.txt
)
target_include_directories(iskra_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(iskra_core PRIVATE iskra_warnings)
# Версия — из файла VERSION, одним числом на весь проект: заголовок окна,
# помощь обоих хостов и имя архива выпуска берут её оттуда.
target_compile_definitions(iskra_core PUBLIC ISKRA_VERSION="${ISKRA_VERSION}")

# ---------------------------------------------------------------------------
# Общая часть оконных хостов: знакоместа экрана — в точки. Про систему не
# знает ничего, поэтому проверяется автотестом без всякого окна.
# ---------------------------------------------------------------------------

add_library(iskra_host_common STATIC
    src/host_common/renderer.h    src/host_common/renderer.cpp
    src/host_common/fileio.h      src/host_common/fileio.cpp
    src/host_common/disk_files.h  src/host_common/disk_files.cpp
    src/host_common/disk_args.h   src/host_common/disk_args.cpp
    src/host_common/printer.h     src/host_common/printer.cpp
    src/host_common/keywords.h    src/host_common/keywords.cpp
)
target_link_libraries(iskra_host_common PUBLIC iskra_core)
target_link_libraries(iskra_host_common PRIVATE iskra_warnings)

# ---------------------------------------------------------------------------
# Хост без окна: автотесты и работа из командной строки
# ---------------------------------------------------------------------------

add_library(iskra_headless STATIC
    src/host_headless/headless_host.h
    src/host_headless/headless_host.cpp
)
target_link_libraries(iskra_headless PUBLIC iskra_core)
target_link_libraries(iskra_headless PRIVATE iskra_warnings)

add_executable(iskra-nohead src/host_headless/main.cpp)
# Диалоговый режим работает с настоящими файлами образов — тем же кодом, что
# и окно. Самому хосту без окна (и автотестам на нём) это не нужно.
target_link_libraries(iskra-nohead PRIVATE iskra_headless iskra_host_common
                                           iskra_warnings)

# shell32 — ради разбора командной строки в широких знаках: узкая приходит в
# кодовой странице системы, и имя файла с кириллицей через неё не проходит.
if(WIN32)
    target_link_libraries(iskra-nohead PRIVATE shell32)
endif()

# MinGW: увести рантайм GCC внутрь exe, чтобы не таскать DLL. Отдельного
# -static-libwinpthread в 4.9.2 нет, поэтому статичим всё разом: зависимостей,
# кроме системных, у хоста без окна нет.
if(MINGW)
    target_link_options(iskra-nohead PRIVATE -static)
endif()

# ---------------------------------------------------------------------------
# Хост с окном — средствами самой системы, без сторонних библиотек.
# Обоснование выбора и раскладка по системам — docs/DECISIONS.md, разд. 14.
# ---------------------------------------------------------------------------

set(ISKRA_WINDOW_HOST "нет")

if(ISKRA_HOST_WINDOW)
    if(EMSCRIPTEN)
        # Канва браузера. Проверяется первой: Emscripten объявляет себя UNIX,
        # и ветка X11 иначе забрала бы его себе.
        #
        # `-sASYNCIFY` — единственная уступка среде: главный цикл в браузере
        # чужой, а ядро блокирует (`Console::read_line`, `Interp::loop`).
        # Стек разматывается и складывается обратно вокруг двух вызовов
        # `emscripten_sleep()` в хосте, и переделывать под браузер диалог с
        # исполнителем не приходится (`docs/DECISIONS.md`, разд. 11 и 14).
        add_executable(iskra
            src/host_wasm/wasm_host.h  src/host_wasm/wasm_host.cpp
            src/host_wasm/main.cpp
        )
        target_link_libraries(iskra PRIVATE iskra_host_common iskra_warnings)
        target_link_options(iskra PRIVATE
            -sASYNCIFY
            # Разложенный стек живёт в куче отдельным куском: разбор
            # выражения — полный рекурсивный спуск, и умолчания в 4 КБ ему
            # мало.
            -sASYNCIFY_STACK_SIZE=65536
            -sSTACK_SIZE=1048576
            -sALLOW_MEMORY_GROWTH=1
            -sMODULARIZE=1
            -sEXPORT_NAME=createIskra
            # Среда нарочно НЕ сужена до `web`: тогда тот же модуль поднимается
            # под node, и границу со страницей — кадры, вкладки, ленту, очередь
            # нажатий — можно проверить без браузера вовсе
            # (`tools/probes/wasm_smoke.js`). Стоит это несколько килобайт
            # обвязки.
            -sEXPORTED_FUNCTIONS=_main,_malloc,_free
            -sEXPORTED_RUNTIME_METHODS=ccall,cwrap,UTF8ToString,stringToUTF8,lengthBytesUTF8,HEAPU8
        )
        set(ISKRA_WINDOW_HOST "канва браузера")
    elseif(WIN32)
        add_executable(iskra WIN32
            src/host_win32/win32_host.h  src/host_win32/win32_host.cpp
            src/host_win32/main.cpp
        )
        target_link_libraries(iskra PRIVATE iskra_host_common iskra_warnings)
        # user32 и gdi32 есть на любой Windows, shell32 — ради разбора
        # командной строки в широких знаках.
        target_link_libraries(iskra PRIVATE user32 gdi32 shell32)
        if(MINGW)
            target_link_options(iskra PRIVATE -static)
        endif()
        set(ISKRA_WINDOW_HOST "Win32")
    elseif(UNIX AND NOT APPLE)
        # Xlib и больше ничего: ни toolkit'а, ни XRender, ни SHM. На Wayland
        # окно идёт через XWayland — своего хоста ему пока нет.
        find_package(X11)
        if(X11_FOUND)
            add_executable(iskra
                src/host_x11/x11_host.h  src/host_x11/x11_host.cpp
                src/host_x11/main.cpp
            )
            target_link_libraries(iskra PRIVATE iskra_host_common iskra_warnings)
            target_include_directories(iskra PRIVATE ${X11_INCLUDE_DIR})
            target_link_libraries(iskra PRIVATE ${X11_X11_LIB})
            set(ISKRA_WINDOW_HOST "X11")
        else()
            message(STATUS "Xlib не найден: нужен libx11-dev (Debian, Ubuntu) "
                           "либо libX11-devel (Fedora, openSUSE)")
        endif()
    elseif(APPLE)
        # AppKit и больше ничего: ни бандла, ни Info.plist, ни подписи —
        # голый исполняемый файл открывает окно сам (docs/DECISIONS.md,
        # разд. 14).
        add_executable(iskra
            src/host_cocoa/cocoa_host.h  src/host_cocoa/cocoa_host.mm
            src/host_cocoa/main.mm
        )
        set_source_files_properties(
            src/host_cocoa/cocoa_host.mm src/host_cocoa/main.mm
            PROPERTIES
                LANGUAGE OBJCXX
                COMPILE_OPTIONS "-fno-objc-arc"
        )
        target_link_libraries(iskra PRIVATE iskra_host_common iskra_warnings)
        target_link_libraries(iskra PRIVATE "-framework Cocoa")
        set(ISKRA_WINDOW_HOST "Cocoa")
    else()
        message(STATUS "Хост с окном для этой системы ещё не написан")
    endif()
endif()

# ---------------------------------------------------------------------------
# Тесты
# ---------------------------------------------------------------------------

if(ISKRA_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()

# ---------------------------------------------------------------------------

message(STATUS "Искра-226 ${ISKRA_VERSION}")
message(STATUS "  хост с окном:      ${ISKRA_WINDOW_HOST}")
message(STATUS "  тесты:             ${ISKRA_BUILD_TESTS}")
message(STATUS "  наборы на корпусе: ${ISKRA_CORPUS_TESTS}")