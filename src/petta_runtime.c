#define _GNU_SOURCE
#include "petta_runtime.h"

#include "library.h"
#include "petta_numeric.h"

#include <errno.h>
#include <inttypes.h>
#include <math.h>
#include <pthread.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

struct CettaPettaRuntimeState {
    pthread_mutex_t random_mutex;
    bool random_mutex_ready;
    uint64_t random_state;
    const SymbolTable *symbol_table;
    uint64_t symbol_table_instance;
    SymbolId argv;
    SymbolId current_time;
    SymbolId format_time;
    SymbolId random_int;
    SymbolId random_float;
    SymbolId rng_marker;
    bool symbol_facts_ready;
};

typedef enum {
    PETTA_RUNTIME_OP_NONE = 0,
    PETTA_RUNTIME_OP_ARGV,
    PETTA_RUNTIME_OP_CURRENT_TIME,
    PETTA_RUNTIME_OP_FORMAT_TIME,
    PETTA_RUNTIME_OP_RANDOM_INT,
    PETTA_RUNTIME_OP_RANDOM_FLOAT,
} PettaRuntimeOp;

static PettaRuntimeOp petta_runtime_op_by_spelling(SymbolId head) {
    if (g_symbols && head != SYMBOL_ID_NONE) {
        if (symbol_eq_cstr(g_symbols, head, "argv"))
            return PETTA_RUNTIME_OP_ARGV;
        if (symbol_eq_cstr(g_symbols, head, "current-time"))
            return PETTA_RUNTIME_OP_CURRENT_TIME;
        if (symbol_eq_cstr(g_symbols, head, "format-time"))
            return PETTA_RUNTIME_OP_FORMAT_TIME;
        if (symbol_eq_cstr(g_symbols, head, "random-int"))
            return PETTA_RUNTIME_OP_RANDOM_INT;
        if (symbol_eq_cstr(g_symbols, head, "random-float"))
            return PETTA_RUNTIME_OP_RANDOM_FLOAT;
    }
    return PETTA_RUNTIME_OP_NONE;
}

static bool petta_runtime_symbol_facts_current(
    const struct CettaPettaRuntimeState *state) {
    return state && state->symbol_facts_ready && g_symbols &&
           state->symbol_table == g_symbols &&
           state->symbol_table_instance ==
               symbol_table_instance_id(g_symbols);
}

static PettaRuntimeOp petta_runtime_op(
    const CettaLibraryContext *context, SymbolId head) {
    const struct CettaPettaRuntimeState *state =
        context ? context->petta_runtime : NULL;
    if (!petta_runtime_symbol_facts_current(state))
        return petta_runtime_op_by_spelling(head);
    if (head == state->argv)
        return PETTA_RUNTIME_OP_ARGV;
    if (head == state->current_time)
        return PETTA_RUNTIME_OP_CURRENT_TIME;
    if (head == state->format_time)
        return PETTA_RUNTIME_OP_FORMAT_TIME;
    if (head == state->random_int)
        return PETTA_RUNTIME_OP_RANDOM_INT;
    if (head == state->random_float)
        return PETTA_RUNTIME_OP_RANDOM_FLOAT;
    return PETTA_RUNTIME_OP_NONE;
}

static uint64_t petta_runtime_mix64(uint64_t value) {
    value ^= value >> 30u;
    value *= UINT64_C(0xbf58476d1ce4e5b9);
    value ^= value >> 27u;
    value *= UINT64_C(0x94d049bb133111eb);
    value ^= value >> 31u;
    return value;
}

static uint64_t petta_runtime_initial_seed(const void *identity) {
    const char *controlled = getenv("CETTA_PETTA_RANDOM_SEED");
    if (controlled && controlled[0] != '\0') {
        char *end = NULL;
        errno = 0;
        uint64_t parsed = strtoull(controlled, &end, 0);
        if (end && *end == '\0' && errno == 0)
            return parsed;
    }
    struct timespec realtime = {0};
    struct timespec monotonic = {0};
    (void)clock_gettime(CLOCK_REALTIME, &realtime);
    (void)clock_gettime(CLOCK_MONOTONIC, &monotonic);
    uint64_t seed = (uint64_t)realtime.tv_sec ^
        ((uint64_t)realtime.tv_nsec << 32u) ^
        (uint64_t)monotonic.tv_sec ^
        ((uint64_t)monotonic.tv_nsec << 1u) ^
        (uint64_t)(uintptr_t)identity ^
        ((uint64_t)(unsigned)getpid() << 17u);
    return petta_runtime_mix64(seed);
}

