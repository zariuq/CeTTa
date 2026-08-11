#define _POSIX_C_SOURCE 200809L

#include "parser_pack_transparent_inline_native_v1.h"

#include "finite_horn_ground_term_v1.h"
#include "native_sha256.h"
#include "parser_pack_abi_stream_v1.h"

#include <errno.h>
#include <limits.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

enum { PP_INLINE_NATIVE_V1_MAX_ACTION_DEPTH = 4096 };

typedef struct {
    PPABIV1Item *items;
    uint32_t item_len;
    Atom *action;
    Atom **trace;
    uint32_t trace_len;
} PPInlineExpansionV1;

typedef struct {
    PPInlineExpansionV1 *data;
    uint32_t len;
    uint32_t cap;
} PPInlineExpansionVecV1;

typedef struct {
    PPABIV1Item *items;
    uint32_t item_len;
    Atom **actions;
    uint32_t action_len;
    Atom **trace;
    uint32_t trace_len;
} PPInlineCombinationV1;

typedef struct {
    PPInlineCombinationV1 *data;
    uint32_t len;
    uint32_t cap;
} PPInlineCombinationVecV1;

typedef struct {
    Atom *production;
    char *canonical;
    Atom **trace;
    uint32_t trace_len;
    char trace_digest[65];
} PPInlineEntryV1;

typedef struct {
    PPInlineEntryV1 *data;
    uint32_t len;
    uint32_t cap;
} PPInlineEntryVecV1;

typedef struct {
    const PPABIV1Pack *source;
    Arena arena;
    bool *transparent;
    bool *cyclic;
    uint8_t *memo_state;
    PPInlineExpansionVecV1 *memo;
    uint32_t max_paths_per_state;
    uint32_t max_productions;
    char *error_buf;
    size_t error_buf_size;
} PPInlineCompilerV1;

typedef struct {
    const PPABIV1Pack *pack;
    const bool *transparent;
    int32_t *index;
    int32_t *low;
    bool *on_stack;
    uint32_t *stack;
    uint32_t stack_len;
    int32_t next_index;
    bool *cyclic;
} PPInlineTarjanV1;

static bool pp_inline_error(char *buf, size_t size,
                            const char *format, ...) {
    if (buf && size > 0u) {
        va_list arguments;
        va_start(arguments, format);
        (void)vsnprintf(buf, size, format, arguments);
        va_end(arguments);
    }
    return false;
}

static bool pp_inline_fail(PPInlineCompilerV1 *compiler,
                           const char *format, ...) {
    if (compiler->error_buf && compiler->error_buf_size > 0u) {
        va_list arguments;
        va_start(arguments, format);
        (void)vsnprintf(compiler->error_buf, compiler->error_buf_size,
                        format, arguments);
        va_end(arguments);
    }
    return false;
}

static bool pp_inline_expr_head(const Atom *term, const char *head,
                                CettaExprLen argument_len) {
    return term && term->kind == ATOM_EXPR &&
        term->expr.len == argument_len + 1u &&
        atom_is_symbol(term->expr.elems[0], head);
}

static bool pp_inline_is_transparent(const Atom *identity) {
    return pp_inline_expr_head(identity, "pp-sub", 2u);
}

static Atom *pp_inline_source_label(const PPABIV1Production *production) {
    if (!production ||
        !pp_inline_expr_head(production->identity, "pp-production", 4u))
        return NULL;
    return production->identity->expr.elems[1];
}

static Atom *pp_inline_expr5(Arena *arena, Atom *head, Atom *a,
                             Atom *b, Atom *c, Atom *d) {
    Atom *items[5] = {head, a, b, c, d};
    return atom_expr(arena, items, 5u);
}

static Atom *pp_inline_expr8(Arena *arena, Atom *head, Atom *a,
                             Atom *b, Atom *c, Atom *d, Atom *e,
                             Atom *f, Atom *g) {
    Atom *items[8] = {head, a, b, c, d, e, f, g};
    return atom_expr(arena, items, 8u);
}

static char *pp_inline_render(const Atom *term) {
    uint8_t *bytes = NULL;
    size_t len = 0u;
    if (!fh_ground_term_v1_render(term, &bytes, &len, NULL, 0u))
        return NULL;
    (void)len;
    return (char *)bytes;
}

static bool pp_inline_qindex(const Atom *term, uint32_t *out) {
    uint32_t value = 0u;
    while (!atom_is_symbol((Atom *)term, "q-zero")) {
        if (!pp_inline_expr_head(term, "q-succ", 1u) ||
            value == UINT32_MAX)
            return false;
        value++;
        term = term->expr.elems[1];
    }
    *out = value;
    return true;
}

static Atom *pp_inline_qterm(PPInlineCompilerV1 *compiler,
                             uint32_t value) {
    Atom *result = atom_symbol(&compiler->arena, "q-zero");
    while (result && value-- > 0u)
        result = atom_expr2(&compiler->arena,
                            atom_symbol(&compiler->arena, "q-succ"),
                            result);
    return result;
}

static Atom *pp_inline_shift_action(PPInlineCompilerV1 *compiler,
                                    Atom *action, uint32_t offset,
                                    unsigned depth);

static Atom *pp_inline_shift_list(PPInlineCompilerV1 *compiler,
                                  Atom *list, uint32_t offset,
                                  unsigned depth) {
    Atom *head;
    Atom *tail;
    if (depth > PP_INLINE_NATIVE_V1_MAX_ACTION_DEPTH) {
        pp_inline_fail(compiler, "semantic action exceeds depth limit");
        return NULL;
    }
    if (atom_is_symbol(list, "pa-nil"))
        return atom_symbol(&compiler->arena, "pa-nil");
    if (!pp_inline_expr_head(list, "pa-cons", 2u)) {
        pp_inline_fail(compiler, "malformed semantic-action list");
        return NULL;
    }
    head = pp_inline_shift_action(
        compiler, list->expr.elems[1], offset, depth + 1u);
    tail = pp_inline_shift_list(
        compiler, list->expr.elems[2], offset, depth + 1u);
    if (!head || !tail)
        return NULL;
    return atom_expr3(&compiler->arena,
                      atom_symbol(&compiler->arena, "pa-cons"),
                      head, tail);
}

