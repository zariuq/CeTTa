#include "petta_specializer.h"

#include "grounded.h"
#include "match.h"
#include "petta_semantics.h"
#include "symbol.h"

#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    Atom **items;
    size_t len;
    size_t cap;
} PettaAtomVector;

typedef struct {
    VarId variable;
    Atom *value;
    CettaExprIndex *path;
    size_t path_len;
} PettaSpecializationBinding;

typedef struct {
    PettaSpecializationBinding *items;
    size_t len;
    size_t cap;
} PettaSpecializationBindings;

typedef struct {
    Atom *atom;
    PettaSpecializerPatternNode *pattern;
    const PettaPlanNode *rhs_plan;
} PettaSpecializationArtifact;

struct PettaSpecializerPatternNode {
    struct PettaSpecializerPatternNode *children;
    CettaExprLen child_count;
    bool structural;
};

typedef struct {
    Space *space;
    uint64_t space_instance;
    SymbolId source;
    SymbolId specialized;
    PettaSpecializationBinding *selectors;
    size_t selector_len;
    PettaSpecializationArtifact *artifacts;
    size_t artifact_len;
    size_t artifact_cap;
    bool productive;
    bool invalidating;
} PettaSpecializationRecord;

typedef struct {
    PettaSpecializationRecord *items;
    size_t len;
    size_t cap;
} PettaSpecializationRecords;

typedef struct {
    SymbolId *items;
    size_t len;
    size_t cap;
} PettaSymbolVector;

typedef struct {
    Atom *source;
    Atom *derived;
    PettaSpecializerPatternNode *pattern;
    uint32_t depth;
} PettaPatternBuildItem;

typedef struct {
    PettaPatternBuildItem *items;
    size_t len;
    size_t cap;
} PettaPatternBuildStack;

typedef struct {
    PettaSpecializerPatternNode **items;
    size_t len;
    size_t cap;
} PettaPatternNodeVector;

typedef struct {
    Atom *atom;
    size_t parent;
    CettaExprIndex edge;
    size_t depth;
} PettaPathSearchItem;

typedef struct {
    PettaPathSearchItem *items;
    size_t len;
    size_t cap;
} PettaPathSearch;

typedef struct {
    char *bytes;
    size_t len;
    size_t cap;
} PettaStringBuilder;

typedef struct {
    Space *space;
    PettaProgram *program;
    Arena *persistent;
    Arena scratch;
    SymbolId *visiting;
    size_t visiting_len;
    size_t visiting_cap;
    uint32_t depth;
    bool capacity;
    bool invalidated;
} PettaSpecializerContext;

typedef struct {
    bool eligible;
    bool productive;
    bool filtered;
    bool relevance_bounded;
    SymbolId specialized;
} PettaSpecializationAnalysis;

static _Thread_local PettaSpecializationRecords
    g_petta_specializations = {0};

enum { PETTA_RELEVANCE_BOUNDED_CACHE_SLOTS = 64 };

typedef struct {
    Space *space;
    uint64_t space_instance;
    SymbolId source;
    bool used;
} PettaRelevanceBoundedCacheSlot;

static _Thread_local PettaRelevanceBoundedCacheSlot
    g_petta_relevance_bounded_cache[
        PETTA_RELEVANCE_BOUNDED_CACHE_SLOTS];

static bool petta_atom_vector_push(
    PettaAtomVector *vector, Atom *atom);
static void petta_remove_record_at(size_t index);

static bool petta_specializer_trace_enabled(void) {
    static _Thread_local int enabled = -1;
    if (enabled < 0) {
        const char *value =
            getenv("CETTA_PETTA_SPECIALIZER_TRACE");
        enabled =
            value && value[0] != '\0' &&
            !(value[0] == '0' && value[1] == '\0')
                ? 1 : 0;
    }
    return enabled == 1;
}

static bool petta_specializer_route_cache_enabled(void) {
    const char *value =
        getenv("CETTA_PETTA_SPECIALIZER_ROUTE_CACHE");
    return !value ||
           (strcmp(value, "0") != 0 &&
            strcmp(value, "false") != 0 &&
            strcmp(value, "off") != 0);
}

static bool petta_specializer_relevance_filter_enabled(void) {
    static _Thread_local int enabled = -1;
    if (enabled < 0) {
        const char *value = getenv(
            "CETTA_PETTA_SPECIALIZER_RELEVANCE_FILTER");
        enabled = value && value[0] == '1' ? 1 : 0;
    }
    return enabled == 1;
}

static size_t petta_relevance_bounded_cache_index(
    Space *space, uint64_t instance, SymbolId source) {
    uint64_t mixed =
        ((uint64_t)(uintptr_t)space >> 4u) ^
        (instance * UINT64_C(0x9e3779b97f4a7c15)) ^
        ((uint64_t)source * UINT64_C(0xbf58476d1ce4e5b9));
    mixed ^= mixed >> 30u;
    mixed *= UINT64_C(0xbf58476d1ce4e5b9);
    mixed ^= mixed >> 27u;
    return (size_t)mixed &
           (PETTA_RELEVANCE_BOUNDED_CACHE_SLOTS - 1u);
}

static bool petta_relevance_filter_is_bounded_out(
    Space *space, SymbolId source) {
    if (!space || source == SYMBOL_ID_NONE)
        return false;
    uint64_t instance = space_instance_id(space);
    PettaRelevanceBoundedCacheSlot *slot =
        &g_petta_relevance_bounded_cache[
            petta_relevance_bounded_cache_index(
                space, instance, source)];
    return slot->used && slot->space == space &&
           slot->space_instance == instance &&
           slot->source == source;
}

static void petta_relevance_filter_mark_bounded_out(
    Space *space, SymbolId source) {
    if (!space || source == SYMBOL_ID_NONE)
        return;
    uint64_t instance = space_instance_id(space);
    PettaRelevanceBoundedCacheSlot *slot =
        &g_petta_relevance_bounded_cache[
            petta_relevance_bounded_cache_index(
                space, instance, source)];
    *slot = (PettaRelevanceBoundedCacheSlot){
        .space = space,
        .space_instance = instance,
        .source = source,
        .used = true,
    };
}

static void petta_relevance_filter_clear_bounded_cache(void) {
    memset(g_petta_relevance_bounded_cache, 0,
           sizeof(g_petta_relevance_bounded_cache));
}

static void petta_specializer_trace_atom(
    const char *label, Atom *atom) {
    if (!petta_specializer_trace_enabled())
        return;
    fputs("[petta-specializer] ", stderr);
    fputs(label, stderr);
    if (atom)
        atom_print(atom, stderr);
    else
        fputs("<null>", stderr);
    fputc('\n', stderr);
}

static void petta_specializer_trace_variables(Atom *root) {
    if (!petta_specializer_trace_enabled() || !root)
        return;
    PettaAtomVector stack = {0};
    if (!petta_atom_vector_push(&stack, root))
        return;
    fputs("[petta-specializer] variables:", stderr);
    while (stack.len > 0u) {
        Atom *atom = stack.items[--stack.len];
        if (atom->kind == ATOM_VAR) {
            fprintf(
                stderr, " %s=%" PRIu64,
                atom->sym_id == SYMBOL_ID_NONE
                    ? "<structural>"
                    : symbol_bytes(g_symbols, atom->sym_id),
                (uint64_t)atom->var_id);
            continue;
        }
        if (atom->kind != ATOM_EXPR)
            continue;
        for (CettaExprIndex index = atom->expr.len;
             index > 0u; index--) {
            if (!petta_atom_vector_push(
                    &stack, atom->expr.elems[index - 1u])) {
                free(stack.items);
                fputs(" <capacity>\n", stderr);
                return;
            }
        }
    }
    free(stack.items);
    fputc('\n', stderr);
}

static bool petta_reserve(
    void **items, size_t *capacity, size_t needed,
    size_t width) {
    if (needed <= *capacity)
        return true;
    if (width == 0u || needed > SIZE_MAX / width)
        return false;
    size_t next = *capacity ? *capacity : 8u;
    while (next < needed) {
        if (next > SIZE_MAX / 2u) {
            next = needed;
            break;
        }
        next *= 2u;
    }
    if (next > SIZE_MAX / width)
        return false;
    *items = *items
        ? cetta_realloc(*items, width * next)
        : cetta_malloc(width * next);
    *capacity = next;
    return true;
}

