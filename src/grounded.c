#define _GNU_SOURCE
#include "grounded.h"
#include "abt.h"
#include "eval.h"
#include "he_typing.h"
#include "petta_semantics.h"
#include "prime_semantics.h"
#include "match.h"
#include "native_sha256.h"
#include "parser.h"
#include "petta_numeric.h"
#include "space.h"
#if CETTA_BUILD_WITH_GMP
#include <gmp.h>
#endif
#include <inttypes.h>
#include <string.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>

#if defined(__GNUC__)
__attribute__((weak))
#endif
bool eval_current_prefer_rationals(void) {
    return false;
}

#if defined(__GNUC__)
__attribute__((weak))
#endif
bool eval_current_uses_rust_he_compat_semantics(void) {
    return false;
}

typedef struct {
    char *buf;
    size_t len;
    size_t cap;
} StringBuf;

typedef struct {
    VarId var_id;
    Atom *mapped;
} FoldVarMapEntry;

typedef struct {
    FoldVarMapEntry *items;
    uint32_t len;
    uint32_t cap;
} FoldVarMap;

static void sb_init(StringBuf *sb) {
    sb->buf = NULL;
    sb->len = 0;
    sb->cap = 0;
}

static void sb_ensure(StringBuf *sb, size_t extra) {
    size_t need = sb->len + extra + 1;
    if (need <= sb->cap) return;
    size_t next = sb->cap ? sb->cap * 2 : 64;
    while (next < need) next *= 2;
    sb->buf = cetta_realloc(sb->buf, next);
    sb->cap = next;
}

static void sb_append_n(StringBuf *sb, const char *s, size_t n) {
    sb_ensure(sb, n);
    memcpy(sb->buf + sb->len, s, n);
    sb->len += n;
    sb->buf[sb->len] = '\0';
}

static void sb_append(StringBuf *sb, const char *s) {
    sb_append_n(sb, s, strlen(s));
}

static void sb_append_char(StringBuf *sb, char value) {
    sb_append_n(sb, &value, 1u);
}

static void sb_free(StringBuf *sb) {
    free(sb->buf);
    sb->buf = NULL;
    sb->len = 0;
    sb->cap = 0;
}

typedef enum {
    PETTA_REPRA_ATOM = 0,
    PETTA_REPRA_CHAR,
} PettaRepraActionKind;

typedef struct {
    PettaRepraActionKind kind;
    Atom *atom;
    char character;
} PettaRepraAction;

typedef struct {
    PettaRepraAction *items;
    size_t length;
    size_t capacity;
} PettaRepraStack;

static bool petta_repra_push(
    PettaRepraStack *stack, PettaRepraAction action) {
    if (stack->length == stack->capacity) {
        size_t next = stack->capacity ? stack->capacity * 2u : 64u;
        if (next <= stack->capacity ||
            next > SIZE_MAX / sizeof(*stack->items)) {
            return false;
        }
        stack->items = cetta_realloc(
            stack->items, sizeof(*stack->items) * next);
        stack->capacity = next;
    }
    stack->items[stack->length++] = action;
    return true;
}

static bool petta_repra_push_atom(
    PettaRepraStack *stack, Atom *atom) {
    return petta_repra_push(
        stack,
        (PettaRepraAction){
            .kind = PETTA_REPRA_ATOM,
            .atom = atom,
        });
}

static bool petta_repra_push_char(
    PettaRepraStack *stack, char character) {
    return petta_repra_push(
        stack,
        (PettaRepraAction){
            .kind = PETTA_REPRA_CHAR,
            .character = character,
        });
}

static bool petta_repra_plain_identifier(const char *text) {
    if (!text || text[0] < 'a' || text[0] > 'z')
        return false;
    for (const unsigned char *cursor =
             (const unsigned char *)text + 1u;
         *cursor; cursor++) {
        if (!((*cursor >= 'a' && *cursor <= 'z') ||
              (*cursor >= 'A' && *cursor <= 'Z') ||
              (*cursor >= '0' && *cursor <= '9') ||
              *cursor == '_')) {
            return false;
        }
    }
    return true;
}

static bool petta_repra_plain_graphic(const char *text) {
    static const char *const graphic = "#$&*+-./:<=>?@^~\\`";
    if (!text || !text[0] || strcmp(text, ".") == 0 ||
        strcmp(text, ",") == 0) {
        return false;
    }
    for (const char *cursor = text; *cursor; cursor++) {
        if (!strchr(graphic, *cursor))
            return false;
    }
    return true;
}

static bool petta_repra_plain_symbol(const char *text) {
    return text &&
           (petta_repra_plain_identifier(text) ||
           petta_repra_plain_graphic(text) ||
           strcmp(text, "{}") == 0 ||
           strcmp(text, "!") == 0 ||
           strcmp(text, ";") == 0);
}

static void petta_repra_append_symbol(
    StringBuf *output, const char *text) {
    if (petta_repra_plain_symbol(text)) {
        sb_append(output, text);
        return;
    }
    sb_append_char(output, '\'');
    for (const unsigned char *cursor =
             (const unsigned char *)(text ? text : "");
         *cursor; cursor++) {
        switch (*cursor) {
        case '\'': sb_append(output, "\\\'"); break;
        case '\\': sb_append(output, "\\\\"); break;
        case '\n': sb_append(output, "\\n"); break;
        case '\r': sb_append(output, "\\r"); break;
        case '\t': sb_append(output, "\\t"); break;
        default: sb_append_char(output, (char)*cursor); break;
        }
    }
    sb_append_char(output, '\'');
}

static void petta_repra_append_string(
    StringBuf *output, const char *text) {
    sb_append_char(output, '"');
    for (const unsigned char *cursor =
             (const unsigned char *)(text ? text : "");
         *cursor; cursor++) {
        switch (*cursor) {
        case '"': sb_append(output, "\\\""); break;
        case '\\': sb_append(output, "\\\\"); break;
        case '\n': sb_append(output, "\\n"); break;
        case '\r': sb_append(output, "\\r"); break;
        case '\t': sb_append(output, "\\t"); break;
        default: sb_append_char(output, (char)*cursor); break;
        }
    }
    sb_append_char(output, '"');
}

/*
 * PeTTa's repra/1 is SWI term_to_atom/2: expressions are Prolog lists and
 * atoms use quoted(true) syntax.  Render directly instead of entering SWI on
 * every key; tile-style programs deliberately use the resulting Symbol as a
 * hashed set key.  The explicit stack keeps this safe for deep MeTTa data.
 */
static bool petta_repra_render(
    Arena *arena, Atom *root, StringBuf *output) {
    PettaRepraStack stack = {0};
    bool ok = root && output;
    if (ok)
        ok = petta_repra_push_atom(&stack, root);

    while (ok && stack.length > 0u) {
        PettaRepraAction action = stack.items[--stack.length];
        if (action.kind == PETTA_REPRA_CHAR) {
            sb_append_char(output, action.character);
            continue;
        }

        Atom *atom = action.atom;
        if (!atom) {
            ok = false;
            break;
        }
        switch (atom->kind) {
        case ATOM_SYMBOL:
            petta_repra_append_symbol(
                output, symbol_bytes(g_symbols, atom->sym_id));
            break;
        case ATOM_VAR: {
            char variable[64];
            int length = snprintf(
                variable, sizeof(variable), "_V%" PRIu64,
                (uint64_t)atom->var_id);
            if (length <= 0 || (size_t)length >= sizeof(variable)) {
                ok = false;
                break;
            }
            sb_append_n(output, variable, (size_t)length);
            break;
        }
        case ATOM_GROUNDED:
            switch (atom->ground.gkind) {
            case GV_INT: {
                char integer[64];
                int length = snprintf(
                    integer, sizeof(integer), "%" PRId64,
                    atom->ground.ival);
                if (length <= 0 || (size_t)length >= sizeof(integer)) {
                    ok = false;
                    break;
                }
                sb_append_n(output, integer, (size_t)length);
                break;
            }
            case GV_FLOAT:
            case GV_BIGINT:
            case GV_RATIONAL: {
                char *rendered = cetta_petta_number_to_string(arena, atom);
                if (!rendered) {
                    ok = false;
                    break;
                }
                sb_append(output, rendered);
                break;
            }
            case GV_BOOL:
                sb_append(output, atom->ground.bval ? "true" : "false");
                break;
            case GV_STRING:
                petta_repra_append_string(output, atom->ground.sval);
                break;
            case GV_SPACE:
            case GV_STATE:
            case GV_CAPTURE:
            case GV_FOREIGN:
            case GV_PRIME_NEED_CAPABILITY:
            case GV_PRIME_CONTEXT:
            case GV_INTERNAL_TAG:
                ok = false;
                break;
            }
            break;
        case ATOM_EXPR: {
            Atom *compound = NULL;
            if (atom_petta_prolog_compound_body(atom, &compound)) {
                CettaExprLen length = compound->expr.len;
                petta_repra_append_symbol(
                    output,
                    symbol_bytes(
                        g_symbols, compound->expr.elems[0]->sym_id));
                if (length == 1u)
                    break;
                sb_append_char(output, '(');
                if (!petta_repra_push_char(&stack, ')')) {
                    ok = false;
                    break;
                }
                for (CettaExprIndex index = length;
                     index > 1u; index--) {
                    if (!petta_repra_push_atom(
                            &stack,
                            compound->expr.elems[index - 1u]) ||
                        (index > 2u &&
                         !petta_repra_push_char(&stack, ','))) {
                        ok = false;
                        break;
                    }
                }
                break;
            }
            if (petta_semantics_is_open_cons_value(atom)) {
                Atom *materialized =
                    petta_semantics_materialize_closed_logical_list(
                        arena, atom);
                if (!materialized) {
                    ok = false;
                    break;
                }
                atom = materialized;
            }
            sb_append_char(output, '[');
            if (!petta_repra_push_char(&stack, ']')) {
                ok = false;
                break;
            }
            for (CettaExprIndex index = atom->expr.len;
                 index > 0u; index--) {
                if (!petta_repra_push_atom(
                        &stack, atom->expr.elems[index - 1u]) ||
                    (index > 1u &&
                     !petta_repra_push_char(&stack, ','))) {
                    ok = false;
                    break;
                }
            }
            break;
        }
        }
    }
    free(stack.items);
    return ok;
}

static uint32_t next_pow2_u32(uint32_t n) {
    uint32_t cap = 1;
    while (cap < n && cap < (UINT32_MAX >> 1))
        cap <<= 1;
    return cap;
}

static void fold_var_map_init(FoldVarMap *map) {
    map->items = NULL;
    map->len = 0;
    map->cap = 0;
}

static void fold_var_map_free(FoldVarMap *map) {
    free(map->items);
    map->items = NULL;
    map->len = 0;
    map->cap = 0;
}

static Atom *fold_var_map_lookup(FoldVarMap *map, VarId var_id) {
    for (uint32_t i = 0; i < map->len; i++) {
        if (map->items[i].var_id == var_id)
            return map->items[i].mapped;
    }
    return NULL;
}

static Atom *fold_var_map_add_fresh(FoldVarMap *map, Arena *a,
                                    Atom *source_var) {
    Atom *fresh = atom_var_like(a, source_var, fresh_var_id());
    if (!fresh) return NULL;
    if (map->len >= map->cap) {
        map->cap = map->cap ? map->cap * 2 : 8;
        map->items = cetta_realloc(map->items, sizeof(FoldVarMapEntry) * map->cap);
    }
    map->items[map->len].var_id = source_var->var_id;
    map->items[map->len].mapped = fresh;
    map->len++;
    return fresh;
}

static Atom *grounded_call_expr(Arena *a, Atom *head, Atom **args, uint32_t nargs) {
    Atom **elems = arena_alloc(a, sizeof(Atom *) * (nargs + 1));
    elems[0] = head;
    for (uint32_t i = 0; i < nargs; i++)
        elems[i + 1] = args[i];
    return atom_expr(a, elems, nargs + 1);
}

static Atom *foldl_bind_step_atom_impl(Arena *a, Atom *atom,
                                       Atom *acc_var, Atom *acc_val,
                                       Atom *item_var, Atom *item_val,
                                       FoldVarMap *fresh_vars) {
    switch (atom->kind) {
    case ATOM_VAR:
        if (acc_var && atom->var_id == acc_var->var_id)
            return acc_val;
        if (item_var && atom->var_id == item_var->var_id)
            return item_val;
        {
            Atom *mapped = fold_var_map_lookup(fresh_vars, atom->var_id);
            if (mapped)
                return mapped;
            return fold_var_map_add_fresh(fresh_vars, a, atom);
        }
    case ATOM_EXPR: {
        Atom **elems = arena_alloc(a, sizeof(Atom *) * atom->expr.len);
        bool changed = false;
        for (CettaExprIndex i = 0; i < atom->expr.len; i++) {
            elems[i] = foldl_bind_step_atom_impl(a, atom->expr.elems[i],
                                                 acc_var, acc_val,
                                                 item_var, item_val,
                                                 fresh_vars);
            if (elems[i] != atom->expr.elems[i])
                changed = true;
        }
        if (!changed)
            return atom;
        return atom_expr(a, elems, atom->expr.len);
    }
    default:
        return atom;
    }
}