static Atom *pp_inline_shift_action(PPInlineCompilerV1 *compiler,
                                    Atom *action, uint32_t offset,
                                    unsigned depth) {
    uint32_t slot;
    Atom *index;
    Atom *list;
    if (depth > PP_INLINE_NATIVE_V1_MAX_ACTION_DEPTH) {
        pp_inline_fail(compiler, "semantic action exceeds depth limit");
        return NULL;
    }
    if (pp_inline_expr_head(action, "pa-slot", 1u)) {
        if (!pp_inline_qindex(action->expr.elems[1], &slot) ||
            slot > UINT32_MAX - offset) {
            pp_inline_fail(compiler, "semantic action has an invalid slot");
            return NULL;
        }
        index = pp_inline_qterm(compiler, slot + offset);
        return index ? atom_expr2(
            &compiler->arena, atom_symbol(&compiler->arena, "pa-slot"),
            index) : NULL;
    }
    if (pp_inline_expr_head(action, "pa-const", 1u))
        return action;
    if (!pp_inline_expr_head(action, "pa-apply", 2u)) {
        pp_inline_fail(compiler, "unknown ParserPack semantic action");
        return NULL;
    }
    list = pp_inline_shift_list(
        compiler, action->expr.elems[2], offset, depth + 1u);
    if (!list)
        return NULL;
    return atom_expr3(&compiler->arena,
                      atom_symbol(&compiler->arena, "pa-apply"),
                      action->expr.elems[1], list);
}

static Atom *pp_inline_substitute_action(
    PPInlineCompilerV1 *compiler, Atom *action,
    Atom *const *replacements, uint32_t replacement_len,
    unsigned depth);

static Atom *pp_inline_substitute_list(
    PPInlineCompilerV1 *compiler, Atom *list,
    Atom *const *replacements, uint32_t replacement_len,
    unsigned depth) {
    Atom *head;
    Atom *tail;
    if (depth > PP_INLINE_NATIVE_V1_MAX_ACTION_DEPTH) {
        pp_inline_fail(compiler, "semantic action exceeds depth limit");
        return NULL;
    }
    if (atom_is_symbol(list, "pa-nil"))
        return atom_symbol(&compiler->arena, "pa-nil");
    if (!pp_inline_expr_head(list, "pa-cons", 2u)) {
        pp_inline_fail(compiler, "malformed semantic-action list");
        return NULL;
    }
    head = pp_inline_substitute_action(
        compiler, list->expr.elems[1], replacements, replacement_len,
        depth + 1u);
    tail = pp_inline_substitute_list(
        compiler, list->expr.elems[2], replacements, replacement_len,
        depth + 1u);
    if (!head || !tail)
        return NULL;
    return atom_expr3(&compiler->arena,
                      atom_symbol(&compiler->arena, "pa-cons"),
                      head, tail);
}

static Atom *pp_inline_substitute_action(
    PPInlineCompilerV1 *compiler, Atom *action,
    Atom *const *replacements, uint32_t replacement_len,
    unsigned depth) {
    uint32_t slot;
    Atom *list;
    if (depth > PP_INLINE_NATIVE_V1_MAX_ACTION_DEPTH) {
        pp_inline_fail(compiler, "semantic action exceeds depth limit");
        return NULL;
    }
    if (pp_inline_expr_head(action, "pa-slot", 1u)) {
        if (!pp_inline_qindex(action->expr.elems[1], &slot) ||
            slot >= replacement_len) {
            pp_inline_fail(
                compiler, "semantic action slot lies outside production");
            return NULL;
        }
        return replacements[slot];
    }
    if (pp_inline_expr_head(action, "pa-const", 1u))
        return action;
    if (!pp_inline_expr_head(action, "pa-apply", 2u)) {
        pp_inline_fail(compiler, "unknown ParserPack semantic action");
        return NULL;
    }
    list = pp_inline_substitute_list(
        compiler, action->expr.elems[2], replacements, replacement_len,
        depth + 1u);
    if (!list)
        return NULL;
    return atom_expr3(&compiler->arena,
                      atom_symbol(&compiler->arena, "pa-apply"),
                      action->expr.elems[1], list);
}

static void pp_inline_expansion_free(PPInlineExpansionV1 *value) {
    if (!value)
        return;
    free(value->items);
    free(value->trace);
    memset(value, 0, sizeof(*value));
}

static void pp_inline_expansion_vec_free(PPInlineExpansionVecV1 *vec) {
    uint32_t index;
    if (!vec)
        return;
    for (index = 0u; index < vec->len; index++)
        pp_inline_expansion_free(&vec->data[index]);
    free(vec->data);
    memset(vec, 0, sizeof(*vec));
}

static bool pp_inline_expansion_push(PPInlineCompilerV1 *compiler,
                                     PPInlineExpansionVecV1 *vec,
                                     PPInlineExpansionV1 value) {
    PPInlineExpansionV1 *next;
    uint32_t cap;
    if (vec->len == compiler->max_paths_per_state)
        return pp_inline_fail(
            compiler, "transparent expansion exceeds its path limit");
    if (vec->len == vec->cap) {
        cap = vec->cap ? vec->cap * 2u : 8u;
        if (cap < vec->cap ||
            (size_t)cap > SIZE_MAX / sizeof(*vec->data))
            return pp_inline_fail(compiler, "transparent expansion is too large");
        next = realloc(vec->data, (size_t)cap * sizeof(*vec->data));
        if (!next)
            return pp_inline_fail(
                compiler, "out of memory expanding transparent state");
        vec->data = next;
        vec->cap = cap;
    }
    vec->data[vec->len++] = value;
    return true;
}

static void pp_inline_combination_free(PPInlineCombinationV1 *value) {
    if (!value)
        return;
    free(value->items);
    free(value->actions);
    free(value->trace);
    memset(value, 0, sizeof(*value));
}

static void pp_inline_combination_vec_free(PPInlineCombinationVecV1 *vec) {
    uint32_t index;
    if (!vec)
        return;
    for (index = 0u; index < vec->len; index++)
        pp_inline_combination_free(&vec->data[index]);
    free(vec->data);
    memset(vec, 0, sizeof(*vec));
}

static bool pp_inline_combination_push(PPInlineCompilerV1 *compiler,
                                       PPInlineCombinationVecV1 *vec,
                                       PPInlineCombinationV1 value) {
    PPInlineCombinationV1 *next;
    uint32_t cap;
    if (vec->len == compiler->max_paths_per_state)
        return pp_inline_fail(
            compiler, "production expansion exceeds its path limit");
    if (vec->len == vec->cap) {
        cap = vec->cap ? vec->cap * 2u : 8u;
        if (cap < vec->cap ||
            (size_t)cap > SIZE_MAX / sizeof(*vec->data))
            return pp_inline_fail(compiler, "production expansion is too large");
        next = realloc(vec->data, (size_t)cap * sizeof(*vec->data));
        if (!next)
            return pp_inline_fail(
                compiler, "out of memory expanding production");
        vec->data = next;
        vec->cap = cap;
    }
    vec->data[vec->len++] = value;
    return true;
}

