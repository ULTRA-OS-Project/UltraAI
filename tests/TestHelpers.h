// TestHelpers.h — tiny cassert-based test harness shared by all suites.
#pragma once

#include <cassert>
#include <cstdio>

#define ULTRAAI_TEST(name) \
    static void name(); \
    struct name##_runner { \
        name##_runner() { std::printf("  [ RUN ] %s\n", #name); name(); \
                          std::printf("  [ OK  ] %s\n", #name); } \
    }; \
    static name##_runner name##_instance; \
    static void name()

#define ULTRAAI_CHECK(cond) assert((cond))
