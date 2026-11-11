#include "asp_clingo_v1.h"

#include <dlfcn.h>
#include <pthread.h>
#include <stdlib.h>
#include <string.h>

/* The subset of clingo's C interface used here, declared by its stable
 * ABI (clingo 5.x): atoms and identifiers are 32-bit unsigned, literals and
 * weights 32-bit signed, symbols 64-bit. */
typedef uint32_t clingo_atom_t;
typedef int32_t clingo_literal_t;
typedef int32_t clingo_weight_t;
typedef uint64_t clingo_symbol_t;
typedef struct { clingo_literal_t literal; clingo_weight_t weight; } clingo_weighted_literal_t;
typedef struct { const char *name; const clingo_symbol_t *params; size_t size; } clingo_part_t;
typedef struct clingo_control clingo_control_t;
typedef struct clingo_backend clingo_backend_t;
typedef struct clingo_solve_handle clingo_solve_handle_t;
typedef struct clingo_model clingo_model_t;
typedef void (*clingo_logger_t)(int code, const char *message, void *data);

/* The ground-program observer, its members in clingo's order. */
typedef struct {
    bool (*init_program)(bool incremental, void *data);
    bool (*begin_step)(void *data);
    bool (*end_step)(void *data);
    bool (*rule)(bool choice, const clingo_atom_t *head, size_t head_size,
                 const clingo_literal_t *body, size_t body_size, void *data);
    bool (*weight_rule)(bool choice, const clingo_atom_t *head, size_t head_size,
                        clingo_weight_t lower_bound,
                        const clingo_weighted_literal_t *body, size_t body_size,
                        void *data);
    bool (*minimize)(clingo_weight_t priority,
                     const clingo_weighted_literal_t *literals, size_t size,
                     void *data);
    bool (*project)(const clingo_atom_t *atoms, size_t size, void *data);
    bool (*output_atom)(clingo_symbol_t symbol, clingo_atom_t atom, void *data);
    bool (*output_term)(clingo_symbol_t symbol, const clingo_literal_t *condition,
                        size_t size, void *data);
    bool (*external)(clingo_atom_t atom, int type, void *data);
    bool (*assume)(const clingo_literal_t *literals, size_t size, void *data);
    bool (*heuristic)(clingo_atom_t atom, int type, int bias, unsigned priority,
                      const clingo_literal_t *condition, size_t size, void *data);
    bool (*acyc_edge)(int node_u, int node_v, const clingo_literal_t *condition,
                      size_t size, void *data);
    bool (*theory_term_number)(clingo_atom_t term_id, int number, void *data);
    bool (*theory_term_string)(clingo_atom_t term_id, const char *name, void *data);
    bool (*theory_term_compound)(clingo_atom_t term_id, int name_id_or_type,
                                 const clingo_atom_t *arguments, size_t size,
                                 void *data);
    bool (*theory_element)(clingo_atom_t element_id, const clingo_atom_t *terms,
                           size_t terms_size, const clingo_literal_t *condition,
                           size_t condition_size, void *data);
    bool (*theory_atom)(clingo_atom_t atom_id_or_zero, clingo_atom_t term_id,
                        const clingo_atom_t *elements, size_t size, void *data);
    bool (*theory_atom_with_guard)(clingo_atom_t atom_id_or_zero,
                                   clingo_atom_t term_id,
                                   const clingo_atom_t *elements, size_t size,
                                   clingo_atom_t operator_id,
                                   clingo_atom_t right_hand_side_id, void *data);
} clingo_ground_program_observer_t;

enum { CLINGO_SYMBOL_NUMBER = 1, CLINGO_SYMBOL_STRING = 4, CLINGO_SYMBOL_FUNCTION = 5 };
enum { CLINGO_SOLVE_MODE_YIELD = 2 };
enum { CLINGO_SHOW_SHOWN = 2 };