static bool pp_inline_state_self_edge(const PPABIV1Pack *pack,
                                      uint32_t state_id) {
    uint32_t production_index;
    for (production_index = 0u;
         production_index < pack->production_len; production_index++) {
        const PPABIV1Production *production =
            &pack->productions[production_index];
        uint32_t item_index;
        if (production->lhs_state_id != state_id)
            continue;
        for (item_index = 0u; item_index < production->item_len;
             item_index++) {
            const PPABIV1Item *item = &production->items[item_index];
            if (item->kind == PPABI_V1_ITEM_NONTERMINAL &&
                item->dense_id == state_id)
                return true;
        }
    }
    return false;
}

static void pp_inline_tarjan_visit(PPInlineTarjanV1 *tarjan,
                                   uint32_t state_id) {
    const PPABIV1Pack *pack = tarjan->pack;
    uint32_t production_index;
    uint32_t component_len = 0u;
    uint32_t component_begin;

    tarjan->index[state_id] = tarjan->next_index;
    tarjan->low[state_id] = tarjan->next_index;
    tarjan->next_index++;
    tarjan->stack[tarjan->stack_len++] = state_id;
    tarjan->on_stack[state_id] = true;

    for (production_index = 0u;
         production_index < pack->production_len; production_index++) {
        const PPABIV1Production *production =
            &pack->productions[production_index];
        uint32_t item_index;
        if (production->lhs_state_id != state_id)
            continue;
        for (item_index = 0u; item_index < production->item_len;
             item_index++) {
            const PPABIV1Item *item = &production->items[item_index];
            uint32_t child;
            if (item->kind != PPABI_V1_ITEM_NONTERMINAL)
                continue;
            child = item->dense_id;
            if (!tarjan->transparent[child])
                continue;
            if (tarjan->index[child] < 0) {
                pp_inline_tarjan_visit(tarjan, child);
                if (tarjan->low[child] < tarjan->low[state_id])
                    tarjan->low[state_id] = tarjan->low[child];
            } else if (tarjan->on_stack[child] &&
                       tarjan->index[child] < tarjan->low[state_id]) {
                tarjan->low[state_id] = tarjan->index[child];
            }
        }
    }
    if (tarjan->low[state_id] != tarjan->index[state_id])
        return;
    component_begin = tarjan->stack_len;
    do {
        uint32_t child = tarjan->stack[--tarjan->stack_len];
        tarjan->on_stack[child] = false;
        component_len++;
    } while (tarjan->stack_len > 0u &&
             tarjan->stack[tarjan->stack_len] != state_id);
    component_begin -= component_len;
    if (component_len > 1u || pp_inline_state_self_edge(pack, state_id)) {
        uint32_t index;
        for (index = component_begin;
             index < component_begin + component_len; index++)
            tarjan->cyclic[tarjan->stack[index]] = true;
    }
}

static bool pp_inline_classify_states(PPInlineCompilerV1 *compiler,
                                      uint32_t *cyclic_len) {
    const PPABIV1Pack *pack = compiler->source;
    PPInlineTarjanV1 tarjan = {0};
    uint32_t state_id;
    bool ok = false;

    compiler->transparent = calloc(
        pack->state_len ? pack->state_len : 1u, sizeof(bool));
    compiler->cyclic = calloc(
        pack->state_len ? pack->state_len : 1u, sizeof(bool));
    tarjan.index = malloc(
        (pack->state_len ? pack->state_len : 1u) * sizeof(*tarjan.index));
    tarjan.low = malloc(
        (pack->state_len ? pack->state_len : 1u) * sizeof(*tarjan.low));
    tarjan.on_stack = calloc(
        pack->state_len ? pack->state_len : 1u, sizeof(*tarjan.on_stack));
    tarjan.stack = malloc(
        (pack->state_len ? pack->state_len : 1u) * sizeof(*tarjan.stack));
    if (!compiler->transparent || !compiler->cyclic || !tarjan.index ||
        !tarjan.low || !tarjan.on_stack || !tarjan.stack) {
        pp_inline_fail(compiler,
                       "out of memory classifying transparent states");
        goto done;
    }
    for (state_id = 0u; state_id < pack->state_len; state_id++) {
        compiler->transparent[state_id] = pp_inline_is_transparent(
            pack->states[state_id].identity);
        tarjan.index[state_id] = -1;
        tarjan.low[state_id] = -1;
    }
    tarjan.pack = pack;
    tarjan.transparent = compiler->transparent;
    tarjan.cyclic = compiler->cyclic;
    for (state_id = 0u; state_id < pack->state_len; state_id++) {
        if (compiler->transparent[state_id] &&
            tarjan.index[state_id] < 0)
            pp_inline_tarjan_visit(&tarjan, state_id);
    }
    *cyclic_len = 0u;
    for (state_id = 0u; state_id < pack->state_len; state_id++)
        *cyclic_len += compiler->cyclic[state_id] ? 1u : 0u;
    ok = true;

done:
    free(tarjan.index);
    free(tarjan.low);
    free(tarjan.on_stack);
    free(tarjan.stack);
    return ok;
}

static bool pp_inline_copy_items(const PPABIV1Item *left,
                                 uint32_t left_len,
                                 const PPABIV1Item *right,
                                 uint32_t right_len,
                                 PPABIV1Item **out) {
    PPABIV1Item *items;
    uint32_t total;
    if (left_len > UINT32_MAX - right_len)
        return false;
    total = left_len + right_len;
    items = malloc((total ? total : 1u) * sizeof(*items));
    if (!items)
        return false;
    if (left_len > 0u)
        memcpy(items, left, (size_t)left_len * sizeof(*items));
    if (right_len > 0u)
        memcpy(items + left_len, right,
               (size_t)right_len * sizeof(*items));
    *out = items;
    return true;
}

static bool pp_inline_copy_atoms(Atom *const *left, uint32_t left_len,
                                 Atom *const *right, uint32_t right_len,
                                 Atom ***out) {
    Atom **atoms;
    uint32_t total;
    if (left_len > UINT32_MAX - right_len)
        return false;
    total = left_len + right_len;
    atoms = malloc((total ? total : 1u) * sizeof(*atoms));
    if (!atoms)
        return false;
    if (left_len > 0u)
        memcpy(atoms, left, (size_t)left_len * sizeof(*atoms));
    if (right_len > 0u)
        memcpy(atoms + left_len, right,
               (size_t)right_len * sizeof(*atoms));
    *out = atoms;
    return true;
}

