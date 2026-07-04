// TestHelpers.h — tiny test harness shared by all suites.
#pragma once

#include <cstdio>
#include <cstdlib>

#define ULTRAAI_TEST(name) \
    static void name(); \
    struct name##_runner { \
        name##_runner() { std::printf("  [ RUN ] %s\n", #name); name(); \
                          std::printf("  [ OK  ] %s\n", #name); } \
    }; \
    static name##_runner name##_instance; \
    static void name()

// NOT assert(): assert is compiled out under NDEBUG (Release), which would
// skip the checks AND any side effects inside them, making suites vacuous.
#define ULTRAAI_CHECK(cond)                                                  \
    do {                                                                     \
        if (!(cond)) {                                                       \
            std::fprintf(stderr, "ULTRAAI_CHECK failed: %s (%s:%d)\n",       \
                         #cond, __FILE__, __LINE__);                         \
            std::abort();                                                    \
        }                                                                    \
    } while (0)