Atom *cetta_fold_bind_step_atom(Arena *a, Atom *atom,
                                Atom *acc_var, Atom *acc_val,
                                Atom *item_var, Atom *item_val) {
    FoldVarMap fresh_vars;
    fold_var_map_init(&fresh_vars);
    Atom *bound = foldl_bind_step_atom_impl(a, atom,
                                            acc_var, acc_val,
                                            item_var, item_val,
                                            &fresh_vars);
    fold_var_map_free(&fresh_vars);
    return bound;
}

static Atom *grounded_bad_arg_type(Arena *a, Atom *head, Atom **args, uint32_t nargs,
                                   int bad_idx, Atom *expected_type, Atom *actual_atom) {
    Atom *actual_type = (actual_atom && actual_atom->kind == ATOM_GROUNDED)
        ? get_grounded_type(a, actual_atom)
        : get_meta_type(a, actual_atom);
    Atom *reason = atom_expr(a, (Atom*[]){
        atom_symbol(a, "BadArgType"),
        atom_int(a, bad_idx),
        expected_type,
        actual_type
    }, 4);
    return atom_error(a, grounded_call_expr(a, head, args, nargs), reason);
}

static Atom *grounded_string_error(Arena *a, Atom *head, Atom **args, uint32_t nargs,
                                   const char *message) {
    return atom_error(a, grounded_call_expr(a, head, args, nargs),
                      atom_symbol(a, message));
}

static Atom *grounded_incorrect_arity(Arena *a, Atom *head, Atom **args, uint32_t nargs) {
    return atom_error(a, grounded_call_expr(a, head, args, nargs),
                      atom_symbol(a, "IncorrectNumberOfArguments"));
}

static Atom *grounded_expr_message_error(Arena *a, Atom *head, Atom **args, uint32_t nargs,
                                         const char *prefix, Atom *expr) {
    char buf[1024];
    int pos = snprintf(buf, sizeof(buf), "%s", prefix);
    if (pos < 0) pos = 0;
    if ((size_t)pos < sizeof(buf)) {
        FILE *tmp = fmemopen(buf + pos, sizeof(buf) - (size_t)pos, "w");
        if (tmp) {
            atom_print(expr, tmp);
            fclose(tmp);
        }
    }
    buf[sizeof(buf) - 1] = '\0';
    return atom_error(a, grounded_call_expr(a, head, args, nargs),
                      atom_symbol(a, buf));
}

static bool find_unused_alpha_equal_atom(Atom **elems, bool *used,
                                         CettaExprLen len, Atom *candidate,
                                         CettaExprIndex *out_index) {
    for (CettaExprIndex i = 0; i < len; i++) {
        if (!used[i] && atom_alpha_eq(elems[i], candidate)) {
            if (out_index)
                *out_index = i;
            return true;
        }
    }
    return false;
}

#define GROUNDED_OP_CACHE_CAP 64u

enum {
    GROUNDED_OP_CACHE_COMMON = 1u << 0,
    GROUNDED_OP_CACHE_PRIME = 1u << 1,
    GROUNDED_OP_CACHE_PETTA = 1u << 2,
    GROUNDED_OP_CACHE_DEPENDENT_TYPING = 1u << 3,
};

typedef struct {
    const SymbolTable *table;
    uint64_t table_instance_id;
    SymbolId ids[GROUNDED_OP_CACHE_CAP];
    uint8_t capabilities[GROUNDED_OP_CACHE_CAP];
} GroundedOpCache;

static _Thread_local GroundedOpCache g_grounded_op_cache;

static bool grounded_op_capabilities_apply(uint8_t capabilities) {
    if ((capabilities & GROUNDED_OP_CACHE_COMMON) != 0u)
        return true;
    if (capabilities == 0u)
        return false;

    const CettaLanguageId language_id = eval_current_language_id
        ? eval_current_language_id() : CETTA_LANGUAGE_HE;
    if (language_id == CETTA_LANGUAGE_PRIME &&
        (capabilities & GROUNDED_OP_CACHE_PRIME) != 0u) {
        return true;
    }
    if (language_id == CETTA_LANGUAGE_PETTA &&
        (capabilities & GROUNDED_OP_CACHE_PETTA) != 0u) {
        return true;
    }
    return (capabilities & GROUNDED_OP_CACHE_DEPENDENT_TYPING) != 0u &&
        eval_current_profile_enables_dependent_telescope &&
        eval_current_profile_enables_dependent_telescope();
}

bool is_grounded_op(SymbolId id) {
    if (id == SYMBOL_ID_NONE)
        return false;

    const uint64_t table_instance_id =
        symbol_table_instance_id(g_symbols);
    GroundedOpCache *cache = &g_grounded_op_cache;
    if (cache->table != g_symbols ||
        cache->table_instance_id != table_instance_id) {
        memset(cache, 0, sizeof(*cache));
        cache->table = g_symbols;
        cache->table_instance_id = table_instance_id;
    }
    size_t cache_slot =
        ((size_t)id * UINT32_C(2654435761)) &
        (GROUNDED_OP_CACHE_CAP - 1u);
    if (cache->ids[cache_slot] == id)
        return grounded_op_capabilities_apply(
            cache->capabilities[cache_slot]);

    uint8_t capabilities = 0u;
    /* Compiled operators are opcodes: dispatch their interned IDs before
       consulting any extensible name-based registry. */
    if ((symbol_flags(g_symbols, id) &
         CETTA_SYMBOL_FLAG_STATIC_GROUNDED_OP) != 0u) {
        capabilities = GROUNDED_OP_CACHE_COMMON;
        goto classified;
    }

    /* Check __cetta_lib_ prefix via string lookup only for non-static IDs. */
    const char *name = symbol_bytes(g_symbols, id);
    if (name && strncmp(name, "__cetta_lib_", 12) == 0) {
        capabilities = GROUNDED_OP_CACHE_COMMON;
        goto classified;
    }

    /* Cache dialect membership rather than a result for the currently active
       profile.  Most symbols have no capability bits and stay on the universal
       false path; profile changes remain exact for the few dialect operators. */
    if (name) {
        bool prime_op = prime_semantics_is_op_id
            ? prime_semantics_is_op_id(id)
            : prime_semantics_is_op && prime_semantics_is_op(name);
        if (prime_op)
            capabilities |= GROUNDED_OP_CACHE_PRIME;

        bool typing_op = he_typing_is_op_id
            ? he_typing_is_op_id(id)
            : he_typing_is_op && he_typing_is_op(name);
        if (typing_op)
            capabilities |= GROUNDED_OP_CACHE_DEPENDENT_TYPING;
    }
    if (petta_semantics_form(id) == PETTA_FORM_REPRA)
        capabilities |= GROUNDED_OP_CACHE_PETTA;

classified:
    cache->ids[cache_slot] = id;
    cache->capabilities[cache_slot] = capabilities;
    return grounded_op_capabilities_apply(capabilities);
}

/* Deliberately absent from the type-pure capability: space mutation
   (add-atom/remove-atom, mork ops), I/O (println!/trace!/print-alternatives!),
   foreign calls (py-*), evaluator-coupled folds, parsing, `size` (reads live
   mutable space state), and every __cetta_lib_ op (semantics not audited).
   Everything here is a pure function of its argument atoms. */
bool grounded_op_is_type_pure(SymbolId id) {
    return (symbol_flags(g_symbols, id) &
            CETTA_SYMBOL_FLAG_TYPE_PURE_GROUNDED_OP) != 0u;
}

/* ── Numeric arg extraction (int or float, promote to double) ──────────── */

typedef struct {
    double val;
    int64_t ival;
    Atom *bigint;
    Atom *rational;
    bool is_float;
    bool is_bigint;
    bool is_rational;
} NumArg;

static bool get_numeric_arg(Atom *a, NumArg *out) {
    if (!a || !out)
        return false;
    /*
     * SWI reads `inf` and `-inf` as atoms but its arithmetic evaluator
     * recognizes them as numeric constants.  Keep the authored atom intact
     * for quotation, equality, and metatype observation; coerce it only at
     * PeTTa's numeric-operation boundary.
     */
    if (a->kind == ATOM_SYMBOL &&
        eval_current_language_id &&
        eval_current_language_id() == CETTA_LANGUAGE_PETTA &&
        (symbol_eq_cstr(g_symbols, a->sym_id, "inf") ||
         symbol_eq_cstr(g_symbols, a->sym_id, "-inf") ||
         symbol_eq_cstr(g_symbols, a->sym_id, "nan"))) {
        bool nan_value = symbol_eq_cstr(g_symbols, a->sym_id, "nan");
        out->val = nan_value
            ? NAN
            : (symbol_eq_cstr(g_symbols, a->sym_id, "-inf")
                   ? -INFINITY : INFINITY);
        out->ival = 0;
        out->bigint = NULL;
        out->rational = NULL;
        out->is_float = true;
        out->is_bigint = false;
        out->is_rational = false;
        return true;
    }
    if (a->kind != ATOM_GROUNDED) return false;
    if (a->ground.gkind == GV_INT) {
        out->val = (double)a->ground.ival;
        out->ival = a->ground.ival;
        out->bigint = NULL;
        out->rational = NULL;
        out->is_float = false;
        out->is_bigint = false;
        out->is_rational = false;
        return true;
    }
    if (a->ground.gkind == GV_BIGINT) {
        out->val = strtod(atom_bigint_cstr(a), NULL);
        out->ival = 0;
        out->bigint = a;
        out->rational = NULL;
        out->is_float = false;
        out->is_bigint = true;
        out->is_rational = false;
        return true;
    }
    if (a->ground.gkind == GV_RATIONAL) {
#if CETTA_BUILD_WITH_GMP
        mpq_t q;
        mpq_init(q);
        bool ok = atom_rational_get_mpq(a, q);
        out->val = ok ? mpq_get_d(q) : 0.0;
        mpq_clear(q);
#else
        out->val = 0.0;
#endif
        out->ival = 0;
        out->bigint = NULL;
        out->rational = a;
        out->is_float = false;
        out->is_bigint = false;
        out->is_rational = true;
        return true;
    }
    if (a->ground.gkind == GV_FLOAT) {
        out->val = a->ground.fval;
        out->ival = 0;
        out->bigint = NULL;
        out->rational = NULL;
        out->is_float = true;
        out->is_bigint = false;
        out->is_rational = false;
        return true;
    }
    return false;
}

static bool get_numeric_arg_for_math(Atom *a, NumArg *out) {
    if (!get_numeric_arg(a, out))
        return false;
    if (eval_current_uses_rust_he_compat_semantics() && out->is_rational)
        return false;
    return true;
}

#if CETTA_BUILD_WITH_GMP
static bool num_arg_to_mpz(const NumArg *arg, mpz_t out) {
    if (!arg || arg->is_float || arg->is_rational)
        return false;
    if (arg->is_bigint)
        return atom_bigint_get_mpz(arg->bigint, out);
    uint64_t magnitude = arg->ival < 0
        ? (uint64_t)(-(arg->ival + 1)) + 1u
        : (uint64_t)arg->ival;
    mpz_import(out, 1, -1, sizeof(magnitude), 0, 0, &magnitude);
    if (arg->ival < 0)
        mpz_neg(out, out);
    return true;
}

static Atom *atom_from_mpz(Arena *a, const mpz_t value) {
    return atom_bigint_from_mpz(a, value);
}

typedef struct {
    mpz_t storage;
    mpz_srcptr value;
    bool owns_storage;
} IntegerMpzView;

static bool integer_mpz_view_init(const NumArg *arg, IntegerMpzView *view) {
    if (!arg || !view || arg->is_float || arg->is_rational)
        return false;
    view->value = NULL;
    view->owns_storage = false;
    if (arg->is_bigint) {
        view->value = atom_bigint_mpz_view(arg->bigint);
        return view->value != NULL;
    }
    mpz_init(view->storage);
    view->owns_storage = true;
    uint64_t magnitude = arg->ival < 0
        ? (uint64_t)(-(arg->ival + 1)) + 1u
        : (uint64_t)arg->ival;
    mpz_import(view->storage, 1, -1, sizeof(magnitude), 0, 0, &magnitude);
    if (arg->ival < 0)
        mpz_neg(view->storage, view->storage);
    view->value = view->storage;
    return true;
}