static bool pp_inline_expand_production(
    PPInlineCompilerV1 *compiler,
    const PPABIV1Production *production,
    PPInlineExpansionVecV1 *out);

static bool pp_inline_expand_state(PPInlineCompilerV1 *compiler,
                                   uint32_t state_id) {
    const PPABIV1Pack *pack = compiler->source;
    PPInlineExpansionVecV1 *result = &compiler->memo[state_id];
    uint32_t production_index;
    bool found = false;

    if (compiler->memo_state[state_id] == 2u)
        return true;
    if (compiler->memo_state[state_id] == 1u)
        return pp_inline_fail(
            compiler, "acyclic classifier missed recursive pp-sub state");
    compiler->memo_state[state_id] = 1u;
    for (production_index = 0u;
         production_index < pack->production_len; production_index++) {
        const PPABIV1Production *production =
            &pack->productions[production_index];
        PPInlineExpansionVecV1 expanded = {0};
        uint32_t expansion_index;
        if (production->lhs_state_id != state_id)
            continue;
        found = true;
        if (!pp_inline_expand_production(compiler, production, &expanded)) {
            pp_inline_expansion_vec_free(&expanded);
            return false;
        }
        for (expansion_index = 0u;
             expansion_index < expanded.len; expansion_index++) {
            PPInlineExpansionV1 value = expanded.data[expansion_index];
            Atom **trace;
            Atom *source_label = pp_inline_source_label(production);
            if (!source_label) {
                pp_inline_expansion_vec_free(&expanded);
                return pp_inline_fail(
                    compiler, "source production has no label");
            }
            if (!pp_inline_copy_atoms(
                    &source_label, 1u,
                    value.trace, value.trace_len, &trace)) {
                pp_inline_expansion_vec_free(&expanded);
                return pp_inline_fail(
                    compiler, "out of memory extending source fiber");
            }
            free(value.trace);
            value.trace = trace;
            value.trace_len++;
            memset(&expanded.data[expansion_index], 0,
                   sizeof(expanded.data[expansion_index]));
            if (!pp_inline_expansion_push(compiler, result, value)) {
                pp_inline_expansion_free(&value);
                pp_inline_expansion_vec_free(&expanded);
                return false;
            }
        }
        pp_inline_expansion_vec_free(&expanded);
    }
    if (!found)
        return pp_inline_fail(
            compiler, "transparent state has no production");
    compiler->memo_state[state_id] = 2u;
    return true;
}

static bool pp_inline_symbol_expansions(
    PPInlineCompilerV1 *compiler, const PPABIV1Item *item,
    PPInlineExpansionV1 *singleton,
    const PPInlineExpansionV1 **values, uint32_t *value_len) {
    if (item->kind == PPABI_V1_ITEM_NONTERMINAL &&
        compiler->transparent[item->dense_id] &&
        !compiler->cyclic[item->dense_id]) {
        if (!pp_inline_expand_state(compiler, item->dense_id))
            return false;
        *values = compiler->memo[item->dense_id].data;
        *value_len = compiler->memo[item->dense_id].len;
        return true;
    }
    memset(singleton, 0, sizeof(*singleton));
    singleton->items = (PPABIV1Item *)item;
    singleton->item_len = 1u;
    singleton->action = atom_expr2(
        &compiler->arena, atom_symbol(&compiler->arena, "pa-slot"),
        atom_symbol(&compiler->arena, "q-zero"));
    if (!singleton->action)
        return pp_inline_fail(
            compiler, "out of memory constructing identity action");
    *values = singleton;
    *value_len = 1u;
    return true;
}

static bool pp_inline_expand_production(
    PPInlineCompilerV1 *compiler,
    const PPABIV1Production *production,
    PPInlineExpansionVecV1 *out) {
    PPInlineCombinationVecV1 combinations = {0};
    PPInlineCombinationV1 empty = {0};
    uint32_t item_index;
    bool ok = false;

    if (!pp_inline_combination_push(compiler, &combinations, empty))
        goto done;
    for (item_index = 0u; item_index < production->item_len; item_index++) {
        PPInlineCombinationVecV1 next = {0};
        PPInlineExpansionV1 singleton = {0};
        const PPInlineExpansionV1 *values = NULL;
        uint32_t value_len = 0u;
        uint32_t combination_index;
        if (!pp_inline_symbol_expansions(
                compiler, &production->items[item_index], &singleton,
                &values, &value_len)) {
            pp_inline_combination_vec_free(&next);
            goto done;
        }
        for (combination_index = 0u;
             combination_index < combinations.len; combination_index++) {
            PPInlineCombinationV1 *prefix =
                &combinations.data[combination_index];
            uint32_t value_index;
            for (value_index = 0u; value_index < value_len; value_index++) {
                const PPInlineExpansionV1 *suffix = &values[value_index];
                PPInlineCombinationV1 combined = {0};
                Atom *shifted;
                if (!pp_inline_copy_items(
                        prefix->items, prefix->item_len,
                        suffix->items, suffix->item_len,
                        &combined.items) ||
                    !pp_inline_copy_atoms(
                        prefix->trace, prefix->trace_len,
                        suffix->trace, suffix->trace_len,
                        &combined.trace)) {
                    pp_inline_combination_free(&combined);
                    pp_inline_combination_vec_free(&next);
                    pp_inline_fail(
                        compiler, "out of memory composing expansion");
                    goto done;
                }
                combined.item_len = prefix->item_len + suffix->item_len;
                combined.trace_len = prefix->trace_len + suffix->trace_len;
                shifted = pp_inline_shift_action(
                    compiler, suffix->action, prefix->item_len, 0u);
                if (!shifted || !pp_inline_copy_atoms(
                        prefix->actions, prefix->action_len,
                        &shifted, 1u, &combined.actions)) {
                    pp_inline_combination_free(&combined);
                    pp_inline_combination_vec_free(&next);
                    if (shifted)
                        pp_inline_fail(
                            compiler, "out of memory composing actions");
                    goto done;
                }
                combined.action_len = prefix->action_len + 1u;
                if (!pp_inline_combination_push(
                        compiler, &next, combined)) {
                    pp_inline_combination_free(&combined);
                    pp_inline_combination_vec_free(&next);
                    goto done;
                }
            }
        }
        pp_inline_combination_vec_free(&combinations);
        combinations = next;
    }
    for (item_index = 0u; item_index < combinations.len; item_index++) {
        PPInlineCombinationV1 *combination = &combinations.data[item_index];
        PPInlineExpansionV1 value = {0};
        value.action = pp_inline_substitute_action(
            compiler, production->action,
            combination->actions, combination->action_len, 0u);
        if (!value.action)
            goto done;
        value.items = combination->items;
        value.item_len = combination->item_len;
        value.trace = combination->trace;
        value.trace_len = combination->trace_len;
        combination->items = NULL;
        combination->trace = NULL;
        if (!pp_inline_expansion_push(compiler, out, value)) {
            pp_inline_expansion_free(&value);
            goto done;
        }
    }
    ok = true;

done:
    pp_inline_combination_vec_free(&combinations);
    if (!ok)
        pp_inline_expansion_vec_free(out);
    return ok;
}