static bool petta_atom_vector_push(
    PettaAtomVector *vector, Atom *atom) {
    if (!vector || !atom ||
        !petta_reserve(
            (void **)&vector->items, &vector->cap,
            vector->len + 1u, sizeof(*vector->items))) {
        return false;
    }
    vector->items[vector->len++] = atom;
    return true;
}

static bool petta_symbol_vector_push_unique(
    PettaSymbolVector *vector, SymbolId symbol) {
    if (!vector || symbol == SYMBOL_ID_NONE)
        return false;
    for (size_t index = 0u; index < vector->len; index++) {
        if (vector->items[index] == symbol)
            return true;
    }
    if (!petta_reserve(
            (void **)&vector->items, &vector->cap,
            vector->len + 1u, sizeof(*vector->items))) {
        return false;
    }
    vector->items[vector->len++] = symbol;
    return true;
}

static void petta_pattern_free(
    PettaSpecializerPatternNode *root) {
    if (!root)
        return;
    PettaPatternNodeVector nodes = {0};
    if (!petta_reserve(
            (void **)&nodes.items, &nodes.cap, 1u,
            sizeof(*nodes.items))) {
        free(root);
        return;
    }
    nodes.items[nodes.len++] = root;
    for (size_t cursor = 0u; cursor < nodes.len; cursor++) {
        PettaSpecializerPatternNode *node = nodes.items[cursor];
        if (!node->children)
            continue;
        if (!petta_reserve(
                (void **)&nodes.items, &nodes.cap,
                nodes.len + (size_t)node->child_count,
                sizeof(*nodes.items))) {
            break;
        }
        for (CettaExprIndex index = 0u;
             index < node->child_count; index++) {
            nodes.items[nodes.len++] = &node->children[index];
        }
    }
    for (size_t index = nodes.len; index > 0u; index--)
        free(nodes.items[index - 1u]->children);
    free(nodes.items);
    free(root);
}

static PettaSpecializerPatternNode *petta_pattern_build(
    PettaSpecializerContext *context, Atom *source,
    Atom *derived, Bindings *selected) {
    if (!context || !source || !derived || !selected)
        return NULL;
    PettaSpecializerPatternNode *root =
        cetta_malloc(sizeof(*root));
    memset(root, 0, sizeof(*root));
    PettaPatternBuildStack stack = {0};
    if (!petta_reserve(
            (void **)&stack.items, &stack.cap, 1u,
            sizeof(*stack.items))) {
        free(root);
        context->capacity = true;
        return NULL;
    }
    stack.items[stack.len++] = (PettaPatternBuildItem){
        .source = source,
        .derived = derived,
        .pattern = root,
    };
    bool saw_structural = false;
    bool ok = true;
    while (stack.len > 0u && ok) {
        PettaPatternBuildItem item =
            stack.items[--stack.len];
        if (!item.derived || item.depth > 2048u) {
            ok = false;
            break;
        }
        if (item.source &&
            item.source->kind == ATOM_EXPR &&
            item.source->expr.len > 0u &&
            item.source->expr.elems[0]->kind == ATOM_VAR &&
            bindings_lookup_id(
                selected,
                item.source->expr.elems[0]->var_id)) {
            item.pattern->structural = true;
            saw_structural = true;
        }
        if (item.derived->kind != ATOM_EXPR)
            continue;
        item.pattern->child_count = item.derived->expr.len;
        if (item.pattern->child_count == 0u)
            continue;
        if (!cetta_expr_len_mul_fits_size(
                item.pattern->child_count,
                sizeof(*item.pattern->children)) ||
            !petta_reserve(
                (void **)&stack.items, &stack.cap,
                stack.len +
                    (size_t)item.pattern->child_count,
                sizeof(*stack.items))) {
            ok = false;
            break;
        }
        item.pattern->children = cetta_malloc(
            sizeof(*item.pattern->children) *
            (size_t)item.pattern->child_count);
        memset(
            item.pattern->children, 0,
            sizeof(*item.pattern->children) *
            (size_t)item.pattern->child_count);
        bool parallel =
            item.source &&
            item.source->kind == ATOM_EXPR &&
            item.source->expr.len ==
                item.derived->expr.len;
        for (CettaExprIndex index =
                 item.pattern->child_count;
             index > 0u; index--) {
            CettaExprIndex child = index - 1u;
            stack.items[stack.len++] =
                (PettaPatternBuildItem){
                    .source = parallel
                        ? item.source->expr.elems[child]
                        : NULL,
                    .derived =
                        item.derived->expr.elems[child],
                    .pattern =
                        &item.pattern->children[child],
                    .depth = item.depth + 1u,
                };
        }
    }
    free(stack.items);
    if (!ok) {
        context->capacity = true;
        petta_pattern_free(root);
        return NULL;
    }
    if (!saw_structural) {
        petta_pattern_free(root);
        return NULL;
    }
    return root;
}

static bool petta_binding_vector_push_unique(
    PettaSpecializationBindings *bindings,
    VarId variable, Atom *value,
    CettaExprIndex *path, size_t path_len) {
    if (!bindings || !value || (!path && path_len > 0u))
        return false;
    for (size_t index = 0u; index < bindings->len; index++) {
        PettaSpecializationBinding *existing =
            &bindings->items[index];
        if (existing->path_len != path_len ||
            (path_len > 0u &&
             memcmp(
                 existing->path, path,
                 sizeof(*path) * path_len) != 0)) {
            continue;
        }
        if (atom_eq(existing->value, value)) {
            free(path);
            return true;
        }
        free(path);
        return false;
    }
    if (!petta_reserve(
            (void **)&bindings->items, &bindings->cap,
            bindings->len + 1u, sizeof(*bindings->items))) {
        free(path);
        return false;
    }
    bindings->items[bindings->len++] =
        (PettaSpecializationBinding){
            .variable = variable,
            .value = value,
            .path = path,
            .path_len = path_len,
        };
    return true;
}

static bool petta_find_variable_path(
    Atom *root, VarId variable,
    CettaExprIndex **out_path, size_t *out_len) {
    if (out_path)
        *out_path = NULL;
    if (out_len)
        *out_len = 0u;
    if (!root || !out_path || !out_len)
        return false;
    PettaPathSearch search = {0};
    if (!petta_reserve(
            (void **)&search.items, &search.cap, 1u,
            sizeof(*search.items))) {
        return false;
    }
    search.items[search.len++] = (PettaPathSearchItem){
        .atom = root,
        .parent = SIZE_MAX,
    };
    size_t found = SIZE_MAX;
    for (size_t cursor = 0u;
         cursor < search.len && found == SIZE_MAX;
         cursor++) {
        PettaPathSearchItem *item = &search.items[cursor];
        if (item->atom->kind == ATOM_VAR &&
            item->atom->var_id == variable) {
            found = cursor;
            break;
        }
        if (item->atom->kind != ATOM_EXPR ||
            item->depth >= 2048u) {
            continue;
        }
        Atom *parent_atom = item->atom;
        size_t parent_depth = item->depth;
        if (!petta_reserve(
                (void **)&search.items, &search.cap,
                search.len +
                    (size_t)parent_atom->expr.len,
                sizeof(*search.items))) {
            free(search.items);
            return false;
        }
        for (CettaExprIndex index = 0u;
             index < parent_atom->expr.len; index++) {
            search.items[search.len++] =
                (PettaPathSearchItem){
                    .atom = parent_atom->expr.elems[index],
                    .parent = cursor,
                    .edge = index,
                    .depth = parent_depth + 1u,
                };
        }
    }
    if (found == SIZE_MAX) {
        free(search.items);
        return false;
    }
    size_t length = search.items[found].depth;
    CettaExprIndex *path = length > 0u
        ? cetta_malloc(sizeof(*path) * length)
        : NULL;
    size_t cursor = found;
    for (size_t index = length; index > 0u; index--) {
        path[index - 1u] = search.items[cursor].edge;
        cursor = search.items[cursor].parent;
    }
    free(search.items);
    *out_path = path;
    *out_len = length;
    return true;
}