static void integer_mpz_view_clear(IntegerMpzView *view) {
    if (view && view->owns_storage)
        mpz_clear(view->storage);
}

static Atom *atom_from_integral_double(Arena *a, double value) {
    if (!isfinite(value))
        return NULL;
    mpz_t integer;
    mpz_init(integer);
    mpz_set_d(integer, value);
    Atom *result = atom_from_mpz(a, integer);
    mpz_clear(integer);
    return result;
}

static bool num_arg_to_mpq(const NumArg *arg, mpq_t out) {
    if (!arg || arg->is_float)
        return false;
    if (arg->is_rational)
        return atom_rational_get_mpq(arg->rational, out);
    mpz_t z;
    mpz_init(z);
    bool ok = num_arg_to_mpz(arg, z);
    if (ok)
        mpq_set_z(out, z);
    mpz_clear(z);
    return ok;
}

static bool num_arg_compare_value(
    const NumArg *left, const NumArg *right, int *ordering) {
    if (!left || !right || !ordering)
        return false;
    if (left->is_float && right->is_float) {
        if (isnan(left->val) || isnan(right->val))
            return false;
        *ordering = left->val < right->val
            ? -1
            : left->val > right->val ? 1 : 0;
        return true;
    }
    if (left->is_float || right->is_float) {
        const NumArg *exact = left->is_float ? right : left;
        double floating = left->is_float ? left->val : right->val;
        if (isnan(floating))
            return false;
        if (isinf(floating)) {
            int exact_against_float = signbit(floating) ? 1 : -1;
            *ordering = left->is_float
                ? -exact_against_float
                : exact_against_float;
            return true;
        }
        mpq_t exact_value;
        mpq_t float_value;
        mpq_inits(exact_value, float_value, NULL);
        bool converted = num_arg_to_mpq(exact, exact_value);
        if (converted)
            mpq_set_d(float_value, floating);
        int exact_against_float = converted
            ? mpq_cmp(exact_value, float_value)
            : 0;
        mpq_clears(exact_value, float_value, NULL);
        if (!converted)
            return false;
        exact_against_float =
            exact_against_float < 0
                ? -1
                : exact_against_float > 0 ? 1 : 0;
        *ordering = left->is_float
            ? -exact_against_float
            : exact_against_float;
        return true;
    }

    mpq_t left_value;
    mpq_t right_value;
    mpq_inits(left_value, right_value, NULL);
    bool converted =
        num_arg_to_mpq(left, left_value) &&
        num_arg_to_mpq(right, right_value);
    int result = converted ? mpq_cmp(left_value, right_value) : 0;
    mpq_clears(left_value, right_value, NULL);
    if (!converted)
        return false;
    *ordering = result < 0 ? -1 : result > 0 ? 1 : 0;
    return true;
}

static Atom *atom_from_mpq(Arena *a, const mpq_t value) {
    return atom_rational_from_mpq(a, value);
}

static Atom *grounded_atom_from_mpq(Arena *a, Atom *head, Atom **args,
                                    uint32_t nargs, const mpq_t value) {
    (void)head;
    (void)args;
    (void)nargs;
    return atom_from_mpq(a, value);
}

static Atom *atom_from_floor_div_mpq(Arena *a, const mpq_t lhs,
                                     const mpq_t rhs) {
    if (mpq_sgn(rhs) == 0)
        return NULL;
    mpq_t q;
    mpz_t z;
    mpq_init(q);
    mpz_init(z);
    mpq_div(q, lhs, rhs);
    mpz_fdiv_q(z, mpq_numref(q), mpq_denref(q));
    Atom *out = atom_from_mpz(a, z);
    mpz_clear(z);
    mpq_clear(q);
    return out;
}

static Atom *atom_from_rational_abs(Arena *a, Atom *head, Atom **args,
                                    uint32_t nargs, const NumArg *arg) {
    mpq_t q;
    mpq_init(q);
    bool ok = num_arg_to_mpq(arg, q);
    if (ok && mpq_sgn(q) < 0)
        mpq_neg(q, q);
    Atom *out = ok ? grounded_atom_from_mpq(a, head, args, nargs, q) : NULL;
    mpq_clear(q);
    return out;
}

static Atom *atom_from_rational_integer_part(Arena *a, const NumArg *arg,
                                             SymbolId op) {
    mpq_t q;
    mpz_t z;
    mpq_init(q);
    mpz_init(z);
    bool ok = num_arg_to_mpq(arg, q);
    if (ok) {
        if (op == g_builtin_syms.trunc_math)
            mpz_tdiv_q(z, mpq_numref(q), mpq_denref(q));
        else if (op == g_builtin_syms.floor_math)
            mpz_fdiv_q(z, mpq_numref(q), mpq_denref(q));
        else
            mpz_cdiv_q(z, mpq_numref(q), mpq_denref(q));
    }
    Atom *out = ok ? atom_from_mpz(a, z) : NULL;
    mpz_clear(z);
    mpq_clear(q);
    return out;
}

static Atom *atom_from_rational_round(Arena *a, const NumArg *arg) {
    mpq_t q;
    mpz_t abs_num, quotient, remainder, twice_remainder;
    mpq_init(q);
    mpz_inits(abs_num, quotient, remainder, twice_remainder, NULL);
    bool ok = num_arg_to_mpq(arg, q);
    if (ok) {
        int sign = mpq_sgn(q);
        mpz_abs(abs_num, mpq_numref(q));
        mpz_tdiv_qr(quotient, remainder, abs_num, mpq_denref(q));
        mpz_mul_ui(twice_remainder, remainder, 2u);
        if (mpz_cmp(twice_remainder, mpq_denref(q)) >= 0)
            mpz_add_ui(quotient, quotient, 1u);
        if (sign < 0)
            mpz_neg(quotient, quotient);
    }
    Atom *out = ok ? atom_from_mpz(a, quotient) : NULL;
    mpz_clears(abs_num, quotient, remainder, twice_remainder, NULL);
    mpq_clear(q);
    return out;
}

static Atom *atom_from_rational_square_root(Arena *a, Atom *head, Atom **args,
                                            uint32_t nargs, const NumArg *arg,
                                            bool *was_exact) {
    mpq_t q;
    mpz_t num_root, den_root;
    mpq_init(q);
    mpz_inits(num_root, den_root, NULL);
    *was_exact = false;
    bool ok = num_arg_to_mpq(arg, q);
    Atom *out = NULL;
    if (ok && mpq_sgn(q) >= 0 &&
        mpz_perfect_square_p(mpq_numref(q)) &&
        mpz_perfect_square_p(mpq_denref(q))) {
        mpq_t root;
        mpq_init(root);
        mpz_sqrt(num_root, mpq_numref(q));
        mpz_sqrt(den_root, mpq_denref(q));
        mpq_set_num(root, num_root);
        mpq_set_den(root, den_root);
        mpq_canonicalize(root);
        out = grounded_atom_from_mpq(a, head, args, nargs, root);
        *was_exact = true;
        mpq_clear(root);
    }
    mpz_clears(num_root, den_root, NULL);
    mpq_clear(q);
    return out;
}
#endif

static Atom *grounded_division_by_zero(Arena *a, Atom *head, Atom **args,
                                       uint32_t nargs);

static Atom *grounded_rational_unavailable(Arena *a, Atom *head,
                                           Atom **args, uint32_t nargs)
    __attribute__((unused));
static Atom *grounded_rational_unavailable(Arena *a, Atom *head,
                                           Atom **args, uint32_t nargs) {
    return atom_error(a, grounded_call_expr(a, head, args, nargs),
                      atom_symbol(a, "RationalArithmeticUnavailable"));
}

static bool grounded_mod_uses_floor_division(void) {
    return eval_current_language_id &&
           eval_current_language_id() == CETTA_LANGUAGE_PETTA;
}

static bool grounded_current_language_is_petta(void) {
    return eval_current_language_id &&
           eval_current_language_id() == CETTA_LANGUAGE_PETTA;
}

#if CETTA_BUILD_WITH_GMP
static Atom *eval_integer_binary_gmp(Arena *a, Atom *head, SymbolId head_id,
                                     Atom **args, uint32_t nargs,
                                     const NumArg *na, const NumArg *nb,
                                     bool prefer_rationals,
                                     bool rust_compat) {
    bool wants_rational = na->is_rational || nb->is_rational ||
                          (prefer_rationals && head_id == g_builtin_syms.op_div);
    if (wants_rational) {
        mpq_t ai, bi, ri;
        mpq_inits(ai, bi, ri, NULL);
        bool ok = num_arg_to_mpq(na, ai) && num_arg_to_mpq(nb, bi);
        if (!ok) {
            mpq_clears(ai, bi, ri, NULL);
            return NULL;
        }
        Atom *result = NULL;
        if (head_id == g_builtin_syms.op_plus) {
            mpq_add(ri, ai, bi);
            result = grounded_atom_from_mpq(a, head, args, nargs, ri);
        } else if (head_id == g_builtin_syms.op_minus) {
            mpq_sub(ri, ai, bi);
            result = grounded_atom_from_mpq(a, head, args, nargs, ri);
        } else if (head_id == g_builtin_syms.op_mul) {
            mpq_mul(ri, ai, bi);
            result = grounded_atom_from_mpq(a, head, args, nargs, ri);
        } else if (head_id == g_builtin_syms.op_div) {
            if (mpq_sgn(bi) == 0)
                result = grounded_division_by_zero(a, head, args, nargs);
            else {
                mpq_div(ri, ai, bi);
                result = grounded_atom_from_mpq(a, head, args, nargs, ri);
            }
        } else if (head_id == g_builtin_syms.op_floor_div) {
            if (mpq_sgn(bi) == 0)
                result = grounded_division_by_zero(a, head, args, nargs);
            else
                result = atom_from_floor_div_mpq(a, ai, bi);
        } else if (head_id == g_builtin_syms.op_mod) {
            if (mpq_sgn(bi) == 0)
                result = grounded_division_by_zero(a, head, args, nargs);
            else {
                int bad_idx = na->is_rational ? 1 : 2;
                result = grounded_bad_arg_type(a, head, args, nargs,
                                               bad_idx, atom_symbol(a, "Number"),
                                               args[bad_idx - 1]);
            }
        } else {
            int cmp = mpq_cmp(ai, bi);
            if (head_id == g_builtin_syms.op_lt)
                result = cmp < 0 ? atom_true(a) : atom_false(a);
            else if (head_id == g_builtin_syms.op_gt)
                result = cmp > 0 ? atom_true(a) : atom_false(a);
            else if (head_id == g_builtin_syms.op_le)
                result = cmp <= 0 ? atom_true(a) : atom_false(a);
            else if (head_id == g_builtin_syms.op_ge)
                result = cmp >= 0 ? atom_true(a) : atom_false(a);
        }
        mpq_clears(ai, bi, ri, NULL);
        return result;
    }

    IntegerMpzView ai = {0};
    IntegerMpzView bi = {0};
    mpz_t ri;
    mpz_init(ri);
    bool ok = integer_mpz_view_init(na, &ai) &&
              integer_mpz_view_init(nb, &bi);
    if (!ok) {
        integer_mpz_view_clear(&ai);
        integer_mpz_view_clear(&bi);
        mpz_clear(ri);
        return NULL;
    }

    Atom *result = NULL;
    if (head_id == g_builtin_syms.op_plus) {
        mpz_add(ri, ai.value, bi.value);
        result = atom_bigint_take_mpz(a, ri);
    } else if (head_id == g_builtin_syms.op_minus) {
        mpz_sub(ri, ai.value, bi.value);
        result = atom_bigint_take_mpz(a, ri);
    } else if (head_id == g_builtin_syms.op_mul) {
        mpz_mul(ri, ai.value, bi.value);
        result = atom_bigint_take_mpz(a, ri);
    } else if (head_id == g_builtin_syms.op_div) {
        if (mpz_sgn(bi.value) == 0) {
            result = grounded_division_by_zero(a, head, args, nargs);
        } else if (rust_compat) {
            mpz_tdiv_q(ri, ai.value, bi.value);
            result = atom_bigint_take_mpz(a, ri);
        } else if (mpz_divisible_p(ai.value, bi.value)) {
            mpz_tdiv_q(ri, ai.value, bi.value);
            result = atom_bigint_take_mpz(a, ri);
        } else {
            result = atom_float(
                a, mpz_get_d(ai.value) / mpz_get_d(bi.value));
        }
    } else if (head_id == g_builtin_syms.op_floor_div) {
        if (mpz_sgn(bi.value) == 0) {
            result = grounded_division_by_zero(a, head, args, nargs);
        } else {
            mpz_fdiv_q(ri, ai.value, bi.value);
            result = atom_bigint_take_mpz(a, ri);
        }
    } else if (head_id == g_builtin_syms.op_mod) {
        if (mpz_sgn(bi.value) == 0) {
            result = grounded_division_by_zero(a, head, args, nargs);
        } else {
            if (grounded_mod_uses_floor_division())
                mpz_fdiv_r(ri, ai.value, bi.value);
            else
                mpz_tdiv_r(ri, ai.value, bi.value);
            result = atom_bigint_take_mpz(a, ri);
        }
    } else {
        int cmp = mpz_cmp(ai.value, bi.value);
        if (head_id == g_builtin_syms.op_lt)
            result = cmp < 0 ? atom_true(a) : atom_false(a);
        else if (head_id == g_builtin_syms.op_gt)
            result = cmp > 0 ? atom_true(a) : atom_false(a);
        else if (head_id == g_builtin_syms.op_le)
            result = cmp <= 0 ? atom_true(a) : atom_false(a);
        else if (head_id == g_builtin_syms.op_ge)
            result = cmp >= 0 ? atom_true(a) : atom_false(a);
    }
    integer_mpz_view_clear(&ai);
    integer_mpz_view_clear(&bi);
    mpz_clear(ri);
    return result;
}
#else
static Atom *grounded_bigint_unavailable(Arena *a, Atom *head, Atom **args,
                                         uint32_t nargs) {
    return atom_error(a, grounded_call_expr(a, head, args, nargs),
                      atom_symbol(a, "BigintArithmeticUnavailable"));
}

