#include "term_canon.h"
#include "petta_specializer.h"

#include "grounded.h"
#include "match.h"
#include "petta_semantics.h"
#include "stats.h"
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
    bool negative;
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
    VarId *items;
    size_t len;
    size_t cap;
} PettaVarVector;

typedef enum {
    PETTA_RELATION_RELEVANCE_UNKNOWN = 0,
    PETTA_RELATION_RELEVANCE_IRRELEVANT,
    PETTA_RELATION_RELEVANCE_CERTAIN,
} PettaRelationRelevance;

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

typedef struct PettaSpecializerSemanticCache
    PettaSpecializerSemanticCache;

typedef struct {
    Space *space;
    PettaProgram *program;
    Arena *persistent;
    Arena scratch;
    PettaSpecializerSemanticCache *semantic_cache;
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
    bool relation_filtered;
    bool relevance_bounded;
    SymbolId specialized;
} PettaSpecializationAnalysis;

static _Thread_local PettaSpecializationRecords
    g_petta_specializations = {0};

enum {
    PETTA_CALLABLE_CACHE_SLOTS = 256,
    PETTA_NAMED_ARITY_CACHE_SLOTS = 256,
    PETTA_RELATION_RELEVANCE_CACHE_SLOTS = 256,
    PETTA_QUERY_FOREST_CACHE_SLOTS = 1024,
    PETTA_QUERY_FOREST_ARITY_CAP = 8,
};

enum {
    PETTA_SYMBOL_CALLABLE = 1u,
    PETTA_SYMBOL_PARTIAL_CONSTRUCTOR = 2u,
};

typedef struct {
    uint64_t generation;
    SymbolId symbol;
    uint8_t classification;
    bool used;
} PettaCallableCacheSlot;

typedef struct {
    uint64_t generation;
    SymbolId symbol;
    CettaExprLen supplied;
    PeTTaNamedArity arity;
    bool used;
} PettaNamedArityCacheSlot;

struct PettaSpecializerSemanticCache {
    Space *space;
    uint64_t space_instance;
    SpaceProgramToken program;
    const SymbolTable *symbols;
    uint64_t symbol_table_instance;
    bool mixed_index;
    /* Invalidation stamps the cache rather than clearing it: a slot is live
     * only when its generation equals the cache generation, so a key change
     * costs one increment instead of zeroing every slot. */
    uint64_t generation;
    PettaCallableCacheSlot callable[PETTA_CALLABLE_CACHE_SLOTS];
    PettaNamedArityCacheSlot
        named_arity[PETTA_NAMED_ARITY_CACHE_SLOTS];
};

/* Relevance reads equations and callable declarations only, so the program
 * token, not every data mutation, qualifies a stored fact. */
typedef struct {
    SpaceProgramToken program;
    uint64_t symbol_table_instance;
    SymbolId source;
    PettaRelationRelevance relevance;
    bool used;
} PettaRelationRelevanceCacheSlot;

static _Thread_local PettaRelationRelevanceCacheSlot
    g_petta_relation_relevance_cache[
        PETTA_RELATION_RELEVANCE_CACHE_SLOTS];

typedef struct {
    Space *space;
    uint64_t space_instance;
    uint64_t environment;
    uint64_t argument_shape;
    SymbolId source;
    CettaExprLen arity;
    uint8_t result;
    bool used;
} PettaQueryForestCacheSlot;

static _Thread_local PettaQueryForestCacheSlot
    g_petta_query_forest_cache[PETTA_QUERY_FOREST_CACHE_SLOTS];
static _Thread_local PettaSpecializerSemanticCache
    g_petta_semantic_cache;

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
    static _Thread_local int enabled = -1;
    if (enabled < 0) {
        const char *value =
            getenv("CETTA_PETTA_SPECIALIZER_ROUTE_CACHE");
        enabled = !value ||
                  (strcmp(value, "0") != 0 &&
                   strcmp(value, "false") != 0 &&
                   strcmp(value, "off") != 0);
    }
    return enabled != 0;
}

static bool petta_specializer_semantic_cache_mixed_index_enabled(void) {
    static _Thread_local int enabled = -1;
    if (enabled < 0) {
        const char *value = getenv(
            "CETTA_PETTA_SPECIALIZER_SEMANTIC_CACHE_LEGACY_INDEX");
        enabled = (!value || value[0] == '\0' ||
                   strcmp(value, "0") == 0 ||
                   strcmp(value, "false") == 0 ||
                   strcmp(value, "off") == 0) ? 1 : 0;
    }
    return enabled != 0;
}

static bool petta_specializer_relevance_filter_enabled(void) {
    static _Thread_local int enabled = -1;
    if (enabled < 0) {
        const char *value = getenv(
            "CETTA_PETTA_SPECIALIZER_RELEVANCE_FILTER");
        enabled = !value ||
                  (strcmp(value, "0") != 0 &&
                   strcmp(value, "false") != 0 &&
                   strcmp(value, "off") != 0);
    }
    return enabled == 1;
}

