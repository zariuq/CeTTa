/* str:format-fixed refuses digit counts whose shortest text cannot fit a
 * string before it asks snprintf for the length. snprintf is wrapped: a
 * probe whose precision exceeds PROBE_LIMIT is counted and refused here,
 * so a regression that reaches libc with such a precision fails without
 * generating billions of digits. */
#include "atom.h"
#include "str_natives.h"
#include "stats.h"
#if CETTA_BUILD_WITH_RUNTIME_STATS
#include "tests/test_runtime_stats_stubs.h"
#endif
#include <limits.h>
#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define PROBE_LIMIT 4096

static unsigned checks, oversized;
#define check(condition) do { checks++; if (!(condition)) { \
    fprintf(stderr, "FAIL at line %d: %s\n", __LINE__, #condition); abort(); \
} } while (0)

/* Only "%.*f" probes carry a precision argument worth inspecting. */
static int bounded(char *s, size_t n, const char *format, va_list arguments) {
    if (!strcmp(format, "%.*f")) {
        va_list copy;
        va_copy(copy, arguments);
        int precision = va_arg(copy, int);
        va_end(copy);
        if (precision > PROBE_LIMIT) {
            oversized++;
            return -1;
        }
    }
    return vsnprintf(s, n, format, arguments);
}

int __wrap_snprintf(char *s, size_t n, const char *format, ...) {
    va_list arguments;
    va_start(arguments, format);
    int result = bounded(s, n, format, arguments);
    va_end(arguments);
    return result;
}

int __wrap___snprintf_chk(char *s, size_t n, int flag, size_t size, const char *format, ...) {
    (void)flag; (void)size;
    va_list arguments;
    va_start(arguments, format);
    int result = bounded(s, n, format, arguments);
    va_end(arguments);
    return result;
}

static Atom *format_fixed(Arena *arena, double value, long long digits, CettaStrRefusal *refusal) {
    const CettaStrNative *native = cetta_str_native("format-fixed");
    check(native);
    memset(refusal, 0, sizeof(*refusal));
    return native->fn(arena, (Atom *[]){atom_float(arena, value), atom_int(arena, digits)}, refusal);
}

static bool refused_as_unfit(Atom *result, const CettaStrRefusal *refusal) {
    return !result && refusal->expected && strstr(refusal->expected, "fits a string");
}

int main(void) {
#if CETTA_BUILD_WITH_RUNTIME_STATS
    cetta_runtime_stats_enable();
#endif
    SymbolTable symbols;
    symbol_table_init(&symbols);
    symbol_table_init_builtins(&symbols, &g_builtin_syms);
    g_symbols = &symbols;
    Arena arena;
    arena_init_detached(&arena);
    CettaStrRefusal refusal;

    /* Ordinary counts agree with printf's '%.*f' and never reach the limit. */
    const double values[] = {0.0, -0.0, 0.5, -0.5, 1.5, -1.5, 9.95, -9.95, 123.456, 2.5,
                             1e300, -1e300, 5e-324, 1761470400.123456789};
    for (size_t i = 0; i < sizeof(values) / sizeof(*values); i++) {
        for (int digits = 0; digits <= 40; digits++) {
            char want[512];
            snprintf(want, sizeof(want), "%.*f", digits, values[i]);
            Atom *result = format_fixed(&arena, values[i], digits, &refusal);
            check(result && result->kind == ATOM_GROUNDED && result->ground.gkind == GV_STRING);
            check(atom_string_len(result) == strlen(want) && !memcmp(result->ground.sval, want, strlen(want)));
        }
    }
    check(oversized == 0u);

    /* Counts whose shortest text cannot fit are refused before any probe:
     * one integer digit, the point and every fraction digit, plus a sign. */
    check(refused_as_unfit(format_fixed(&arena, 1.5, INT_MAX, &refusal), &refusal));
    check(refused_as_unfit(format_fixed(&arena, 1.5, INT_MAX - 1, &refusal), &refusal));
    check(refused_as_unfit(format_fixed(&arena, -1.5, INT_MAX - 2, &refusal), &refusal));
    check(refused_as_unfit(format_fixed(&arena, -0.0, INT_MAX - 2, &refusal), &refusal));
    check(oversized == 0u);

    /* The largest positive count whose shortest text could still fit does
     * reach the length probe (here refused by the seam), so the early check
     * refuses no count that might fit. */
    check(refused_as_unfit(format_fixed(&arena, 1.5, INT_MAX - 2, &refusal), &refusal));
    check(oversized == 1u);

    arena_free(&arena);
    printf("format-fixed bound: %u checks\n", checks);
    return 0;
}
