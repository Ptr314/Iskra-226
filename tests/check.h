// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Mikhail Revzin <p3.141592653589793238462643@gmail.com>
// Part of the Iskra-226 project: https://github.com/Ptr314/Iskra-226
// Description: проверки для автотестов

#pragma once

#include <cstdio>
#include <string>

// Сторонних библиотек для тестов не берём: их ровно столько, сколько нужно,
// чтобы увидеть, что именно и где разошлось.

namespace test {

inline int & failures()
{
    static int n = 0;
    return n;
}

inline void report(const char * file, int line, const char * expr,
                   const std::string & got, const std::string & want)
{
    std::printf("  ПРОВАЛ %s:%d  %s\n", file, line, expr);
    if (!want.empty() || !got.empty()) {
        std::printf("    ожидалось: %s\n", want.c_str());
        std::printf("    получено:  %s\n", got.c_str());
    }
    ++failures();
}

inline std::string str(unsigned v)
{
    char buf[32];
    std::sprintf(buf, "%u", v);
    return buf;
}

inline int summary(const char * name)
{
    if (failures() == 0) {
        std::printf("%s: пройден\n", name);
        return 0;
    }
    std::printf("%s: провалов %d\n", name, failures());
    return 1;
}

} // namespace test

#define CHECK(expr) \
    do { if (!(expr)) test::report(__FILE__, __LINE__, #expr, "", ""); } while (0)

#define CHECK_EQ(got, want) \
    do { \
        if (!((got) == (want))) \
            test::report(__FILE__, __LINE__, #got " == " #want, \
                         test::str(static_cast<unsigned>(got)), \
                         test::str(static_cast<unsigned>(want))); \
    } while (0)

#define CHECK_STR(got, want) \
    do { \
        const std::string g_ = (got), w_ = (want); \
        if (g_ != w_) \
            test::report(__FILE__, __LINE__, #got " == " #want, g_, w_); \
    } while (0)