struct CettaPettaRuntimeState *cetta_petta_runtime_state_new(void) {
    struct CettaPettaRuntimeState *state = calloc(1u, sizeof(*state));
    if (!state)
        return NULL;
    state->random_mutex_ready =
        pthread_mutex_init(&state->random_mutex, NULL) == 0;
    if (!state->random_mutex_ready) {
        free(state);
        return NULL;
    }
    state->random_state = petta_runtime_initial_seed(state);
    state->symbol_table = g_symbols;
    state->symbol_table_instance = symbol_table_instance_id(g_symbols);
    if (g_symbols && state->symbol_table_instance != 0u) {
        state->argv = symbol_intern_cstr(g_symbols, "argv");
        state->current_time = symbol_intern_cstr(
            g_symbols, "current-time");
        state->format_time = symbol_intern_cstr(
            g_symbols, "format-time");
        state->random_int = symbol_intern_cstr(
            g_symbols, "random-int");
        state->random_float = symbol_intern_cstr(
            g_symbols, "random-float");
        state->rng_marker = symbol_intern_cstr(g_symbols, "&rng");
        state->symbol_facts_ready =
            state->argv != SYMBOL_ID_NONE &&
            state->current_time != SYMBOL_ID_NONE &&
            state->format_time != SYMBOL_ID_NONE &&
            state->random_int != SYMBOL_ID_NONE &&
            state->random_float != SYMBOL_ID_NONE &&
            state->rng_marker != SYMBOL_ID_NONE;
    }
    return state;
}

void cetta_petta_runtime_state_free(
    struct CettaPettaRuntimeState *state) {
    if (!state)
        return;
    if (state->random_mutex_ready)
        pthread_mutex_destroy(&state->random_mutex);
    free(state);
}

static uint64_t petta_runtime_random_u64(
    struct CettaPettaRuntimeState *state) {
    state->random_state += UINT64_C(0x9e3779b97f4a7c15);
    return petta_runtime_mix64(state->random_state);
}

static uint64_t petta_runtime_random_bounded(
    struct CettaPettaRuntimeState *state, uint64_t bound) {
    if (bound == 0u)
        return petta_runtime_random_u64(state);
    uint64_t threshold = (uint64_t)(0u - bound) % bound;
    for (;;) {
        uint64_t value = petta_runtime_random_u64(state);
        if (value >= threshold)
            return value % bound;
    }
}

static bool petta_runtime_is_rng_marker(
    const CettaLibraryContext *context, const Atom *atom) {
    if (!atom || atom->kind != ATOM_SYMBOL)
        return false;
    const struct CettaPettaRuntimeState *state =
        context ? context->petta_runtime : NULL;
    if (petta_runtime_symbol_facts_current(state))
        return atom->sym_id == state->rng_marker;
    return symbol_eq_cstr(g_symbols, atom->sym_id, "&rng");
}

static bool petta_runtime_int64(const Atom *atom, int64_t *value) {
    if (!atom || !value || atom->kind != ATOM_GROUNDED)
        return false;
    if (atom->ground.gkind == GV_INT) {
        *value = atom->ground.ival;
        return true;
    }
#if CETTA_BUILD_WITH_GMP
    if (atom->ground.gkind == GV_BIGINT) {
        mpz_t integer;
        mpz_init(integer);
        bool valid = atom_bigint_get_mpz(atom, integer) &&
            mpz_fits_slong_p(integer);
        if (valid) {
            long narrowed = mpz_get_si(integer);
            *value = (int64_t)narrowed;
            valid = (long)*value == narrowed;
        }
        mpz_clear(integer);
        return valid;
    }
#endif
    return false;
}