static Atom *petta_atom_at_path(
    Atom *root, const CettaExprIndex *path,
    size_t path_len) {
    Atom *atom = root;
    for (size_t index = 0u; index < path_len; index++) {
        if (!atom || atom->kind != ATOM_EXPR ||
            path[index] >= atom->expr.len) {
            return NULL;
        }
        atom = atom->expr.elems[path[index]];
    }
    return atom;
}

static bool petta_string_append_span(
    PettaStringBuilder *builder, const char *bytes,
    size_t length) {
    if (!builder || (!bytes && length != 0u) ||
        length > SIZE_MAX - builder->len - 1u ||
        !petta_reserve(
            (void **)&builder->bytes, &builder->cap,
            builder->len + length + 1u, sizeof(char))) {
        return false;
    }
    if (length > 0u)
        memcpy(builder->bytes + builder->len, bytes, length);
    builder->len += length;
    builder->bytes[builder->len] = '\0';
    return true;
}

static bool petta_string_append(
    PettaStringBuilder *builder, const char *text) {
    return text &&
        petta_string_append_span(builder, text, strlen(text));
}

static bool petta_specialization_key_atom(
    PettaStringBuilder *builder, Arena *scratch,
    Atom *atom, uint32_t depth) {
    if (!builder || !scratch || !atom || depth > 256u)
        return false;
    Atom *base = NULL;
    Atom *arguments = NULL;
    if (petta_semantics_partial_view(atom, &base, &arguments)) {
        if (!petta_string_append(builder, "partial(") ||
            !petta_specialization_key_atom(
                builder, scratch, base, depth + 1u) ||
            !petta_string_append(builder, ",[")) {
            return false;
        }
        for (CettaExprIndex index = 0u;
             index < arguments->expr.len; index++) {
            if ((index > 0u &&
                 !petta_string_append(builder, ",")) ||
                !petta_specialization_key_atom(
                    builder, scratch,
                    arguments->expr.elems[index],
                    depth + 1u)) {
                return false;
            }
        }
        return petta_string_append(builder, "])");
    }
    if (atom->kind == ATOM_VAR)
        return petta_string_append(builder, "VAR");
    if (atom->kind == ATOM_SYMBOL) {
        return petta_string_append_span(
            builder, symbol_bytes(g_symbols, atom->sym_id),
            symbol_len(g_symbols, atom->sym_id));
    }
    if (atom->kind == ATOM_EXPR) {
        if (!petta_string_append(builder, "["))
            return false;
        for (CettaExprIndex index = 0u;
             index < atom->expr.len; index++) {
            if ((index > 0u &&
                 !petta_string_append(builder, ",")) ||
                !petta_specialization_key_atom(
                    builder, scratch,
                    atom->expr.elems[index], depth + 1u)) {
                return false;
            }
        }
        return petta_string_append(builder, "]");
    }
    if (atom->kind == ATOM_GROUNDED) {
        char scalar[128];
        int length = 0;
        switch (atom->ground.gkind) {
        case GV_INT:
            length = snprintf(
                scalar, sizeof(scalar), "%" PRId64,
                atom->ground.ival);
            break;
        case GV_FLOAT:
            length = cetta_format_float(
                scalar, sizeof(scalar), atom->ground.fval);
            break;
        case GV_BOOL:
            return petta_string_append(
                builder,
                atom->ground.bval ? "True" : "False");
        case GV_BIGINT:
            return petta_string_append(
                builder, atom_bigint_cstr(atom));
        case GV_RATIONAL:
            return petta_string_append(
                builder, atom_rational_cstr(atom));
        default:
            break;
        }
        if (length > 0 &&
            (size_t)length < sizeof(scalar)) {
            return petta_string_append_span(
                builder, scalar, (size_t)length);
        }
    }
    char *rendered = atom_to_parseable_string(scratch, atom);
    return rendered && petta_string_append(builder, rendered);
}

static SymbolId petta_specialized_symbol(
    PettaSpecializerContext *context, SymbolId source,
    const PettaSpecializationBindings *bindings) {
    if (!context || source == SYMBOL_ID_NONE || !bindings ||
        bindings->len == 0u) {
        return SYMBOL_ID_NONE;
    }
    PettaStringBuilder builder = {0};
    bool ok =
        petta_string_append_span(
            &builder, symbol_bytes(g_symbols, source),
            symbol_len(g_symbols, source)) &&
        petta_string_append(&builder, "_Spec_[");
    for (size_t index = 0u; ok && index < bindings->len; index++) {
        ok = (index == 0u ||
              petta_string_append(&builder, ",")) &&
             petta_specialization_key_atom(
                 &builder, &context->scratch,
                 bindings->items[index].value, 0u);
    }
    ok = ok && petta_string_append(&builder, "]");
    SymbolId result = ok
        ? symbol_intern_cstr(g_symbols, builder.bytes)
        : SYMBOL_ID_NONE;
    free(builder.bytes);
    if (result == SYMBOL_ID_NONE)
        context->capacity = true;
    return result;
}

static bool petta_atom_contains_variable(
    Atom *root, VarId variable) {
    if (!root)
        return false;
    PettaAtomVector stack = {0};
    if (!petta_atom_vector_push(&stack, root))
        return false;
    while (stack.len > 0u) {
        Atom *atom = stack.items[--stack.len];
        if (atom->kind == ATOM_VAR &&
            atom->var_id == variable) {
            free(stack.items);
            return true;
        }
        if (atom->kind != ATOM_EXPR)
            continue;
        for (CettaExprIndex index = 0u;
             index < atom->expr.len; index++) {
            if (!petta_atom_vector_push(
                    &stack, atom->expr.elems[index])) {
                free(stack.items);
                return false;
            }
        }
    }
    free(stack.items);
    return false;
}

static bool petta_variable_is_direct_callable(
    Atom *root, VarId variable) {
    if (!root)
        return false;
    PettaAtomVector stack = {0};
    if (!petta_atom_vector_push(&stack, root))
        return false;
    while (stack.len > 0u) {
        Atom *atom = stack.items[--stack.len];
        if (atom->kind != ATOM_EXPR)
            continue;
        if (atom->expr.len > 0u &&
            atom->expr.elems[0]->kind == ATOM_VAR &&
            atom->expr.elems[0]->var_id == variable) {
            free(stack.items);
            return true;
        }
        for (CettaExprIndex index = 0u;
             index < atom->expr.len; index++) {
            if (!petta_atom_vector_push(
                    &stack, atom->expr.elems[index])) {
                free(stack.items);
                return false;
            }
        }
    }
    free(stack.items);
    return false;
}

static bool petta_symbol_is_callable(
    Space *space, Arena *scratch, SymbolId symbol) {
    if (!space || !scratch || symbol == SYMBOL_ID_NONE)
        return false;
    if (space_equations_may_match_known_head(space, symbol) ||
        is_grounded_op(symbol) ||
        petta_semantics_form(symbol) != PETTA_FORM_NONE) {
        return true;
    }
    CettaExprLen intrinsic = 0u;
    if (petta_semantics_intrinsic_partial_arity(
            symbol, &intrinsic)) {
        return true;
    }
    Atom *subject = atom_symbol_id(scratch, symbol);
    Atom **types = NULL;
    uint32_t count = space_get_declared_types(
        space, scratch, subject, &types);
    bool callable = false;
    for (uint32_t index = 0u; index < count; index++) {
        Atom *type = types[index];
        if (type && type->kind == ATOM_EXPR &&
            type->expr.len >= 2u &&
            atom_is_symbol_id(
                type->expr.elems[0],
                g_builtin_syms.arrow)) {
            callable = true;
            break;
        }
    }
    free(types);
    return callable;
}

static Atom *petta_specializable_value(
    PettaSpecializerContext *context, Arena *arena,
    Atom *atom) {
    if (!context || !arena || !atom || atom_has_vars(atom))
        return NULL;
    Atom *base = NULL;
    Atom *arguments = NULL;
    if (petta_semantics_partial_view(
            atom, &base, &arguments)) {
        return base && arguments ? atom : NULL;
    }
    if (atom->kind == ATOM_SYMBOL) {
        return petta_symbol_is_callable(
                   context->space, &context->scratch,
                   atom->sym_id)
            ? atom : NULL;
    }
    if (atom->kind != ATOM_EXPR ||
        atom->expr.len == 0u ||
        atom->expr.elems[0]->kind != ATOM_SYMBOL) {
        return NULL;
    }
    CettaExprLen supplied = atom->expr.len - 1u;
    PeTTaNamedArity arity = petta_semantics_named_arity(
        context->space, &context->scratch,
        atom->expr.elems[0], supplied);
    if (!arity.known || arity.exact || !arity.larger)
        return NULL;
    return petta_semantics_partial_value(
        arena, atom->expr.elems[0],
        atom->expr.elems + 1u, supplied);
}