static bool petta_specializer_relation_prefilter_enabled(void) {
    static _Thread_local int enabled = -1;
    if (enabled < 0) {
        const char *reference = getenv(
            "CETTA_PETTA_SPECIALIZER_RELATION_PREFILTER_REFERENCE");
        enabled = !reference || strcmp(reference, "1") != 0;
    }
    return enabled != 0;
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

static bool petta_var_vector_contains(
    const PettaVarVector *vector, VarId variable) {
    if (!vector || variable == VAR_ID_NONE)
        return false;
    for (size_t index = 0u; index < vector->len; index++) {
        if (vector->items[index] == variable)
            return true;
    }
    return false;
}

static bool petta_var_vector_push_unique(
    PettaVarVector *vector, VarId variable) {
    if (!vector || variable == VAR_ID_NONE)
        return false;
    if (petta_var_vector_contains(vector, variable))
        return true;
    if (!petta_reserve(
            (void **)&vector->items, &vector->cap,
            vector->len + 1u, sizeof(*vector->items))) {
        return false;
    }
    vector->items[vector->len++] = variable;
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
            bindings_lookup_value_id(
                selected,
                item.source->expr.elems[0]->var_id).skeleton) {
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

static PettaSpecializerSemanticCache *
petta_semantic_cache_prepare(
    Space *space) {
    if (!space || !g_symbols)
        return NULL;
    uint64_t space_instance = space_instance_id(space);
    SpaceProgramToken program = space_program_token(space);
    uint64_t symbol_table_instance =
        symbol_table_instance_id(g_symbols);
    if (g_petta_semantic_cache.space != space ||
        g_petta_semantic_cache.space_instance != space_instance ||
        !space_program_token_eq(g_petta_semantic_cache.program, program) ||
        g_petta_semantic_cache.symbols != g_symbols ||
        g_petta_semantic_cache.symbol_table_instance !=
            symbol_table_instance) {
        g_petta_semantic_cache.generation++;
        g_petta_semantic_cache.space = space;
        g_petta_semantic_cache.space_instance = space_instance;
        g_petta_semantic_cache.program = program;
        g_petta_semantic_cache.symbols = g_symbols;
        g_petta_semantic_cache.symbol_table_instance =
            symbol_table_instance;
        g_petta_semantic_cache.mixed_index =
            petta_specializer_semantic_cache_mixed_index_enabled();
    }
    return &g_petta_semantic_cache;
}

static uint8_t petta_symbol_classification_uncached(
    Space *space, Arena *scratch, SymbolId symbol) {
    if (!space || !scratch || symbol == SYMBOL_ID_NONE)
        return 0u;
    Atom *subject = atom_symbol_id(scratch, symbol);
    uint8_t classification = petta_semantics_partial_head(subject)
        ? PETTA_SYMBOL_PARTIAL_CONSTRUCTOR : 0u;
    if (space_equations_may_match_known_head(space, symbol) ||
        is_grounded_op(symbol) ||
        petta_semantics_form(symbol) != PETTA_FORM_NONE) {
        return classification | PETTA_SYMBOL_CALLABLE;
    }
    CettaExprLen intrinsic = 0u;
    if (petta_semantics_intrinsic_partial_arity(
            symbol, &intrinsic)) {
        return classification | PETTA_SYMBOL_CALLABLE;
    }
    Atom **types = NULL;
    cetta_runtime_stats_inc(
        CETTA_RUNTIME_COUNTER_DECLARED_TYPE_SPECIALIZER_CALLABLE);
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
    return classification | (callable ? PETTA_SYMBOL_CALLABLE : 0u);
}

/* Callability is a property of one admitted space state and symbol table,
 * not of an individual call.  Keep the ordinary evaluator authoritative and
 * memoize only this exact classification.  The process-wide mutation epoch
 * also covers an overlay whose base changes without changing the overlay's
 * own revision. */
static uint8_t petta_symbol_classification(
    PettaSpecializerContext *context, SymbolId symbol) {
    if (!context || !context->space ||
        symbol == SYMBOL_ID_NONE) {
        return 0u;
    }
    PettaSpecializerSemanticCache *cache =
        context->semantic_cache;
    if (!cache) {
        return petta_symbol_classification_uncached(
            context->space, &context->scratch, symbol);
    }
    uint32_t mixed = symbol * UINT32_C(2654435761);
    mixed ^= mixed >> 16u;
    size_t index = cache->mixed_index
        ? (size_t)mixed & (PETTA_CALLABLE_CACHE_SLOTS - 1u)
        : (size_t)symbol & (PETTA_CALLABLE_CACHE_SLOTS - 1u);
    PettaCallableCacheSlot *slot = &cache->callable[
        index];
    if (slot->used && slot->generation == cache->generation &&
        slot->symbol == symbol) {
        cetta_runtime_stats_inc(
            CETTA_RUNTIME_COUNTER_PETTA_SPECIALIZER_CALLABLE_CACHE_HIT);
        return slot->classification;
    }
    cetta_runtime_stats_inc(
        CETTA_RUNTIME_COUNTER_PETTA_SPECIALIZER_CALLABLE_CACHE_MISS);
    *slot = (PettaCallableCacheSlot){
        .generation = cache->generation,
        .symbol = symbol,
        .classification = petta_symbol_classification_uncached(
            context->space, &context->scratch, symbol),
        .used = true,
    };
    return slot->classification;
}

static bool petta_symbol_is_callable(
    PettaSpecializerContext *context, SymbolId symbol) {
    return (petta_symbol_classification(context, symbol) &
        PETTA_SYMBOL_CALLABLE) != 0u;
}

static PeTTaNamedArity petta_specializer_named_arity(
    PettaSpecializerContext *context, Atom *head,
    CettaExprLen supplied) {
    if (!context || !context->space || !head ||
        head->kind != ATOM_SYMBOL) {
        return (PeTTaNamedArity){0};
    }
    PettaSpecializerSemanticCache *cache =
        context->semantic_cache;
    if (!cache) {
        return petta_semantics_named_arity(
            context->space, &context->scratch, head, supplied);
    }
    SymbolId symbol = head->sym_id;
    uint32_t mixed = symbol ^ (symbol >> 8u) ^
        (symbol >> 16u) ^ (symbol >> 24u);
    mixed = mixed * 33u + (uint32_t)supplied;
    size_t index = cache->mixed_index
        ? (size_t)mixed & (PETTA_NAMED_ARITY_CACHE_SLOTS - 1u)
        : ((size_t)symbol * 33u + (size_t)supplied) &
            (PETTA_NAMED_ARITY_CACHE_SLOTS - 1u);
    PettaNamedArityCacheSlot *slot =
        &cache->named_arity[index];
    if (slot->used && slot->generation == cache->generation &&
        slot->symbol == symbol && slot->supplied == supplied) {
        cetta_runtime_stats_inc(
            CETTA_RUNTIME_COUNTER_PETTA_SPECIALIZER_ARITY_CACHE_HIT);
        return slot->arity;
    }
    cetta_runtime_stats_inc(
        CETTA_RUNTIME_COUNTER_PETTA_SPECIALIZER_ARITY_CACHE_MISS);
    *slot = (PettaNamedArityCacheSlot){
        .generation = cache->generation,
        .symbol = symbol,
        .supplied = supplied,
        .arity = petta_semantics_named_arity(
            context->space, &context->scratch, head, supplied),
        .used = true,
    };
    return slot->arity;
}

static size_t petta_relation_relevance_cache_index(
    SpaceProgramToken program, SymbolId source) {
    uint64_t mixed =
        ((uint64_t)(uintptr_t)program.space >> 4u) ^
        (program.instance_id * UINT64_C(0x9e3779b97f4a7c15)) ^
        (program.equation_revision * UINT64_C(0xbf58476d1ce4e5b9)) ^
        (program.declaration_revision * UINT64_C(0xd6e8feb86659fd93)) ^
        (program.base_dependency_epoch * UINT64_C(0xa0761d6478bd642f)) ^
        ((uint64_t)source * UINT64_C(0x94d049bb133111eb));
    mixed ^= mixed >> 30u;
    mixed *= UINT64_C(0xbf58476d1ce4e5b9);
    mixed ^= mixed >> 27u;
    return (size_t)mixed &
           (PETTA_RELATION_RELEVANCE_CACHE_SLOTS - 1u);
}

static bool petta_relation_relevance_cache_lookup(
    SpaceProgramToken program, SymbolId source,
    PettaRelationRelevance *relevance) {
    if (relevance)
        *relevance = PETTA_RELATION_RELEVANCE_UNKNOWN;
    if (!program.space || source == SYMBOL_ID_NONE || !relevance ||
        !g_symbols)
        return false;
    PettaRelationRelevanceCacheSlot *slot =
        &g_petta_relation_relevance_cache[
            petta_relation_relevance_cache_index(program, source)];
    if (!slot->used ||
        !space_program_token_eq(slot->program, program) ||
        slot->symbol_table_instance !=
            symbol_table_instance_id(g_symbols) ||
        slot->source != source) {
        return false;
    }
    *relevance = slot->relevance;
    return true;
}

static void petta_relation_relevance_cache_store(
    SpaceProgramToken program, SymbolId source,
    PettaRelationRelevance relevance) {
    if (!program.space || source == SYMBOL_ID_NONE || !g_symbols ||
        (relevance != PETTA_RELATION_RELEVANCE_IRRELEVANT &&
         relevance != PETTA_RELATION_RELEVANCE_CERTAIN)) {
        return;
    }
    PettaRelationRelevanceCacheSlot *slot =
        &g_petta_relation_relevance_cache[
            petta_relation_relevance_cache_index(program, source)];
    *slot = (PettaRelationRelevanceCacheSlot){
        .program = program,
        .symbol_table_instance = symbol_table_instance_id(g_symbols),
        .source = source,
        .relevance = relevance,
        .used = true,
    };
}

static void petta_relation_relevance_cache_clear(void) {
    memset(g_petta_relation_relevance_cache, 0,
           sizeof(g_petta_relation_relevance_cache));
    memset(g_petta_query_forest_cache, 0,
           sizeof(g_petta_query_forest_cache));
}

/*
 * Argument-forest fact (Maranget 2008: constructor shape with holes).
 * Variables occupy one hole cell; symbols contribute only callability
 * and partial-constructor bits, never interned-closed identity of unique
 * copies.  An unknown cursor scope cannot certify absence of a supplier.
 * The live environment is the supplier class of each bound inventory
 * slot (root kind and head classification), or the ordinary binding-image
 * length when no dense inventory is present.  Bound-value pointers,
 * growth counters, and ordinary data-addition revisions are not the key:
 * isomorphic implication forests must hit across appends.  Equation and
 * type mutation still discard the table.  Eval heap and persistent intern
 * stay separate (Ershov; Filliâtre and Conchon).
 */
static uint64_t petta_forest_mix(uint64_t mixed, uint64_t value) {
    mixed ^= value * UINT64_C(0x9e3779b97f4a7c15);
    mixed ^= mixed >> 30u;
    mixed *= UINT64_C(0xbf58476d1ce4e5b9);
    mixed ^= mixed >> 27u;
    return mixed;
}

static uint64_t petta_forest_supplier_class(
        PettaSpecializerContext *context, const Atom *atom) {
    if (!atom)
        return UINT64_C(1);
    switch (atom->kind) {
    case ATOM_VAR:
        return UINT64_C(0x11);
    case ATOM_SYMBOL:
        return UINT64_C(0x22) |
            ((uint64_t)petta_symbol_classification(
                 context, atom->sym_id) << 8);
    case ATOM_GROUNDED:
        return UINT64_C(0x33) | ((uint64_t)atom->ground.gkind << 8);
    case ATOM_EXPR: {
        if (atom->expr.len == 0u)
            return UINT64_C(0x44);
        Atom *head = atom->expr.elems ? atom->expr.elems[0] : NULL;
        uint8_t classification = 0u;
        if (head && head->kind == ATOM_SYMBOL)
            classification = petta_symbol_classification(
                context, head->sym_id);
        else if (head && head->kind == ATOM_VAR)
            classification = 0xffu;
        uint64_t mixed = UINT64_C(0x55) |
            ((uint64_t)classification << 8) |
            ((uint64_t)atom->expr.len << 16);
        if (atom->expr.len == 3u &&
            (classification & PETTA_SYMBOL_PARTIAL_CONSTRUCTOR) &&
            atom->expr.elems && atom->expr.elems[2] &&
            atom->expr.elems[2]->kind == ATOM_EXPR)
            mixed |= UINT64_C(1) << 24;
        return mixed;
    }
    }
    return UINT64_C(0x66);
}

static bool petta_query_forest_environment(
        PettaSpecializerContext *context,
        CettaGsltTermCursorObserverV1 observer, uint64_t *environment) {
    *environment = 0u;
    if (observer.resolve != bindings_resolve_term_cursor_v1 ||
        !observer.context)
        return true;
    const BindingsTermCursorContextV1 *cursor = observer.context;
    if (cursor->frame) {
        const BindingsActivationView *frame = cursor->frame;
        BindingsActivationRead read;
        if (!bindings_activation_view_borrow(cursor->bindings, frame, &read))
            return false;
        uint64_t mixed = (uint64_t)frame->len + 1u;
        for (uint32_t slot = 0u; slot < frame->len; slot++) {
            BindingValue value = binding_value_from_atom(NULL);
            bool present = false;
            if (!bindings_activation_read_slot(
                    &read, slot, &value, &present))
                return false;
            if (!present)
                value = binding_value_from_atom(NULL);
            /* The shallow stamp does not describe dependencies in another
             * lexical context. Let the ordinary relevance observer decide. */
            if (binding_value_is_contextual(value) &&
                atom_has_vars(value.skeleton))
                return false;
            mixed = petta_forest_mix(
                mixed, petta_forest_supplier_class(context, value.skeleton) +
                    (uint64_t)slot);
        }
        *environment = mixed;
        return true;
    }
    if (cursor->bindings) {
        size_t count = 0u;
        if (!bindings_current_binding_count(cursor->bindings, &count))
            return false;
        *environment = count;
    }
    return true;
}

static bool petta_query_forest_constructor_shape(
        PettaSpecializerContext *context,
        Atom *const *arguments, const CettaGsltTermCursorV1 *views,
        CettaExprLen arity, uint64_t *shape_out) {
    enum {
        PETTA_FOREST_SHAPE_STACK = 32,
        PETTA_FOREST_SHAPE_NODES = 32,
        PETTA_FOREST_SHAPE_DEPTH = 8,
    };
    typedef struct {
        const Atom *atom;
        uint32_t depth;
    } PettaForestShapeItem;
    if (!shape_out || (arity > 0u && !arguments && !views))
        return false;
    PettaForestShapeItem stack[PETTA_FOREST_SHAPE_STACK];
    size_t length = 0u;
    size_t visited = 0u;
    uint64_t mixed = (uint64_t)arity * UINT64_C(0x27d4eb2f165667c5);
    if ((size_t)arity > PETTA_FOREST_SHAPE_STACK)
        return false;
    for (CettaExprIndex index = 0u; index < arity; index++) {
        const Atom *atom = arguments ? arguments[index]
            : (views ? views[index].source : NULL);
        if (!atom)
            return false;
        stack[length++] = (PettaForestShapeItem){atom, 0u};
    }
    while (length > 0u) {
        if (visited++ >= PETTA_FOREST_SHAPE_NODES)
            return false;
        PettaForestShapeItem item = stack[--length];
        if (item.depth > PETTA_FOREST_SHAPE_DEPTH || !item.atom)
            return false;
        mixed = petta_forest_mix(
            mixed, petta_forest_supplier_class(context, item.atom));
        if (item.atom->kind != ATOM_EXPR || item.atom->expr.len == 0u)
            continue;
        if (length + (size_t)item.atom->expr.len >
                PETTA_FOREST_SHAPE_STACK)
            return false;
        for (CettaExprIndex index = 0u;
             index < item.atom->expr.len; index++) {
            const Atom *child = item.atom->expr.elems
                ? item.atom->expr.elems[index] : NULL;
            if (!child)
                return false;
            mixed = petta_forest_mix(mixed, (uint64_t)index + 1u);
            stack[length++] = (PettaForestShapeItem){
                child, item.depth + 1u};
        }
    }
    *shape_out = mixed ? mixed : UINT64_C(1);
    return true;
}

static bool petta_atom_published_interned_closed(const Atom *atom) {
    const uint32_t required =
        ATOM_FLAG_HASHCONS_ELIGIBLE | ATOM_FLAG_ARENA_CLOSED;
    return atom && atom->arena_id == 0u &&
        (atom->flags & required) == required &&
        !atom_has_vars(atom);
}

static bool petta_query_forest_closed_leaf_shape(
        Atom *const *arguments, const CettaGsltTermCursorV1 *views,
        CettaExprLen arity, uint64_t *shape_out) {
    if (!shape_out || (arity > 0u && !arguments && !views))
        return false;
    uint64_t mixed = (uint64_t)arity + 1u;
    for (CettaExprIndex index = 0u; index < arity; index++) {
        const Atom *atom = arguments ? arguments[index]
            : (views ? views[index].source : NULL);
        if (!atom)
            return false;
        if (petta_atom_published_interned_closed(atom)) {
            mixed = petta_forest_mix(mixed, (uint64_t)(uintptr_t)atom);
            continue;
        }
        /* Arena-local grounded leaves and symbols have no holes and
         * cannot supply a nested callable.  Mixing kind, not unique
         * copy identity, keeps this path a table read. */
        if (atom->kind == ATOM_GROUNDED) {
            mixed = petta_forest_mix(
                mixed, UINT64_C(0x33) |
                    ((uint64_t)atom->ground.gkind << 8));
            continue;
        }
        if (atom->kind == ATOM_SYMBOL) {
            mixed = petta_forest_mix(
                mixed, UINT64_C(0x22) | ((uint64_t)atom->sym_id << 8));
            continue;
        }
        return false;
    }
    *shape_out = mixed;
    return true;
}

static bool petta_query_forest_key(
        PettaSpecializerContext *context,
        Atom *const *arguments, const CettaGsltTermCursorV1 *views,
        CettaExprLen arity, CettaGsltTermCursorObserverV1 observer,
        uint64_t *environment, uint64_t *argument_shape) {
    if (arity > PETTA_QUERY_FOREST_ARITY_CAP || !environment ||
        !argument_shape)
        return false;
    if (views) {
        const void *frame = NULL;
        if (observer.resolve == bindings_resolve_term_cursor_v1 &&
            observer.context) {
            const BindingsTermCursorContextV1 *cursor = observer.context;
            frame = cursor->frame;
        }
        for (CettaExprIndex index = 0u; index < arity; index++) {
            if (views[index].scope && views[index].scope != frame)
                return false;
        }
    }
    /* Closed leaves have no holes.  Published interned identity is the
     * shape when present (Ershov; Filliâtre and Conchon); grounded
     * leaves share by kind.  The live environment is not mixed in. */
    if (petta_query_forest_closed_leaf_shape(
            arguments, views, arity, argument_shape)) {
        *environment = 0u;
        return true;
    }
    if (!petta_query_forest_constructor_shape(
            context, arguments, views, arity, argument_shape))
        return false;
    return petta_query_forest_environment(context, observer, environment);
}

static size_t petta_query_forest_cache_index(
        Space *space, uint64_t instance,
        uint64_t environment, uint64_t argument_shape, SymbolId source,
        CettaExprLen arity) {
    uint64_t mixed =
        ((uint64_t)(uintptr_t)space >> 4u) ^
        (instance * UINT64_C(0x9e3779b97f4a7c15)) ^
        (environment * UINT64_C(0x94d049bb133111eb)) ^
        (argument_shape * UINT64_C(0x85ebca77c2b2ae63)) ^
        ((uint64_t)source * UINT64_C(0x27d4eb2f165667c5)) ^
        ((uint64_t)arity * UINT64_C(0x165667b19e3779f9));
    mixed ^= mixed >> 30u;
    mixed *= UINT64_C(0xbf58476d1ce4e5b9);
    mixed ^= mixed >> 27u;
    return (size_t)mixed & (PETTA_QUERY_FOREST_CACHE_SLOTS - 1u);
}

static bool petta_query_forest_cache_lookup(
        Space *space, uint64_t instance,
        uint64_t environment, uint64_t argument_shape, SymbolId source,
        CettaExprLen arity, uint8_t *result) {
    if (!space || !result)
        return false;
    PettaQueryForestCacheSlot *slot =
        &g_petta_query_forest_cache[
            petta_query_forest_cache_index(
                space, instance, environment, argument_shape,
                source, arity)];
    if (!slot->used || slot->space != space ||
        slot->space_instance != instance ||
        slot->environment != environment ||
        slot->argument_shape != argument_shape ||
        slot->source != source ||
        slot->arity != arity)
        return false;
    *result = slot->result;
    return true;
}

static void petta_query_forest_cache_store(
        Space *space, uint64_t instance,
        uint64_t environment, uint64_t argument_shape, SymbolId source,
        CettaExprLen arity, uint8_t result) {
    if (!space)
        return;
    PettaQueryForestCacheSlot *slot =
        &g_petta_query_forest_cache[
            petta_query_forest_cache_index(
                space, instance, environment, argument_shape,
                source, arity)];
    *slot = (PettaQueryForestCacheSlot){
        .space = space,
        .space_instance = instance,
        .environment = environment,
        .argument_shape = argument_shape,
        .source = source,
        .arity = arity,
        .result = result,
        .used = true,
    };
}

static bool petta_collect_pattern_variables(
    Atom *root, PettaVarVector *variables) {
    PettaAtomVector stack = {0};
    if (!root || !variables ||
        !petta_atom_vector_push(&stack, root)) {
        return false;
    }
    bool ok = true;
    while (stack.len > 0u && ok) {
        Atom *atom = stack.items[--stack.len];
        if (atom->kind == ATOM_VAR) {
            ok = petta_var_vector_push_unique(
                variables, atom->var_id);
            continue;
        }
        if (atom->kind != ATOM_EXPR)
            continue;
        for (CettaExprIndex index = 0u;
             ok && index < atom->expr.len; index++) {
            ok = petta_atom_vector_push(
                &stack, atom->expr.elems[index]);
        }
    }
    free(stack.items);
    return ok;
}

static bool petta_atom_contains_any_variable(
    Atom *root, const PettaVarVector *variables, bool *contains) {
    *contains = false;
    PettaAtomVector stack = {0};
    if (!petta_atom_vector_push(&stack, root))
        return false;
    while (stack.len > 0u) {
        Atom *atom = stack.items[--stack.len];
        if (atom->kind == ATOM_VAR) {
            if (petta_var_vector_contains(variables, atom->var_id)) {
                *contains = true;
                break;
            }
            continue;
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
    return true;
}

/*
 * One equation body, read the way the derivation reads it.  A candidate
 * becomes productive only through a pattern variable applied as a dynamic
 * head (`direct`), or through a pattern variable carried into an argument
 * of a callable form whose head has source equations, when that callee is
 * itself productive (`forwards`).  Carrying a variable into a callable
 * without source equations, such as a grounded operation, can mark a
 * candidate but never makes it productive: the derivation analyzes children
 * only through `space_equations_may_match_known_head`.
 */
static bool petta_relation_body_facts(
    PettaSpecializerContext *context, Atom *root,
    const PettaVarVector *pattern_variables,
    bool *direct, PettaSymbolVector *forwards) {
    *direct = false;
    PettaAtomVector stack = {0};
    if (!petta_atom_vector_push(&stack, root))
        return false;
    bool ok = true;
    while (ok && stack.len > 0u) {
        Atom *atom = stack.items[--stack.len];
        if (!atom) {
            ok = false;
            break;
        }
        if (atom->kind != ATOM_EXPR || atom->expr.len == 0u)
            continue;
        Atom *head = atom->expr.elems[0];
        if (head && head->kind == ATOM_VAR &&
            petta_var_vector_contains(
                pattern_variables, head->var_id)) {
            *direct = true;
            break;
        }
        if (atom->expr.len > 1u && head &&
            head->kind == ATOM_SYMBOL &&
            petta_symbol_is_callable(context, head->sym_id) &&
            space_equations_may_match_known_head(
                context->space, head->sym_id)) {
            bool carries = false;
            for (CettaExprIndex index = 1u;
                 ok && !carries && index < atom->expr.len; index++) {
                ok = petta_atom_contains_any_variable(
                    atom->expr.elems[index], pattern_variables,
                    &carries);
            }
            if (ok && carries)
                ok = petta_symbol_vector_push_unique(
                    forwards, head->sym_id);
        }
        for (CettaExprIndex index = 0u;
             ok && index < atom->expr.len; index++) {
            ok = petta_atom_vector_push(
                &stack, atom->expr.elems[index]);
        }
    }
    free(stack.items);
    return ok;
}

typedef struct {
    SymbolId symbol;
    bool direct;
    bool unknown;
    bool cached;
    bool possible;
    bool certain;
    PettaSymbolVector forwards;
} PettaRelationRelevanceNode;

enum { PETTA_RELATION_RELEVANCE_NODE_LIMIT = 4096 };

static size_t petta_relation_relevance_node_find(
    const PettaRelationRelevanceNode *nodes, size_t len,
    SymbolId symbol) {
    for (size_t index = 0u; index < len; index++) {
        if (nodes[index].symbol == symbol)
            return index;
    }
    return SIZE_MAX;
}

/* Read every source equation of one relation into its node. */
static bool petta_relation_relevance_node_scan(
    PettaSpecializerContext *context,
    PettaRelationRelevanceNode *node, bool *saw_equation) {
    *saw_equation = false;
    SpaceEquationCursor cursor;
    if (!space_equation_cursor_init(
            context->space, node->symbol, &cursor)) {
        node->unknown = true;
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
        if (!space_equation_occurrence_resolve(id, &occurrence)) {
            context->invalidated = true;
            return false;
        }
        if (!occurrence.lhs || !occurrence.rhs ||
            occurrence.lhs->kind != ATOM_EXPR ||
            occurrence.lhs->expr.len == 0u ||
            !atom_is_symbol_id(
                occurrence.lhs->expr.elems[0], node->symbol)) {
            continue;
        }
        *saw_equation = true;
        PettaVarVector variables = {0};
        bool direct = false;
        bool scanned =
            petta_collect_pattern_variables(
                occurrence.lhs, &variables) &&
            petta_relation_body_facts(
                context, occurrence.rhs, &variables,
                &direct, &node->forwards);
        free(variables.items);
        if (!scanned) {
            node->unknown = true;
            return true;
        }
        if (direct) {
            node->direct = true;
            return true;
        }
    }
}

/*
 * A relation is relevant to specialization when some ready call could make
 * a derivation productive.  The derivation is productive exactly when a
 * chain of forwards through equation-backed callees ends at a dynamic-head
 * application; recursion back into a relation already under analysis adds
 * nothing.  So relevance is the least fixed point of
 *
 *     relevant(R) <=> direct(R) or exists S in forwards(R). relevant(S)
 *
 * over the relations reachable from `source`.  A non-productive candidate
 * leaves only a negative record, which changes no execution, so the
 * relations outside the fixed point are irrelevant.  A body that could not
 * be read may hide a dynamic-head application, so two fixed points are
 * computed.  `possible` is seeded also by unreadable bodies; on the explored
 * relations it holds exactly when some chain ends at a dynamic-head
 * application or an unreadable body, whichever source the exploration
 * started from, so a relation outside it is certified irrelevant.  `certain`
 * is seeded only by applications seen: reading stops at the first
 * application or unreadable body, so an application behind an unreadable
 * body goes unseen, and a relation in `possible` but not in `certain` is
 * reported unknown and never stored.  A stored class stands in for its
 * relation's subgraph and is keyed by the program token, so a new equation
 * or callable declaration invalidates it.
 */
static PettaRelationRelevance
petta_relation_specialization_relevance(
    PettaSpecializerContext *context, SymbolId source) {
    if (!context || !context->space ||
        source == SYMBOL_ID_NONE) {
        return PETTA_RELATION_RELEVANCE_UNKNOWN;
    }
    SpaceProgramToken program = space_program_token(context->space);
    PettaRelationRelevance cached =
        PETTA_RELATION_RELEVANCE_UNKNOWN;
    if (petta_relation_relevance_cache_lookup(program, source, &cached))
        return cached;

    PettaRelationRelevanceNode *nodes = NULL;
    size_t len = 0u;
    size_t cap = 0u;
    PettaRelationRelevance result = PETTA_RELATION_RELEVANCE_UNKNOWN;
    bool ok = petta_reserve(
        (void **)&nodes, &cap, 1u, sizeof(*nodes));
    bool source_has_equation = false;
    if (ok)
        nodes[len++] = (PettaRelationRelevanceNode){.symbol = source};
    for (size_t next = 0u; ok && next < len; next++) {
        /* A stored callee fact is already this fixed point's answer. */
        if (nodes[next].cached)
            continue;
        bool saw_equation = false;
        if (!petta_relation_relevance_node_scan(
                context, &nodes[next], &saw_equation)) {
            ok = false;
            break;
        }
        if (next == 0u)
            source_has_equation = saw_equation;
        if (nodes[next].direct || nodes[next].unknown)
            continue;
        for (size_t index = 0u;
             ok && index < nodes[next].forwards.len; index++) {
            SymbolId callee = nodes[next].forwards.items[index];
            if (petta_relation_relevance_node_find(
                    nodes, len, callee) != SIZE_MAX)
                continue;
            PettaRelationRelevance known =
                PETTA_RELATION_RELEVANCE_UNKNOWN;
            if (len >= PETTA_RELATION_RELEVANCE_NODE_LIMIT ||
                !petta_reserve(
                    (void **)&nodes, &cap, len + 1u,
                    sizeof(*nodes))) {
                ok = false;
                break;
            }
            nodes[len] = (PettaRelationRelevanceNode){.symbol = callee};
            if (petta_relation_relevance_cache_lookup(
                    program, callee, &known)) {
                nodes[len].direct =
                    known == PETTA_RELATION_RELEVANCE_CERTAIN;
                nodes[len].cached = true;
            }
            len++;
        }
    }

    if (ok && source_has_equation &&
        space_program_token_eq(
            space_program_token(context->space), program)) {
        /* Two least fixed points: `possible` treats unreadable bodies as
         * relevant; `certain` asks whether relevance holds without them. */
        for (size_t index = 0u; index < len; index++) {
            nodes[index].possible =
                nodes[index].direct || nodes[index].unknown;
            nodes[index].certain = nodes[index].direct;
        }
        bool changed = true;
        while (changed) {
            changed = false;
            for (size_t index = 0u; index < len; index++) {
                PettaRelationRelevanceNode *node = &nodes[index];
                for (size_t edge = 0u;
                     edge < node->forwards.len; edge++) {
                    size_t target = petta_relation_relevance_node_find(
                        nodes, len, node->forwards.items[edge]);
                    if (target == SIZE_MAX)
                        continue;
                    if (!node->possible && nodes[target].possible) {
                        node->possible = true;
                        changed = true;
                    }
                    if (!node->certain && nodes[target].certain) {
                        node->certain = true;
                        changed = true;
                    }
                }
            }
        }
        for (size_t index = 0u; index < len; index++) {
            PettaRelationRelevance relevance =
                nodes[index].certain
                    ? PETTA_RELATION_RELEVANCE_CERTAIN
                    : nodes[index].possible
                        ? PETTA_RELATION_RELEVANCE_UNKNOWN
                        : PETTA_RELATION_RELEVANCE_IRRELEVANT;
            if (index == 0u)
                result = relevance;
            if (!nodes[index].cached)
                petta_relation_relevance_cache_store(
                    program, nodes[index].symbol, relevance);
        }
    }
    for (size_t index = 0u; index < len; index++)
        free(nodes[index].forwards.items);
    free(nodes);
    return result;
}

/*
 * The relation-side factor of the admission conjunction.  It is a program
 * fact, so it is read before any query inspection: after its first
 * computation for a program token, every call of the relation is answered
 * from the cache without a scratch arena or an argument walk.  A cursor
 * invalidated during the computation is reported, never cached.
 */
static bool petta_specializer_relation_factor(
        Space *space, SymbolId source,
        PettaRelationRelevance *relevance) {
    *relevance = PETTA_RELATION_RELEVANCE_UNKNOWN;
    if (petta_relation_relevance_cache_lookup(
            space_program_token(space), source, relevance))
        return true;
    PettaSpecializerContext context = {
        .space = space,
        .semantic_cache = petta_semantic_cache_prepare(space),
    };
    arena_init(&context.scratch);
    arena_set_runtime_kind(
        &context.scratch, CETTA_ARENA_RUNTIME_KIND_SCRATCH);
    arena_set_hashcons(&context.scratch, NULL);
    *relevance = petta_relation_specialization_relevance(
        &context, source);
    arena_free(&context.scratch);
    if (context.invalidated) {
        *relevance = PETTA_RELATION_RELEVANCE_UNKNOWN;
        return false;
    }
    return true;
}

PettaSpecializerRelationAdmission
petta_specializer_relation_execution_admission(
        Space *space, SymbolId source) {
    if (!space || source == SYMBOL_ID_NONE)
        return PETTA_SPECIALIZER_RELATION_DEFER;
    PettaSpecializerContext context = {
        .space = space,
        .semantic_cache = petta_semantic_cache_prepare(space),
    };
    arena_init(&context.scratch);
    arena_set_runtime_kind(
        &context.scratch, CETTA_ARENA_RUNTIME_KIND_SCRATCH);
    arena_set_hashcons(&context.scratch, NULL);
    PettaRelationRelevance relevance =
        petta_relation_specialization_relevance(
            &context, source);
    arena_free(&context.scratch);
    if (context.invalidated)
        return PETTA_SPECIALIZER_RELATION_INVALIDATED;
    return relevance == PETTA_RELATION_RELEVANCE_IRRELEVANT
        ? PETTA_SPECIALIZER_RELATION_IRRELEVANT
        : PETTA_SPECIALIZER_RELATION_DEFER;
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
                   context, atom->sym_id)
            ? atom : NULL;
    }
    if (atom->kind != ATOM_EXPR ||
        atom->expr.len == 0u ||
        atom->expr.elems[0]->kind != ATOM_SYMBOL) {
        return NULL;
    }
    CettaExprLen supplied = atom->expr.len - 1u;
    PeTTaNamedArity arity = petta_specializer_named_arity(
        context, atom->expr.elems[0], supplied);
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
 * forest is bounded separately by a per-call node budget.  Reaching that
 * budget declines specialization for this call and leaves it to the ordinary
 * evaluator; it must not turn a bounded rejection test into an unbounded
 * optimization attempt.  Later calls receive their own relevance check.
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
petta_query_arguments_may_supply_specializable_value(
    PettaSpecializerContext *context,
    Atom *const *arguments, const CettaGsltTermCursorV1 *views,
    CettaExprLen arity, CettaGsltTermCursorObserverV1 observer) {
    enum {
        PETTA_RELEVANCE_STACK_CAPACITY = 128,
        PETTA_RELEVANCE_NODE_LIMIT = 64,
    };
    CettaGsltTermCursorV1 stack[PETTA_RELEVANCE_STACK_CAPACITY];
    size_t length = 0u;
    size_t visited = 0u;
    if (!context || (arity > 0u && !arguments && !views)) {
        return PETTA_RELEVANCE_YES;
    }
    if ((size_t)arity >
        PETTA_RELEVANCE_STACK_CAPACITY) {
        return PETTA_RELEVANCE_YES;
    }
    for (CettaExprIndex index = 0u; index < arity; index++) {
        stack[length++] = views ? views[index]
            : (CettaGsltTermCursorV1){.source = arguments[index]};
    }

    while (length > 0u) {
        if (visited++ >= PETTA_RELEVANCE_NODE_LIMIT)
            return PETTA_RELEVANCE_NODE_BUDGET;
        CettaGsltTermCursorV1 cursor;
        if (cetta_gslt_term_cursor_resolve_root_v1(
                observer, stack[--length], &cursor) !=
                CETTA_GSLT_TERM_VIEW_OK_V1)
            return PETTA_RELEVANCE_YES;
        Atom *atom = cursor.source;
        if (atom->kind == ATOM_SYMBOL &&
            petta_symbol_is_callable(
                context, atom->sym_id)) {
            return PETTA_RELEVANCE_YES;
        }
        if (atom->kind != ATOM_EXPR || atom->expr.len == 0u)
            continue;
        CettaGsltTermCursorV1 head_cursor = {
            .source = atom->expr.elems[0], .scope = cursor.scope};
        if (cetta_gslt_term_cursor_resolve_root_v1(
                observer, head_cursor, &head_cursor) != CETTA_GSLT_TERM_VIEW_OK_V1)
            return PETTA_RELEVANCE_YES;
        Atom *head = head_cursor.source;
        if (!head)
            return PETTA_RELEVANCE_YES;
        /* Both head facts belong to the same revision and symbol table.
         * Read their prepared classification once per head observation. */
        uint8_t classification = head->kind == ATOM_SYMBOL
            ? petta_symbol_classification(context, head->sym_id) : 0u;
        if (classification & PETTA_SYMBOL_CALLABLE)
            return PETTA_RELEVANCE_YES;
        if (atom->expr.len == 3u &&
            (classification & PETTA_SYMBOL_PARTIAL_CONSTRUCTOR)) {
            CettaGsltTermCursorV1 tuple = {
                .source = atom->expr.elems[2], .scope = cursor.scope};
            if (cetta_gslt_term_cursor_resolve_root_v1(
                    observer, tuple, &tuple) != CETTA_GSLT_TERM_VIEW_OK_V1 ||
                tuple.source->kind == ATOM_EXPR)
                return PETTA_RELEVANCE_YES;
        }
        if ((size_t)atom->expr.len >
            PETTA_RELEVANCE_STACK_CAPACITY - length) {
            return PETTA_RELEVANCE_YES;
        }
        CettaExprIndex first = head->kind == ATOM_SYMBOL ? 1u : 0u;
        visited += first;
        for (CettaExprIndex index = first;
             index < atom->expr.len; index++) {
            stack[length++] = (CettaGsltTermCursorV1){
                .source = atom->expr.elems[index], .scope = cursor.scope};
            if (index == 0u)
                stack[length - 1u] = head_cursor;
        }
    }
    return PETTA_RELEVANCE_NO;
}

static BindingValue petta_binding_value_child(
        BindingValue parent, Atom *child) {
    return binding_value_is_contextual(parent)
        ? binding_value_from_context_kind(
              child, parent.epoch, parent.kind)
        : binding_value_from_atom(child);
}

/* Traverse explicit substitutions without first reifying their lexical
 * identities into one Atom tree.  Each stack item is a closure fragment:
 * syntax plus the context in which its variables are interpreted. */
static PettaRelevanceResult
petta_query_values_may_supply_specializable_value(
        PettaSpecializerContext *context, Bindings *environment,
        const BindingValue *arguments, CettaExprLen arity) {
    enum {
        PETTA_VALUE_RELEVANCE_STACK_CAPACITY = 128,
        PETTA_VALUE_RELEVANCE_NODE_LIMIT = 64,
    };
    BindingValue stack[PETTA_VALUE_RELEVANCE_STACK_CAPACITY];
    size_t length = 0u;
    size_t visited = 0u;
    if (!context || !environment || (arity > 0u && !arguments) ||
        (size_t)arity > PETTA_VALUE_RELEVANCE_STACK_CAPACITY) {
        return PETTA_RELEVANCE_YES;
    }
    for (CettaExprIndex index = 0u; index < arity; index++)
        stack[length++] = arguments[index];

    while (length > 0u) {
        if (visited++ >= PETTA_VALUE_RELEVANCE_NODE_LIMIT)
            return PETTA_RELEVANCE_NODE_BUDGET;
        BindingValue value;
        if (!bindings_resolve_value_preview(
                environment, stack[--length], &value) ||
            !value.skeleton) {
            return PETTA_RELEVANCE_YES;
        }
        Atom *atom = value.skeleton;
        if (atom->kind == ATOM_SYMBOL &&
            petta_symbol_is_callable(context, atom->sym_id)) {
            return PETTA_RELEVANCE_YES;
        }
        if (atom->kind != ATOM_EXPR || atom->expr.len == 0u)
            continue;

        BindingValue head_value;
        if (!bindings_resolve_value_preview(
                environment,
                petta_binding_value_child(
                    value, atom->expr.elems[0]),
                &head_value) ||
            !head_value.skeleton) {
            return PETTA_RELEVANCE_YES;
        }
        Atom *head = head_value.skeleton;
        uint8_t classification = head->kind == ATOM_SYMBOL
            ? petta_symbol_classification(context, head->sym_id) : 0u;
        if (classification & PETTA_SYMBOL_CALLABLE)
            return PETTA_RELEVANCE_YES;
        if (atom->expr.len == 3u &&
            (classification & PETTA_SYMBOL_PARTIAL_CONSTRUCTOR)) {
            BindingValue tuple;
            if (!bindings_resolve_value_preview(
                    environment,
                    petta_binding_value_child(
                        value, atom->expr.elems[2]),
                    &tuple) ||
                !tuple.skeleton || tuple.skeleton->kind == ATOM_EXPR) {
                return PETTA_RELEVANCE_YES;
            }
        }
        if ((size_t)atom->expr.len >
            PETTA_VALUE_RELEVANCE_STACK_CAPACITY - length) {
            return PETTA_RELEVANCE_YES;
        }
        CettaExprIndex first = head->kind == ATOM_SYMBOL ? 1u : 0u;
        visited += first;
        for (CettaExprIndex index = first;
             index < atom->expr.len; index++) {
            stack[length++] = index == 0u
                ? head_value
                : petta_binding_value_child(
                      value, atom->expr.elems[index]);
        }
    }
    return PETTA_RELEVANCE_NO;
}

static bool petta_query_value_forest_shape(
        PettaSpecializerContext *context, Bindings *environment,
        const BindingValue *arguments, CettaExprLen arity,
        uint64_t *shape_out) {
    enum {
        PETTA_VALUE_FOREST_STACK = 32,
        PETTA_VALUE_FOREST_NODES = 32,
        PETTA_VALUE_FOREST_DEPTH = 8,
    };
    typedef struct {
        BindingValue value;
        uint32_t depth;
    } PettaValueForestItem;
    if (!context || !environment || !shape_out ||
        (arity > 0u && !arguments) ||
        (size_t)arity > PETTA_VALUE_FOREST_STACK) {
        return false;
    }
    PettaValueForestItem stack[PETTA_VALUE_FOREST_STACK];
    size_t length = 0u;
    size_t visited = 0u;
    uint64_t mixed = UINT64_C(0xd6e8feb86659fd93) ^
        ((uint64_t)arity * UINT64_C(0x27d4eb2f165667c5));
    for (CettaExprIndex index = 0u; index < arity; index++) {
        stack[length++] = (PettaValueForestItem){arguments[index], 0u};
    }
    while (length > 0u) {
        if (visited++ >= PETTA_VALUE_FOREST_NODES)
            return false;
        PettaValueForestItem item = stack[--length];
        BindingValue value;
        if (item.depth > PETTA_VALUE_FOREST_DEPTH ||
            !bindings_resolve_value_preview(
                environment, item.value, &value) ||
            !value.skeleton) {
            return false;
        }
        Atom *atom = value.skeleton;
        mixed = petta_forest_mix(
            mixed, petta_forest_supplier_class(context, atom));
        if (atom->kind != ATOM_EXPR || atom->expr.len == 0u)
            continue;
        if (length + (size_t)atom->expr.len >
            PETTA_VALUE_FOREST_STACK) {
            return false;
        }
        for (CettaExprIndex index = 0u;
             index < atom->expr.len; index++) {
            mixed = petta_forest_mix(mixed, (uint64_t)index + 1u);
            stack[length++] = (PettaValueForestItem){
                petta_binding_value_child(
                    value, atom->expr.elems[index]),
                item.depth + 1u,
            };
        }
    }
    *shape_out = mixed ? mixed : UINT64_C(1);
    return true;
}

static PettaRelevanceResult
petta_call_may_supply_specializable_value(
    PettaSpecializerContext *context, Atom *call) {
    if (!call || call->kind != ATOM_EXPR ||
        call->expr.len == 0u) {
        return PETTA_RELEVANCE_YES;
    }
    return petta_query_arguments_may_supply_specializable_value(
        context, call->expr.elems + 1u, NULL,
        call->expr.len - 1u, (CettaGsltTermCursorObserverV1){0});
}

static PettaSpecializerRelationAdmission
petta_specializer_query_execution_admission_core(
        Space *space, SymbolId source,
        Atom *const *arguments, const CettaGsltTermCursorV1 *views,
        CettaExprLen arity, CettaGsltTermCursorObserverV1 observer) {
    if (!space || source == SYMBOL_ID_NONE ||
        (arity > 0u && !arguments && !views)) {
        return PETTA_SPECIALIZER_RELATION_DEFER;
    }
    uint64_t instance = space_instance_id(space);
    PettaRelationRelevance cached_relation =
        PETTA_RELATION_RELEVANCE_UNKNOWN;
    bool relation_cached = false;
    if (petta_specializer_relation_prefilter_enabled()) {
        if (!petta_specializer_relation_factor(
                space, source, &cached_relation))
            return PETTA_SPECIALIZER_RELATION_INVALIDATED;
        relation_cached =
            cached_relation != PETTA_RELATION_RELEVANCE_UNKNOWN;
    }
    /*
     * Specialization requires both a relation-side consumer and a
     * query-side supplier.  A program-pinned proof that the first factor is
     * absent refutes the conjunction for every concrete query, so inspecting
     * that query cannot add information.  Unknown and positive certificates
     * retain the ordinary bounded query analysis below.
     */
    if (relation_cached &&
        cached_relation == PETTA_RELATION_RELEVANCE_IRRELEVANT) {
        return PETTA_SPECIALIZER_RELATION_IRRELEVANT;
    }

    /*
     * Closed leaves have no holes.  A table hit is the whole query-side
     * fact; do not allocate a scratch arena to reread it.
     */
    uint64_t leaf_shape = 0u;
    bool scopes_ok = true;
    if (views) {
        const void *frame = NULL;
        if (observer.resolve == bindings_resolve_term_cursor_v1 &&
            observer.context) {
            const BindingsTermCursorContextV1 *cursor = observer.context;
            frame = cursor->frame;
        }
        for (CettaExprIndex index = 0u; index < arity; index++) {
            if (views[index].scope && views[index].scope != frame) {
                scopes_ok = false;
                break;
            }
        }
    }
    if (scopes_ok && arity <= PETTA_QUERY_FOREST_ARITY_CAP &&
        petta_query_forest_closed_leaf_shape(
            arguments, views, arity, &leaf_shape)) {
        uint8_t cached_forest = 0u;
        if (petta_query_forest_cache_lookup(
                space, instance, 0u, leaf_shape, source, arity,
                &cached_forest)) {
            PettaRelevanceResult query_relevance =
                (PettaRelevanceResult)cached_forest;
            if (query_relevance == PETTA_RELEVANCE_NO ||
                (relation_cached &&
                 cached_relation ==
                     PETTA_RELATION_RELEVANCE_IRRELEVANT)) {
                return PETTA_SPECIALIZER_RELATION_IRRELEVANT;
            }
            if (relation_cached)
                return PETTA_SPECIALIZER_RELATION_DEFER;
        }
    }

    PettaSpecializerContext context = {
        .space = space,
        .semantic_cache = petta_semantic_cache_prepare(space),
    };
    arena_init(&context.scratch);
    arena_set_runtime_kind(
        &context.scratch, CETTA_ARENA_RUNTIME_KIND_SCRATCH);
    arena_set_hashcons(&context.scratch, NULL);

    uint64_t forest_environment = 0u;
    uint64_t forest_shape = 0u;
    bool forest_keyed = petta_query_forest_key(
        &context, arguments, views, arity, observer,
        &forest_environment, &forest_shape);
    uint8_t cached_forest = 0u;
    PettaRelevanceResult query_relevance = PETTA_RELEVANCE_YES;
    if (forest_keyed &&
        petta_query_forest_cache_lookup(
            space, instance, forest_environment, forest_shape,
            source, arity, &cached_forest)) {
        query_relevance = (PettaRelevanceResult)cached_forest;
    } else {
        query_relevance =
            petta_query_arguments_may_supply_specializable_value(
                &context, arguments, views, arity, observer);
        if (forest_keyed && !context.invalidated)
            petta_query_forest_cache_store(
                space, instance, forest_environment,
                forest_shape, source, arity,
                (uint8_t)query_relevance);
    }
    PettaRelationRelevance relation_relevance =
        relation_cached ? cached_relation
                        : PETTA_RELATION_RELEVANCE_UNKNOWN;
    if (query_relevance != PETTA_RELEVANCE_NO) {
        if (!relation_cached) {
            relation_relevance =
                petta_relation_specialization_relevance(
                    &context, source);
        }
    }
    arena_free(&context.scratch);

    if (context.invalidated)
        return PETTA_SPECIALIZER_RELATION_INVALIDATED;
    if (query_relevance == PETTA_RELEVANCE_NO ||
        relation_relevance ==
            PETTA_RELATION_RELEVANCE_IRRELEVANT) {
        return PETTA_SPECIALIZER_RELATION_IRRELEVANT;
    }
    return PETTA_SPECIALIZER_RELATION_DEFER;
}

PettaSpecializerRelationAdmission
petta_specializer_query_execution_admission(
        Space *space, SymbolId source,
        Atom *const *arguments, CettaExprLen arity) {
    return petta_specializer_query_execution_admission_core(
        space, source, arguments, NULL, arity,
        (CettaGsltTermCursorObserverV1){0});
}

PettaSpecializerRelationAdmission
petta_specializer_query_view_execution_admission(
        Space *space, SymbolId source,
        const CettaGsltTermCursorV1 *arguments, CettaExprLen arity,
        CettaGsltTermCursorObserverV1 observer) {
    return petta_specializer_query_execution_admission_core(
        space, source, NULL, arguments, arity, observer);
}

PettaSpecializerRelationAdmission
petta_specializer_query_value_execution_admission(
        Space *space, SymbolId source, Bindings *environment,
        const BindingValue *arguments, CettaExprLen arity) {
    if (!space || source == SYMBOL_ID_NONE || !environment ||
        (arity > 0u && !arguments)) {
        return PETTA_SPECIALIZER_RELATION_DEFER;
    }
    uint64_t instance = space_instance_id(space);
    PettaRelationRelevance relation_relevance =
        PETTA_RELATION_RELEVANCE_UNKNOWN;
    bool relation_cached = false;
    if (petta_specializer_relation_prefilter_enabled()) {
        if (!petta_specializer_relation_factor(
                space, source, &relation_relevance))
            return PETTA_SPECIALIZER_RELATION_INVALIDATED;
        relation_cached =
            relation_relevance != PETTA_RELATION_RELEVANCE_UNKNOWN;
    }
    if (relation_cached &&
        relation_relevance == PETTA_RELATION_RELEVANCE_IRRELEVANT) {
        return PETTA_SPECIALIZER_RELATION_IRRELEVANT;
    }

    PettaSpecializerContext context = {
        .space = space,
        .semantic_cache = petta_semantic_cache_prepare(space),
    };
    arena_init(&context.scratch);
    arena_set_runtime_kind(
        &context.scratch, CETTA_ARENA_RUNTIME_KIND_SCRATCH);
    arena_set_hashcons(&context.scratch, NULL);

    uint64_t forest_shape = 0u;
    const uint64_t forest_domain = UINT64_C(0xb5ad4eceda1ce2a9);
    bool forest_keyed = petta_query_value_forest_shape(
        &context, environment, arguments, arity,
        &forest_shape);
    uint8_t cached_forest = 0u;
    PettaRelevanceResult query_relevance = PETTA_RELEVANCE_YES;
    if (forest_keyed &&
        petta_query_forest_cache_lookup(
            space, instance, forest_domain, forest_shape,
            source, arity, &cached_forest)) {
        query_relevance = (PettaRelevanceResult)cached_forest;
    } else {
        query_relevance =
            petta_query_values_may_supply_specializable_value(
                &context, environment, arguments, arity);
        if (forest_keyed && !context.invalidated) {
            petta_query_forest_cache_store(
                space, instance, forest_domain, forest_shape,
                source, arity, (uint8_t)query_relevance);
        }
    }
    if (query_relevance != PETTA_RELEVANCE_NO &&
        !relation_cached) {
        relation_relevance =
            petta_relation_specialization_relevance(
                &context, source);
    }
    arena_free(&context.scratch);

    if (context.invalidated)
        return PETTA_SPECIALIZER_RELATION_INVALIDATED;
    if (query_relevance == PETTA_RELEVANCE_NO ||
        relation_relevance ==
            PETTA_RELATION_RELEVANCE_IRRELEVANT) {
        return PETTA_SPECIALIZER_RELATION_IRRELEVANT;
    }
    return PETTA_SPECIALIZER_RELATION_DEFER;
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
    Atom *fresh = cetta_instantiate_frame_syntax(&context->scratch, equation);
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
    if (matched) {
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
        context, head->sym_id);
    PeTTaNamedArity arity = callable
        ? petta_specializer_named_arity(
              context, head, supplied)
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
                context, atom->expr.elems[0]->sym_id)) {
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

static bool petta_specialization_selectors_match(
    PettaSpecializerContext *context, Atom *call,
    const PettaSpecializationBindings *selectors) {
    if (!context || !call || !selectors || selectors->len == 0u)
        return false;
    for (size_t index = 0u; index < selectors->len; index++) {
        const PettaSpecializationBinding *selector =
            &selectors->items[index];
        Atom *actual = petta_atom_at_path(
            call, selector->path, selector->path_len);
        Atom *value = actual
            ? petta_specializable_value(
                  context, &context->scratch, actual)
            : NULL;
        if (!value || !atom_eq(value, selector->value))
            return false;
    }
    return true;
}

/* Specialization fixes higher-order selectors for the lifetime of a derived
 * relation.  Route recursive calls carrying those same selector values back
 * to that relation once, when the artifact is built, instead of rediscovering
 * the route at every recursive step.  Opaque callable and quotation payloads
 * remain source data.  Applying a canonical partial is expanded exactly as
 * the PeTTa machine expands it before dispatch, exposing ordinary register
 * instructions to later generated-machine compilation. */
static Atom *petta_rewrite_specialized_body(
    PettaSpecializerContext *context, Atom *atom,
    SymbolId source, SymbolId specialized,
    const PettaSpecializationBindings *selectors,
    uint32_t depth) {
    if (!context || !atom || depth > 2048u) {
        if (context)
            context->capacity = true;
        return NULL;
    }
    if (atom->kind != ATOM_EXPR || atom->expr.len == 0u)
        return atom;

    Atom *ignored = NULL;
    SymbolId head = atom_head_symbol_id(atom);
    PeTTaForm form = head == SYMBOL_ID_NONE
        ? PETTA_FORM_NONE : petta_semantics_form(head);
    if (head == g_builtin_syms.quote ||
        head == g_builtin_syms.return_text ||
        form == PETTA_FORM_PREDICATE ||
        form == PETTA_FORM_LAMBDA ||
        petta_semantics_lambda_body(atom, &ignored) ||
        petta_semantics_nullary_lambda_body(atom, &ignored) ||
        petta_semantics_partial_view(atom, NULL, NULL)) {
        return atom;
    }

    Atom *partial_base = NULL;
    Atom *bound = NULL;
    if (petta_semantics_partial_view(
            atom->expr.elems[0], &partial_base, &bound)) {
        CettaExprLen supplied = atom->expr.len - 1u;
        if (!partial_base || !bound || bound->kind != ATOM_EXPR ||
            bound->expr.len > UINT64_MAX - supplied - 1u) {
            context->capacity = true;
            return NULL;
        }
        CettaExprLen combined = bound->expr.len + supplied + 1u;
        if (!cetta_expr_len_mul_fits_size(
                combined, sizeof(Atom *))) {
            context->capacity = true;
            return NULL;
        }
        Atom **elements = arena_alloc(
            &context->scratch,
            sizeof(*elements) * (size_t)combined);
        elements[0] = partial_base;
        if (bound->expr.len > 0u) {
            memcpy(
                elements + 1u, bound->expr.elems,
                sizeof(*elements) * (size_t)bound->expr.len);
        }
        for (CettaExprIndex index = 0u; index < supplied; index++) {
            elements[bound->expr.len + index + 1u] =
                petta_rewrite_specialized_body(
                    context, atom->expr.elems[index + 1u],
                    source, specialized, selectors, depth + 1u);
            if (!elements[bound->expr.len + index + 1u])
                return NULL;
        }
        Atom *expanded = atom_expr(
            &context->scratch, elements, combined);
        if (!expanded)
            return NULL;
        return petta_rewrite_specialized_body(
            context, expanded, source, specialized,
            selectors, depth + 1u);
    }

    if (!cetta_expr_len_mul_fits_size(
            atom->expr.len, sizeof(Atom *))) {
        context->capacity = true;
        return NULL;
    }
    Atom **elements = arena_alloc(
        &context->scratch,
        sizeof(*elements) * (size_t)atom->expr.len);
    elements[0] = atom->expr.elems[0];
    for (CettaExprIndex index = 1u; index < atom->expr.len; index++) {
        elements[index] = petta_rewrite_specialized_body(
            context, atom->expr.elems[index],
            source, specialized, selectors, depth + 1u);
        if (!elements[index])
            return NULL;
    }
    Atom *rewritten = atom_expr(
        &context->scratch, elements, atom->expr.len);
    if (!rewritten)
        return NULL;
    if (head == source &&
        petta_specialization_selectors_match(
            context, rewritten, selectors)) {
        elements[0] = atom_symbol_id(
            &context->scratch, specialized);
        rewritten = atom_expr(
            &context->scratch, elements, atom->expr.len);
    }
    return rewritten;
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
        Atom *source_equation = cetta_instantiate_frame_syntax(&context->scratch, equations->items[index]);
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
        Atom *rewritten = substituted
            ? petta_rewrite_specialized_body(
                  context, substituted, source, specialized,
                  candidates, 0u)
            : NULL;
        Atom *promoted = rewritten
            ? atom_deep_copy(
                  context->persistent, rewritten)
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
    cetta_runtime_stats_inc(
        CETTA_RUNTIME_COUNTER_DECLARED_TYPE_SPECIALIZER_COPY);
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
        PettaRelationRelevance relation_relevance =
            PETTA_RELATION_RELEVANCE_UNKNOWN;
        bool relation_cached =
            petta_relation_relevance_cache_lookup(
                space_program_token(context->space), source,
                &relation_relevance);
        if (relation_cached &&
            relation_relevance ==
                PETTA_RELATION_RELEVANCE_IRRELEVANT) {
            analysis->relation_filtered = true;
            return true;
        }
        PettaRelevanceResult relevance =
            petta_call_may_supply_specializable_value(
                context, call);
        if (relevance == PETTA_RELEVANCE_NO) {
            analysis->filtered = true;
            return true;
        }
        if (!relation_cached) {
            relation_relevance =
                petta_relation_specialization_relevance(
                    context, source);
        }
        if (relation_relevance ==
            PETTA_RELATION_RELEVANCE_IRRELEVANT) {
            analysis->relation_filtered = true;
            return true;
        }
        if (relevance == PETTA_RELEVANCE_NODE_BUDGET) {
            analysis->relevance_bounded = true;
            return true;
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
        BindingsIterator iterator = {.bindings = &bindings};
        Binding logical_binding;
        while (ok && bindings_iterator_next(&iterator, &logical_binding)) {
            const Binding *binding = &logical_binding;
            Atom *specializable = petta_specializable_value(
                context, &candidate_values,
                binding_value_materialize(&context->scratch, binding->value));
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
        PettaSpecializationRecord *negative = petta_create_record(
            context, source, specialized, &candidates);
        if (!negative) {
            ok = false;
        } else {
            negative->negative = true;
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
        .semantic_cache = petta_semantic_cache_prepare(space),
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
    if (analysis.relation_filtered)
        return PETTA_SPECIALIZE_UNCHANGED_RELATION_FILTERED;
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

static void petta_record_release_owned(
    PettaSpecializationRecord *record) {
    if (!record)
        return;
    for (size_t artifact = 0u;
         artifact < record->artifact_len; artifact++) {
        petta_pattern_free(record->artifacts[artifact].pattern);
    }
    for (size_t selector = 0u;
         selector < record->selector_len; selector++) {
        free(record->selectors[selector].path);
    }
    free(record->selectors);
    free(record->artifacts);
    record->selectors = NULL;
    record->selector_len = 0u;
    record->artifacts = NULL;
    record->artifact_len = 0u;
    record->artifact_cap = 0u;
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
    petta_record_release_owned(record);
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
    /* Any equation or type mutation can make a nested symbol callable, so the
     * relation-level negative cache is discarded wholesale.  Ordinary data
     * additions never reach this path, and the program-token key
     * independently prevents stale reuse. */
    petta_relation_relevance_cache_clear();
    uint64_t instance = space_instance_id(space);
    /* A failed derivation can depend on callees absent from the productive
     * artifact graph. Clear completed failures for this space whenever a
     * definition changes; the dependency closure below still handles
     * productive records precisely. */
    for (size_t index = 0u; index < g_petta_specializations.len;) {
        PettaSpecializationRecord *record =
            &g_petta_specializations.items[index];
        if (record->negative && record->space == space &&
            record->space_instance == instance) {
            petta_remove_record_at(index);
        } else {
            index++;
        }
    }
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
        petta_record_release_owned(
            &g_petta_specializations.items[index]);
    }
    free(g_petta_specializations.items);
    memset(
        &g_petta_specializations, 0,
        sizeof(g_petta_specializations));
    memset(&g_petta_semantic_cache, 0,
           sizeof(g_petta_semantic_cache));
    petta_relation_relevance_cache_clear();
}