static Atom *eval_integer_binary_gmp(Arena *a, Atom *head, SymbolId head_id,
                                     Atom **args, uint32_t nargs,
                                     const NumArg *na, const NumArg *nb,
                                     bool prefer_rationals,
                                     bool rust_compat) {
    (void)na;
    (void)nb;
    (void)rust_compat;
    if (prefer_rationals && head_id == g_builtin_syms.op_div)
        return grounded_rational_unavailable(a, head, args, nargs);
    (void)head_id;
    return grounded_bigint_unavailable(a, head, args, nargs);
}
#endif

/* Return int if both inputs were int and result is exact, otherwise float */
static Atom *make_numeric(Arena *a, double val, bool any_float) {
    if (any_float)
        return atom_float(a, val);
    if (!isfinite(val))
        return atom_float(a, val);
    if (val < (double)INT64_MIN || val > (double)INT64_MAX)
        return atom_float(a, val);
    int64_t lv = (int64_t)val;
    if ((double)lv == val)
        return atom_int(a, lv);
    return atom_float(a, val);
}

static Atom *grounded_division_by_zero(Arena *a, Atom *head, Atom **args, uint32_t nargs) {
    return atom_error(a, grounded_call_expr(a, head, args, nargs),
                      atom_symbol(a, "DivisionByZero"));
}

static Atom *grounded_math_domain_error(Arena *a, Atom *head, Atom **args,
                                        uint32_t nargs, int bad_idx,
                                        const char *constraint) {
    Atom *reason = atom_expr(a, (Atom *[]){
        atom_symbol(a, "MathDomainError"),
        atom_int(a, bad_idx),
        atom_symbol(a, constraint)
    }, 3);
    return atom_error(a, grounded_call_expr(a, head, args, nargs), reason);
}

static bool numeric_arg_is_integral(const NumArg *arg) {
    if (!arg->is_float) return true;
    if (!isfinite(arg->val)) return false;
    double whole = 0.0;
    return modf(arg->val, &whole) == 0.0;
}

static int64_t floor_div_i64(int64_t lhs, int64_t rhs) {
    int64_t q = lhs / rhs;
    int64_t r = lhs % rhs;
    if (r != 0 && ((r > 0) != (rhs > 0)))
        q -= 1;
    return q;
}

static int64_t floor_mod_i64(int64_t lhs, int64_t rhs) {
    if (lhs == INT64_MIN && rhs == -1)
        return 0;
    int64_t remainder = lhs % rhs;
    if (remainder != 0 && ((remainder < 0) != (rhs < 0)))
        remainder += rhs;
    return remainder;
}

/* ── Boolean arg extraction (True/False symbols) ──────────────────────── */

static bool get_bool_arg(Atom *a, bool *out) {
    if (eval_current_language_id &&
        eval_current_language_id() == CETTA_LANGUAGE_PETTA &&
        petta_semantics_truth_value(a, out)) {
        return true;
    }
    if (atom_is_symbol_id(a, g_builtin_syms.true_text))  { *out = true;  return true; }
    if (atom_is_symbol_id(a, g_builtin_syms.false_text)) { *out = false; return true; }
    if (a->kind == ATOM_GROUNDED && a->ground.gkind == GV_BOOL) {
        *out = a->ground.bval; return true;
    }
    return false;
}

static Atom *grounded_bool_bad_arg(Arena *a, Atom *head, Atom **args, uint32_t nargs,
                                   int bad_idx, Atom *actual) {
    return grounded_bad_arg_type(a, head, args, nargs, bad_idx, atom_symbol(a, "Bool"), actual);
}

static Atom *grounded_format_args(Arena *a, Atom *head, Atom **args, uint32_t nargs) {
    if (nargs != 2)
        return grounded_incorrect_arity(a, head, args, nargs);
    if (!(args[0]->kind == ATOM_GROUNDED && args[0]->ground.gkind == GV_STRING))
        return grounded_string_error(a, head, args, nargs,
                                     "format-args expects format string as a first argument and expression as a second argument");
    if (args[1]->kind != ATOM_EXPR)
        return grounded_string_error(a, head, args, nargs, "Atom is not an ExpressionAtom");

    const char *fmt = args[0]->ground.sval;
    Atom *arg_list = args[1];
    StringBuf sb;
    sb_init(&sb);
    uint32_t argi = 0;
    for (const char *p = fmt; *p; ) {
        if (p[0] == '{' && p[1] == '}') {
            if (argi < arg_list->expr.len) {
                char *rendered = atom_to_string(a, arg_list->expr.elems[argi++]);
                sb_append(&sb, rendered);
            }
            p += 2;
            continue;
        }
        sb_append_n(&sb, p, 1);
        p++;
    }
    Atom *out = atom_string(a, sb.buf ? sb.buf : "");
    sb_free(&sb);
    return out;
}

static int grounded_sort_strings_cmp(const void *lhs, const void *rhs) {
    const char *const *a = lhs;
    const char *const *b = rhs;
    return strcmp(*a, *b);
}

static Atom *grounded_sort_strings(Arena *a, Atom *head, Atom **args, uint32_t nargs) {
    if (nargs != 1)
        return grounded_incorrect_arity(a, head, args, nargs);
    if (args[0]->kind != ATOM_EXPR)
        return grounded_string_error(a, head, args, nargs,
                                     "sort-strings expects expression with strings as a first argument");

    Atom *list = args[0];
    const char **strings = arena_alloc(a, sizeof(const char *) * list->expr.len);
    Atom **sorted = arena_alloc(a, sizeof(Atom *) * list->expr.len);
    for (CettaExprIndex i = 0; i < list->expr.len; i++) {
        Atom *elem = list->expr.elems[i];
        if (!(elem->kind == ATOM_GROUNDED && elem->ground.gkind == GV_STRING)) {
            return grounded_string_error(a, head, args, nargs,
                                         "sort-strings expects expression with strings as a first argument");
        }
        strings[i] = elem->ground.sval;
    }

    qsort(strings, list->expr.len, sizeof(const char *), grounded_sort_strings_cmp);
    for (CettaExprIndex i = 0; i < list->expr.len; i++)
        sorted[i] = atom_string(a, strings[i]);
    return atom_expr(a, sorted, list->expr.len);
}

static Atom *grounded_repr(Arena *a, Atom *head, Atom **args, uint32_t nargs) {
    if (nargs != 1)
        return grounded_incorrect_arity(a, head, args, nargs);

    if (eval_current_language_id &&
        eval_current_language_id() == CETTA_LANGUAGE_PRIME) {
        char *text = parser_render_syntax(
            a, args[0], PARSER_SYNTAX_PRINT_COMPACT);
        if (text)
            return atom_string(a, text);
    }
    if (eval_current_language_id &&
        eval_current_language_id() == CETTA_LANGUAGE_PETTA) {
        return atom_string(
            a, atom_to_parseable_string_petta(a, args[0]));
    }
    return atom_string(a, atom_to_parseable_string(a, args[0]));
}

static Atom *grounded_repra(
    Arena *a, Atom *head, Atom **args, uint32_t nargs) {
    if (nargs != 1u)
        return grounded_incorrect_arity(a, head, args, nargs);
    StringBuf rendered;
    sb_init(&rendered);
    bool exact = petta_repra_render(a, args[0], &rendered);
    Atom *result = atom_symbol(
        a, exact && rendered.buf
            ? rendered.buf
            : (eval_current_language_id &&
                       eval_current_language_id() == CETTA_LANGUAGE_PETTA
                   ? atom_to_parseable_string_petta(a, args[0])
                   : atom_to_parseable_string(a, args[0])));
    sb_free(&rendered);
    return result;
}

static Atom *grounded_sha256(Arena *a, Atom *head, Atom **args,
                             uint32_t nargs) {
    char digest[65];

    if (nargs != 1)
        return grounded_incorrect_arity(a, head, args, nargs);
    if (!(args[0]->kind == ATOM_GROUNDED &&
          args[0]->ground.gkind == GV_STRING)) {
        if (args[0]->kind != ATOM_GROUNDED)
            return NULL;
        return grounded_bad_arg_type(a, head, args, nargs, 1,
                                     atom_symbol(a, "String"), args[0]);
    }
    cetta_native_sha256_hex((const uint8_t *)args[0]->ground.sval,
                            strlen(args[0]->ground.sval), digest);
    /* The portable digest doubles as a cheap process-local interned handle.
       Hash-consing is performed only when sha256 is explicitly invoked. */
    Atom *result = atom_string(a, digest);
    return g_hashcons ? hashcons_get(g_hashcons, result) : result;
}

/* Text parsing deliberately has two syntax forms:
   - parse is strict: the string must contain exactly one atom, with only
     whitespace/comments around it. This is the safer PeTTa-style default.
   - parse-first is stream-like: it returns the first parsed atom and ignores
     all remaining text, including malformed trailing text. */
static Atom *grounded_parse_text(Arena *a, Atom *head, Atom **args,
                                uint32_t nargs, bool require_all_input) {
    if (nargs != 1)
        return grounded_incorrect_arity(a, head, args, nargs);
    if (!(args[0]->kind == ATOM_GROUNDED && args[0]->ground.gkind == GV_STRING)) {
        if (args[0]->kind != ATOM_GROUNDED)
            return NULL;
        return grounded_bad_arg_type(a, head, args, nargs, 1,
                                     atom_symbol(a, "String"), args[0]);
    }

    bool old_rational_literals = parser_set_rational_literals_enabled(
        !eval_current_uses_rust_he_compat_semantics());

    if (require_all_input && eval_current_language_id &&
        eval_current_language_id() == CETTA_LANGUAGE_PRIME) {
        Atom *parsed = parser_read_source_form(a, args[0]->ground.sval);
        parser_set_rational_literals_enabled(old_rational_literals);
        return parsed
            ? parsed
            : atom_error(a, grounded_call_expr(a, head, args, nargs),
                         atom_symbol(a, "ParseFailed"));
    }

    if (require_all_input && !parser_text_well_formed(args[0]->ground.sval)) {
        parser_set_rational_literals_enabled(old_rational_literals);
        return atom_error(a, grounded_call_expr(a, head, args, nargs),
                          atom_symbol(a, "ParseFailed"));
    }

    size_t pos = 0;
    Atom *parsed = parse_sexpr(a, args[0]->ground.sval, &pos);
    parser_set_rational_literals_enabled(old_rational_literals);
    if (!parsed)
        return atom_error(a, grounded_call_expr(a, head, args, nargs),
                          atom_symbol(a, "ParseFailed"));
    if (require_all_input && !parser_rest_is_delimiters(args[0]->ground.sval, &pos))
        return atom_error(a, grounded_call_expr(a, head, args, nargs),
                          atom_symbol(a, "ParseFailed"));
    return parsed;
}