/*
 * Source matching can bind a source-pattern variable only to a subtree of a
 * ready call.  The cons-constraint exception constructs an internal open-cons
 * spine, which is not a specializable value.  Therefore a call whose argument
 * forest contains no callable symbol, canonical partial, or under-application
 * cannot produce a specialization selector.
 *
 * This is a bounded accelerator, never an authority: an oversized frontier
 * or any shape we cannot classify takes the original analysis.  A deep unary
 * forest is bounded separately by a node budget; after that budget is reached
 * the relation uses the source matcher directly until a semantic
 * equation/type mutation clears the derived decision.
 * The outer call head is deliberately excluded because source equations pin
 * it to the selected relation head; nested expression heads remain visible
 * because a nested source variable may bind them.
 */
typedef enum {
    PETTA_RELEVANCE_NO = 0,
    PETTA_RELEVANCE_YES,
    PETTA_RELEVANCE_NODE_BUDGET,
} PettaRelevanceResult;

static PettaRelevanceResult
petta_call_may_supply_specializable_value(
    PettaSpecializerContext *context, Atom *call) {
    enum {
        PETTA_RELEVANCE_STACK_CAPACITY = 128,
        PETTA_RELEVANCE_NODE_LIMIT = 16,
    };
    Atom *stack[PETTA_RELEVANCE_STACK_CAPACITY];
    size_t length = 0u;
    size_t visited = 0u;
    if (!context || !call || call->kind != ATOM_EXPR ||
        call->expr.len == 0u) {
        return PETTA_RELEVANCE_YES;
    }
    if ((size_t)(call->expr.len - 1u) >
        PETTA_RELEVANCE_STACK_CAPACITY) {
        return PETTA_RELEVANCE_YES;
    }
    for (CettaExprIndex index = 1u;
         index < call->expr.len; index++) {
        stack[length++] = call->expr.elems[index];
    }

    while (length > 0u) {
        if (visited++ >= PETTA_RELEVANCE_NODE_LIMIT)
            return PETTA_RELEVANCE_NODE_BUDGET;
        Atom *atom = stack[--length];
        if (!atom)
            return PETTA_RELEVANCE_YES;
        if (atom->kind == ATOM_SYMBOL &&
            petta_symbol_is_callable(
                context->space, &context->scratch,
                atom->sym_id)) {
            return PETTA_RELEVANCE_YES;
        }
        if (atom->kind != ATOM_EXPR || atom->expr.len == 0u)
            continue;
        if (petta_semantics_partial_view(atom, NULL, NULL))
            return PETTA_RELEVANCE_YES;
        Atom *head = atom->expr.elems[0];
        if (head && head->kind == ATOM_SYMBOL) {
            CettaExprLen supplied = atom->expr.len - 1u;
            PeTTaNamedArity arity = petta_semantics_named_arity(
                context->space, &context->scratch,
                head, supplied);
            if (arity.known && !arity.exact && arity.larger)
                return PETTA_RELEVANCE_YES;
        }
        if ((size_t)atom->expr.len >
            PETTA_RELEVANCE_STACK_CAPACITY - length) {
            return PETTA_RELEVANCE_YES;
        }
        for (CettaExprIndex index = 0u;
             index < atom->expr.len; index++) {
            stack[length++] = atom->expr.elems[index];
        }
    }
    return PETTA_RELEVANCE_NO;
}

static bool petta_collect_source_equations(
    PettaSpecializerContext *context, SymbolId head,
    PettaAtomVector *equations) {
    SpaceEquationCursor cursor;
    if (!space_equation_cursor_init(
            context->space, head, &cursor)) {
        return true;
    }
    for (;;) {
        SpaceEquationOccurrenceId id;
        SpaceEquationCursorStep step =
            space_equation_cursor_next(&cursor, &id);
        if (step == SPACE_EQUATION_CURSOR_END)
            return true;
        if (step == SPACE_EQUATION_CURSOR_INVALIDATED) {
            context->invalidated = true;
            return false;
        }
        SpaceEquationOccurrence occurrence;
        if (!space_equation_occurrence_resolve(
                id, &occurrence)) {
            context->invalidated = true;
            return false;
        }
        Atom *lhs = occurrence.lhs;
        if (!lhs || lhs->kind != ATOM_EXPR ||
            lhs->expr.len == 0u ||
            !atom_is_symbol_id(lhs->expr.elems[0], head)) {
            continue;
        }
        petta_specializer_trace_atom("source equation: ",
                                     occurrence.equation);
        if (!petta_atom_vector_push(
                equations, occurrence.equation)) {
            context->capacity = true;
            return false;
        }
    }
}

static bool petta_match_source_call(
    PettaSpecializerContext *context, Atom *equation,
    Atom *call, Atom **fresh_equation, Bindings *bindings) {
    if (fresh_equation)
        *fresh_equation = NULL;
    bindings_init(bindings);
    if (!context || !equation || !call ||
        call->kind != ATOM_EXPR || call->expr.len == 0u) {
        return false;
    }
    Atom *fresh = atom_freshen_epoch(
        &context->scratch, equation, fresh_var_suffix());
    if (!fresh || fresh->kind != ATOM_EXPR ||
        fresh->expr.len != 3u ||
        !atom_is_symbol_id(
            fresh->expr.elems[0], g_builtin_syms.equals)) {
        context->capacity = true;
        return false;
    }
    Atom *lhs = fresh->expr.elems[1];
    if (!lhs || lhs->kind != ATOM_EXPR ||
        lhs->expr.len != call->expr.len ||
        lhs->expr.len == 0u ||
        !atom_eq(lhs->expr.elems[0], call->expr.elems[0])) {
        return false;
    }
    BindingsBuilder builder;
    if (!bindings_builder_init(&builder, NULL)) {
        context->capacity = true;
        return false;
    }
    bool matched =
        petta_semantics_contains_cons_constraint(lhs)
            ? petta_semantics_match_cons_constraint(
                  &context->scratch, lhs, call, &builder)
            : match_atoms_builder(lhs, call, &builder);
    if (matched &&
        !bindings_has_loop(
            (Bindings *)bindings_builder_bindings(&builder))) {
        bindings_builder_take(&builder, bindings);
    } else {
        matched = false;
        bindings_builder_free(&builder);
    }
    if (matched && fresh_equation)
        *fresh_equation = fresh;
    return matched;
}

static bool petta_visiting_contains(
    const PettaSpecializerContext *context, SymbolId head) {
    for (size_t index = 0u;
         index < context->visiting_len; index++) {
        if (context->visiting[index] == head)
            return true;
    }
    return false;
}

static bool petta_visiting_push(
    PettaSpecializerContext *context, SymbolId head) {
    if (!petta_reserve(
            (void **)&context->visiting,
            &context->visiting_cap,
            context->visiting_len + 1u,
            sizeof(*context->visiting))) {
        context->capacity = true;
        return false;
    }
    context->visiting[context->visiting_len++] = head;
    return true;
}

static PettaSpecializationRecord *petta_find_record(
    Space *space, SymbolId source, SymbolId specialized) {
    uint64_t instance = space_instance_id(space);
    for (size_t index = 0u;
         index < g_petta_specializations.len; index++) {
        PettaSpecializationRecord *record =
            &g_petta_specializations.items[index];
        if (record->space == space &&
            record->space_instance == instance &&
            record->source == source &&
            record->specialized == specialized) {
            return record;
        }
    }
    return NULL;
}