static struct {
    void *handle;
    bool loaded;
    const char *(*error_message)(void);
    bool (*control_new)(const char *const *, size_t, clingo_logger_t, void *,
                        unsigned, clingo_control_t **);
    void (*control_free)(clingo_control_t *);
    bool (*control_add)(clingo_control_t *, const char *, const char *const *,
                        size_t, const char *);
    bool (*control_ground)(clingo_control_t *, const clingo_part_t *, size_t,
                           void *, void *);
    bool (*control_register_observer)(clingo_control_t *,
                                      const clingo_ground_program_observer_t *,
                                      bool, void *);
    bool (*control_backend)(clingo_control_t *, clingo_backend_t **);
    bool (*backend_begin)(clingo_backend_t *);
    bool (*backend_end)(clingo_backend_t *);
    bool (*backend_add_atom)(clingo_backend_t *, const clingo_symbol_t *,
                             clingo_atom_t *);
    bool (*backend_rule)(clingo_backend_t *, bool, const clingo_atom_t *, size_t,
                         const clingo_literal_t *, size_t);
    bool (*control_solve)(clingo_control_t *, int, const clingo_literal_t *,
                          size_t, void *, void *, clingo_solve_handle_t **);
    bool (*solve_handle_resume)(clingo_solve_handle_t *);
    bool (*solve_handle_model)(clingo_solve_handle_t *, const clingo_model_t **);
    bool (*solve_handle_close)(clingo_solve_handle_t *);
    bool (*model_is_true)(const clingo_model_t *, clingo_literal_t, bool *);
    int (*symbol_type)(clingo_symbol_t);
    bool (*symbol_number)(clingo_symbol_t, int *);
    bool (*symbol_name)(clingo_symbol_t, const char **);
    bool (*symbol_string)(clingo_symbol_t, const char **);
    bool (*symbol_arguments)(clingo_symbol_t, const clingo_symbol_t **, size_t *);
    bool (*symbol_is_negative)(clingo_symbol_t, bool *);
    bool (*model_symbols_size)(const clingo_model_t *, unsigned, size_t *);
    bool (*model_symbols)(const clingo_model_t *, unsigned, clingo_symbol_t *, size_t);
} clingo;

static pthread_once_t clingo_once = PTHREAD_ONCE_INIT;

static void asp_clingo_load(void) {
    const char *path = getenv("CETTA_CLINGO_LIBRARY");
    void *handle = dlopen(path && *path ? path : "libclingo.so.4",
                          RTLD_NOW | RTLD_LOCAL);
    if (!handle)
        return;
#define CLINGO_BIND(field, name) \
    if (!(*(void **)&clingo.field = dlsym(handle, name))) { dlclose(handle); return; }
    CLINGO_BIND(error_message, "clingo_error_message")
    CLINGO_BIND(control_new, "clingo_control_new")
    CLINGO_BIND(control_free, "clingo_control_free")
    CLINGO_BIND(control_add, "clingo_control_add")
    CLINGO_BIND(control_ground, "clingo_control_ground")
    CLINGO_BIND(control_register_observer, "clingo_control_register_observer")
    CLINGO_BIND(control_backend, "clingo_control_backend")
    CLINGO_BIND(backend_begin, "clingo_backend_begin")
    CLINGO_BIND(backend_end, "clingo_backend_end")
    CLINGO_BIND(backend_add_atom, "clingo_backend_add_atom")
    CLINGO_BIND(backend_rule, "clingo_backend_rule")
    CLINGO_BIND(control_solve, "clingo_control_solve")
    CLINGO_BIND(solve_handle_resume, "clingo_solve_handle_resume")
    CLINGO_BIND(solve_handle_model, "clingo_solve_handle_model")
    CLINGO_BIND(solve_handle_close, "clingo_solve_handle_close")
    CLINGO_BIND(model_is_true, "clingo_model_is_true")
    CLINGO_BIND(symbol_type, "clingo_symbol_type")
    CLINGO_BIND(symbol_number, "clingo_symbol_number")
    CLINGO_BIND(symbol_name, "clingo_symbol_name")
    CLINGO_BIND(symbol_string, "clingo_symbol_string")
    CLINGO_BIND(symbol_arguments, "clingo_symbol_arguments")
    CLINGO_BIND(symbol_is_negative, "clingo_symbol_is_negative")
    CLINGO_BIND(model_symbols_size, "clingo_model_symbols_size")
    CLINGO_BIND(model_symbols, "clingo_model_symbols")
#undef CLINGO_BIND
    clingo.handle = handle;
    clingo.loaded = true;
}

bool asp_clingo_available(void) {
    pthread_once(&clingo_once, asp_clingo_load);
    return clingo.loaded;
}

static const char *asp_clingo_error(void) {
    const char *message = clingo.error_message();
    return message && *message ? message : "clingo failed";
}