static bool pp_inline_hex_byte(char high, char low, uint8_t *out) {
    int left;
    int right;
    if (high >= '0' && high <= '9')
        left = high - '0';
    else if (high >= 'a' && high <= 'f')
        left = high - 'a' + 10;
    else
        return false;
    if (low >= '0' && low <= '9')
        right = low - '0';
    else if (low >= 'a' && low <= 'f')
        right = low - 'a' + 10;
    else
        return false;
    *out = (uint8_t)((left << 4) | right);
    return true;
}

static bool pp_inline_atom_digest(const Atom *term, char digest[65]) {
    char *canonical = pp_inline_render(term);
    if (!canonical)
        return false;
    cetta_native_sha256_hex((const uint8_t *)canonical,
                            strlen(canonical), digest);
    free(canonical);
    return true;
}

static bool pp_inline_trace_digest(Atom *const *trace, uint32_t trace_len,
                                   char digest[65]) {
    static const uint8_t domain[] =
        "ParserPackTransparentInlineTraceV1\0";
    CettaNativeSha256 sha;
    uint32_t index;
    cetta_native_sha256_init(&sha);
    cetta_native_sha256_update(&sha, domain, sizeof(domain) - 1u);
    for (index = 0u; index < trace_len; index++) {
        char identity_digest[65];
        uint8_t binary[32];
        uint8_t length[8] = {0u, 0u, 0u, 0u, 0u, 0u, 0u, 32u};
        uint32_t byte;
        if (!pp_inline_atom_digest(trace[index], identity_digest))
            return false;
        for (byte = 0u; byte < 32u; byte++) {
            if (!pp_inline_hex_byte(identity_digest[byte * 2u],
                                    identity_digest[byte * 2u + 1u],
                                    &binary[byte]))
                return false;
        }
        cetta_native_sha256_update(&sha, length, sizeof(length));
        cetta_native_sha256_update(&sha, binary, sizeof(binary));
    }
    cetta_native_sha256_finish_hex(&sha, digest);
    return true;
}

static Atom *pp_inline_items_term(PPInlineCompilerV1 *compiler,
                                  const PPABIV1Item *items,
                                  uint32_t item_len) {
    const PPABIV1Pack *pack = compiler->source;
    Atom *result = atom_symbol(&compiler->arena, "pp-items-nil");
    uint32_t index = item_len;
    while (result && index > 0u) {
        const PPABIV1Item *item = &items[--index];
        Atom *identity;
        Atom *wrapped;
        if (item->kind == PPABI_V1_ITEM_TERMINAL) {
            if (item->dense_id >= pack->terminal_len)
                return NULL;
            identity = pack->terminals[item->dense_id].identity;
            wrapped = atom_expr2(
                &compiler->arena,
                atom_symbol(&compiler->arena, "pp-terminal"), identity);
        } else {
            if (item->dense_id >= pack->state_len)
                return NULL;
            identity = pack->states[item->dense_id].identity;
            wrapped = atom_expr2(
                &compiler->arena,
                atom_symbol(&compiler->arena, "pp-nonterminal"), identity);
        }
        if (!wrapped)
            return NULL;
        result = atom_expr3(
            &compiler->arena,
            atom_symbol(&compiler->arena, "pp-items-cons"),
            wrapped, result);
    }
    return result;
}

static Atom *pp_inline_production_term(
    PPInlineCompilerV1 *compiler,
    const PPABIV1Production *source_production,
    const PPInlineExpansionV1 *expansion,
    const char trace_digest[65]) {
    Atom *label;
    Atom *items;
    if (source_production->lhs_state_id >= compiler->source->state_len)
        return NULL;
    label = atom_expr2(
        &compiler->arena,
        atom_symbol(&compiler->arena, "pp-inline-label"),
        atom_symbol(&compiler->arena, trace_digest));
    items = pp_inline_items_term(
        compiler, expansion->items, expansion->item_len);
    if (!label || !items)
        return NULL;
    return pp_inline_expr5(
        &compiler->arena,
        atom_symbol(&compiler->arena, "pp-production"),
        label,
        compiler->source->states[source_production->lhs_state_id].identity,
        items,
        expansion->action);
}

static int pp_inline_entry_compare(const void *left, const void *right) {
    const PPInlineEntryV1 *lhs = left;
    const PPInlineEntryV1 *rhs = right;
    return strcmp(lhs->canonical, rhs->canonical);
}

static void pp_inline_entry_vec_free(PPInlineEntryVecV1 *vec) {
    uint32_t index;
    if (!vec)
        return;
    for (index = 0u; index < vec->len; index++) {
        free(vec->data[index].canonical);
        free(vec->data[index].trace);
    }
    free(vec->data);
    memset(vec, 0, sizeof(*vec));
}

static bool pp_inline_entry_push(PPInlineCompilerV1 *compiler,
                                 PPInlineEntryVecV1 *vec,
                                 PPInlineEntryV1 value) {
    PPInlineEntryV1 *next;
    uint32_t cap;
    if (vec->len == compiler->max_productions)
        return pp_inline_fail(
            compiler, "normalized ParserPack exceeds production limit");
    if (vec->len == vec->cap) {
        cap = vec->cap ? vec->cap * 2u : 128u;
        if (cap < vec->cap ||
            (size_t)cap > SIZE_MAX / sizeof(*vec->data))
            return pp_inline_fail(
                compiler, "normalized ParserPack is too large");
        next = realloc(vec->data, (size_t)cap * sizeof(*vec->data));
        if (!next)
            return pp_inline_fail(
                compiler, "out of memory collecting normalized productions");
        vec->data = next;
        vec->cap = cap;
    }
    vec->data[vec->len++] = value;
    return true;
}