static Atom *grounded_collapse_add_next(Arena *a, Atom *head, Atom **args, uint32_t nargs) {
    if (nargs != 2)
        return grounded_incorrect_arity(a, head, args, nargs);
    if (args[0]->kind != ATOM_EXPR)
        return grounded_bad_arg_type(a, head, args, nargs, 1,
                                     atom_expression_type(a), args[0]);

    Atom *pair = args[1];
    if (pair->kind != ATOM_EXPR || pair->expr.len != 2)
        return grounded_string_error(a, head, args, nargs,
                                     "(Atom Bindings) pair is expected as a second argument");

    Bindings bindings;
    if (!bindings_from_atom(pair->expr.elems[1], &bindings))
        return grounded_string_error(a, head, args, nargs,
                                     "(Atom Bindings) pair is expected as a second argument");

    Atom *next_atom = bindings_apply(&bindings, a, pair->expr.elems[0]);
    bindings_free(&bindings);

    Atom *list = args[0];
    Atom **elems = arena_alloc(a, sizeof(Atom *) * (list->expr.len + 1));
    for (CettaExprIndex i = 0; i < list->expr.len; i++)
        elems[i] = list->expr.elems[i];
    elems[list->expr.len] = next_atom;
    return atom_expr(a, elems, list->expr.len + 1);
}

static Atom *grounded_space_contains_exact(Arena *a, Atom *head,
                                           Atom **args, uint32_t nargs) {
    if (nargs != 2)
        return grounded_incorrect_arity(a, head, args, nargs);
    if (!(args[0]->kind == ATOM_GROUNDED && args[0]->ground.gkind == GV_SPACE))
        return grounded_bad_arg_type(a, head, args, nargs, 1,
                                     atom_symbol(a, "SpaceType"), args[0]);
    return atom_bool(a, space_contains_exact((Space *)args[0]->ground.ptr, args[1]));
}

static Atom *grounded_space_revision(Arena *a, Atom *head,
                                     Atom **args, uint32_t nargs) {
    if (nargs != 1)
        return grounded_incorrect_arity(a, head, args, nargs);
    if (!(args[0]->kind == ATOM_GROUNDED && args[0]->ground.gkind == GV_SPACE))
        return grounded_bad_arg_type(a, head, args, nargs, 1,
                                     atom_symbol(a, "SpaceType"), args[0]);
    uint64_t revision = space_revision((Space *)args[0]->ground.ptr);
    if (revision > (uint64_t)INT64_MAX)
        return grounded_string_error(a, head, args, nargs,
                                     "space revision exceeds Number range");
    return atom_int(a, (int64_t)revision);
}

static Atom *grounded_foldl_in_space(Arena *a, Atom *head, Atom **args, uint32_t nargs) {
    if (nargs != 6)
        return grounded_incorrect_arity(a, head, args, nargs);
    if (args[0]->kind != ATOM_EXPR)
        return grounded_bad_arg_type(a, head, args, nargs, 1,
                                     atom_expression_type(a), args[0]);
    if (args[2]->kind != ATOM_VAR)
        return grounded_bad_arg_type(a, head, args, nargs, 3,
                                     atom_variable_type(a), args[2]);
    if (args[3]->kind != ATOM_VAR)
        return grounded_bad_arg_type(a, head, args, nargs, 4,
                                     atom_variable_type(a), args[3]);
    if (!(args[5]->kind == ATOM_GROUNDED && args[5]->ground.gkind == GV_SPACE))
        return grounded_bad_arg_type(a, head, args, nargs, 6,
                                     atom_symbol(a, "SpaceType"), args[5]);

    Atom *list = args[0];
    Atom *init = args[1];
    Atom *acc_var = args[2];
    Atom *item_var = args[3];
    Atom *op_expr = args[4];
    Atom *space = args[5];

    Atom *head_item;
    Atom *tail;
    if (list->expr.len == 0)
        return atom_expr2(a, atom_symbol(a, "return"), init);
    head_item = list->expr.elems[0];
    tail = atom_expr(a, list->expr.elems + 1, list->expr.len - 1);

    Atom *step_op = cetta_fold_bind_step_atom(a, op_expr,
                                              acc_var, init,
                                              item_var, head_item);


    char tmp_name[256];
    snprintf(tmp_name, sizeof(tmp_name), "$__foldl_step#%u", fresh_var_suffix());
    Atom *tmp = atom_var(a, tmp_name);

    Atom *metta_args[4] = {
        atom_symbol(a, "metta"),
        step_op,
        atom_undefined_type(a),
        space
    };
    Atom *recur_args[7] = {
        head,
        tail,
        tmp,
        acc_var,
        item_var,
        op_expr,
        space
    };
    Atom *chain_args[4] = {
        atom_symbol(a, "chain"),
        atom_expr(a, metta_args, 4),
        tmp,
        atom_expr2(a, atom_symbol(a, "eval"), atom_expr(a, recur_args, 7))
    };
    return atom_expr(a, chain_args, 4);
}

static Atom *grounded_range_atom(Arena *a, Atom *head, Atom **args, uint32_t nargs) {
    int64_t start = 0;
    int64_t end = 0;

    if (nargs != 1 && nargs != 2)
        return grounded_incorrect_arity(a, head, args, nargs);

    if (nargs == 1) {
        if (args[0]->kind != ATOM_GROUNDED || args[0]->ground.gkind != GV_INT) {
            if (args[0]->kind == ATOM_GROUNDED)
                return grounded_bad_arg_type(a, head, args, nargs, 1,
                                             atom_symbol(a, "Number"), args[0]);
            return NULL;
        }
        end = args[0]->ground.ival;
    } else {
        if (args[0]->kind != ATOM_GROUNDED || args[0]->ground.gkind != GV_INT) {
            if (args[0]->kind == ATOM_GROUNDED)
                return grounded_bad_arg_type(a, head, args, nargs, 1,
                                             atom_symbol(a, "Number"), args[0]);
            return NULL;
        }
        if (args[1]->kind != ATOM_GROUNDED || args[1]->ground.gkind != GV_INT) {
            if (args[1]->kind == ATOM_GROUNDED)
                return grounded_bad_arg_type(a, head, args, nargs, 2,
                                             atom_symbol(a, "Number"), args[1]);
            return NULL;
        }
        start = args[0]->ground.ival;
        end = args[1]->ground.ival;
    }

    if (end <= start)
        return atom_expr(a, NULL, 0);

    CettaExprLen len = (CettaExprLen)(end - start);
    if (!cetta_expr_len_mul_fits_size(len, sizeof(Atom *))) {
        return atom_error(a, grounded_call_expr(a, head, args, nargs),
                          atom_symbol(a, "RangeTooLarge"));
    }

    Atom **elems = arena_alloc(a, sizeof(Atom *) * (size_t)len);
    for (CettaExprIndex i = 0; i < len; i++)
        elems[i] = atom_int(a, start + (int64_t)i);
    return atom_expr(a, elems, len);
}

static Atom *grounded_repeat_atom(Arena *a, Atom *head, Atom **args, uint32_t nargs) {
    if (nargs != 2)
        return grounded_incorrect_arity(a, head, args, nargs);

    if (args[0]->kind != ATOM_GROUNDED || args[0]->ground.gkind != GV_INT) {
        if (args[0]->kind == ATOM_GROUNDED)
            return grounded_bad_arg_type(a, head, args, nargs, 1,
                                         atom_symbol(a, "Number"), args[0]);
        return NULL;
    }

    int64_t count = args[0]->ground.ival;
    if (count <= 0)
        return atom_expr(a, NULL, 0);
    if (!cetta_expr_len_mul_fits_size((CettaExprLen)count, sizeof(Atom *))) {
        return atom_error(a, grounded_call_expr(a, head, args, nargs),
                          atom_symbol(a, "RepeatTooLarge"));
    }

    CettaExprLen len = (CettaExprLen)count;
    Atom **elems = arena_alloc(a, sizeof(Atom *) * (size_t)len);
    for (CettaExprIndex i = 0; i < len; i++)
        elems[i] = args[1];
    return atom_expr(a, elems, len);
}

/* ── Dispatch ──────────────────────────────────────────────────────────── */