static bool petta_record_set_selectors(
    PettaSpecializerContext *context,
    PettaSpecializationRecord *record,
    const PettaSpecializationBindings *candidates) {
    if (!context || !record || !candidates ||
        candidates->len == 0u ||
        candidates->len >
            SIZE_MAX / sizeof(*record->selectors)) {
        return false;
    }
    record->selectors = cetta_malloc(
        sizeof(*record->selectors) * candidates->len);
    memset(
        record->selectors, 0,
        sizeof(*record->selectors) * candidates->len);
    record->selector_len = candidates->len;
    for (size_t index = 0u;
         index < candidates->len; index++) {
        const PettaSpecializationBinding *candidate =
            &candidates->items[index];
        PettaSpecializationBinding *selector =
            &record->selectors[index];
        selector->variable = candidate->variable;
        selector->value = atom_deep_copy(
            context->persistent, candidate->value);
        selector->path_len = candidate->path_len;
        if (candidate->path_len > 0u) {
            if (candidate->path_len >
                SIZE_MAX / sizeof(*selector->path)) {
                context->capacity = true;
                return false;
            }
            selector->path = cetta_malloc(
                sizeof(*selector->path) *
                candidate->path_len);
            memcpy(
                selector->path, candidate->path,
                sizeof(*selector->path) *
                    candidate->path_len);
        }
        if (!selector->value) {
            context->capacity = true;
            return false;
        }
    }
    return true;
}

static PettaSpecializationRecord *
petta_find_record_for_call(
    PettaSpecializerContext *context,
    SymbolId source, Atom *call) {
    if (!context || source == SYMBOL_ID_NONE || !call ||
        !petta_specializer_route_cache_enabled()) {
        return NULL;
    }
    uint64_t instance = space_instance_id(context->space);
    for (size_t record_index = 0u;
         record_index < g_petta_specializations.len;
         record_index++) {
        PettaSpecializationRecord *record =
            &g_petta_specializations.items[record_index];
        if (record->space != context->space ||
            record->space_instance != instance ||
            record->source != source ||
            record->selector_len == 0u) {
            continue;
        }
        ArenaMark mark = arena_mark(&context->scratch);
        bool matches = true;
        for (size_t selector_index = 0u;
             selector_index < record->selector_len;
             selector_index++) {
            PettaSpecializationBinding *selector =
                &record->selectors[selector_index];
            Atom *actual = petta_atom_at_path(
                call, selector->path, selector->path_len);
            Atom *specializable = actual
                ? petta_specializable_value(
                      context, &context->scratch, actual)
                : NULL;
            if (!specializable ||
                !atom_eq(specializable, selector->value)) {
                matches = false;
                break;
            }
        }
        arena_reset(&context->scratch, mark);
        if (matches) {
            if (petta_specializer_trace_enabled()) {
                fprintf(
                    stderr,
                    "[petta-specializer] route-cache hit %s -> %s\n",
                    symbol_bytes(g_symbols, source),
                    symbol_bytes(
                        g_symbols, record->specialized));
            }
            return record;
        }
    }
    return NULL;
}

static bool petta_record_add(
    PettaSpecializationRecord *record, Atom *artifact,
    PettaSpecializerPatternNode *pattern,
    const PettaPlanNode *rhs_plan) {
    if (!record || !artifact)
        return false;
    if (!petta_reserve(
            (void **)&record->artifacts,
            &record->artifact_cap,
            record->artifact_len + 1u,
            sizeof(*record->artifacts))) {
        return false;
    }
    record->artifacts[record->artifact_len++] =
        (PettaSpecializationArtifact){
            .atom = artifact,
            .pattern = pattern,
            .rhs_plan = rhs_plan,
        };
    return true;
}

static PettaSpecializationRecord *petta_create_record(
    PettaSpecializerContext *context, SymbolId source,
    SymbolId specialized,
    const PettaSpecializationBindings *candidates) {
    if (!petta_reserve(
            (void **)&g_petta_specializations.items,
            &g_petta_specializations.cap,
            g_petta_specializations.len + 1u,
            sizeof(*g_petta_specializations.items))) {
        context->capacity = true;
        return NULL;
    }
    PettaSpecializationRecord *record =
        &g_petta_specializations.items[
            g_petta_specializations.len++];
    memset(record, 0, sizeof(*record));
    record->space = context->space;
    record->space_instance =
        space_instance_id(context->space);
    record->source = source;
    record->specialized = specialized;
    if (!petta_record_set_selectors(
            context, record, candidates)) {
        context->capacity = true;
        return NULL;
    }
    return record;
}

static bool petta_specializer_analyze_call(
    PettaSpecializerContext *context, Atom *call,
    bool materialize, PettaSpecializationAnalysis *analysis);

/*
 * A forwarded call is analyzed while its enclosing source equation is being
 * specialized, before that equation executes.  Its direct arguments must
 * therefore be represented as the values PeTTa's argument evaluator can
 * already determine at that boundary.  In particular, an exact nested call
 * denotes an unknown future result; treating its source head as list data
 * can manufacture a specialization that no evaluated call can satisfy.
 *
 * Under-applications are different: PeTTa deterministically turns them into
 * canonical partial values, so they remain legitimate specialization keys.
 * Unknown/data constructors retain their shape while any executable children
 * are projected by the same rule.
 */
static Atom *petta_specializer_ready_value(
    PettaSpecializerContext *context, Atom *atom,
    uint32_t depth) {
    if (!context || !atom || depth > 2048u) {
        if (context)
            context->capacity = true;
        return NULL;
    }
    if (atom->kind != ATOM_EXPR || atom->expr.len == 0u)
        return atom;
    /*
     * The open-cons carrier is a value produced by relational list
     * matching, not executable source syntax.  Its elements have already
     * crossed the argument-demand boundary.  Treat the shared spine as one
     * ready value: descending through it would both reconsider data as code
     * and make specialization proportional to every retained list suffix.
     */
    if (petta_semantics_is_open_cons_value(atom))
        return atom;

    Atom *partial_base = NULL;
    Atom *partial_arguments = NULL;
    if (petta_semantics_partial_view(
            atom, &partial_base, &partial_arguments)) {
        return atom;
    }

    Atom *head = atom->expr.elems[0];
    CettaExprLen supplied = atom->expr.len - 1u;
    if (head->kind != ATOM_SYMBOL) {
        return atom_var_with_id(
            &context->scratch, "__petta_specializer_result",
            fresh_var_id());
    }

    bool callable = petta_symbol_is_callable(
        context->space, &context->scratch, head->sym_id);
    PeTTaNamedArity arity = callable
        ? petta_semantics_named_arity(
              context->space, &context->scratch,
              head, supplied)
        : (PeTTaNamedArity){0};
    if (callable && (!arity.known || arity.exact ||
                     !arity.larger)) {
        return atom_var_with_id(
            &context->scratch, "__petta_specializer_result",
            fresh_var_id());
    }

    Atom **elements = arena_alloc(
        &context->scratch,
        sizeof(*elements) * (size_t)atom->expr.len);
    if (!elements) {
        context->capacity = true;
        return NULL;
    }
    elements[0] = head;
    for (CettaExprIndex index = 1u;
         index < atom->expr.len; index++) {
        elements[index] = petta_specializer_ready_value(
            context, atom->expr.elems[index], depth + 1u);
        if (!elements[index])
            return NULL;
    }

    if (callable && arity.known && !arity.exact &&
        arity.larger) {
        return petta_semantics_partial_value(
            &context->scratch, head,
            elements + 1u, supplied);
    }
    return atom_expr(
        &context->scratch, elements, atom->expr.len);
}

static Atom *petta_specializer_ready_call(
    PettaSpecializerContext *context, Atom *source_call,
    Atom *concrete_call,
    VarId forwarded_variable, bool *carries_ready) {
    if (carries_ready)
        *carries_ready = false;
    if (!context || !source_call || !concrete_call ||
        !carries_ready ||
        source_call->kind != ATOM_EXPR ||
        concrete_call->kind != ATOM_EXPR ||
        source_call->expr.len != concrete_call->expr.len ||
        concrete_call->expr.len == 0u) {
        return concrete_call;
    }
    Atom **elements = arena_alloc(
        &context->scratch,
        sizeof(*elements) * (size_t)concrete_call->expr.len);
    if (!elements) {
        context->capacity = true;
        return NULL;
    }
    elements[0] = concrete_call->expr.elems[0];
    for (CettaExprIndex index = 1u;
         index < concrete_call->expr.len; index++) {
        Atom *source_argument = source_call->expr.elems[index];
        Atom *concrete_argument =
            concrete_call->expr.elems[index];
        Atom *ready_argument = petta_specializer_ready_value(
            context, concrete_argument, 0u);
        if (!ready_argument)
            return NULL;
        elements[index] = ready_argument;
        if (petta_atom_contains_variable(
                source_argument, forwarded_variable) &&
            !atom_has_vars(ready_argument)) {
            *carries_ready = true;
        }
    }
    return atom_expr(
        &context->scratch, elements, concrete_call->expr.len);
}