static bool petta_runtime_double(const Atom *atom, double *value) {
    if (!atom || !value || atom->kind != ATOM_GROUNDED)
        return false;
    switch (atom->ground.gkind) {
    case GV_INT:
        *value = (double)atom->ground.ival;
        return true;
    case GV_FLOAT:
        *value = atom->ground.fval;
        return true;
#if CETTA_BUILD_WITH_GMP
    case GV_BIGINT: {
        mpz_t integer;
        mpz_init(integer);
        bool valid = atom_bigint_get_mpz(atom, integer);
        if (valid)
            *value = mpz_get_d(integer);
        mpz_clear(integer);
        return valid;
    }
    case GV_RATIONAL: {
        mpq_t rational;
        mpq_init(rational);
        bool valid = atom_rational_get_mpq(atom, rational);
        if (valid)
            *value = mpq_get_d(rational);
        mpq_clear(rational);
        return valid;
    }
#endif
    default:
        return false;
    }
}

static const char *petta_runtime_text(Atom *atom) {
    if (!atom)
        return NULL;
    if (atom->kind == ATOM_SYMBOL)
        return atom_name_cstr(atom);
    if (atom->kind == ATOM_GROUNDED && atom->ground.gkind == GV_STRING)
        return atom->ground.sval;
    return NULL;
}

static bool petta_runtime_wall_clock(struct timespec *now) {
    return now && clock_gettime(CLOCK_REALTIME, now) == 0;
}

static bool petta_format_time_directive(char directive) {
    return strchr(
        "aAbBcCdDeFgGhHIjklmMnNpPrRStTuUVwWxXyYzZ+", directive) != NULL;
}

static bool petta_format_append(
    char *output, size_t capacity, size_t *length,
    const char *text, size_t text_length) {
    if (!output || !length || !text ||
        text_length >= capacity || *length > capacity - text_length - 1u) {
        return false;
    }
    memcpy(output + *length, text, text_length);
    *length += text_length;
    output[*length] = '\0';
    return true;
}

static bool petta_format_time_at(
    const char *format, double instant,
    char *output, size_t capacity) {
    if (!format || !isfinite(instant) || !output || capacity == 0u)
        return false;
    time_t seconds = (time_t)instant;
    struct tm local;
    if (!localtime_r(&seconds, &local))
        return false;
    size_t length = 0u;
    output[0] = '\0';
    for (size_t index = 0u; format[index] != '\0'; index++) {
        if (format[index] != '%') {
            if (!petta_format_append(
                    output, capacity, &length, format + index, 1u)) {
                return false;
            }
            continue;
        }
        index++;
        if (format[index] == '\0')
            return false;
        if (format[index] == '%') {
            if (!petta_format_append(output, capacity, &length, "%", 1u))
                return false;
            continue;
        }
        if (format[index] == 's') {
            char epoch[64];
            int written = snprintf(
                epoch, sizeof(epoch), "%" PRIdMAX, (intmax_t)seconds);
            if (written <= 0 || (size_t)written >= sizeof(epoch) ||
                !petta_format_append(
                    output, capacity, &length, epoch, (size_t)written)) {
                return false;
            }
            continue;
        }
        if (format[index] == 'f' ||
            (format[index] >= '0' && format[index] <= '9')) {
            size_t precision = 6u;
            if (format[index] != 'f') {
                precision = 0u;
                while (format[index] >= '0' && format[index] <= '9') {
                    if (precision > (capacity - 1u) / 10u)
                        return false;
                    precision = precision * 10u +
                        (size_t)(format[index] - '0');
                    index++;
                }
                if (format[index] != 'f' || precision >= capacity)
                    return false;
            }
            if (precision == 0u)
                continue;
            char fraction[4096];
            if (!cetta_petta_number_format_fraction(
                    fraction, sizeof(fraction), instant, precision) ||
                !petta_format_append(
                    output, capacity, &length, fraction, precision)) {
                return false;
            }
            continue;
        }
        if (format[index] == ':' && format[index + 1u] == 'z') {
            char offset[16];
            size_t written = strftime(offset, sizeof(offset), "%z", &local);
            if (written != 5u ||
                !petta_format_append(output, capacity, &length, offset, 3u) ||
                !petta_format_append(
                    output, capacity, &length, ":", 1u) ||
                !petta_format_append(
                    output, capacity, &length, offset + 3u, 2u)) {
                return false;
            }
            index++;
            continue;
        }
        char directive[3] = {'%', '\0', '\0'};
        if (!petta_format_time_directive(format[index]))
            return false;
        directive[1] = format[index] == '+' ? 'c' : format[index];
        char piece[256];
        size_t written = strftime(piece, sizeof(piece), directive, &local);
        if (written == 0u ||
            !petta_format_append(
                output, capacity, &length, piece, written)) {
            return false;
        }
    }
    return true;
}

