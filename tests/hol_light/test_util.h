/* Minimal test harness shared by the C test programs. */
#ifndef HOL_TEST_UTIL_H
#define HOL_TEST_UTIL_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static unsigned long g_checks, g_failures;
static const char *g_section = "";

#define SECTION(name) (g_section = (name))

#define CHECK(cond)                                                              \
    do {                                                                         \
        g_checks++;                                                              \
        if (!(cond)) {                                                           \
            g_failures++;                                                        \
            fprintf(stderr, "FAIL [%s] %s:%d: %s\n", g_section, __FILE__, __LINE__, #cond); \
        }                                                                        \
    } while (0)

#define CHECK_EQ_STR(a, b)                                                       \
    do {                                                                         \
        g_checks++;                                                              \
        const char *a_ = (a), *b_ = (b);                                         \
        if (!a_ || !b_ || strcmp(a_, b_) != 0) {                                 \
            g_failures++;                                                        \
            fprintf(stderr, "FAIL [%s] %s:%d:\n  got:      %s\n  expected: %s\n", \
                    g_section, __FILE__, __LINE__, a_ ? a_ : "(null)", b_ ? b_ : "(null)"); \
        }                                                                        \
    } while (0)

static int test_summary(const char *name) {
    printf("%s: %lu checks, %lu failures\n", name, g_checks, g_failures);
    return g_failures ? 1 : 0;
}

#endif