static bool pp_inline_build_entries(
    PPInlineCompilerV1 *compiler, PPInlineEntryVecV1 *entries,
    uint32_t *removed_production_len) {
    const PPABIV1Pack *pack = compiler->source;
    uint32_t production_index;

    *removed_production_len = 0u;
    for (production_index = 0u;
         production_index < pack->production_len; production_index++) {
        const PPABIV1Production *production =
            &pack->productions[production_index];
        PPInlineExpansionVecV1 expanded = {0};
        uint32_t expansion_index;
        if (compiler->transparent[production->lhs_state_id] &&
            !compiler->cyclic[production->lhs_state_id]) {
            (*removed_production_len)++;
            continue;
        }
        if (!pp_inline_expand_production(compiler, production, &expanded))
            return false;
        for (expansion_index = 0u;
             expansion_index < expanded.len; expansion_index++) {
            PPInlineExpansionV1 *expansion =
                &expanded.data[expansion_index];
            PPInlineEntryV1 entry = {0};
            Atom *source_label = pp_inline_source_label(production);
            if (!source_label) {
                pp_inline_expansion_vec_free(&expanded);
                return pp_inline_fail(
                    compiler, "source production has no label");
            }
            if (!pp_inline_copy_atoms(
                    &source_label, 1u,
                    expansion->trace, expansion->trace_len,
                    &entry.trace)) {
                pp_inline_expansion_vec_free(&expanded);
                return pp_inline_fail(
                    compiler, "out of memory constructing source fiber");
            }
            entry.trace_len = expansion->trace_len + 1u;
            if (!pp_inline_trace_digest(
                    entry.trace, entry.trace_len, entry.trace_digest)) {
                free(entry.trace);
                pp_inline_expansion_vec_free(&expanded);
                return pp_inline_fail(
                    compiler, "cannot digest normalized source fiber");
            }
            entry.production = pp_inline_production_term(
                compiler, production, expansion, entry.trace_digest);
            entry.canonical = entry.production
                ? pp_inline_render(entry.production) : NULL;
            if (!entry.production || !entry.canonical ||
                !pp_inline_entry_push(compiler, entries, entry)) {
                free(entry.canonical);
                free(entry.trace);
                pp_inline_expansion_vec_free(&expanded);
                if (compiler->error_buf && compiler->error_buf[0] == '\0')
                    pp_inline_fail(
                        compiler, "cannot construct normalized production");
                return false;
            }
        }
        pp_inline_expansion_vec_free(&expanded);
    }
    qsort(entries->data, entries->len, sizeof(*entries->data),
          pp_inline_entry_compare);
    for (production_index = 1u;
         production_index < entries->len; production_index++) {
        if (strcmp(entries->data[production_index - 1u].canonical,
                   entries->data[production_index].canonical) == 0 ||
            strcmp(entries->data[production_index - 1u].trace_digest,
                   entries->data[production_index].trace_digest) == 0) {
            return pp_inline_fail(
                compiler, "normalized ParserPack repeats a production label");
        }
    }
    return entries->len > 0u || pp_inline_fail(
        compiler, "normalized ParserPack has no productions");
}

static Atom *pp_inline_trace_term_balanced(PPInlineCompilerV1 *compiler,
                                           Atom *const *trace,
                                           uint32_t begin,
                                           uint32_t end) {
    uint32_t length = end - begin;
    if (length == 0u)
        return atom_symbol(&compiler->arena, "pp-inline-trace-nil");
    if (length == 1u) {
        char digest[65];
        Atom *occurrence;
        if (!pp_inline_atom_digest(trace[begin], digest))
            return NULL;
        occurrence = atom_expr2(
            &compiler->arena,
            atom_symbol(&compiler->arena, "pp-inline-source-occurrence"),
            atom_symbol(&compiler->arena, digest));
        return occurrence ? atom_expr2(
            &compiler->arena,
            atom_symbol(&compiler->arena, "pp-inline-trace-one"),
            occurrence) : NULL;
    }
    {
        uint32_t middle = begin + length / 2u;
        Atom *left = pp_inline_trace_term_balanced(
            compiler, trace, begin, middle);
        Atom *right = pp_inline_trace_term_balanced(
            compiler, trace, middle, end);
        if (!left || !right)
            return NULL;
        return atom_expr3(
            &compiler->arena,
            atom_symbol(&compiler->arena, "pp-inline-trace-append"),
            left, right);
    }
}

static void pp_inline_compiler_digest(const char *base_digest,
                                      char digest[65]) {
    static const uint8_t domain[] =
        "ParserPackTransparentInlineNativeCompilerV1\0";
    CettaNativeSha256 sha;
    cetta_native_sha256_init(&sha);
    cetta_native_sha256_update(&sha, domain, sizeof(domain) - 1u);
    cetta_native_sha256_update(
        &sha, (const uint8_t *)base_digest, strlen(base_digest));
    cetta_native_sha256_finish_hex(&sha, digest);
}