static bool petta_analyze_forwarded_calls(
    PettaSpecializerContext *context, Atom *rhs,
    const Bindings *bindings, VarId variable,
    bool materialize, bool *saw_forward,
    bool *productive) {
    PettaAtomVector stack = {0};
    if (!petta_atom_vector_push(&stack, rhs)) {
        context->capacity = true;
        return false;
    }
    while (stack.len > 0u) {
        Atom *atom = stack.items[--stack.len];
        if (atom->kind != ATOM_EXPR)
            continue;
        if (atom->expr.len > 1u &&
            atom->expr.elems[0]->kind == ATOM_SYMBOL &&
            petta_symbol_is_callable(
                context->space, &context->scratch,
                atom->expr.elems[0]->sym_id)) {
            bool carries = false;
            for (CettaExprIndex index = 1u;
                 index < atom->expr.len; index++) {
                if (petta_atom_contains_variable(
                        atom->expr.elems[index], variable)) {
                    carries = true;
                    break;
                }
            }
            if (carries) {
                *saw_forward = true;
                Atom *concrete = bindings_apply_if_vars(
                    bindings, &context->scratch, atom);
                if (!concrete) {
                    context->capacity = true;
                    free(stack.items);
                    return false;
                }
                bool carries_ready = false;
                Atom *ready = petta_specializer_ready_call(
                    context, atom, concrete, variable,
                    &carries_ready);
                if (!ready) {
                    context->capacity = true;
                    free(stack.items);
                    return false;
                }
                SymbolId callee = atom_head_symbol_id(ready);
                if (carries_ready &&
                    callee != SYMBOL_ID_NONE &&
                    space_equations_may_match_known_head(
                        context->space, callee)) {
                    PettaSpecializationAnalysis child = {0};
                    if (!petta_specializer_analyze_call(
                            context, ready, materialize,
                            &child)) {
                        free(stack.items);
                        return false;
                    }
                    if (child.productive)
                        *productive = true;
                }
            }
        }
        for (CettaExprIndex index = 0u;
             index < atom->expr.len; index++) {
            if (!petta_atom_vector_push(
                    &stack, atom->expr.elems[index])) {
                context->capacity = true;
                free(stack.items);
                return false;
            }
        }
    }
    free(stack.items);
    return true;
}

static bool petta_rewrite_head(
    Arena *arena, Atom *equation, SymbolId specialized,
    Atom **out) {
    if (out)
        *out = NULL;
    if (!arena || !equation ||
        equation->kind != ATOM_EXPR ||
        equation->expr.len != 3u ||
        !atom_is_symbol_id(
            equation->expr.elems[0], g_builtin_syms.equals)) {
        return false;
    }
    Atom *lhs = equation->expr.elems[1];
    if (!lhs || lhs->kind != ATOM_EXPR ||
        lhs->expr.len == 0u ||
        !cetta_expr_len_mul_fits_size(
            lhs->expr.len, sizeof(Atom *))) {
        return false;
    }
    Atom **elements = arena_alloc(
        arena, sizeof(*elements) * (size_t)lhs->expr.len);
    elements[0] = atom_symbol_id(arena, specialized);
    if (lhs->expr.len > 1u) {
        memcpy(
            elements + 1u, lhs->expr.elems + 1u,
            sizeof(*elements) *
                (size_t)(lhs->expr.len - 1u));
    }
    Atom *specialized_lhs =
        atom_expr(arena, elements, lhs->expr.len);
    Atom *specialized_equation = atom_expr3(
        arena,
        atom_symbol_id(arena, g_builtin_syms.equals),
        specialized_lhs, equation->expr.elems[2]);
    if (out)
        *out = specialized_equation;
    return specialized_equation != NULL;
}

static bool petta_materialize_specialization(
    PettaSpecializerContext *context, SymbolId source,
    SymbolId specialized, const PettaAtomVector *equations,
    const PettaSpecializationBindings *candidates) {
    if (petta_find_record(
            context->space, source, specialized)) {
        return true;
    }
    PettaSpecializationRecord *record =
        petta_create_record(
            context, source, specialized, candidates);
    if (!record)
        return false;

    for (size_t index = 0u;
         index < equations->len; index++) {
        ArenaMark mark = arena_mark(&context->scratch);
        Atom *source_equation = atom_freshen_epoch(
            &context->scratch,
            equations->items[index],
            fresh_var_suffix());
        if (!source_equation) {
            context->capacity = true;
            return false;
        }

        Bindings selected;
        bindings_init(&selected);
        Atom *lhs = source_equation->expr.elems[1];
        for (size_t binding_index = 0u;
             binding_index < candidates->len;
             binding_index++) {
            const PettaSpecializationBinding *candidate =
                &candidates->items[binding_index];
            Atom *formal = petta_atom_at_path(
                lhs, candidate->path, candidate->path_len);
            if (!formal || formal->kind != ATOM_VAR)
                continue;
            if (!bindings_add_id(
                    &selected, formal->var_id,
                    formal->sym_id, candidate->value)) {
                context->capacity = true;
                bindings_free(&selected);
                return false;
            }
        }
        Atom *substituted = bindings_apply_if_vars(
            &selected, &context->scratch,
            source_equation);
        Atom *promoted = substituted
            ? atom_deep_copy(
                  context->persistent, substituted)
            : NULL;
        Atom *derived = NULL;
        bool built = promoted &&
            petta_rewrite_head(
                context->persistent, promoted,
                specialized, &derived);
        PettaSpecializerPatternNode *pattern =
            built && derived
            ? petta_pattern_build(
                  context,
                  source_equation->expr.elems[1],
                  derived->expr.elems[1],
                  &selected)
            : NULL;
        const PettaPlanNode *equation_plan =
            built && derived && context->program
                ? petta_program_plan_dynamic_add(
                      context->program, derived)
                : NULL;
        const PettaPlanNode *rhs_plan =
            petta_plan_child(equation_plan, 2u);
        bindings_free(&selected);
        if (!built || !derived || context->capacity ||
            (context->program && !rhs_plan) ||
            !petta_record_add(
                record, derived, pattern, rhs_plan)) {
            petta_pattern_free(pattern);
            context->capacity = true;
            return false;
        }
        if (!space_admit_atom(
                context->space, context->persistent,
                derived)) {
            record->artifact_len--;
            petta_pattern_free(pattern);
            context->capacity = true;
            return false;
        }
        if (petta_specializer_trace_enabled()) {
            fprintf(
                stderr,
                "[petta-specializer] structural pattern map=%u\n",
                pattern ? 1u : 0u);
        }
        petta_specializer_trace_atom("derived equation: ", derived);
        petta_specializer_trace_variables(derived);
        arena_reset(&context->scratch, mark);
    }

    Atom *source_atom =
        atom_symbol_id(&context->scratch, source);
    Atom **types = NULL;
    uint32_t type_count = space_get_declared_types(
        context->space, &context->scratch,
        source_atom, &types);
    for (uint32_t index = 0u; index < type_count; index++) {
        Atom *promoted_type = atom_deep_copy(
            context->persistent, types[index]);
        Atom *annotation = promoted_type
            ? atom_expr3(
            context->persistent,
            atom_symbol_id(
                context->persistent,
                g_builtin_syms.colon),
            atom_symbol_id(
                context->persistent, specialized),
            promoted_type)
            : NULL;
        if (!annotation ||
            !petta_record_add(
                record, annotation, NULL, NULL)) {
            free(types);
            context->capacity = true;
            return false;
        }
        if (!space_admit_atom(
                context->space, context->persistent,
                annotation)) {
            record->artifact_len--;
            free(types);
            context->capacity = true;
            return false;
        }
        petta_specializer_trace_atom("derived type: ", annotation);
    }
    free(types);
    record->productive = true;
    return true;
}