/* clingo prints its warnings; a reference run keeps them out of the
 * caller's output. */
static void asp_clingo_quiet(int code, const char *message, void *data) {
    (void)code; (void)message; (void)data;
}

typedef struct {
    const AspClingoGroundSink *sink;
    void *context;
    int32_t *literals;
    int32_t *weights;
    size_t cap;
} AspClingoObserver;

static bool observe_rule(bool choice, const clingo_atom_t *head, size_t head_size,
                         const clingo_literal_t *body, size_t body_size,
                         void *data) {
    AspClingoObserver *o = data;
    return o->sink->rule(o->context, choice, head, head_size, body, body_size);
}

static bool observe_weight_rule(bool choice, const clingo_atom_t *head,
                                size_t head_size, clingo_weight_t lower,
                                const clingo_weighted_literal_t *body,
                                size_t body_size, void *data) {
    AspClingoObserver *o = data;
    if (body_size > o->cap) {
        size_t cap = body_size * 2u;
        int32_t *literals = realloc(o->literals, sizeof(*literals) * cap);
        if (!literals)
            return false;
        o->literals = literals;
        int32_t *weights = realloc(o->weights, sizeof(*weights) * cap);
        if (!weights)
            return false;
        o->weights = weights;
        o->cap = cap;
    }
    for (size_t i = 0u; i < body_size; i++) {
        o->literals[i] = body[i].literal;
        o->weights[i] = body[i].weight;
    }
    return o->sink->weight_rule(o->context, choice, head, head_size, lower,
                                o->literals, o->weights, body_size);
}

static bool observe_output_atom(clingo_symbol_t symbol, clingo_atom_t atom,
                                void *data) {
    AspClingoObserver *o = data;
    return o->sink->output(o->context, symbol, atom);
}

static bool observe_unsupported(void *data, const char *what) {
    AspClingoObserver *o = data;
    return o->sink->unsupported(o->context, what);
}

static bool observe_minimize(clingo_weight_t priority,
                             const clingo_weighted_literal_t *literals,
                             size_t size, void *data) {
    AspClingoObserver *o = data;
    if (!o->sink->minimize)
        return observe_unsupported(data, "minimize statement");
    if (size > o->cap) {
        size_t cap = size * 2u;
        int32_t *lits = realloc(o->literals, sizeof(*lits) * cap);
        if (!lits)
            return false;
        o->literals = lits;
        int32_t *weights = realloc(o->weights, sizeof(*weights) * cap);
        if (!weights)
            return false;
        o->weights = weights;
        o->cap = cap;
    }
    for (size_t i = 0u; i < size; i++) {
        o->literals[i] = literals[i].literal;
        o->weights[i] = literals[i].weight;
    }
    return o->sink->minimize(o->context, priority, o->literals, o->weights,
                             size);
}

static bool observe_external(clingo_atom_t atom, int type, void *data) {
    (void)atom; (void)type;
    return observe_unsupported(data, "external atom");
}

static bool observe_heuristic(clingo_atom_t atom, int type, int bias,
                              unsigned priority,
                              const clingo_literal_t *condition, size_t size,
                              void *data) {
    (void)atom; (void)type; (void)bias; (void)priority; (void)condition;
    (void)size;
    return observe_unsupported(data, "heuristic directive");
}

static bool observe_acyc_edge(int node_u, int node_v,
                              const clingo_literal_t *condition, size_t size,
                              void *data) {
    (void)node_u; (void)node_v; (void)condition; (void)size;
    return observe_unsupported(data, "edge directive");
}

static bool observe_theory_atom(clingo_atom_t atom, clingo_atom_t term,
                                const clingo_atom_t *elements, size_t size,
                                void *data) {
    (void)atom; (void)term; (void)elements; (void)size;
    return observe_unsupported(data, "theory atom");
}

static bool observe_theory_atom_with_guard(clingo_atom_t atom,
                                           clingo_atom_t term,
                                           const clingo_atom_t *elements,
                                           size_t size, clingo_atom_t op,
                                           clingo_atom_t rhs, void *data) {
    (void)atom; (void)term; (void)elements; (void)size; (void)op; (void)rhs;
    return observe_unsupported(data, "theory atom");
}

/* A shown term under a condition, which is no atom, has no reading as a
 * statement over atoms. */