static bool pp_inline_build_derivations(
    PPInlineCompilerV1 *compiler,
    const PPABIV1Wire *source_wire,
    const PPInlineEntryVecV1 *entries,
    PPABIV1DerivationInput **out,
    size_t *out_len) {
    size_t class_len = 0u;
    size_t total;
    size_t cursor = 0u;
    size_t index;
    PPABIV1DerivationInput *derivations;
    Atom *owner;

    for (index = 0u; index < source_wire->derivation_len; index++)
        class_len += source_wire->derivations[index].kind ==
            PPABI_V1_EVIDENCE_CLASS ? 1u : 0u;
    if ((size_t)entries->len > SIZE_MAX - class_len)
        return pp_inline_fail(compiler, "normalized evidence is too large");
    total = (size_t)entries->len + class_len;
    derivations = calloc(total ? total : 1u, sizeof(*derivations));
    if (!derivations)
        return pp_inline_fail(
            compiler, "out of memory constructing normalized evidence");
    owner = atom_expr2(
        &compiler->arena,
        atom_symbol(&compiler->arena, "pp-transparent-inline-v1"),
        atom_symbol(&compiler->arena,
                    source_wire->expected_pack_digest));
    if (!owner) {
        free(derivations);
        return pp_inline_fail(
            compiler, "out of memory constructing normalization owner");
    }
    for (index = 0u; index < entries->len; index++) {
        const PPInlineEntryV1 *entry = &entries->data[index];
        Atom *trace = pp_inline_trace_term_balanced(
            compiler, entry->trace, 0u, entry->trace_len);
        Atom *answer = atom_expr3(
            &compiler->arena,
            atom_symbol(&compiler->arena, "compile-pack-production"),
            owner, entry->production);
        Atom *certificate = trace ? pp_inline_expr8(
            &compiler->arena,
            atom_symbol(&compiler->arena,
                        "pp-transparent-inline-certificate-v1"),
            atom_symbol(&compiler->arena, source_wire->source_digest),
            atom_symbol(&compiler->arena, source_wire->compiler_digest),
            atom_symbol(&compiler->arena,
                        source_wire->environment_digest),
            atom_symbol(&compiler->arena,
                        source_wire->expected_pack_digest),
            atom_int(&compiler->arena, (int64_t)entry->trace_len),
            atom_symbol(&compiler->arena, entry->trace_digest),
            trace) : NULL;
        if (!trace || !answer || !certificate) {
            free(derivations);
            return pp_inline_fail(
                compiler, "out of memory constructing source certificate");
        }
        derivations[cursor++] = (PPABIV1DerivationInput){
            .kind = PPABI_V1_EVIDENCE_PRODUCTION,
            .artifact = entry->production,
            .answer = answer,
            .certificate = certificate,
        };
    }
    for (index = 0u; index < source_wire->derivation_len; index++) {
        if (source_wire->derivations[index].kind ==
            PPABI_V1_EVIDENCE_CLASS)
            derivations[cursor++] = source_wire->derivations[index];
    }
    *out = derivations;
    *out_len = total;
    return true;
}

static int pp_inline_text_compare(const void *left, const void *right) {
    const char *const *lhs = left;
    const char *const *rhs = right;
    return strcmp(*lhs, *rhs);
}

static bool pp_inline_write_evidence_record(
    FILE *stream, const char *kind,
    const PPABIV1DerivationInput *derivation) {
    char *artifact = pp_inline_render(derivation->artifact);
    char *answer = pp_inline_render(derivation->answer);
    char *certificate = pp_inline_render(derivation->certificate);
    bool ok = artifact && answer && certificate &&
        fprintf(stream, "%s\t%s\t%s\t%s\n",
                kind, artifact, answer, certificate) >= 0;
    free(artifact);
    free(answer);
    free(certificate);
    return ok;
}

static bool pp_inline_emit_abi(
    PPInlineCompilerV1 *compiler,
    const PPABIV1Wire *source_wire,
    const PPInlineEntryVecV1 *entries,
    const PPABIV1DerivationInput *derivations,
    size_t derivation_len,
    const char *compiler_digest,
    const char *pack_digest,
    const char *out_path) {
    char temporary[PATH_MAX] = {0};
    char *start = NULL;
    char **classes = NULL;
    int descriptor = -1;
    FILE *stream = NULL;
    size_t index;
    PPABIV1Wire verification_wire;
    PPABIV1Pack verification_pack;
    bool ok = false;

    ppabi_v1_wire_init(&verification_wire);
    ppabi_v1_pack_init(&verification_pack);
    if (snprintf(temporary, sizeof(temporary), "%s.tmp.XXXXXX",
                 out_path) >= (int)sizeof(temporary))
        return pp_inline_fail(compiler,
                              "normalized ABI path is too long");
    descriptor = mkstemp(temporary);
    if (descriptor < 0)
        return pp_inline_fail(
            compiler, "cannot create normalized ABI temporary: %s",
            strerror(errno));
    stream = fdopen(descriptor, "wb");
    if (!stream) {
        close(descriptor);
        descriptor = -1;
        pp_inline_fail(compiler,
                       "cannot open normalized ABI stream: %s",
                       strerror(errno));
        goto done;
    }
    descriptor = -1;
    start = pp_inline_render(source_wire->start);
    classes = calloc(source_wire->class_len ? source_wire->class_len : 1u,
                     sizeof(*classes));
    if (!start || !classes) {
        pp_inline_fail(compiler,
                       "out of memory serializing normalized ABI");
        goto done;
    }
    for (index = 0u; index < source_wire->class_len; index++) {
        classes[index] = pp_inline_render(source_wire->classes[index]);
        if (!classes[index]) {
            pp_inline_fail(compiler,
                           "cannot render lexical class clause");
            goto done;
        }
    }
    qsort(classes, source_wire->class_len, sizeof(*classes),
          pp_inline_text_compare);
    if (fprintf(
            stream,
            "parser-pack-abi-v1\n"
            "source-digest\t%s\n"
            "compiler-digest\t%s\n"
            "environment-digest\t%s\n"
            "pack-digest\t%s\n"
            "start\t%s\n"
            "closure\t%s\n",
            source_wire->source_digest, compiler_digest,
            source_wire->environment_digest, pack_digest, start,
            source_wire->expected_closed ? "closed" : "partial") < 0)
        goto write_failed;
    for (index = 0u; index < entries->len; index++) {
        if (fprintf(stream, "production\t%s\n",
                    entries->data[index].canonical) < 0)
            goto write_failed;
    }
    for (index = 0u; index < source_wire->class_len; index++) {
        if (fprintf(stream, "class-clause\t%s\n", classes[index]) < 0)
            goto write_failed;
    }
    for (index = 0u; index < derivation_len; index++) {
        const PPABIV1DerivationInput *derivation = &derivations[index];
        if (derivation->kind == PPABI_V1_EVIDENCE_PRODUCTION &&
            !pp_inline_write_evidence_record(
                stream, "production-evidence", derivation))
            goto write_failed;
    }
    for (index = 0u; index < derivation_len; index++) {
        const PPABIV1DerivationInput *derivation = &derivations[index];
        if (derivation->kind == PPABI_V1_EVIDENCE_CLASS &&
            !pp_inline_write_evidence_record(
                stream, "class-evidence", derivation))
            goto write_failed;
    }
    if (fputs("end\n", stream) == EOF || fflush(stream) != 0 ||
        fsync(fileno(stream)) != 0 || fclose(stream) != 0) {
        stream = NULL;
        goto write_failed;
    }
    stream = NULL;
    if (!ppabi_v1_wire_read(
            &verification_wire, temporary,
            compiler->error_buf, compiler->error_buf_size) ||
        !ppabi_v1_wire_load_pack(
            &verification_wire, &verification_pack,
            compiler->error_buf, compiler->error_buf_size) ||
        verification_pack.production_len != entries->len ||
        strcmp(verification_pack.pack_digest, pack_digest) != 0 ||
        strcmp(verification_pack.compiler_digest, compiler_digest) != 0) {
        if (compiler->error_buf && compiler->error_buf[0] == '\0')
            pp_inline_fail(
                compiler, "normalized ABI changed during serialization");
        goto done;
    }
    if (rename(temporary, out_path) != 0) {
        pp_inline_fail(compiler,
                       "cannot publish normalized ABI: %s",
                       strerror(errno));
        goto done;
    }
    temporary[0] = '\0';
    ok = true;
    goto done;

write_failed:
    pp_inline_fail(compiler, "cannot write normalized ABI: %s",
                   strerror(errno));

done:
    if (stream)
        (void)fclose(stream);
    if (descriptor >= 0)
        close(descriptor);
    if (temporary[0] != '\0')
        (void)unlink(temporary);
    if (classes) {
        for (index = 0u; index < source_wire->class_len; index++)
            free(classes[index]);
    }
    free(classes);
    free(start);
    ppabi_v1_pack_free(&verification_pack);
    ppabi_v1_wire_free(&verification_wire);
    return ok;
}