static bool petta_specializer_analyze_call(
    PettaSpecializerContext *context, Atom *call,
    bool materialize, PettaSpecializationAnalysis *analysis) {
    memset(analysis, 0, sizeof(*analysis));
    if (!context || !call ||
        call->kind != ATOM_EXPR ||
        call->expr.len == 0u ||
        call->expr.elems[0]->kind != ATOM_SYMBOL) {
        return true;
    }
    SymbolId source = call->expr.elems[0]->sym_id;
    petta_specializer_trace_atom("analyze call: ", call);
    if (!space_equations_may_match_known_head(
            context->space, source)) {
        return true;
    }
    PettaSpecializationRecord *routed =
        petta_find_record_for_call(context, source, call);
    if (routed) {
        analysis->eligible = true;
        analysis->productive = routed->productive;
        analysis->specialized = routed->specialized;
        return true;
    }
    if (petta_specializer_relevance_filter_enabled()) {
        if (petta_relevance_filter_is_bounded_out(
                context->space, source)) {
            analysis->relevance_bounded = true;
        } else {
            PettaRelevanceResult relevance =
                petta_call_may_supply_specializable_value(
                    context, call);
            if (relevance == PETTA_RELEVANCE_NO) {
                analysis->filtered = true;
                return true;
            }
            if (relevance == PETTA_RELEVANCE_NODE_BUDGET) {
                petta_relevance_filter_mark_bounded_out(
                    context->space, source);
                analysis->relevance_bounded = true;
            }
        }
    }
    if (petta_visiting_contains(context, source))
        return true;
    if (context->depth >= 128u ||
        !petta_visiting_push(context, source)) {
        context->capacity = true;
        return false;
    }
    context->depth++;

    PettaAtomVector equations = {0};
    PettaSpecializationBindings candidates = {0};
    Arena candidate_values;
    arena_init(&candidate_values);
    arena_set_runtime_kind(
        &candidate_values,
        CETTA_ARENA_RUNTIME_KIND_SCRATCH);
    arena_set_hashcons(&candidate_values, NULL);
    bool productive = false;
    bool ok = petta_collect_source_equations(
        context, source, &equations);
    for (size_t equation_index = 0u;
         ok && equation_index < equations.len;
         equation_index++) {
        ArenaMark mark = arena_mark(&context->scratch);
        Atom *fresh = NULL;
        Bindings bindings;
        if (!petta_match_source_call(
                context, equations.items[equation_index],
                call, &fresh, &bindings)) {
            arena_reset(&context->scratch, mark);
            continue;
        }
        Atom *rhs = fresh->expr.elems[2];
        for (uint32_t index = 0u;
             ok && index < bindings.len; index++) {
            Binding *binding = &bindings.entries[index];
            Atom *specializable = petta_specializable_value(
                context, &candidate_values, binding->val);
            if (!specializable) {
                continue;
            }
            bool direct =
                petta_variable_is_direct_callable(
                    rhs, binding->var_id);
            bool forwarded = false;
            bool downstream = false;
            ok = petta_analyze_forwarded_calls(
                context, rhs, &bindings,
                binding->var_id, materialize,
                &forwarded, &downstream);
            if (!ok)
                break;
            if (!direct && !forwarded)
                continue;
            CettaExprIndex *path = NULL;
            size_t path_len = 0u;
            if (!petta_find_variable_path(
                    fresh->expr.elems[1],
                    binding->var_id,
                    &path, &path_len)) {
                continue;
            }
            if (petta_specializer_trace_enabled()) {
                fprintf(
                    stderr,
                    "[petta-specializer] candidate direct=%u "
                    "forwarded=%u downstream=%u value=",
                    direct ? 1u : 0u,
                    forwarded ? 1u : 0u,
                    downstream ? 1u : 0u);
                atom_print(specializable, stderr);
                fputc('\n', stderr);
            }
            if (!petta_binding_vector_push_unique(
                    &candidates, binding->var_id,
                    specializable, path, path_len)) {
                context->capacity = true;
                ok = false;
                break;
            }
            productive = productive || direct || downstream;
        }
        bindings_free(&bindings);
        arena_reset(&context->scratch, mark);
    }

    SymbolId specialized = SYMBOL_ID_NONE;
    if (ok && candidates.len > 0u) {
        analysis->eligible = true;
        specialized = petta_specialized_symbol(
            context, source, &candidates);
        ok = specialized != SYMBOL_ID_NONE;
    }
    PettaSpecializationRecord *known =
        ok && specialized != SYMBOL_ID_NONE
            ? petta_find_record(
                  context->space, source, specialized)
            : NULL;
    if (known) {
        analysis->productive = known->productive;
        analysis->specialized = specialized;
    } else if (ok && candidates.len > 0u && productive) {
        if (materialize &&
            !petta_materialize_specialization(
                context, source, specialized,
                &equations, &candidates)) {
            ok = false;
        } else {
            analysis->productive = true;
            analysis->specialized = specialized;
        }
    } else if (ok && candidates.len > 0u && materialize) {
        if (petta_specializer_trace_enabled()) {
            fprintf(
                stderr,
                "[petta-specializer] not specialized %s/%" PRIu64 "\n",
                symbol_bytes(g_symbols, specialized),
                (uint64_t)call->expr.len);
        }
        /*
         * The SWI translator makes this decision once for the compiled
         * call path.  The native machine revisits the source occurrence on
         * recursion, so retain an invalidation-aware negative derivation
         * record and avoid both repeated analysis and repeated diagnostics.
         * Source mutation removes this record through the same dependency
         * closure as a productive specialization.
         */
        if (!petta_create_record(
                context, source, specialized,
                &candidates)) {
            ok = false;
        }
    }

    for (size_t index = 0u; index < candidates.len; index++)
        free(candidates.items[index].path);
    free(candidates.items);
    free(equations.items);
    arena_free(&candidate_values);
    context->depth--;
    context->visiting_len--;
    return ok;
}

PettaSpecializeResult petta_specializer_prepare_call(
    Space *space, PettaProgram *program,
    Arena *persistent_arena,
    Arena *result_arena, Atom *call, Atom **out_call) {
    if (out_call)
        *out_call = call;
    if (!space || !persistent_arena || !result_arena ||
        !call || !out_call) {
        return PETTA_SPECIALIZE_CAPACITY;
    }
    size_t record_checkpoint = g_petta_specializations.len;
    PettaSpecializerContext context = {
        .space = space,
        .program = program,
        .persistent = persistent_arena,
    };
    arena_init(&context.scratch);
    arena_set_runtime_kind(
        &context.scratch,
        CETTA_ARENA_RUNTIME_KIND_SCRATCH);
    arena_set_hashcons(&context.scratch, NULL);

    /*
     * Source matching is the authority for specialization relevance.  It
     * binds only variables that occur in a source pattern, so it avoids
     * recursively scanning unrelated persistent data carried by a call.
     * This is important for functional queues and other shared values whose
     * logical size grows while the source pattern touches only their root.
     */
    PettaSpecializationAnalysis analysis = {0};
    bool ok = petta_specializer_analyze_call(
        &context, call, true, &analysis);
    if (ok && analysis.productive) {
        if (!cetta_expr_len_mul_fits_size(
                call->expr.len, sizeof(Atom *))) {
            context.capacity = true;
            ok = false;
        } else {
            Atom **elements = arena_alloc(
                result_arena,
                sizeof(*elements) *
                    (size_t)call->expr.len);
            elements[0] = atom_symbol_id(
                result_arena, analysis.specialized);
            if (call->expr.len > 1u) {
                memcpy(
                    elements + 1u,
                    call->expr.elems + 1u,
                    sizeof(*elements) *
                        (size_t)(call->expr.len - 1u));
            }
            *out_call = atom_expr(
                result_arena, elements, call->expr.len);
            if (!*out_call) {
                context.capacity = true;
                ok = false;
            } else
                petta_specializer_trace_atom(
                    "rewritten call: ", *out_call);
        }
    }

    free(context.visiting);
    arena_free(&context.scratch);
    if (!ok || context.capacity) {
        while (g_petta_specializations.len >
               record_checkpoint) {
            petta_remove_record_at(
                g_petta_specializations.len - 1u);
        }
        return PETTA_SPECIALIZE_CAPACITY;
    }
    if (context.invalidated)
        return PETTA_SPECIALIZE_INVALIDATED;
    if (analysis.productive)
        return PETTA_SPECIALIZE_REWRITTEN;
    if (analysis.filtered)
        return PETTA_SPECIALIZE_UNCHANGED_FILTERED;
    return analysis.relevance_bounded
        ? PETTA_SPECIALIZE_UNCHANGED_RELEVANCE_BOUNDED
        : PETTA_SPECIALIZE_UNCHANGED;
}