static PeTTaNamedArity petta_runtime_arity(
    CettaExprLen supplied, CettaExprLen minimum,
    CettaExprLen maximum) {
    return (PeTTaNamedArity){
        .known = true,
        .exact = supplied >= minimum && supplied <= maximum,
        .larger = supplied < maximum,
        .smaller = supplied > minimum,
    };
}

PeTTaNamedArity cetta_petta_runtime_named_arity(
    const CettaLibraryContext *context,
    SymbolId head, CettaExprLen supplied) {
    /* Importing lib_import installs one capability-indexed native family.
     * Keep its accepted interval here, at the same named-arity authority
     * consulted by canonical and prepared PeTTa calls.  The evaluator owns
     * the effect itself; before the capability is present the authored head
     * remains ordinary uninterpreted MeTTa syntax. */
    if (context && g_symbols && head != SYMBOL_ID_NONE &&
        symbol_eq_cstr(g_symbols, head, "git-import!") &&
        cetta_library_petta_git_import_enabled(context)) {
        return petta_runtime_arity(supplied, 1u, 4u);
    }
    switch (petta_runtime_op(context, head)) {
    case PETTA_RUNTIME_OP_ARGV:
    case PETTA_RUNTIME_OP_FORMAT_TIME:
        return petta_runtime_arity(supplied, 1u, 1u);
    case PETTA_RUNTIME_OP_CURRENT_TIME:
        return petta_runtime_arity(supplied, 0u, 0u);
    case PETTA_RUNTIME_OP_RANDOM_INT:
    case PETTA_RUNTIME_OP_RANDOM_FLOAT:
        return petta_runtime_arity(supplied, 2u, 3u);
    case PETTA_RUNTIME_OP_NONE:
        return (PeTTaNamedArity){0};
    }
    return (PeTTaNamedArity){0};
}

static bool petta_runtime_argv(
    const CettaLibraryContext *context, Arena *arena,
    Atom *expression, Atom **result) {
    if (expression->expr.len != 2u)
        return true;
    Atom *index_atom = expression->expr.elems[1];
    if (!index_atom || index_atom->kind != ATOM_GROUNDED ||
        index_atom->ground.gkind != GV_INT || index_atom->ground.ival < 0) {
        return true;
    }
    uint64_t index = (uint64_t)index_atom->ground.ival;
    const char *text = NULL;
    if (index == 0u) {
        if (context->script_path[0] != '\0')
            text = context->script_path;
    } else if (index - 1u < (uint64_t)context->cmdline_arg_len) {
        text = context->cmdline_args[index - 1u];
    }
    if (!text)
        return true;
    *result = cetta_petta_number_parse_atom(
        arena, (const uint8_t *)text, strlen(text),
        CETTA_PETTA_NUMBER_ATOM_TEXT);
    if (!*result)
        *result = atom_symbol(arena, text);
    return *result != NULL;
}

static bool petta_runtime_current_time(Arena *arena, Atom **result) {
    struct timespec now;
    if (!petta_runtime_wall_clock(&now))
        return false;
    *result = atom_float(
        arena, (double)now.tv_sec + (double)now.tv_nsec / 1000000000.0);
    return *result != NULL;
}

static bool petta_runtime_format_time(
    Arena *arena, Atom *expression,
    Atom **result, bool *recognized) {
    if (expression->expr.len != 2u)
        return true;
    const char *format = petta_runtime_text(expression->expr.elems[1]);
    if (!format)
        return true;
    struct timespec now;
    char formatted[4096];
    if (!petta_runtime_wall_clock(&now))
        return false;
    double instant =
        (double)now.tv_sec + (double)now.tv_nsec / 1000000000.0;
    if (!petta_format_time_at(
            format, instant, formatted, sizeof(formatted))) {
        *recognized = false;
        return false;
    }
    *result = atom_symbol(arena, formatted);
    return *result != NULL;
}

