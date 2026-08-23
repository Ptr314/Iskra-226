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

option(ISKRA_WITH_DSK_TOOLS "Работа с образами дискет через dsk_tools" ON)
option(ISKRA_HOST_SDL2      "Хост с окном на SDL2"                     OFF)
option(ISKRA_BUILD_TESTS    "Сборка автотестов"                        ON)

# Под Emscripten образ приходит в браузер массивом байт, а не файлом,
# поэтому dsk_tools там не нужен; хост, наоборот, всегда графический.
if(EMSCRIPTEN)
    set(ISKRA_WITH_DSK_TOOLS OFF CACHE BOOL "" FORCE)
    set(ISKRA_HOST_SDL2      ON  CACHE BOOL "" FORCE)
endif()

# Предупреждения включаем только для своего кода: dsk_tools — чужой
# подмодуль, его диагностика тут только зашумляет сборку.
add_library(iskra_warnings INTERFACE)
if(MSVC)
    target_compile_options(iskra_warnings INTERFACE /W4 /utf-8)
else()
    target_compile_options(iskra_warnings INTERFACE -Wall -Wextra -Wno-unused-parameter)
endif()

# ---------------------------------------------------------------------------
# Ядро: ни SDL, ни Qt, ни dsk_tools. Только стандартная библиотека.
# ---------------------------------------------------------------------------

add_library(iskra_core STATIC
    src/core/host.h
    src/core/koi8.h             src/core/koi8.cpp
    src/core/screen.h           src/core/screen.cpp

    src/font/font.h             src/font/font.cpp
    src/font/font_data.cpp      # генерируется tools/psf2c.py из src/font/*.psf
)
target_include_directories(iskra_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(iskra_core PRIVATE iskra_warnings)

# ---------------------------------------------------------------------------
# Работа с образами дискет: единственный модуль, который видит dsk_tools
# ---------------------------------------------------------------------------

if(ISKRA_WITH_DSK_TOOLS)
    if(NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/libs/dsk_tools/CMakeLists.txt")
        message(FATAL_ERROR
            "Подмодуль libs/dsk_tools не выкачан. Выполните:\n"
            "    git submodule update --init --recursive\n"
            "либо соберите без него: -DISKRA_WITH_DSK_TOOLS=OFF")
    endif()

    # В dsk_tools по умолчанию собирается ещё и утилита fddconv; она нам не нужна.
    set(ENABLE_DSK_TOOLS OFF CACHE BOOL "" FORCE)
    add_subdirectory(libs/dsk_tools EXCLUDE_FROM_ALL)

    # Геометрии CP/M и FAT dsk_tools берёт из текстового файла. DISK Commander
    # подаёт его ресурсом Qt; здесь Qt нет, поэтому вшиваем в код.
    set(DISKDEFS_SOURCE_FILE ${CMAKE_CURRENT_SOURCE_DIR}/libs/dsk_tools/src/diskdefs)
    file(READ ${DISKDEFS_SOURCE_FILE} DISKDEFS_CONTENTS)
    configure_file(
        ${CMAKE_CURRENT_SOURCE_DIR}/src/diskio/diskdefs_embed.h.in
        ${CMAKE_CURRENT_BINARY_DIR}/generated/diskdefs_embed.h
        @ONLY
    )
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${DISKDEFS_SOURCE_FILE})

    add_library(iskra_diskio STATIC
        src/diskio/image.h      src/diskio/image.cpp
        ${CMAKE_CURRENT_BINARY_DIR}/generated/diskdefs_embed.h
    )
    target_include_directories(iskra_diskio
        PUBLIC  ${CMAKE_CURRENT_SOURCE_DIR}/src
        PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/generated
    )
    target_link_libraries(iskra_diskio PUBLIC iskra_core dsk_tools)
    target_link_libraries(iskra_diskio PRIVATE iskra_warnings)

    if(APPLE)
        target_link_libraries(iskra_diskio PUBLIC "-framework Foundation")
    endif()
endif()

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

if(ISKRA_WITH_DSK_TOOLS)
    target_link_libraries(iskra PRIVATE iskra_diskio)
    target_compile_definitions(iskra PRIVATE ISKRA_WITH_DSK_TOOLS)
endif()

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
message(STATUS "  dsk_tools: ${ISKRA_WITH_DSK_TOOLS}")
message(STATUS "  хост SDL2: ${ISKRA_HOST_SDL2}")
message(STATUS "  тесты:     ${ISKRA_BUILD_TESTS}")