static bool observe_output_term(clingo_symbol_t symbol,
                                const clingo_literal_t *condition, size_t size,
                                void *data) {
    (void)symbol; (void)condition; (void)size;
    return observe_unsupported(data, "shown term");
}

bool asp_clingo_ground(const char *text, const AspClingoGroundSink *sink,
                       void *context, const char **error) {
    *error = NULL;
    if (!asp_clingo_available()) {
        *error = "clingo is not loaded";
        return false;
    }
    clingo_control_t *control = NULL;
    if (!clingo.control_new(NULL, 0u, asp_clingo_quiet, NULL, 20u, &control)) {
        *error = asp_clingo_error();
        return false;
    }
    AspClingoObserver observer = {.sink = sink, .context = context};
    clingo_ground_program_observer_t calls = {
        .rule = observe_rule,
        .weight_rule = observe_weight_rule,
        .minimize = observe_minimize,
        .output_atom = observe_output_atom,
        .output_term = observe_output_term,
        .external = observe_external,
        .heuristic = observe_heuristic,
        .acyc_edge = observe_acyc_edge,
        .theory_atom = observe_theory_atom,
        .theory_atom_with_guard = observe_theory_atom_with_guard,
    };
    clingo_part_t base = {.name = "base", .params = NULL, .size = 0u};
    /* replace: the grounding goes to the observer only, never into clasp's
     * own program, which nothing here solves */
    bool ok = clingo.control_register_observer(control, &calls, true, &observer) &&
        clingo.control_add(control, "base", NULL, 0u, text) &&
        clingo.control_ground(control, &base, 1u, NULL, NULL);
    if (!ok)
        *error = asp_clingo_error();
    free(observer.literals);
    free(observer.weights);
    clingo.control_free(control);
    return ok;
}

AspClingoSymbolKind asp_clingo_symbol_kind(AspClingoSymbol symbol) {
    switch (clingo.symbol_type(symbol)) {
    case CLINGO_SYMBOL_NUMBER: return ASP_CLINGO_SYMBOL_NUMBER;
    case CLINGO_SYMBOL_STRING: return ASP_CLINGO_SYMBOL_STRING;
    case CLINGO_SYMBOL_FUNCTION: return ASP_CLINGO_SYMBOL_FUNCTION;
    default: return ASP_CLINGO_SYMBOL_OTHER;
    }
}

bool asp_clingo_symbol_number(AspClingoSymbol symbol, int *number) {
    return clingo.symbol_number(symbol, number);
}

bool asp_clingo_symbol_string(AspClingoSymbol symbol, const char **text) {
    return clingo.symbol_string(symbol, text);
}

bool asp_clingo_symbol_function(AspClingoSymbol symbol, const char **name,
                                const AspClingoSymbol **arguments,
                                size_t *count, bool *negative) {
    return clingo.symbol_name(symbol, name) &&
        clingo.symbol_arguments(symbol, arguments, count) &&
        clingo.symbol_is_negative(symbol, negative);
}