static bool petta_runtime_random_int(
    CettaLibraryContext *context, Arena *arena,
    Atom *expression, Atom **result, bool *recognized) {
    CettaExprIndex start = 1u;
    CettaExprLen arguments = expression->expr.len - 1u;
    if (arguments == 3u) {
        if (!petta_runtime_is_rng_marker(
                context, expression->expr.elems[1]))
            return true;
        start++;
    } else if (arguments != 2u) {
        return true;
    }
    int64_t minimum;
    int64_t maximum;
    if (!petta_runtime_int64(expression->expr.elems[start], &minimum) ||
        !petta_runtime_int64(
            expression->expr.elems[start + 1u], &maximum)) {
        *recognized = false;
        return false;
    }
    if (minimum > maximum)
        return true;
    struct CettaPettaRuntimeState *state = context->petta_runtime;
    if (!state || !state->random_mutex_ready)
        return false;
    pthread_mutex_lock(&state->random_mutex);
    uint64_t bound = (uint64_t)maximum - (uint64_t)minimum + 1u;
    uint64_t offset = petta_runtime_random_bounded(state, bound);
    pthread_mutex_unlock(&state->random_mutex);
    *result = atom_int(arena, (int64_t)((uint64_t)minimum + offset));
    return *result != NULL;
}

static bool petta_runtime_random_float(
    CettaLibraryContext *context, Arena *arena,
    Atom *expression, Atom **result, bool *recognized) {
    CettaExprIndex start = 1u;
    CettaExprLen arguments = expression->expr.len - 1u;
    if (arguments == 3u) {
        if (!petta_runtime_is_rng_marker(
                context, expression->expr.elems[1]))
            return true;
        start++;
    } else if (arguments != 2u) {
        return true;
    }
    double minimum;
    double maximum;
    if (!petta_runtime_double(expression->expr.elems[start], &minimum) ||
        !petta_runtime_double(
            expression->expr.elems[start + 1u], &maximum)) {
        *recognized = false;
        return false;
    }
    if (!isfinite(minimum) || !isfinite(maximum)) {
        *recognized = false;
        return false;
    }
    struct CettaPettaRuntimeState *state = context->petta_runtime;
    if (!state || !state->random_mutex_ready)
        return false;
    pthread_mutex_lock(&state->random_mutex);
    double unit = (double)(petta_runtime_random_u64(state) >> 11u) *
        0x1.0p-53;
    pthread_mutex_unlock(&state->random_mutex);
    *result = atom_float(arena, minimum + unit * (maximum - minimum));
    return *result != NULL;
}

bool cetta_petta_runtime_call(
    CettaLibraryContext *context,
    Arena *arena, Atom *expression,
    Atom **result, bool *recognized) {
    if (result)
        *result = NULL;
    if (recognized)
        *recognized = false;
    if (!context || !arena || !expression || !result || !recognized ||
        expression->kind != ATOM_EXPR || expression->expr.len == 0u ||
        expression->expr.elems[0]->kind != ATOM_SYMBOL) {
        return false;
    }
    SymbolId head = expression->expr.elems[0]->sym_id;
    switch (petta_runtime_op(context, head)) {
    case PETTA_RUNTIME_OP_ARGV:
        *recognized = true;
        return petta_runtime_argv(context, arena, expression, result);
    case PETTA_RUNTIME_OP_CURRENT_TIME:
        *recognized = true;
        return expression->expr.len == 1u
            ? petta_runtime_current_time(arena, result) : true;
    case PETTA_RUNTIME_OP_FORMAT_TIME:
        *recognized = true;
        return petta_runtime_format_time(
            arena, expression, result, recognized);
    case PETTA_RUNTIME_OP_RANDOM_INT:
        *recognized = true;
        return petta_runtime_random_int(
            context, arena, expression, result, recognized);
    case PETTA_RUNTIME_OP_RANDOM_FLOAT:
        *recognized = true;
        return petta_runtime_random_float(
            context, arena, expression, result, recognized);
    case PETTA_RUNTIME_OP_NONE:
        return false;
    }
    return false;
}