static SymbolId petta_mutated_source_head(Atom *atom) {
    if (!atom || atom->kind != ATOM_EXPR ||
        atom->expr.len != 3u)
        return SYMBOL_ID_NONE;
    if (atom_is_symbol_id(
            atom->expr.elems[0],
            g_builtin_syms.equals)) {
        Atom *lhs = atom->expr.elems[1];
        return lhs && lhs->kind == ATOM_EXPR &&
               lhs->expr.len > 0u &&
               lhs->expr.elems[0]->kind == ATOM_SYMBOL
            ? lhs->expr.elems[0]->sym_id
            : SYMBOL_ID_NONE;
    }
    if (atom_is_symbol_id(
            atom->expr.elems[0],
            g_builtin_syms.colon) &&
        atom->expr.elems[1]->kind == ATOM_SYMBOL) {
        return atom->expr.elems[1]->sym_id;
    }
    return SYMBOL_ID_NONE;
}

static void petta_remove_record_at(size_t index) {
    PettaSpecializationRecord *record =
        &g_petta_specializations.items[index];
    if (record->space &&
        record->space_instance ==
            space_instance_id(record->space)) {
        for (size_t artifact = record->artifact_len;
             artifact > 0u; artifact--) {
            (void)space_remove(
                record->space,
                record->artifacts[artifact - 1u].atom);
        }
    }
    for (size_t artifact = 0u;
         artifact < record->artifact_len; artifact++) {
        petta_pattern_free(
            record->artifacts[artifact].pattern);
    }
    for (size_t selector = 0u;
         selector < record->selector_len; selector++) {
        free(record->selectors[selector].path);
    }
    free(record->selectors);
    free(record->artifacts);
    g_petta_specializations.items[index] =
        g_petta_specializations.items[
            g_petta_specializations.len - 1u];
    g_petta_specializations.len--;
}

void petta_specializer_note_mutation(
    Space *space, Atom *atom) {
    space_execution_analysis_note_mutation(space);
    SymbolId source = petta_mutated_source_head(atom);
    if (!space || source == SYMBOL_ID_NONE)
        return;
    /*
     * The bounded-out cache depends on which nested symbols are callable.
     * Any equation or type mutation can change that fact for calls of an
     * unrelated outer head, so clear the small derived cache wholesale.
     * Ordinary data additions never reach this point.
     */
    petta_relevance_filter_clear_bounded_cache();
    uint64_t instance = space_instance_id(space);
    PettaSymbolVector invalid_heads = {0};
    bool invalidate_all =
        !petta_symbol_vector_push_unique(
            &invalid_heads, source);

    for (size_t cursor = 0u;
         !invalidate_all && cursor < invalid_heads.len;
         cursor++) {
        SymbolId invalid_head = invalid_heads.items[cursor];
        for (size_t index = 0u;
             index < g_petta_specializations.len;
             index++) {
            PettaSpecializationRecord *record =
                &g_petta_specializations.items[index];
            if (record->space != space ||
                record->space_instance != instance ||
                record->invalidating ||
                (record->source != invalid_head &&
                 record->specialized != invalid_head)) {
                continue;
            }
            record->invalidating = true;
            if (!petta_symbol_vector_push_unique(
                    &invalid_heads,
                    record->specialized)) {
                invalidate_all = true;
                break;
            }
        }
    }
    free(invalid_heads.items);

    for (size_t index = 0u;
         index < g_petta_specializations.len;) {
        PettaSpecializationRecord *record =
            &g_petta_specializations.items[index];
        if (record->space == space &&
            record->space_instance == instance &&
            (invalidate_all || record->invalidating)) {
            petta_remove_record_at(index);
            continue;
        }
        record->invalidating = false;
        index++;
    }
}

const PettaSpecializerPatternNode *
petta_specializer_pattern_root(
    Space *space, Atom *equation) {
    if (!space || !equation ||
        equation->kind != ATOM_EXPR ||
        equation->expr.len != 3u ||
        !atom_is_symbol_id(
            equation->expr.elems[0],
            g_builtin_syms.equals)) {
        return NULL;
    }
    Atom *lhs = equation->expr.elems[1];
    SymbolId head =
        lhs && lhs->kind == ATOM_EXPR &&
        lhs->expr.len > 0u &&
        lhs->expr.elems[0]->kind == ATOM_SYMBOL
            ? lhs->expr.elems[0]->sym_id
            : SYMBOL_ID_NONE;
    if (head == SYMBOL_ID_NONE)
        return NULL;
    uint64_t instance = space_instance_id(space);
    for (size_t record_index = 0u;
         record_index < g_petta_specializations.len;
         record_index++) {
        PettaSpecializationRecord *record =
            &g_petta_specializations.items[record_index];
        if (record->space != space ||
            record->space_instance != instance ||
            record->specialized != head) {
            continue;
        }
        petta_specializer_trace_atom(
            "pattern query: ", equation);
        for (size_t artifact_index = 0u;
             artifact_index < record->artifact_len;
             artifact_index++) {
            PettaSpecializationArtifact *artifact =
                &record->artifacts[artifact_index];
            bool same =
                atom_alpha_eq(artifact->atom, equation);
            if (petta_specializer_trace_enabled()) {
                fprintf(
                    stderr,
                    "[petta-specializer] pattern lookup "
                    "artifact=%zu map=%u alpha=%u\n",
                    artifact_index,
                    artifact->pattern ? 1u : 0u,
                    same ? 1u : 0u);
                petta_specializer_trace_atom(
                    "pattern artifact: ", artifact->atom);
            }
            if (artifact->pattern && same) {
                return artifact->pattern;
            }
        }
    }
    return NULL;
}

const PettaPlanNode *
petta_specializer_equation_plan(
    Space *space, Atom *equation) {
    if (!space || !equation ||
        equation->kind != ATOM_EXPR ||
        equation->expr.len != 3u ||
        !atom_is_symbol_id(
            equation->expr.elems[0],
            g_builtin_syms.equals)) {
        return NULL;
    }
    Atom *lhs = equation->expr.elems[1];
    SymbolId head =
        lhs && lhs->kind == ATOM_EXPR &&
        lhs->expr.len > 0u &&
        lhs->expr.elems[0]->kind == ATOM_SYMBOL
            ? lhs->expr.elems[0]->sym_id
            : SYMBOL_ID_NONE;
    if (head == SYMBOL_ID_NONE)
        return NULL;
    uint64_t instance = space_instance_id(space);
    for (size_t record_index = 0u;
         record_index < g_petta_specializations.len;
         record_index++) {
        PettaSpecializationRecord *record =
            &g_petta_specializations.items[record_index];
        if (record->space != space ||
            record->space_instance != instance ||
            record->specialized != head) {
            continue;
        }
        for (size_t artifact_index = 0u;
             artifact_index < record->artifact_len;
             artifact_index++) {
            PettaSpecializationArtifact *artifact =
                &record->artifacts[artifact_index];
            if (artifact->rhs_plan &&
                atom_alpha_eq(artifact->atom, equation)) {
                return artifact->rhs_plan;
            }
        }
    }
    return NULL;
}

bool petta_specializer_pattern_is_structural(
    const PettaSpecializerPatternNode *node) {
    return node && node->structural;
}

const PettaSpecializerPatternNode *
petta_specializer_pattern_child(
    const PettaSpecializerPatternNode *node,
    CettaExprIndex index) {
    return node && node->children &&
           index < node->child_count
        ? &node->children[index]
        : NULL;
}

void petta_specializer_reset_thread(void) {
    for (size_t index = 0u;
         index < g_petta_specializations.len; index++) {
        for (size_t artifact = 0u;
             artifact <
                 g_petta_specializations.items[index].artifact_len;
             artifact++) {
            petta_pattern_free(
                g_petta_specializations.items[index]
                    .artifacts[artifact].pattern);
        }
        free(g_petta_specializations.items[index].artifacts);
    }
    free(g_petta_specializations.items);
    memset(
        &g_petta_specializations, 0,
        sizeof(g_petta_specializations));
    petta_relevance_filter_clear_bounded_cache();
}
