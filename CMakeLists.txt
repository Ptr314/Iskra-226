# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Mikhail Revzin <p3.141592653589793238462643@gmail.com>
# Part of the Iskra-226 project: https://github.com/Ptr314/Iskra-226

cmake_minimum_required(VERSION 3.16)

file(READ "${CMAKE_CURRENT_SOURCE_DIR}/VERSION" ISKRA_VERSION_RAW)
string(STRIP "${ISKRA_VERSION_RAW}" ISKRA_VERSION)

project(iskra226 VERSION ${ISKRA_VERSION} LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "" FORCE)
endif()

# ---------------------------------------------------------------------------
# Параметры сборки
# ---------------------------------------------------------------------------

option(ISKRA_HOST_SDL2      "Хост с окном на SDL2"                     OFF)
option(ISKRA_BUILD_TESTS    "Сборка автотестов"                        ON)

# В браузере хост всегда графический.
if(EMSCRIPTEN)
    set(ISKRA_HOST_SDL2 ON CACHE BOOL "" FORCE)
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
    src/core/koi8.h             src/core/koi8.cpp
    src/core/number.h           src/core/number.cpp
    src/core/value.h
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
    src/core/interp.h           src/core/interp.cpp
    src/core/console.h          src/core/console.cpp
    src/core/screen.h           src/core/screen.cpp

    src/font/font.h             src/font/font.cpp
    src/font/font_data.cpp      # генерируется tools/psf2c.py из src/font/*.psf
)
target_include_directories(iskra_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(iskra_core PRIVATE iskra_warnings)

# ---------------------------------------------------------------------------
# Хост без окна: автотесты и работа из командной строки
# ---------------------------------------------------------------------------

add_library(iskra_headless STATIC
    src/host_headless/headless_host.h
    src/host_headless/headless_host.cpp
)
target_link_libraries(iskra_headless PUBLIC iskra_core)
target_link_libraries(iskra_headless PRIVATE iskra_warnings)

add_executable(iskra src/host_headless/main.cpp)
target_link_libraries(iskra PRIVATE iskra_headless iskra_warnings)

# MinGW: увести рантайм GCC внутрь exe, чтобы не таскать DLL. Отдельного
# -static-libwinpthread в 4.9.2 нет, поэтому статичим всё разом: зависимостей,
# кроме системных, у хоста без окна нет.
if(MINGW)
    target_link_options(iskra PRIVATE -static)
endif()

# ---------------------------------------------------------------------------
# Хост с окном (появится вместе с интерпретатором)
# ---------------------------------------------------------------------------

if(ISKRA_HOST_SDL2)
    message(FATAL_ERROR "Хост на SDL2 ещё не написан; собирайте с -DISKRA_HOST_SDL2=OFF")
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
message(STATUS "  хост SDL2: ${ISKRA_HOST_SDL2}")
message(STATUS "  тесты:     ${ISKRA_BUILD_TESTS}")