Atom *grounded_dispatch(Arena *a, Atom *head, Atom **args, uint32_t nargs) {
    if (head->kind != ATOM_SYMBOL) return NULL;
    {
        Atom *abt = abt_grounded_dispatch(a, head, args, nargs);
        if (abt) return abt;
    }
    {
        Atom *prime = prime_semantics_dispatch
            ? prime_semantics_dispatch(a, head, args, nargs) : NULL;
        if (prime) return prime;
    }
    {
        Atom *he = he_typing_dispatch
            ? he_typing_dispatch(a, head, args, nargs) : NULL;
        if (he) return he;
    }
    SymbolId head_id = head->sym_id;

    if (head_id == g_builtin_syms.println_bang) {
        if (nargs != 1)
            return grounded_incorrect_arity(a, head, args, nargs);
        if (eval_current_language_id &&
            eval_current_language_id() == CETTA_LANGUAGE_PETTA) {
            atom_print_petta(args[0], stdout);
        } else if (args[0]->kind == ATOM_GROUNDED &&
                   args[0]->ground.gkind == GV_STRING) {
            fputs(args[0]->ground.sval, stdout);
        } else {
            atom_print(args[0], stdout);
        }
        fputc('\n', stdout);
        fflush(stdout);
        if (eval_current_language_id &&
            eval_current_language_id() == CETTA_LANGUAGE_PETTA) {
            return petta_semantics_boolean_value(a, true);
        }
        return atom_unit(a);
    }

    if (head_id == g_builtin_syms.readln_bang) {
        if (nargs != 0)
            return grounded_incorrect_arity(a, head, args, nargs);
        char *line = NULL;
        size_t capacity = 0u;
        ssize_t length = getline(&line, &capacity, stdin);
        if (length < 0) {
            free(line);
            return atom_symbol(a, "end_of_file");
        }
        while (length > 0 &&
               (line[length - 1] == '\n' ||
                line[length - 1] == '\r')) {
            line[--length] = '\0';
        }
        Atom *text = atom_string(a, line);
        free(line);
        if (!text)
            return NULL;
        Atom *parse_args[1] = {text};
        return grounded_parse_text(
            a, head, parse_args, 1u, true);
    }

    if (head_id == g_builtin_syms.flush_output_bang) {
        if (nargs != 0)
            return grounded_incorrect_arity(a, head, args, nargs);
        if (fflush(stdout) != 0)
            return atom_error(
                a, grounded_call_expr(a, head, args, nargs),
                atom_symbol(a, "FlushFailed"));
        if (eval_current_language_id &&
            eval_current_language_id() == CETTA_LANGUAGE_PETTA) {
            return petta_semantics_boolean_value(a, true);
        }
        return atom_unit(a);
    }

    if (head_id == g_builtin_syms.trace_bang) {
        if (nargs != 2)
            return grounded_incorrect_arity(a, head, args, nargs);
        bool petta =
            eval_current_language_id &&
            eval_current_language_id() == CETTA_LANGUAGE_PETTA;
        FILE *destination = petta ? stdout : stderr;
        if (petta)
            atom_print_petta(args[0], destination);
        else
            atom_print(args[0], destination);
        fputc('\n', destination);
        fflush(destination);
        return args[1];
    }

    if (head_id == g_builtin_syms.format_args)
        return grounded_format_args(a, head, args, nargs);

    if (head_id == g_builtin_syms.sort_strings)
        return grounded_sort_strings(a, head, args, nargs);

    if (head_id == g_builtin_syms.repr)
        return grounded_repr(a, head, args, nargs);

    if (eval_current_language_id &&
        eval_current_language_id() == CETTA_LANGUAGE_PETTA &&
        petta_semantics_form(head_id) == PETTA_FORM_REPRA)
        return grounded_repra(a, head, args, nargs);

    if (head_id == g_builtin_syms.sha256)
        return grounded_sha256(a, head, args, nargs);

    if (head_id == g_builtin_syms.parse)
        return grounded_parse_text(a, head, args, nargs,
                                   !eval_current_uses_rust_he_compat_semantics());

    if (head_id == g_builtin_syms.parse_first)
        return grounded_parse_text(a, head, args, nargs, false);

    if (head_id == g_builtin_syms.collapse_add_next)
        return grounded_collapse_add_next(a, head, args, nargs);

    if (head_id == g_builtin_syms.minimal_space_contains_exact)
        return grounded_space_contains_exact(a, head, args, nargs);

    if (head_id == g_builtin_syms.minimal_space_revision)
        return grounded_space_revision(a, head, args, nargs);

    if (head_id == g_builtin_syms.minimal_foldl_atom ||
        head_id == g_builtin_syms.foldl_atom_in_space)
        return grounded_foldl_in_space(a, head, args, nargs);

    if (head_id == g_builtin_syms.range_atom)
        return grounded_range_atom(a, head, args, nargs);

    if (head_id == g_builtin_syms.repeat_atom)
        return grounded_repeat_atom(a, head, args, nargs);

    /*
     * Pure atom observers are also used directly by the PeTTa machine after
     * its readiness phase.  Wrong arities remain owned by the ordinary
     * evaluator so this shared capability is an exact refinement of the
     * existing one-argument cases.
     */
    if ((head_id == g_builtin_syms.car_atom ||
         head_id == g_builtin_syms.cdr_atom) &&
        nargs == 1u) {
        Atom *argument = args[0];
        if (argument->kind == ATOM_EXPR && argument->expr.len > 0u) {
            if (head_id == g_builtin_syms.car_atom)
                return argument->expr.elems[0];
            return atom_expr(
                a, argument->expr.elems + 1u, argument->expr.len - 1u);
        }
        return atom_error(
            a, grounded_call_expr(a, head, args, nargs),
            atom_string(
                a, head_id == g_builtin_syms.car_atom
                       ? "car-atom expects a non-empty expression as an argument"
                       : "cdr-atom expects a non-empty expression as an argument"));
    }

    if (head_id == g_builtin_syms.alpha_eq) {
        if (nargs != 2)
            return grounded_incorrect_arity(a, head, args, nargs);
        return atom_alpha_eq(args[0], args[1]) ? atom_true(a) : atom_false(a);
    }

    if (head_id == g_builtin_syms.if_equal) {
        if (nargs != 4)
            return grounded_incorrect_arity(a, head, args, nargs);
        return atom_alpha_eq(args[0], args[1]) ? args[2] : args[3];
    }

    if (head_id == g_builtin_syms.sealed_text) {
        if (nargs != 2)
            return grounded_incorrect_arity(a, head, args, nargs);
        /* Two dialect contracts share this name.  HE's operator standardizes
           free metavariables apart: every variable NOT protected by the
           binder list is renamed.  PeTTa's translator form is the exact
           complement (SWI copy_term/4): ONLY the listed variables are
           freshened and every other variable keeps caller identity — rule
           compilers depend on the retained sharing, and an empty seal list
           is the identity.  Object-binder ABT indices are ordinary canonical
           expressions and remain untouched either way. */
        const CettaLanguageId sealed_language = eval_current_language_id
            ? eval_current_language_id() : CETTA_LANGUAGE_HE;
        Atom *renamed = sealed_language == CETTA_LANGUAGE_PETTA
            ? rename_vars_only(a, args[1], args[0])
            : rename_vars_except(a, args[1], args[0]);
        return renamed ? renamed
                       : atom_error(a, grounded_call_expr(a, head, args, nargs),
                                    atom_symbol(a, "SealedInvalidTerm"));
    }

    if (head_id == g_builtin_syms.print_alternatives_bang) {
        if (nargs != 2)
            return grounded_incorrect_arity(a, head, args, nargs);
        if (args[1]->kind != ATOM_EXPR)
            return grounded_string_error(a, head, args, nargs, "Atom is not an ExpressionAtom");
        char *label = atom_to_string(a, args[0]);
        printf("%" PRIu64 " %s:\n", (uint64_t)args[1]->expr.len, label);
        for (CettaExprIndex i = 0; i < args[1]->expr.len; i++) {
            char *rendered = atom_to_string(a, args[1]->expr.elems[i]);
            printf("    %s\n", rendered);
        }
        fflush(stdout);
        return atom_unit(a);
    }

    if (head_id == g_builtin_syms.pow_math) {
        if (nargs != 2)
            return grounded_incorrect_arity(a, head, args, nargs);
        NumArg base = {0};
        if (!get_numeric_arg_for_math(args[0], &base))
            return grounded_string_error(a, head, args, nargs,
                                         "pow-math expects two arguments: number (base) and number (power)");
        NumArg power = {0};
        double power_val;
        if (args[1]->kind == ATOM_GROUNDED && args[1]->ground.gkind == GV_INT) {
            int64_t n = args[1]->ground.ival;
            if (n > INT32_MAX || n < INT32_MIN)
                return grounded_string_error(a, head, args, nargs,
                                             "power argument is too big, try using float value");
            power.ival = n;
            power.val = (double)n;
            power.is_float = false;
            power_val = (double)n;
        } else {
            if (!get_numeric_arg_for_math(args[1], &power))
                return grounded_string_error(a, head, args, nargs,
                                             "pow-math expects two arguments: number (base) and number (power)");
            power_val = power.val;
        }
        bool rust_compat = eval_current_uses_rust_he_compat_semantics();
        if (!rust_compat && base.val == 0.0 && power.val < 0.0)
            return grounded_math_domain_error(a, head, args, nargs, 1,
                                              "NonZeroBaseWhenExponentNegative");
        if (!rust_compat && base.val < 0.0 && !numeric_arg_is_integral(&power))
            return grounded_math_domain_error(a, head, args, nargs, 2,
                                              "IntegralExponentWhenBaseNegative");
#if CETTA_BUILD_WITH_GMP
        if (eval_current_language_id &&
            eval_current_language_id() == CETTA_LANGUAGE_PETTA &&
            !base.is_float && !base.is_rational &&
            !power.is_float && !power.is_rational) {
            mpz_t exact_base;
            mpz_t exact_power;
            mpz_t exact_result;
            mpz_inits(exact_base, exact_power, exact_result, NULL);
            bool exact =
                num_arg_to_mpz(&base, exact_base) &&
                num_arg_to_mpz(&power, exact_power) &&
                mpz_sgn(exact_power) >= 0 &&
                mpz_fits_ulong_p(exact_power);
            Atom *result = NULL;
            if (exact) {
                mpz_pow_ui(
                    exact_result, exact_base,
                    mpz_get_ui(exact_power));
                result = atom_from_mpz(a, exact_result);
            }
            mpz_clears(exact_base, exact_power, exact_result, NULL);
            if (result)
                return result;
        }
#endif
        double res = pow(base.val, power_val);
        return atom_float(a, res);
    }

    if (head_id == g_builtin_syms.log_math) {
        if (nargs != 2)
            return grounded_incorrect_arity(a, head, args, nargs);
        NumArg base, input;
        if (!get_numeric_arg_for_math(args[0], &base) ||
            !get_numeric_arg_for_math(args[1], &input))
            return grounded_string_error(a, head, args, nargs,
                                         "log-math expects two arguments: base (number) and input value (number)");
        bool rust_compat = eval_current_uses_rust_he_compat_semantics();
        if (!rust_compat && (!(base.val > 0.0) || base.val == 1.0))
            return grounded_math_domain_error(a, head, args, nargs, 1,
                                              "PositiveRealNotOne");
        if (!rust_compat && !(input.val > 0.0))
            return grounded_math_domain_error(a, head, args, nargs, 2,
                                              "PositiveReal");
        return atom_float(a, log(input.val) / log(base.val));
    }

    if (head_id == g_builtin_syms.sqrt_math || head_id == g_builtin_syms.abs_math ||
        head_id == g_builtin_syms.trunc_math || head_id == g_builtin_syms.ceil_math ||
        head_id == g_builtin_syms.floor_math || head_id == g_builtin_syms.round_math ||
        head_id == g_builtin_syms.sin_math || head_id == g_builtin_syms.asin_math ||
        head_id == g_builtin_syms.cos_math || head_id == g_builtin_syms.acos_math ||
        head_id == g_builtin_syms.tan_math || head_id == g_builtin_syms.atan_math ||
        head_id == g_builtin_syms.isnan_math || head_id == g_builtin_syms.isinf_math) {
        if (nargs != 1)
            return grounded_incorrect_arity(a, head, args, nargs);
        NumArg input;
        if (!get_numeric_arg_for_math(args[0], &input)) {
            const char *msg = NULL;
            if (head_id == g_builtin_syms.sqrt_math)
                msg = "sqrt-math expects one argument: number";
            else if (head_id == g_builtin_syms.abs_math)
                msg = "abs-math expects one argument: number";
            else if (head_id == g_builtin_syms.trunc_math)
                msg = "trunc-math expects one argument: input number";
            else if (head_id == g_builtin_syms.ceil_math)
                msg = "ceil-math expects one argument: input number";
            else if (head_id == g_builtin_syms.floor_math)
                msg = "floor-math expects one argument: input number";
            else if (head_id == g_builtin_syms.round_math)
                msg = "round-math expects one argument: input number";
            else if (head_id == g_builtin_syms.sin_math)
                msg = "sin-math expects one argument: input number";
            else if (head_id == g_builtin_syms.asin_math)
                msg = "asin-math expects one argument: input number";
            else if (head_id == g_builtin_syms.cos_math)
                msg = "cos-math expects one argument: input number";
            else if (head_id == g_builtin_syms.acos_math)
                msg = "acos-math expects one argument: input number";
            else if (head_id == g_builtin_syms.tan_math)
                msg = "tan-math expects one argument: input number";
            else if (head_id == g_builtin_syms.atan_math)
                msg = "atan-math expects one argument: input number";
            else if (head_id == g_builtin_syms.isnan_math)
                msg = "isnan-math expects one argument: input number";
            else
                msg = "isinf-math expects one argument: input number";
            return grounded_string_error(a, head, args, nargs, msg);
        }

        if (head_id == g_builtin_syms.sqrt_math) {
            bool rust_compat = eval_current_uses_rust_he_compat_semantics();
            if (!rust_compat && input.val < 0.0)
                return grounded_math_domain_error(a, head, args, nargs, 1,
                                                  "NonNegativeReal");
#if CETTA_BUILD_WITH_GMP
            if (input.is_rational) {
                bool was_exact = false;
                Atom *exact = atom_from_rational_square_root(a, head, args, nargs,
                                                             &input, &was_exact);
                if (was_exact)
                    return exact;
            }
#endif
            return atom_float(a, sqrt(input.val));
        }
        if (head_id == g_builtin_syms.abs_math) {
            if (args[0]->kind == ATOM_GROUNDED &&
                args[0]->ground.gkind == GV_INT) {
                if (args[0]->ground.ival == INT64_MIN) {
#if CETTA_BUILD_WITH_GMP
                    mpz_t z;
                    mpz_init(z);
                    NumArg min_arg = {
                        .ival = args[0]->ground.ival,
                        .bigint = NULL,
                        .is_float = false,
                        .is_bigint = false,
                    };
                    num_arg_to_mpz(&min_arg, z);
                    mpz_abs(z, z);
                    Atom *out = atom_from_mpz(a, z);
                    mpz_clear(z);
                    return out;
#else
                    return atom_bigint(a, "9223372036854775808");
#endif
                }
                return atom_int(a, llabs(args[0]->ground.ival));
            }
            if (args[0]->kind == ATOM_GROUNDED &&
                args[0]->ground.gkind == GV_BIGINT) {
#if CETTA_BUILD_WITH_GMP
                mpz_t z;
                mpz_init(z);
                atom_bigint_get_mpz(args[0], z);
                mpz_abs(z, z);
                Atom *out = atom_from_mpz(a, z);
                mpz_clear(z);
                return out;
#else
                const char *text = atom_bigint_cstr(args[0]);
                return atom_bigint(a, text && text[0] == '-' ? text + 1 : text);
#endif
            }
#if CETTA_BUILD_WITH_GMP
            if (input.is_rational)
                return atom_from_rational_abs(a, head, args, nargs, &input);
#endif
            return atom_float(a, fabs(input.val));
        }
        if (head_id == g_builtin_syms.trunc_math) {
            if (args[0]->kind == ATOM_GROUNDED && args[0]->ground.gkind == GV_INT)
                return atom_int(a, args[0]->ground.ival);
            if (args[0]->kind == ATOM_GROUNDED && args[0]->ground.gkind == GV_BIGINT)
                return atom_bigint_copy(a, args[0]);
#if CETTA_BUILD_WITH_GMP
            if (input.is_rational)
                return atom_from_rational_integer_part(a, &input,
                                                       g_builtin_syms.trunc_math);
            if (eval_current_language_id &&
                eval_current_language_id() == CETTA_LANGUAGE_PETTA)
                return atom_from_integral_double(a, trunc(input.val));
#endif
            return atom_float(a, trunc(input.val));
        }
        if (head_id == g_builtin_syms.ceil_math) {
            if (args[0]->kind == ATOM_GROUNDED && args[0]->ground.gkind == GV_INT)
                return atom_int(a, args[0]->ground.ival);
            if (args[0]->kind == ATOM_GROUNDED && args[0]->ground.gkind == GV_BIGINT)
                return atom_bigint_copy(a, args[0]);
#if CETTA_BUILD_WITH_GMP
            if (input.is_rational)
                return atom_from_rational_integer_part(a, &input,
                                                       g_builtin_syms.ceil_math);
            if (eval_current_language_id &&
                eval_current_language_id() == CETTA_LANGUAGE_PETTA)
                return atom_from_integral_double(a, ceil(input.val));
#endif
            return atom_float(a, ceil(input.val));
        }
        if (head_id == g_builtin_syms.floor_math) {
            if (args[0]->kind == ATOM_GROUNDED && args[0]->ground.gkind == GV_INT)
                return atom_int(a, args[0]->ground.ival);
            if (args[0]->kind == ATOM_GROUNDED && args[0]->ground.gkind == GV_BIGINT)
                return atom_bigint_copy(a, args[0]);
#if CETTA_BUILD_WITH_GMP
            if (input.is_rational)
                return atom_from_rational_integer_part(a, &input,
                                                       g_builtin_syms.floor_math);
            if (eval_current_language_id &&
                eval_current_language_id() == CETTA_LANGUAGE_PETTA)
                return atom_from_integral_double(a, floor(input.val));
#endif
            return atom_float(a, floor(input.val));
        }
        if (head_id == g_builtin_syms.round_math) {
            if (args[0]->kind == ATOM_GROUNDED && args[0]->ground.gkind == GV_INT)
                return atom_int(a, args[0]->ground.ival);
            if (args[0]->kind == ATOM_GROUNDED && args[0]->ground.gkind == GV_BIGINT)
                return atom_bigint_copy(a, args[0]);
#if CETTA_BUILD_WITH_GMP
            if (input.is_rational)
                return atom_from_rational_round(a, &input);
            if (eval_current_language_id &&
                eval_current_language_id() == CETTA_LANGUAGE_PETTA)
                return atom_from_integral_double(a, round(input.val));
#endif
            return atom_float(a, round(input.val));
        }
        if (head_id == g_builtin_syms.sin_math)
            return atom_float(a, sin(input.val));
        if (head_id == g_builtin_syms.asin_math) {
            bool rust_compat = eval_current_uses_rust_he_compat_semantics();
            if (!rust_compat && (input.val < -1.0 || input.val > 1.0))
                return grounded_math_domain_error(a, head, args, nargs, 1,
                                                  "ClosedUnitInterval");
            return atom_float(a, asin(input.val));
        }
        if (head_id == g_builtin_syms.cos_math)
            return atom_float(a, cos(input.val));
        if (head_id == g_builtin_syms.acos_math) {
            bool rust_compat = eval_current_uses_rust_he_compat_semantics();
            if (!rust_compat && (input.val < -1.0 || input.val > 1.0))
                return grounded_math_domain_error(a, head, args, nargs, 1,
                                                  "ClosedUnitInterval");
            return atom_float(a, acos(input.val));
        }
        if (head_id == g_builtin_syms.tan_math)
            return atom_float(a, tan(input.val));
        if (head_id == g_builtin_syms.atan_math)
            return atom_float(a, atan(input.val));
        if (head_id == g_builtin_syms.isnan_math)
            return isnan(input.val) ? atom_true(a) : atom_false(a);
        return isinf(input.val) ? atom_true(a) : atom_false(a);
    }

    if (head_id == g_builtin_syms.max_atom || head_id == g_builtin_syms.min_atom) {
        bool want_max = head_id == g_builtin_syms.max_atom;
        bool rust_compat = eval_current_uses_rust_he_compat_semantics();
        if (nargs != 1)
            return grounded_incorrect_arity(a, head, args, nargs);
        if (args[0]->kind != ATOM_EXPR) {
            if (grounded_current_language_is_petta())
                return atom_unit(a);
            return grounded_string_error(a, head, args, nargs,
                                         "Atom is not an ExpressionAtom");
        }
        if (args[0]->expr.len == 0) {
            if (grounded_current_language_is_petta())
                return atom_empty(a);
            return grounded_string_error(a, head, args, nargs, "Empty expression");
        }

        bool has_float = false;
        bool has_exact_extended = false;
        for (CettaExprIndex i = 0; i < args[0]->expr.len; i++) {
            NumArg n;
            if (!get_numeric_arg(args[0]->expr.elems[i], &n) ||
                (rust_compat && n.is_rational))
                return grounded_expr_message_error(
                    a, head, args, nargs,
                    "Only numbers are allowed in expression: ",
                    args[0]);
            has_float = has_float || n.is_float;
            has_exact_extended = has_exact_extended || n.is_bigint || n.is_rational;
        }

        if (eval_current_language_id &&
            eval_current_language_id() == CETTA_LANGUAGE_PETTA) {
            Atom *best_atom = args[0]->expr.elems[0];
            NumArg best;
            (void)get_numeric_arg(best_atom, &best);
            for (CettaExprIndex i = 1u; i < args[0]->expr.len; i++) {
                Atom *candidate = args[0]->expr.elems[i];
                NumArg current;
                (void)get_numeric_arg(candidate, &current);
                int comparison = 0;
#if CETTA_BUILD_WITH_GMP
                bool compared =
                    num_arg_compare_value(&current, &best, &comparison);
#else
                bool compared =
                    !isnan(current.val) && !isnan(best.val);
                comparison = current.val < best.val
                    ? -1
                    : current.val > best.val ? 1 : 0;
#endif
                if (!compared)
                    return grounded_expr_message_error(
                        a, head, args, nargs,
                        "Only ordered numbers are allowed in expression: ",
                        args[0]);
                bool better = want_max
                    ? comparison > 0
                    : comparison < 0;
                bool equal_float_promotion =
                    comparison == 0 &&
                    current.is_float != best.is_float &&
                    current.is_float;
                if (better || equal_float_promotion) {
                    best_atom = candidate;
                    best = current;
                }
            }
            return best_atom;
        }

#if CETTA_BUILD_WITH_GMP
        if (has_exact_extended && !has_float) {
            mpq_t best_q, cur_q;
            mpq_inits(best_q, cur_q, NULL);
            Atom *best_atom = NULL;
            for (CettaExprIndex i = 0; i < args[0]->expr.len; i++) {
                NumArg n;
                if (!get_numeric_arg(args[0]->expr.elems[i], &n) ||
                    (rust_compat && n.is_rational) ||
                    !num_arg_to_mpq(&n, cur_q)) {
                    mpq_clears(best_q, cur_q, NULL);
                    return grounded_expr_message_error(
                        a, head, args, nargs,
                        "Only numbers are allowed in expression: ",
                        args[0]);
                }
                if (!best_atom ||
                    (want_max ? mpq_cmp(cur_q, best_q) > 0
                              : mpq_cmp(cur_q, best_q) < 0)) {
                    mpq_set(best_q, cur_q);
                    best_atom = args[0]->expr.elems[i];
                }
            }
            mpq_clears(best_q, cur_q, NULL);
            return best_atom;
        }
#else
        (void)has_exact_extended;
#endif

        double acc = want_max ? -INFINITY : INFINITY;
        for (CettaExprIndex i = 0; i < args[0]->expr.len; i++) {
            NumArg n;
            (void)get_numeric_arg(args[0]->expr.elems[i], &n);
            acc = want_max ? fmax(acc, n.val) : fmin(acc, n.val);
        }
        return atom_float(a, acc);
    }

    /* ── Expression introspection ─────────────────────────────────────── */
    if ((head_id == g_builtin_syms.size || head_id == g_builtin_syms.size_atom) && nargs == 1) {
        if (args[0]->kind == ATOM_EXPR)
            return atom_int(a, args[0]->expr.len);
        if (head_id == g_builtin_syms.size_atom &&
            grounded_current_language_is_petta()) {
            return atom_unit(a);
        }
        if (head_id == g_builtin_syms.size &&
            args[0]->kind == ATOM_GROUNDED &&
            args[0]->ground.gkind == GV_SPACE) {
            return atom_int(a, (int64_t)space_length64((Space *)args[0]->ground.ptr));
        }
        if (args[0]->kind == ATOM_GROUNDED) {
            Atom *expected = (head_id == g_builtin_syms.size_atom)
                ? atom_expression_type(a)
                : atom_symbol(a, "ExpressionOrSpace");
            return grounded_bad_arg_type(a, head, args, nargs, 1, expected, args[0]);
        }
        return NULL;
    }

    if (head_id == g_builtin_syms.index_atom && nargs == 2) {
        if (args[0]->kind != ATOM_EXPR) {
            if (args[0]->kind == ATOM_GROUNDED)
                return grounded_bad_arg_type(a, head, args, nargs, 1,
                                             atom_expression_type(a), args[0]);
            return NULL;
        }
        if (args[1]->kind != ATOM_GROUNDED || args[1]->ground.gkind != GV_INT) {
            if (grounded_current_language_is_petta())
                return atom_empty(a);
            if (args[1]->kind == ATOM_GROUNDED)
                return grounded_bad_arg_type(a, head, args, nargs, 2,
                                             atom_symbol(a, "Number"), args[1]);
            return NULL;
        }
        int64_t idx = args[1]->ground.ival;
        if (idx < 0 || (uint64_t)idx >= args[0]->expr.len) {
            if (grounded_current_language_is_petta())
                return atom_empty(a);
            return atom_error(a, grounded_call_expr(a, head, args, nargs),
                              atom_string(a, "Index is out of bounds"));
        }
        return args[0]->expr.elems[idx];
    }

    if (head_id == g_builtin_syms.unique_atom && nargs == 1) {
        if (args[0]->kind != ATOM_EXPR) {
            if (grounded_current_language_is_petta())
                return atom_unit(a);
            if (args[0]->kind == ATOM_GROUNDED)
                return grounded_bad_arg_type(a, head, args, nargs, 1,
                                             atom_expression_type(a), args[0]);
            return NULL;
        }
        if (!cetta_expr_len_fits_u32(args[0]->expr.len))
            return atom_error(a, grounded_call_expr(a, head, args, nargs),
                              atom_symbol(a, "ArityTooLarge"));
        if (eval_current_language_id &&
            eval_current_language_id() == CETTA_LANGUAGE_PETTA) {
            return petta_semantics_list_to_set(a, args[0]);
        }
        Atom **uniq = arena_alloc(a, sizeof(Atom *) * args[0]->expr.len);
        uint32_t table_cap = next_pow2_u32(args[0]->expr.len > 0
            ? args[0]->expr.len * 2
            : 1);
        uint32_t *ground_slots = arena_alloc(a, sizeof(uint32_t) * table_cap);
        for (uint32_t i = 0; i < table_cap; i++)
            ground_slots[i] = UINT32_MAX;
        CettaExprLen out_len = 0;
        for (CettaExprIndex i = 0; i < args[0]->expr.len; i++) {
            Atom *candidate = args[0]->expr.elems[i];
            bool seen = false;
            bool candidate_has_vars = atom_has_vars(candidate);
            if (!candidate_has_vars) {
                uint32_t mask = table_cap - 1;
                uint32_t slot = atom_hash(candidate) & mask;
                while (true) {
                    uint32_t existing = ground_slots[slot];
                    if (existing == UINT32_MAX)
                        break;
                    if (atom_eq(uniq[existing], candidate)) {
                        seen = true;
                        break;
                    }
                    slot = (slot + 1) & mask;
                }
                if (!seen) {
                    uniq[out_len] = candidate;
                    ground_slots[slot] = (uint32_t)out_len;
                    out_len++;
                }
                continue;
            }
            for (CettaExprIndex j = 0; j < out_len; j++) {
                if (atom_alpha_eq(uniq[j], candidate)) {
                    seen = true;
                    break;
                }
            }
            if (!seen)
                uniq[out_len++] = candidate;
        }
        return atom_expr(a, uniq, out_len);
    }

    if (head_id == g_builtin_syms.intersection_atom && nargs == 2) {
        if (args[0]->kind != ATOM_EXPR || args[1]->kind != ATOM_EXPR) {
            if (grounded_current_language_is_petta())
                return atom_unit(a);
            if (args[0]->kind == ATOM_GROUNDED)
                return grounded_bad_arg_type(a, head, args, nargs, 1,
                                             atom_expression_type(a), args[0]);
            if (args[1]->kind == ATOM_GROUNDED)
                return grounded_bad_arg_type(a, head, args, nargs, 2,
                                             atom_expression_type(a), args[1]);
            return NULL;
        }
        Atom **out = arena_alloc(a, sizeof(Atom *) * args[0]->expr.len);
        bool *rhs_used = arena_alloc(a, sizeof(bool) * args[1]->expr.len);
        memset(rhs_used, 0, sizeof(bool) * args[1]->expr.len);
        CettaExprLen out_len = 0;
        for (CettaExprIndex i = 0; i < args[0]->expr.len; i++) {
            Atom *candidate = args[0]->expr.elems[i];
            CettaExprIndex match_idx = 0;
            if (find_unused_alpha_equal_atom(args[1]->expr.elems, rhs_used,
                                             args[1]->expr.len, candidate,
                                             &match_idx)) {
                rhs_used[match_idx] = true;
                out[out_len++] = candidate;
            }
        }
        return atom_expr(a, out, out_len);
    }

    if (head_id == g_builtin_syms.subtraction_atom && nargs == 2) {
        if (args[0]->kind != ATOM_EXPR || args[1]->kind != ATOM_EXPR) {
            if (args[0]->kind == ATOM_GROUNDED)
                return grounded_bad_arg_type(a, head, args, nargs, 1,
                                             atom_expression_type(a), args[0]);
            if (args[1]->kind == ATOM_GROUNDED)
                return grounded_bad_arg_type(a, head, args, nargs, 2,
                                             atom_expression_type(a), args[1]);
            return NULL;
        }
        Atom **out = arena_alloc(a, sizeof(Atom *) * args[0]->expr.len);
        bool *rhs_used = arena_alloc(a, sizeof(bool) * args[1]->expr.len);
        memset(rhs_used, 0, sizeof(bool) * args[1]->expr.len);
        CettaExprLen out_len = 0;
        for (CettaExprIndex i = 0; i < args[0]->expr.len; i++) {
            Atom *candidate = args[0]->expr.elems[i];
            CettaExprIndex match_idx = 0;
            if (find_unused_alpha_equal_atom(args[1]->expr.elems, rhs_used,
                                             args[1]->expr.len, candidate,
                                             &match_idx)) {
                rhs_used[match_idx] = true;
                continue;
            }
            out[out_len++] = candidate;
        }
        return atom_expr(a, out, out_len);
    }

    /* ── Structural equality (any atom type) ───────────────────────────── */
    if (head_id == g_builtin_syms.op_eq && nargs == 2) {
        return atom_eq(args[0], args[1]) ? atom_true(a) : atom_false(a);
    }

    /* ── Boolean ops ───────────────────────────────────────────────────── */
    if (head_id == g_builtin_syms.op_not) {
        if (nargs != 1)
            return grounded_incorrect_arity(a, head, args, nargs);
        bool bv;
        if (get_bool_arg(args[0], &bv))
            return bv ? atom_false(a) : atom_true(a);
        if (args[0]->kind == ATOM_GROUNDED)
            return grounded_bool_bad_arg(a, head, args, nargs, 1, args[0]);
        return NULL;
    }
    if ((head_id == g_builtin_syms.op_and || head_id == g_builtin_syms.op_or || head_id == g_builtin_syms.op_xor) && nargs != 2)
        return grounded_incorrect_arity(a, head, args, nargs);
    if (nargs == 2) {
        bool bx, by;
        if (head_id == g_builtin_syms.op_and || head_id == g_builtin_syms.op_or || head_id == g_builtin_syms.op_xor) {
            bool okx = get_bool_arg(args[0], &bx);
            bool oky = get_bool_arg(args[1], &by);
            if (okx && oky) {
                if (head_id == g_builtin_syms.op_and)
                    return (bx && by) ? atom_true(a) : atom_false(a);
                if (head_id == g_builtin_syms.op_or)
                    return (bx || by) ? atom_true(a) : atom_false(a);
                return (bx != by) ? atom_true(a) : atom_false(a);
            }
            if (!okx && args[0]->kind == ATOM_GROUNDED)
                return grounded_bool_bad_arg(a, head, args, nargs, 1, args[0]);
            if (!oky && args[1]->kind == ATOM_GROUNDED)
                return grounded_bool_bad_arg(a, head, args, nargs, 2, args[1]);
            return NULL;
        }
    }

    /* ── Numeric ops ───────────────────────────────────────────────────── */
    if (nargs != 2) return NULL;

    /* Check if this is an arithmetic op that expects numeric args */
    bool is_arith = (head_id == g_builtin_syms.op_plus || head_id == g_builtin_syms.op_minus ||
                     head_id == g_builtin_syms.op_mul || head_id == g_builtin_syms.op_div ||
                     head_id == g_builtin_syms.op_floor_div ||
                     head_id == g_builtin_syms.op_mod || head_id == g_builtin_syms.op_lt ||
                     head_id == g_builtin_syms.op_gt || head_id == g_builtin_syms.op_le ||
                     head_id == g_builtin_syms.op_ge ||
                     head_id == g_builtin_syms.numeric_eq);
    bool rust_compat = eval_current_uses_rust_he_compat_semantics();
    if (rust_compat && head_id == g_builtin_syms.op_floor_div)
        return NULL;
    NumArg na = {0}, nb = {0};
    bool na_ok = get_numeric_arg(args[0], &na);
    bool nb_ok = get_numeric_arg(args[1], &nb);
    if (is_arith && (!na_ok || !nb_ok)) {
        /*
         * PeTTa's arithmetic predicates are SWI `is` relations: once called,
         * a non-numeric operand is an exception rather than an inert
         * expression.  The PeTTa catch delimiter observes the corresponding
         * Error value.  HE and Prime retain their established
         * "unknown stays unevaluated" behavior for symbols and variables.
         */
        Atom *bad_arg = !na_ok ? args[0] : args[1];
        bool petta_strict =
            eval_current_language_id &&
            eval_current_language_id() == CETTA_LANGUAGE_PETTA;
        if (bad_arg->kind == ATOM_GROUNDED || petta_strict) {
            return grounded_bad_arg_type(a, head, args, nargs,
                                         !na_ok ? 1 : 2,
                                         atom_symbol(a, "Number"),
                                         bad_arg);
        }
        return NULL; /* Symbol/variable args → return unchanged */
    }
    if (!na_ok || !nb_ok)
        return NULL;
    if (rust_compat && (na.is_rational || nb.is_rational))
        return NULL;
    /* Both args are numeric from here */
    if (head_id == g_builtin_syms.numeric_eq) {
        if (na.is_float || nb.is_float)
            return na.val == nb.val ? atom_true(a) : atom_false(a);
#if CETTA_BUILD_WITH_GMP
        if (na.is_bigint || nb.is_bigint ||
            na.is_rational || nb.is_rational) {
            mpq_t ai, bi;
            mpq_inits(ai, bi, NULL);
            bool ok = num_arg_to_mpq(&na, ai) && num_arg_to_mpq(&nb, bi);
            bool eq = ok && mpq_cmp(ai, bi) == 0;
            mpq_clears(ai, bi, NULL);
            return eq ? atom_true(a) : atom_false(a);
        }
#else
        if (na.is_bigint || nb.is_bigint || na.is_rational || nb.is_rational)
            return atom_eq(args[0], args[1]) ? atom_true(a) : atom_false(a);
#endif
        return na.ival == nb.ival ? atom_true(a) : atom_false(a);
    }
    if (head_id == g_builtin_syms.op_mod &&
        (na.is_rational || nb.is_rational)) {
        int bad_idx = na.is_rational ? 1 : 2;
        return grounded_bad_arg_type(a, head, args, nargs,
                                     bad_idx, atom_symbol(a, "Number"),
                                     args[bad_idx - 1]);
    }
    bool fl = na.is_float || nb.is_float;
    bool prefer_rationals = eval_current_prefer_rationals();

    if (!fl && (na.is_bigint || nb.is_bigint ||
                na.is_rational || nb.is_rational)) {
        return eval_integer_binary_gmp(a, head, head_id, args, nargs, &na, &nb,
                                       prefer_rationals, rust_compat);
    }

    if (!fl) {
        int64_t ai = na.ival;
        int64_t bi = nb.ival;

        if (head_id == g_builtin_syms.op_plus) {
            __int128 sum = (__int128)ai + (__int128)bi;
            if (sum >= INT64_MIN && sum <= INT64_MAX)
                return atom_int(a, (int64_t)sum);
            return eval_integer_binary_gmp(a, head, head_id, args, nargs, &na, &nb,
                                           prefer_rationals, rust_compat);
        }
        if (head_id == g_builtin_syms.op_minus) {
            __int128 diff = (__int128)ai - (__int128)bi;
            if (diff >= INT64_MIN && diff <= INT64_MAX)
                return atom_int(a, (int64_t)diff);
            return eval_integer_binary_gmp(a, head, head_id, args, nargs, &na, &nb,
                                           prefer_rationals, rust_compat);
        }
        if (head_id == g_builtin_syms.op_mul) {
            __int128 prod = (__int128)ai * (__int128)bi;
            if (prod >= INT64_MIN && prod <= INT64_MAX)
                return atom_int(a, (int64_t)prod);
            return eval_integer_binary_gmp(a, head, head_id, args, nargs, &na, &nb,
                                           prefer_rationals, rust_compat);
        }
        if (head_id == g_builtin_syms.op_div) {
            if (bi == 0)
                return grounded_division_by_zero(a, head, args, nargs);
            if (ai == INT64_MIN && bi == -1)
                return eval_integer_binary_gmp(a, head, head_id, args, nargs, &na, &nb,
                                               prefer_rationals, rust_compat);
            if (rust_compat)
                return atom_int(a, ai / bi);
            if (ai % bi == 0)
                return atom_int(a, ai / bi);
            if (prefer_rationals)
                return eval_integer_binary_gmp(a, head, head_id, args, nargs, &na, &nb,
                                               prefer_rationals, rust_compat);
            return atom_float(a, (double)ai / (double)bi);
        }
        if (head_id == g_builtin_syms.op_floor_div) {
            if (bi == 0)
                return grounded_division_by_zero(a, head, args, nargs);
            if (ai == INT64_MIN && bi == -1)
                return eval_integer_binary_gmp(a, head, head_id, args, nargs, &na, &nb,
                                               prefer_rationals, rust_compat);
            return atom_int(a, floor_div_i64(ai, bi));
        }
        if (head_id == g_builtin_syms.op_mod) {
            if (bi == 0)
                return grounded_division_by_zero(a, head, args, nargs);
            if (grounded_mod_uses_floor_division())
                return atom_int(a, floor_mod_i64(ai, bi));
            return atom_int(a,
                            ai == INT64_MIN && bi == -1 ? 0 : ai % bi);
        }
        if (head_id == g_builtin_syms.op_lt)  return ai < bi  ? atom_true(a) : atom_false(a);
        if (head_id == g_builtin_syms.op_gt)  return ai > bi  ? atom_true(a) : atom_false(a);
        if (head_id == g_builtin_syms.op_le) return ai <= bi ? atom_true(a) : atom_false(a);
        if (head_id == g_builtin_syms.op_ge) return ai >= bi ? atom_true(a) : atom_false(a);
    }

    if (head_id == g_builtin_syms.op_plus) return make_numeric(a, na.val + nb.val, fl);
    if (head_id == g_builtin_syms.op_minus) return make_numeric(a, na.val - nb.val, fl);
    if (head_id == g_builtin_syms.op_mul) return make_numeric(a, na.val * nb.val, fl);
    /* Keep IEEE float division semantics so `isnan-math` / `isinf-math`
       remain usable on direct arithmetic results. */
    if (head_id == g_builtin_syms.op_div) return nb.val != 0 ? make_numeric(a, na.val / nb.val, fl)
                                                              : atom_float(a, na.val / nb.val);
    if (head_id == g_builtin_syms.op_floor_div) return nb.val != 0 ? atom_float(a, floor(na.val / nb.val))
                                                                   : grounded_division_by_zero(a, head, args, nargs);
    if (head_id == g_builtin_syms.op_mod) return nb.val != 0 ? make_numeric(a, fmod(na.val, nb.val), fl)
                                                              : grounded_division_by_zero(a, head, args, nargs);
    if (head_id == g_builtin_syms.op_lt)  return na.val < nb.val  ? atom_true(a) : atom_false(a);
    if (head_id == g_builtin_syms.op_gt)  return na.val > nb.val  ? atom_true(a) : atom_false(a);
    if (head_id == g_builtin_syms.op_le) return na.val <= nb.val ? atom_true(a) : atom_false(a);
    if (head_id == g_builtin_syms.op_ge) return na.val >= nb.val ? atom_true(a) : atom_false(a);

    return NULL;
}
