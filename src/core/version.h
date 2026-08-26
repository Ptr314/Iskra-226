// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Mikhail Revzin <p3.141592653589793238462643@gmail.com>
// Part of the Iskra-226 project: https://github.com/Ptr314/Iskra-226
// Описание: версия эмулятора. Число одно на весь проект и лежит в файле
// VERSION в корне; CMake читает его оттуда и передаёт сюда определением
// ISKRA_VERSION. Оно же попадает в имя архива выпуска (deploy/README.md).

#ifndef ISKRA_CORE_VERSION_H
#define ISKRA_CORE_VERSION_H

// Собранное не через CMake (одиночный g++, чужая система сборки) версии не
// знает. Врать числом тут нельзя, поэтому вместо него стоит слово.
#ifndef ISKRA_VERSION
#define ISKRA_VERSION "без версии"
#endif

namespace iskra {

inline const char * version() { return ISKRA_VERSION; }

}  // namespace iskra

#endif  // ISKRA_CORE_VERSION_H