static void pp_inline_compiler_free(PPInlineCompilerV1 *compiler) {
    uint32_t state_id;
    if (!compiler)
        return;
    if (compiler->memo) {
        for (state_id = 0u; state_id < compiler->source->state_len;
             state_id++)
            pp_inline_expansion_vec_free(&compiler->memo[state_id]);
    }
    free(compiler->memo);
    free(compiler->memo_state);
    free(compiler->transparent);
    free(compiler->cyclic);
    arena_free(&compiler->arena);
}

bool pp_transparent_inline_native_v1_compile_file(
    const char *source_abi_path,
    const char *out_path,
    uint32_t max_paths_per_state,
    uint32_t max_productions,
    PPTransparentInlineNativeV1Summary *summary,
    char *error_buf,
    size_t error_buf_size) {
    PPABIV1Wire source_wire;
    PPABIV1Pack source_pack;
    PPABIV1Pack normalized_pack;
    PPInlineCompilerV1 compiler = {0};
    PPInlineEntryVecV1 entries = {0};
    PPABIV1DerivationInput *derivations = NULL;
    size_t derivation_len = 0u;
    Atom **production_terms = NULL;
    PPABIV1ProvenanceInput provenance;
    uint32_t cyclic_len = 0u;
    uint32_t removed_len = 0u;
    char compiler_digest[65];
    char closure_error[512] = {0};
    bool closed;
    uint32_t index;
    bool ok = false;

    ppabi_v1_wire_init(&source_wire);
    ppabi_v1_pack_init(&source_pack);
    ppabi_v1_pack_init(&normalized_pack);
    if (error_buf && error_buf_size > 0u)
        error_buf[0] = '\0';
    if (!source_abi_path || !out_path || !summary ||
        max_paths_per_state == 0u || max_productions == 0u) {
        pp_inline_error(error_buf, error_buf_size,
                        "invalid transparent-inline request");
        goto done;
    }
    memset(summary, 0, sizeof(*summary));
    if (!ppabi_v1_wire_read(
            &source_wire, source_abi_path, error_buf, error_buf_size) ||
        !ppabi_v1_wire_load_pack(
            &source_wire, &source_pack, error_buf, error_buf_size))
        goto done;
    compiler.source = &source_pack;
    compiler.max_paths_per_state = max_paths_per_state;
    compiler.max_productions = max_productions;
    compiler.error_buf = error_buf;
    compiler.error_buf_size = error_buf_size;
    arena_init(&compiler.arena);
    compiler.memo_state = calloc(
        source_pack.state_len ? source_pack.state_len : 1u,
        sizeof(*compiler.memo_state));
    compiler.memo = calloc(
        source_pack.state_len ? source_pack.state_len : 1u,
        sizeof(*compiler.memo));
    if (!compiler.memo_state || !compiler.memo ||
        !pp_inline_classify_states(&compiler, &cyclic_len) ||
        !pp_inline_build_entries(&compiler, &entries, &removed_len) ||
        !pp_inline_build_derivations(
            &compiler, &source_wire, &entries,
            &derivations, &derivation_len)) {
        if (error_buf && error_buf[0] == '\0')
            pp_inline_fail(&compiler,
                           "out of memory initializing normalizer");
        goto done;
    }
    production_terms = malloc(
        (entries.len ? entries.len : 1u) * sizeof(*production_terms));
    if (!production_terms) {
        pp_inline_fail(&compiler,
                       "out of memory loading normalized ParserPack");
        goto done;
    }
    for (index = 0u; index < entries.len; index++)
        production_terms[index] = entries.data[index].production;
    pp_inline_compiler_digest(source_wire.compiler_digest,
                              compiler_digest);
    provenance = (PPABIV1ProvenanceInput){
        .source_digest = source_wire.source_digest,
        .compiler_digest = compiler_digest,
        .environment_digest = source_wire.environment_digest,
        .derivations = derivations,
        .derivation_len = derivation_len,
    };
    if (!ppabi_v1_pack_load(
            &normalized_pack, production_terms, entries.len,
            source_wire.classes, source_wire.class_len,
            &provenance, error_buf, error_buf_size))
        goto done;
    closed = ppabi_v1_pack_start_is_closed(
        &normalized_pack, source_wire.start,
        closure_error, sizeof(closure_error));
    if (closed != source_wire.expected_closed) {
        pp_inline_fail(
            &compiler, "transparent inlining changed start closure");
        goto done;
    }
    if (!pp_inline_emit_abi(
            &compiler, &source_wire, &entries,
            derivations, derivation_len, compiler_digest,
            normalized_pack.pack_digest, out_path))
        goto done;
    summary->source_production_len = source_pack.production_len;
    summary->normalized_production_len = entries.len;
    summary->removed_transparent_production_len = removed_len;
    summary->cyclic_transparent_state_len = cyclic_len;
    memcpy(summary->source_pack_digest, source_pack.pack_digest, 65u);
    memcpy(summary->normalized_pack_digest,
           normalized_pack.pack_digest, 65u);
    memcpy(summary->compiler_digest, compiler_digest, 65u);
    ok = true;

done:
    free(production_terms);
    free(derivations);
    pp_inline_entry_vec_free(&entries);
    pp_inline_compiler_free(&compiler);
    ppabi_v1_pack_free(&normalized_pack);
    ppabi_v1_pack_free(&source_pack);
    ppabi_v1_wire_free(&source_wire);
    return ok;
}