bool asp_clingo_models(uint32_t atom_count, const AspClingoRule *rules,
                       size_t rule_count,
                       AspClingoNext (*model)(void *context,
                                              const uint32_t *atoms,
                                              size_t len),
                       void *context, const char **error) {
    *error = NULL;
    if (!asp_clingo_available()) {
        *error = "clingo is not loaded";
        return false;
    }
    const char *arguments[] = {"--models=0"};
    clingo_control_t *control = NULL;
    if (!clingo.control_new(arguments, 1u, asp_clingo_quiet, NULL, 20u, &control)) {
        *error = asp_clingo_error();
        return false;
    }
    clingo_atom_t *ids = malloc(sizeof(*ids) * ((size_t)atom_count + 1u));
    clingo_literal_t *body = NULL;
    size_t body_cap = 0u;
    uint32_t *true_atoms = malloc(sizeof(*true_atoms) * ((size_t)atom_count + 1u));
    clingo_backend_t *backend = NULL;
    bool ok = ids && true_atoms && clingo.control_backend(control, &backend) &&
        clingo.backend_begin(backend);
    for (uint32_t a = 1u; ok && a <= atom_count; a++)
        ok = clingo.backend_add_atom(backend, NULL, &ids[a]);
    for (size_t r = 0u; ok && r < rule_count; r++) {
        const AspClingoRule *rule = &rules[r];
        size_t len = rule->pos_len + rule->neg_len;
        if (len > body_cap) {
            body_cap = len * 2u;
            clingo_literal_t *grown = realloc(body, sizeof(*grown) * body_cap);
            if (!(ok = grown != NULL))
                break;
            body = grown;
        }
        for (size_t i = 0u; i < rule->pos_len; i++)
            body[i] = (clingo_literal_t)ids[rule->pos[i]];
        for (size_t i = 0u; i < rule->neg_len; i++)
            body[rule->pos_len + i] = -(clingo_literal_t)ids[rule->neg[i]];
        clingo_atom_t head = rule->head ? ids[rule->head] : 0u;
        ok = clingo.backend_rule(backend, rule->choice, &head,
                                 rule->head ? 1u : 0u, body, len);
    }
    if (backend && !clingo.backend_end(backend))
        ok = false;
    clingo_solve_handle_t *handle = NULL;
    if (ok)
        ok = clingo.control_solve(control, CLINGO_SOLVE_MODE_YIELD, NULL, 0u,
                                  NULL, NULL, &handle);
    while (ok) {
        const clingo_model_t *found = NULL;
        if (!(ok = clingo.solve_handle_resume(handle) &&
                   clingo.solve_handle_model(handle, &found)) || !found)
            break;
        size_t len = 0u;
        for (uint32_t a = 1u; ok && a <= atom_count; a++) {
            bool holds = false;
            ok = clingo.model_is_true(found, (clingo_literal_t)ids[a], &holds);
            if (holds)
                true_atoms[len++] = a;
        }
        if (!ok)
            break;
        AspClingoNext next = model(context, true_atoms, len);
        if (!(ok = next != ASP_CLINGO_FAIL) || next == ASP_CLINGO_STOP)
            break;
    }
    if (!ok && !*error)
        *error = asp_clingo_error();
    if (handle && !clingo.solve_handle_close(handle) && ok) {
        ok = false;
        *error = asp_clingo_error();
    }
    free(ids);
    free(body);
    free(true_atoms);
    clingo.control_free(control);
    return ok;
}

bool asp_clingo_text_models(const char *text,
                            AspClingoNext (*model)(void *context,
                                                   const AspClingoSymbol *symbols,
                                                   size_t len),
                            void *context, const char **error) {
    *error = NULL;
    if (!asp_clingo_available()) {
        *error = "clingo is not loaded";
        return false;
    }
    const char *arguments[] = {"--models=0"};
    clingo_control_t *control = NULL;
    if (!clingo.control_new(arguments, 1u, asp_clingo_quiet, NULL, 20u, &control)) {
        *error = asp_clingo_error();
        return false;
    }
    clingo_part_t base = {.name = "base", .params = NULL, .size = 0u};
    clingo_solve_handle_t *handle = NULL;
    clingo_symbol_t *symbols = NULL;
    size_t cap = 0u;
    bool ok = clingo.control_add(control, "base", NULL, 0u, text) &&
        clingo.control_ground(control, &base, 1u, NULL, NULL) &&
        clingo.control_solve(control, CLINGO_SOLVE_MODE_YIELD, NULL, 0u,
                             NULL, NULL, &handle);
    while (ok) {
        const clingo_model_t *found = NULL;
        if (!(ok = clingo.solve_handle_resume(handle) &&
                   clingo.solve_handle_model(handle, &found)) || !found)
            break;
        size_t len = 0u;
        if (!(ok = clingo.model_symbols_size(found, CLINGO_SHOW_SHOWN, &len)))
            break;
        if (len > cap) {
            clingo_symbol_t *grown = realloc(symbols, sizeof(*grown) * len);
            if (!(ok = grown != NULL))
                break;
            symbols = grown;
            cap = len;
        }
        if (!(ok = clingo.model_symbols(found, CLINGO_SHOW_SHOWN, symbols, len)))
            break;
        AspClingoNext next = model(context, symbols, len);
        if (!(ok = next != ASP_CLINGO_FAIL) || next == ASP_CLINGO_STOP)
            break;
    }
    if (!ok && !*error)
        *error = asp_clingo_error();
    if (handle && !clingo.solve_handle_close(handle) && ok) {
        ok = false;
        *error = asp_clingo_error();
    }
    free(symbols);
    clingo.control_free(control);
    return ok;
}
