/* Native realization of the EBNF derivation projection.
 *
 * Mirrored, observation-for-observation, from the authored transformation
 * langdef/bnf/ebnf_derivation_projection_v1.metta, which remains the
 * independently executable oracle. Generic over receipt data: grammar
 * definitions, lexical declarations, helper origins, and CST nodes are read
 * as plain atoms. No grammar family, rule name, arity, or input size is
 * recognized specially.
 *
 * Per call, rule definitions (alternative labels precomputed once as
 * name/suffix strings), lexical declarations, and helper origins are
 * prepared once into first-binding lookup tables. The walk then consults
 * the tables instead of rescanning the receipt. All traversal state lives
 * in an explicit heap frame stack: nested references and left-recursive
 * helper chains never grow the C call stack, so input depth is bounded by
 * memory, not by a platform stack. Tables and frame storage are freed on
 * return; emitted atoms share receipt subatoms that the authored
 * transformation also carried through unchanged.
 */

#include "ebnf_derivation_projection_native_v1.h"
#include "utf8_scalar_v1.h"

#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ---------------------------------------------------------------- helpers */

static bool proj_error(char *error, size_t size, const char *format, ...) {
    va_list arguments;
    va_start(arguments, format);
    if (error && size > 0u)
        (void)vsnprintf(error, size, format, arguments);
    va_end(arguments);
    return false;
}

static bool is_expr_head(const Atom *atom, const char *head, uint32_t arity) {
    return atom && atom->kind == ATOM_EXPR && atom->expr.len == arity + 1u &&
           atom->expr.elems[0] &&
           atom->expr.elems[0]->kind == ATOM_SYMBOL &&
           strcmp(atom_name_cstr(atom->expr.elems[0]), head) == 0;
}

static bool is_ground_int(const Atom *atom) {
    return atom && atom->kind == ATOM_GROUNDED && atom->ground.gkind == GV_INT;
}

static bool is_ground_string(const Atom *atom) {
    return atom && atom->kind == ATOM_GROUNDED &&
           atom->ground.gkind == GV_STRING;
}

static Atom *make_expr(Arena *arena, const char *head, Atom **arguments,
                       uint32_t arity) {
    Atom **elements = malloc(((size_t)arity + 1u) * sizeof(*elements));
    Atom *result;
    if (!elements)
        return NULL;
    elements[0] = atom_symbol(arena, head);
    if (!elements[0]) {
        free(elements);
        return NULL;
    }
    for (uint32_t index = 0u; index < arity; index++)
        elements[index + 1u] = arguments[index];
    result = atom_expr_shared(arena, elements, (CettaExprLen)arity + 1u);
    free(elements);
    return result;
}

static Atom *make_expr0(Arena *arena, const char *head) {
    Atom *symbol = atom_symbol(arena, head);
    if (!symbol)
        return NULL;
    return atom_expr_shared(arena, &symbol, 1u);
}

static Atom *make_expr2(Arena *arena, const char *head, Atom *a, Atom *b) {
    Atom *arguments[2] = {a, b};
    return make_expr(arena, head, arguments, 2u);
}

static Atom *make_expr3(Arena *arena, const char *head, Atom *a, Atom *b,
                        Atom *c) {
    Atom *arguments[3] = {a, b, c};
    return make_expr(arena, head, arguments, 3u);
}

static Atom *make_expr4(Arena *arena, const char *head, Atom *a, Atom *b,
                        Atom *c, Atom *d) {
    Atom *arguments[4] = {a, b, c, d};
    return make_expr(arena, head, arguments, 4u);
}

static Atom *make_expr5(Arena *arena, const char *head, Atom *a, Atom *b,
                        Atom *c, Atom *d, Atom *e) {
    Atom *arguments[5] = {a, b, c, d, e};
    return make_expr(arena, head, arguments, 5u);
}

static Atom *make_expr6(Arena *arena, const char *head, Atom *a, Atom *b,
                        Atom *c, Atom *d, Atom *e, Atom *f) {
    Atom *arguments[6] = {a, b, c, d, e, f};
    return make_expr(arena, head, arguments, 6u);
}

typedef struct {
    Atom **items;
    uint32_t len;
    uint32_t cap;
} AtomVec;

static bool vec_push(AtomVec *vec, Atom *item) {
    if (!vec)
        return false;
    if (vec->len == vec->cap) {
        uint32_t next = vec->cap ? vec->cap * 2u : 16u;
        if (next < vec->cap || (size_t)next > SIZE_MAX / sizeof(*vec->items))
            return false;
        Atom **grown = realloc(vec->items, (size_t)next * sizeof(*vec->items));
        if (!grown)
            return false;
        vec->items = grown;
        vec->cap = next;
    }
    vec->items[vec->len++] = item;
    return true;
}

/* ------------------------------------------------------------- bnf texts */

static bool text_cons_parts(const Atom *atom, int64_t *scalar,
                            const Atom **rest) {
    if (!is_expr_head(atom, "bnf-v1:text-cons", 2u) ||
        !is_ground_int(atom->expr.elems[1]))
        return false;
    *scalar = atom->expr.elems[1]->ground.ival;
    *rest = atom->expr.elems[2];
    return true;
}

static bool text_well_formed(const Atom *atom) {
    int64_t scalar;
    const Atom *rest;
    while (atom && is_expr_head(atom, "bnf-v1:text-cons", 2u)) {
        if (!text_cons_parts(atom, &scalar, &rest))
            return false;
        atom = rest;
    }
    return atom && is_expr_head(atom, "bnf-v1:text-nil", 0u);
}

static bool text_equal(const Atom *left, const Atom *right) {
    int64_t left_scalar, right_scalar;
    const Atom *left_rest, *right_rest;
    for (;;) {
        bool left_cons =
            left && is_expr_head(left, "bnf-v1:text-cons", 2u);
        bool right_cons =
            right && is_expr_head(right, "bnf-v1:text-cons", 2u);
        if (left_cons != right_cons)
            return false;
        if (!left_cons)
            return is_expr_head(left, "bnf-v1:text-nil", 0u) &&
                   is_expr_head(right, "bnf-v1:text-nil", 0u);
        if (!text_cons_parts(left, &left_scalar, &left_rest) ||
            !text_cons_parts(right, &right_scalar, &right_rest))
            return false;
        if (left_scalar != right_scalar)
            return false;
        left = left_rest;
        right = right_rest;
    }
}

static int64_t text_length(const Atom *atom, bool *ok) {
    int64_t length = 0;
    int64_t scalar;
    const Atom *rest;
    while (atom && is_expr_head(atom, "bnf-v1:text-cons", 2u)) {
        if (!text_cons_parts(atom, &scalar, &rest)) {
            *ok = false;
            return 0;
        }
        if (length == INT64_MAX) {
            *ok = false;
            return 0;
        }
        length++;
        atom = rest;
    }
    if (!atom || !is_expr_head(atom, "bnf-v1:text-nil", 0u)) {
        *ok = false;
        return 0;
    }
    *ok = true;
    return length;
}

/* The single UTF-8 scalar encoding shared with the language-definition
 * codec: NUL, surrogates, and out-of-range scalars are rejected, exactly as
 * the authored text conversion rejects them. */
static bool i64_add_checked(int64_t left, int64_t right, int64_t *out) {
    /* Same overflow contract as the deterministic integer-addition
     * primitive: overflow is an explicit error, never a wrapped span. */
    if ((right > 0 && left > INT64_MAX - right) ||
        (right < 0 && left < INT64_MIN - right))
        return false;
    *out = left + right;
    return true;
}

static char *text_to_bytes(const Atom *atom) {
    uint8_t *bytes = NULL;
    size_t len = 0u;
    size_t cap = 0u;
    int64_t scalar;
    const Atom *rest;

    while (atom && is_expr_head(atom, "bnf-v1:text-cons", 2u)) {
        if (!text_cons_parts(atom, &scalar, &rest) ||
            !cetta_utf8_append_scalar_v1(&bytes, &len, &cap, scalar)) {
            free(bytes);
            return NULL;
        }
        atom = rest;
    }
    if (!atom || !is_expr_head(atom, "bnf-v1:text-nil", 0u)) {
        free(bytes);
        return NULL;
    }
    if (!bytes) {
        bytes = malloc(1u);
        if (!bytes)
            return NULL;
        bytes[0] = '\0';
    }
    return (char *)bytes;
}

static uint32_t text_hash(const Atom *atom) {
    uint32_t hash = 2166136261u;
    int64_t scalar;
    const Atom *rest;
    while (atom && is_expr_head(atom, "bnf-v1:text-cons", 2u)) {
        if (!text_cons_parts(atom, &scalar, &rest))
            break;
        uint64_t bits = (uint64_t)scalar;
        for (unsigned shift = 0u; shift < 64u; shift += 8u) {
            hash ^= (uint32_t)((bits >> shift) & 0xFFu);
            hash *= 16777619u;
        }
        atom = rest;
    }
    return hash;
}

/* ------------------------------------------------------ prepared metadata */

#define PROJ_BUCKET_MIN 64u

static uint32_t bucket_size_for(uint32_t count) {
    uint32_t size = PROJ_BUCKET_MIN;
    uint64_t wanted = (uint64_t)count * 2u;
    while ((uint64_t)size < wanted) {
        if (size > (UINT32_MAX >> 1u))
            return 0u;
        size *= 2u;
    }
    return size;
}

static uint32_t cstr_hash(const char *label) {
    uint32_t hash = 2166136261u;
    for (const unsigned char *scan = (const unsigned char *)label; *scan;
         scan++) {
        hash ^= *scan;
        hash *= 16777619u;
    }
    return hash;
}

typedef struct {
    char *label;                /* "name" + "#" + "x" * index */
    int64_t index;
    const Atom *elements;
    const Atom *span;
} ProjAlt;

typedef struct ProjDef {
    const Atom *name;
    Atom *name_string;
    const Atom *rule_span;
    ProjAlt *alts;
    uint32_t alt_count;
    uint32_t *alt_index;        /* open-addressed: alt index + 1, 0 = empty */
    uint32_t alt_index_size;    /* always a power of two */
    struct ProjDef *bucket_next;
    struct ProjDef *alloc_next;
} ProjDef;

typedef struct ProjLex {
    const Atom *name;
    Atom *name_string;
    const Atom *label;
    const Atom *origin;
    struct ProjLex *bucket_next;
    struct ProjLex *alloc_next;
} ProjLex;

typedef struct {
    int64_t index;
    const Atom *kind;
    const Atom *span;
} ProjOrg;

typedef struct ProjOrgEntry {
    const Atom *name;
    ProjOrg *orgs;
    uint32_t org_count;
    uint32_t org_cap;
    struct ProjOrgEntry *bucket_next;
    struct ProjOrgEntry *alloc_next;
} ProjOrgEntry;

typedef struct {
    Arena *arena;
    ProjDef **def_buckets;
    uint32_t def_bucket_mask;
    ProjLex **lex_buckets;
    uint32_t lex_bucket_mask;
    ProjOrgEntry **org_buckets;
    uint32_t org_bucket_mask;
    ProjDef *def_allocs;
    ProjLex *lex_allocs;
    ProjOrgEntry *org_allocs;
} ProjTables;

static void tables_free(ProjTables *tables) {
    free(tables->def_buckets);
    free(tables->lex_buckets);
    free(tables->org_buckets);
    ProjDef *def = tables->def_allocs;
    while (def) {
        ProjDef *next = def->alloc_next;
        for (uint32_t index = 0u; index < def->alt_count; index++)
            free(def->alts[index].label);
        free(def->alts);
        free(def->alt_index);
        free(def);
        def = next;
    }
    ProjLex *lex = tables->lex_allocs;
    while (lex) {
        ProjLex *next = lex->alloc_next;
        free(lex);
        lex = next;
    }
    ProjOrgEntry *org = tables->org_allocs;
    while (org) {
        ProjOrgEntry *next = org->alloc_next;
        free(org->orgs);
        free(org);
        org = next;
    }
}

static void def_discard(ProjDef *def) {
    if (!def)
        return;
    for (uint32_t index = 0u; index < def->alt_count; index++)
        free(def->alts[index].label);
    free(def->alts);
    free(def->alt_index);
    free(def);
}

/* Open-addressed label lookup per definition: expected O(1) probes with
 * bounded occupancy; a chosen-collision adversary degrades toward the
 * alternative count of one definition, never the whole grammar. */
static bool def_build_alt_index(ProjDef *def) {
    uint32_t size = 8u;
    while ((uint64_t)size < (uint64_t)def->alt_count * 2u) {
        if (size > (UINT32_MAX >> 1u))
            return false;
        size *= 2u;
    }
    def->alt_index = calloc(size, sizeof(*def->alt_index));
    if (!def->alt_index)
        return false;
    def->alt_index_size = size;
    for (uint32_t index = 0u; index < def->alt_count; index++) {
        uint32_t slot = cstr_hash(def->alts[index].label) & (size - 1u);
        while (def->alt_index[slot] != 0u)
            slot = (slot + 1u) & (size - 1u);
        def->alt_index[slot] = index + 1u;
    }
    return true;
}

static const ProjAlt *def_find_alt(const ProjDef *def, const char *label) {
    if (!def->alt_index || def->alt_index_size == 0u)
        return NULL;
    uint32_t slot = cstr_hash(label) & (def->alt_index_size - 1u);
    while (def->alt_index[slot] != 0u) {
        const ProjAlt *alt = &def->alts[def->alt_index[slot] - 1u];
        if (strcmp(alt->label, label) == 0)
            return alt;
        slot = (slot + 1u) & (def->alt_index_size - 1u);
    }
    return NULL;
}

static bool prepare_def_alts(ProjTables *tables, ProjDef *def,
                             const Atom *name_bytes_atom, char *error,
                             size_t error_size) {
    (void)tables;
    char *name_bytes = text_to_bytes(name_bytes_atom);
    if (!name_bytes)
        return proj_error(error, error_size,
                          "ebnf derivation projection: rule name encoding failed");
    size_t name_len = strlen(name_bytes);
    for (uint32_t index = 0u; index < def->alt_count; index++) {
        size_t suffix_len = 1u + (size_t)index;
        if (name_len > SIZE_MAX - suffix_len - 1u) {
            free(name_bytes);
            return proj_error(error, error_size,
                              "ebnf derivation projection: alternative label size overflow");
        }
        def->alts[index].label = malloc(name_len + suffix_len + 1u);
        if (!def->alts[index].label) {
            free(name_bytes);
            return proj_error(error, error_size,
                              "ebnf derivation projection: alternative label allocation failed");
        }
        memcpy(def->alts[index].label, name_bytes, name_len);
        def->alts[index].label[name_len] = '#';
        memset(def->alts[index].label + name_len + 1u, 'x', index);
        def->alts[index].label[name_len + suffix_len] = '\0';
    }
    free(name_bytes);
    return true;
}

static bool prepare_definitions(ProjTables *tables, const Atom *entries,
                                char *error, size_t error_size) {
    uint32_t rule_count = 0u;
    {
        const Atom *counting = entries;
        while (counting && is_expr_head(counting, "bnf-v1:entries-cons", 2u)) {
            if (is_expr_head(counting->expr.elems[1], "bnf-v1:rule", 3u)) {
                if (rule_count == UINT32_MAX)
                    return proj_error(error, error_size,
                                      "ebnf derivation projection: definition count overflow");
                rule_count++;
            }
            counting = counting->expr.elems[2];
        }
    }
    uint32_t buckets = bucket_size_for(rule_count);
    if (buckets == 0u)
        return proj_error(error, error_size,
                          "ebnf derivation projection: definition table size overflow");
    tables->def_buckets = calloc(buckets, sizeof(*tables->def_buckets));
    if (!tables->def_buckets)
        return proj_error(error, error_size,
                          "ebnf derivation projection: definition table allocation failed");
    tables->def_bucket_mask = buckets - 1u;
    const Atom *cursor = entries;
    while (cursor && is_expr_head(cursor, "bnf-v1:entries-cons", 2u)) {
        const Atom *entry = cursor->expr.elems[1];
        cursor = cursor->expr.elems[2];
        if (is_expr_head(entry, "bnf-v1:comment", 2u) ||
            is_expr_head(entry, "bnf-v1:blank", 1u))
            continue;
        if (!is_expr_head(entry, "bnf-v1:rule", 3u))
            return proj_error(error, error_size,
                              "ebnf derivation projection: malformed grammar entry");
        const Atom *name = entry->expr.elems[1];
        const Atom *expression = entry->expr.elems[2];
        const Atom *rule_span = entry->expr.elems[3];
        if (!text_well_formed(name) ||
            !is_expr_head(expression, "bnf-v1:expression", 2u))
            return proj_error(error, error_size,
                              "ebnf derivation projection: malformed rule entry");

        uint32_t alt_count = 0u;
        const Atom *scan = expression->expr.elems[1];
        while (scan && is_expr_head(scan, "bnf-v1:alternatives-cons", 2u)) {
            alt_count++;
            scan = scan->expr.elems[2];
        }
        if (!scan || !is_expr_head(scan, "bnf-v1:alternatives-nil", 0u))
            return proj_error(error, error_size,
                              "ebnf derivation projection: malformed alternatives list");

        uint32_t bucket = text_hash(name) & tables->def_bucket_mask;
        bool duplicate = false;
        for (ProjDef *prior = tables->def_buckets[bucket]; prior;
             prior = prior->bucket_next)
            if (text_equal(prior->name, name)) {
                duplicate = true;
                break;
            }
        if (duplicate)
            continue; /* first binding wins */

        ProjDef *def = calloc(1u, sizeof(*def));
        if (!def)
            return proj_error(error, error_size,
                              "ebnf derivation projection: definition allocation failed");
        def->alts = calloc(alt_count ? alt_count : 1u, sizeof(*def->alts));
        if (!def->alts) {
            free(def);
            return proj_error(error, error_size,
                              "ebnf derivation projection: alternative allocation failed");
        }
        def->name = name;
        def->rule_span = rule_span;
        def->alt_count = alt_count;

        scan = expression->expr.elems[1];
        for (uint32_t index = 0u; index < alt_count; index++) {
            const Atom *alternative = scan->expr.elems[1];
            scan = scan->expr.elems[2];
            if (!is_expr_head(alternative, "bnf-v1:alternative", 2u)) {
                def_discard(def);
                return proj_error(error, error_size,
                                  "ebnf derivation projection: malformed alternative");
            }
            def->alts[index].index = (int64_t)index;
            def->alts[index].elements = alternative->expr.elems[1];
            def->alts[index].span = alternative->expr.elems[2];
        }
        if (!prepare_def_alts(tables, def, name, error, error_size) ||
            !def_build_alt_index(def)) {
            def_discard(def);
            return proj_error(error, error_size,
                              "ebnf derivation projection: alternative index allocation failed");
        }
        char *name_bytes = text_to_bytes(name);
        if (!name_bytes) {
            def_discard(def);
            return proj_error(error, error_size,
                              "ebnf derivation projection: rule name encoding failed");
        }
        def->name_string = atom_string(tables->arena, name_bytes);
        free(name_bytes);
        if (!def->name_string) {
            def_discard(def);
            return proj_error(error, error_size,
                              "ebnf derivation projection: rule name string allocation failed");
        }
        def->bucket_next = tables->def_buckets[bucket];
        tables->def_buckets[bucket] = def;
        def->alloc_next = tables->def_allocs;
        tables->def_allocs = def;
    }
    if (!cursor || !is_expr_head(cursor, "bnf-v1:entries-nil", 0u))
        return proj_error(error, error_size,
                          "ebnf derivation projection: malformed entry list");
    return true;
}

static bool prepare_lexicals(ProjTables *tables, const Atom *lexicals,
                             char *error, size_t error_size) {
    uint32_t lex_count = 0u;
    {
        const Atom *counting = lexicals;
        while (counting &&
               is_expr_head(counting, "bnf-v1:lexical-declarations-cons", 2u)) {
            if (lex_count == UINT32_MAX)
                return proj_error(error, error_size,
                                  "ebnf derivation projection: lexical count overflow");
            lex_count++;
            counting = counting->expr.elems[2];
        }
    }
    uint32_t buckets = bucket_size_for(lex_count);
    if (buckets == 0u)
        return proj_error(error, error_size,
                          "ebnf derivation projection: lexical table size overflow");
    tables->lex_buckets = calloc(buckets, sizeof(*tables->lex_buckets));
    if (!tables->lex_buckets)
        return proj_error(error, error_size,
                          "ebnf derivation projection: lexical table allocation failed");
    tables->lex_bucket_mask = buckets - 1u;
    const Atom *cursor = lexicals;
    while (cursor &&
           is_expr_head(cursor, "bnf-v1:lexical-declarations-cons", 2u)) {
        const Atom *declaration = cursor->expr.elems[1];
        cursor = cursor->expr.elems[2];
        if (!is_expr_head(declaration, "bnf-v1:lexical-declaration", 5u) ||
            !text_well_formed(declaration->expr.elems[1]))
            return proj_error(error, error_size,
                              "ebnf derivation projection: malformed lexical declaration");
        const Atom *name = declaration->expr.elems[1];
        uint32_t bucket = text_hash(name) & tables->lex_bucket_mask;
        bool duplicate = false;
        for (ProjLex *prior = tables->lex_buckets[bucket]; prior;
             prior = prior->bucket_next)
            if (text_equal(prior->name, name)) {
                duplicate = true;
                break;
            }
        if (duplicate)
            continue;
        ProjLex *lex = calloc(1u, sizeof(*lex));
        if (!lex)
            return proj_error(error, error_size,
                              "ebnf derivation projection: lexical allocation failed");
        char *name_bytes = text_to_bytes(name);
        if (!name_bytes) {
            free(lex);
            return proj_error(error, error_size,
                              "ebnf derivation projection: lexical name encoding failed");
        }
        lex->name = name;
        lex->label = declaration->expr.elems[4];
        lex->origin = declaration->expr.elems[5];
        lex->name_string = atom_string(tables->arena, name_bytes);
        free(name_bytes);
        if (!lex->name_string) {
            free(lex);
            return proj_error(error, error_size,
                              "ebnf derivation projection: lexical name string allocation failed");
        }
        lex->bucket_next = tables->lex_buckets[bucket];
        tables->lex_buckets[bucket] = lex;
        lex->alloc_next = tables->lex_allocs;
        tables->lex_allocs = lex;
    }
    if (!cursor ||
        !is_expr_head(cursor, "bnf-v1:lexical-declarations-nil", 0u))
        return proj_error(error, error_size,
                          "ebnf derivation projection: malformed lexical list");
    return true;
}

static bool prepare_origins(ProjTables *tables, const Atom *origins,
                            char *error, size_t error_size) {
    uint32_t org_name_count = 0u;
    {
        const Atom *counting = origins;
        while (counting && is_expr_head(counting, "ebnf-v1:origins-cons", 2u)) {
            if (org_name_count == UINT32_MAX)
                return proj_error(error, error_size,
                                  "ebnf derivation projection: origin count overflow");
            org_name_count++;
            counting = counting->expr.elems[2];
        }
    }
    uint32_t buckets = bucket_size_for(org_name_count);
    if (buckets == 0u)
        return proj_error(error, error_size,
                          "ebnf derivation projection: origin table size overflow");
    tables->org_buckets = calloc(buckets, sizeof(*tables->org_buckets));
    if (!tables->org_buckets)
        return proj_error(error, error_size,
                          "ebnf derivation projection: origin table allocation failed");
    tables->org_bucket_mask = buckets - 1u;
    const Atom *cursor = origins;
    int64_t position = 0;
    while (cursor && is_expr_head(cursor, "ebnf-v1:origins-cons", 2u)) {
        const Atom *origin = cursor->expr.elems[1];
        cursor = cursor->expr.elems[2];
        if (!is_expr_head(origin, "ebnf-v1:helper-origin", 4u) ||
            !text_well_formed(origin->expr.elems[1]))
            return proj_error(error, error_size,
                              "ebnf derivation projection: malformed helper origin");
        const Atom *name = origin->expr.elems[1];
        uint32_t bucket = text_hash(name) & tables->org_bucket_mask;
        ProjOrgEntry *entry = NULL;
        for (ProjOrgEntry *prior = tables->org_buckets[bucket]; prior;
             prior = prior->bucket_next)
            if (text_equal(prior->name, name)) {
                entry = prior;
                break;
            }
        if (!entry) {
            entry = calloc(1u, sizeof(*entry));
            if (!entry)
                return proj_error(error, error_size,
                                  "ebnf derivation projection: origin allocation failed");
            entry->name = name;
            entry->bucket_next = tables->org_buckets[bucket];
            tables->org_buckets[bucket] = entry;
            entry->alloc_next = tables->org_allocs;
            tables->org_allocs = entry;
        }
        if (entry->org_count == entry->org_cap) {
            uint32_t next = entry->org_cap ? entry->org_cap * 2u : 4u;
            ProjOrg *grown =
                realloc(entry->orgs, (size_t)next * sizeof(*entry->orgs));
            if (!grown)
                return proj_error(error, error_size,
                                  "ebnf derivation projection: origin list allocation failed");
            entry->orgs = grown;
            entry->org_cap = next;
        }
        entry->orgs[entry->org_count].index = position;
        entry->orgs[entry->org_count].kind = origin->expr.elems[2];
        entry->orgs[entry->org_count].span = origin->expr.elems[4];
        entry->org_count++;
        position++;
    }
    if (!cursor || !is_expr_head(cursor, "ebnf-v1:origins-nil", 0u))
        return proj_error(error, error_size,
                          "ebnf derivation projection: malformed origins list");
    return true;
}

static const ProjDef *find_def(const ProjTables *tables, const Atom *name) {
    if (!tables->def_buckets)
        return NULL;
    uint32_t bucket = text_hash(name) & tables->def_bucket_mask;
    for (const ProjDef *def = tables->def_buckets[bucket]; def;
         def = def->bucket_next)
        if (text_equal(def->name, name))
            return def;
    return NULL;
}

static const ProjLex *find_lex(const ProjTables *tables, const Atom *name) {
    if (!tables->lex_buckets)
        return NULL;
    uint32_t bucket = text_hash(name) & tables->lex_bucket_mask;
    for (const ProjLex *lex = tables->lex_buckets[bucket]; lex;
         lex = lex->bucket_next)
        if (text_equal(lex->name, name))
            return lex;
    return NULL;
}

static const ProjOrg *find_org(const ProjTables *tables, const Atom *name) {
    if (!tables->org_buckets)
        return NULL;
    uint32_t bucket = text_hash(name) & tables->org_bucket_mask;
    for (const ProjOrgEntry *entry = tables->org_buckets[bucket]; entry;
         entry = entry->bucket_next)
        if (text_equal(entry->name, name))
            return entry->org_count ? &entry->orgs[0] : NULL;
    return NULL;
}

/* ------------------------------------------------------------ frame state */

typedef enum {
    FR_ROOTS = 0,
    FR_TREE,
    FR_ELEMENTS,
    FR_FINISH,
    FR_SO_MAP,
    FR_SO_ITER,
    FR_SO_PAIR
} ProjFrameKind;

#define ROOTS_STEP 0u
#define ROOTS_WAIT_TREE 1u
#define ROOTS_WAIT_SO 2u

typedef struct ProjFrame {
    ProjFrameKind kind;
    Atom **target;

    /* FR_ROOTS */
    const Atom *trees;
    const Atom *start;
    uint32_t next_root;
    uint8_t roots_phase;
    Atom *root_scratch;
    Atom *root_final;

    /* FR_TREE */
    const Atom *name;
    const Atom *tree;

    /* FR_FINISH */
    Atom *elems_slot;
    const Atom *origin_kind;   /* NULL when authored */
    int64_t origin_index;
    const Atom *origin_span;
    const Atom *rule_span;
    int64_t alt_index;
    const Atom *alt_span;
    Atom *rule_name_string;
    int64_t node_left;
    int64_t node_right;

    /* FR_ELEMENTS */
    const Atom *rest;
    const Atom *cst;
    uint32_t child_index;
    int64_t pos;
    AtomVec out;
    bool waiting_ref;
    const Atom *ref_name;
    const Atom *ref_span;
    const Atom *ref_origin_kind;
    int64_t ref_origin_index;
    const Atom *ref_origin_span;
    int64_t ref_left;
    int64_t ref_right;
    Atom *ref_slot;

    /* FR_SO_MAP */
    const Atom *so_src;
    Atom *so_slot0;
    uint32_t so_child;
    uint8_t so_phase;

    /* FR_SO_ITER */
    AtomVec bodies;
    AtomVec mapped;
    uint32_t iter_next;

    /* FR_SO_PAIR */
    uint8_t pair_phase;
    Atom *so_slot1;
} ProjFrame;

typedef struct {
    Arena *arena;
    char *error;
    size_t error_size;
    const ProjTables *tables;

    ProjFrame **frames;
    uint32_t frame_count;
    uint32_t frame_cap;
    AtomVec roots;
} ProjMachine;

static void frame_free(ProjFrame *frame) {
    if (!frame)
        return;
    if (frame->kind == FR_ELEMENTS)
        free(frame->out.items);
    if (frame->kind == FR_SO_ITER) {
        free(frame->bodies.items);
        free(frame->mapped.items);
    }
    free(frame);
}

static ProjFrame *machine_push(ProjMachine *machine, ProjFrameKind kind,
                               Atom **target) {
    if (machine->frame_count == machine->frame_cap) {
        uint32_t next = machine->frame_cap ? machine->frame_cap * 2u : 32u;
        if (next < machine->frame_cap ||
            (size_t)next > SIZE_MAX / sizeof(*machine->frames))
            return NULL;
        ProjFrame **grown =
            realloc(machine->frames, (size_t)next * sizeof(*grown));
        if (!grown)
            return NULL;
        machine->frames = grown;
        machine->frame_cap = next;
    }
    ProjFrame *frame = calloc(1u, sizeof(*frame));
    if (!frame)
        return NULL;
    frame->kind = kind;
    frame->target = target;
    machine->frames[machine->frame_count++] = frame;
    return frame;
}

static void machine_pop(ProjMachine *machine) {
    if (machine->frame_count == 0u)
        return;
    frame_free(machine->frames[machine->frame_count - 1u]);
    machine->frame_count--;
}

static ProjFrame *machine_top(ProjMachine *machine) {
    return machine->frame_count
        ? machine->frames[machine->frame_count - 1u]
        : NULL;
}

/* ------------------------------------------------------- validation layer */

static bool cst_rule_parts(const Atom *tree, const Atom **label,
                           int64_t *left, int64_t *right) {
    if (!tree || tree->kind != ATOM_EXPR || tree->expr.len < 4u ||
        !tree->expr.elems[0] || tree->expr.elems[0]->kind != ATOM_SYMBOL ||
        strcmp(atom_name_cstr(tree->expr.elems[0]), "CstRuleV1") != 0)
        return false;
    if (!is_ground_string(tree->expr.elems[1]) ||
        !is_ground_int(tree->expr.elems[2]) ||
        !is_ground_int(tree->expr.elems[3]))
        return false;
    *label = tree->expr.elems[1];
    *left = tree->expr.elems[2]->ground.ival;
    *right = tree->expr.elems[3]->ground.ival;
    return true;
}

/* ---------------------------------------------------------- tree dispatch */

static bool machine_tree_enter(ProjMachine *machine, ProjFrame *frame) {
    const Atom *label;
    int64_t left, right;

    if (!cst_rule_parts(frame->tree, &label, &left, &right))
        return proj_error(machine->error, machine->error_size,
                          "ebnf derivation projection: malformed CST rule node");
    if (!text_well_formed(frame->name))
        return proj_error(machine->error, machine->error_size,
                          "ebnf derivation projection: malformed rule name");

    const ProjDef *def = find_def(machine->tables, frame->name);
    if (def) {
        const ProjAlt *selected = def_find_alt(def, label->ground.sval);
        if (!selected)
            return proj_error(machine->error, machine->error_size,
                              "ebnf derivation projection: no alternative matches the node label");
        const ProjOrg *origin = find_org(machine->tables, frame->name);
        frame->kind = FR_FINISH;
        frame->elems_slot = NULL;
        frame->origin_kind = origin ? origin->kind : NULL;
        frame->origin_index = origin ? origin->index : 0;
        frame->origin_span = origin ? origin->span : NULL;
        frame->rule_span = def->rule_span;
        frame->alt_index = selected->index;
        frame->alt_span = selected->span;
        frame->rule_name_string = def->name_string;
        frame->node_left = left;
        frame->node_right = right;
        ProjFrame *elements =
            machine_push(machine, FR_ELEMENTS, &frame->elems_slot);
        if (!elements)
            return proj_error(machine->error, machine->error_size,
                              "ebnf derivation projection: frame allocation failed");
        elements->rest = selected->elements;
        elements->cst = frame->tree;
        elements->child_index = 4u;
        elements->pos = left;
        return true;
    }

    const ProjLex *lex = find_lex(machine->tables, frame->name);
    if (lex) {
        if (frame->tree->expr.len != 5u)
            return proj_error(machine->error, machine->error_size,
                              "ebnf derivation projection: lexical node arity mismatch");
        const Atom *leaf = frame->tree->expr.elems[4];
        if (!is_expr_head(leaf, "cp", 1u) ||
            !is_ground_int(leaf->expr.elems[1]))
            return proj_error(machine->error, machine->error_size,
                              "ebnf derivation projection: malformed lexical leaf");
        if (!atom_eq((Atom *)lex->label, (Atom *)label))
            return proj_error(machine->error, machine->error_size,
                              "ebnf derivation projection: lexical label mismatch");
        int64_t left_one;
        if (!i64_add_checked(left, 1, &left_one))
            return proj_error(machine->error, machine->error_size,
                              "ebnf derivation projection: integer addition overflowed");
        if (left_one != right)
            return proj_error(machine->error, machine->error_size,
                              "ebnf derivation projection: lexical span is not a unit span");
        Atom *span = make_expr2(machine->arena, "EBNF:InputSpan",
                                frame->tree->expr.elems[2],
                                frame->tree->expr.elems[3]);
        Atom *node = span
            ? make_expr5(machine->arena, "EBNF:Lexical", lex->name_string,
                         (Atom *)lex->label, (Atom *)lex->origin, span,
                         leaf->expr.elems[1])
            : NULL;
        if (!node)
            return proj_error(machine->error, machine->error_size,
                              "ebnf derivation projection: lexical node allocation failed");
        *frame->target = node;
        machine_pop(machine);
        return true;
    }

    return proj_error(machine->error, machine->error_size,
                      "ebnf derivation projection: no rule or lexical declaration for the referenced name");
}

/* --------------------------------------------------------- element walker */

static bool machine_elements_step(ProjMachine *machine, ProjFrame *frame) {
    for (;;) {
        if (!frame->rest ||
            !is_expr_head(frame->rest, "bnf-v1:elements-cons", 2u)) {
            if (!is_expr_head(frame->rest, "bnf-v1:elements-nil", 0u))
                return proj_error(machine->error, machine->error_size,
                                  "ebnf derivation projection: malformed element list");
            const Atom *label;
            int64_t left, right;
            if (!cst_rule_parts(frame->cst, &label, &left, &right))
                return proj_error(machine->error, machine->error_size,
                                  "ebnf derivation projection: malformed CST rule node");
            if (frame->child_index != frame->cst->expr.len)
                return proj_error(machine->error, machine->error_size,
                                  "ebnf derivation projection: unconsumed CST children");
            if (frame->pos != right)
                return proj_error(machine->error, machine->error_size,
                                  "ebnf derivation projection: element coverage stopped before the node span end");
            Atom *chain = make_expr0(machine->arena, "EBNF:ElementsNil");
            for (uint32_t index = frame->out.len; index > 0u && chain;
                 index--)
                chain = make_expr2(machine->arena, "EBNF:ElementsCons",
                                   frame->out.items[index - 1u], chain);
            if (!chain)
                return proj_error(machine->error, machine->error_size,
                                  "ebnf derivation projection: element chain allocation failed");
            *frame->target = chain;
            machine_pop(machine);
            return true;
        }
        const Atom *element = frame->rest->expr.elems[1];
        const Atom *tail = frame->rest->expr.elems[2];

        if (is_expr_head(element, "bnf-v1:literal", 2u)) {
            const Atom *text = element->expr.elems[1];
            const Atom *span = element->expr.elems[2];
            bool ok = false;
            int64_t length = text_length(text, &ok);
            if (!ok)
                return proj_error(machine->error, machine->error_size,
                                  "ebnf derivation projection: malformed literal text");
            char *bytes = text_to_bytes(text);
            if (!bytes)
                return proj_error(machine->error, machine->error_size,
                                  "ebnf derivation projection: literal encoding failed");
            Atom *text_atom = atom_string(machine->arena, bytes);
            free(bytes);
            if (!text_atom)
                return proj_error(machine->error, machine->error_size,
                                  "ebnf derivation projection: literal text allocation failed");
            int64_t next_pos;
            if (!i64_add_checked(frame->pos, length, &next_pos))
                return proj_error(machine->error, machine->error_size,
                                  "ebnf derivation projection: integer addition overflowed");
            Atom *input =
                make_expr2(machine->arena, "EBNF:InputSpan",
                           atom_int(machine->arena, frame->pos),
                           atom_int(machine->arena, next_pos));
            Atom *node = input
                ? make_expr3(machine->arena, "EBNF:Literal", text_atom,
                             (Atom *)span, input)
                : NULL;
            if (!node || !vec_push(&frame->out, node))
                return proj_error(machine->error, machine->error_size,
                                  "ebnf derivation projection: literal node allocation failed");
            frame->pos = next_pos;
            frame->rest = tail;
            continue;
        }

        if (is_expr_head(element, "bnf-v1:reference", 2u)) {
            frame->ref_name = element->expr.elems[1];
            frame->ref_span = element->expr.elems[2];
            if (frame->child_index >= frame->cst->expr.len)
                return proj_error(machine->error, machine->error_size,
                                  "ebnf derivation projection: reference without a matching CST child");
            const Atom *child = frame->cst->expr.elems[frame->child_index];
            const Atom *label;
            int64_t left, right;
            if (!cst_rule_parts(child, &label, &left, &right))
                return proj_error(machine->error, machine->error_size,
                                  "ebnf derivation projection: malformed referenced CST node");
            if (frame->pos != left)
                return proj_error(machine->error, machine->error_size,
                                  "ebnf derivation projection: referenced node does not start at the element position");
            const ProjOrg *origin = find_org(machine->tables, frame->ref_name);
            frame->ref_origin_kind = origin ? origin->kind : NULL;
            frame->ref_origin_index = origin ? origin->index : 0;
            frame->ref_origin_span = origin ? origin->span : NULL;
            frame->ref_left = left;
            frame->ref_right = right;
            frame->ref_slot = NULL;
            frame->waiting_ref = true;
            frame->rest = tail;
            frame->child_index++;
            ProjFrame *child_frame =
                machine_push(machine, FR_TREE, &frame->ref_slot);
            if (!child_frame)
                return proj_error(machine->error, machine->error_size,
                                  "ebnf derivation projection: frame allocation failed");
            child_frame->name = frame->ref_name;
            child_frame->tree = child;
            return true;
        }

        return proj_error(machine->error, machine->error_size,
                          "ebnf derivation projection: unknown grammar element kind");
    }
}

static bool machine_elements_resume(ProjMachine *machine, ProjFrame *frame) {
    Atom *node = frame->ref_slot;
    if (!node)
        return proj_error(machine->error, machine->error_size,
                          "ebnf derivation projection: missing referenced derivation");
    frame->ref_slot = NULL;
    frame->waiting_ref = false;
    if (!frame->ref_origin_kind) {
        char *bytes = text_to_bytes(frame->ref_name);
        if (!bytes)
            return proj_error(machine->error, machine->error_size,
                              "ebnf derivation projection: reference name encoding failed");
        Atom *name_string = atom_string(machine->arena, bytes);
        free(bytes);
        if (!name_string)
            return proj_error(machine->error, machine->error_size,
                              "ebnf derivation projection: reference name allocation failed");
        Atom *input =
            make_expr2(machine->arena, "EBNF:InputSpan",
                       atom_int(machine->arena, frame->ref_left),
                       atom_int(machine->arena, frame->ref_right));
        Atom *wrapped = input
            ? make_expr4(machine->arena, "EBNF:Reference", name_string,
                         (Atom *)frame->ref_span, input, node)
            : NULL;
        if (!wrapped)
            return proj_error(machine->error, machine->error_size,
                              "ebnf derivation projection: reference node allocation failed");
        node = wrapped;
    }
    if (!vec_push(&frame->out, node))
        return proj_error(machine->error, machine->error_size,
                          "ebnf derivation projection: element node allocation failed");
    frame->pos = frame->ref_right;
    return machine_elements_step(machine, frame);
}

/* ------------------------------------------------------------ node finish */

static bool machine_finish_enter(ProjMachine *machine, ProjFrame *frame) {
    Arena *arena = machine->arena;
    Atom *elements = frame->elems_slot;
    if (!elements)
        return proj_error(machine->error, machine->error_size,
                          "ebnf derivation projection: missing node elements");
    Atom *occurrence = NULL;
    if (frame->origin_kind) {
        occurrence = atom_int(arena, frame->origin_index);
        if (!occurrence)
            return proj_error(machine->error, machine->error_size,
                              "ebnf derivation projection: occurrence allocation failed");
    }
    Atom *input = make_expr2(arena, "EBNF:InputSpan",
                             atom_int(arena, frame->node_left),
                             atom_int(arena, frame->node_right));
    Atom *alt_index_atom = atom_int(arena, frame->alt_index);
    if (!input || !alt_index_atom)
        return proj_error(machine->error, machine->error_size,
                          "ebnf derivation projection: node span allocation failed");
    Atom *node = NULL;

    if (!frame->origin_kind) {
        node = make_expr6(arena, "EBNF:Rule", frame->rule_name_string,
                          (Atom *)frame->rule_span, alt_index_atom,
                          (Atom *)frame->alt_span, input, elements);
    } else {
        const char *kind = frame->origin_kind->kind == ATOM_SYMBOL
            ? atom_name_cstr((Atom *)frame->origin_kind)
            : "";
        if (strcmp(kind, "group") == 0) {
            node = make_expr6(arena, "EBNF:Group", occurrence,
                              (Atom *)frame->origin_span, alt_index_atom,
                              (Atom *)frame->alt_span, input, elements);
        } else if (strcmp(kind, "optional") == 0) {
            if (frame->alt_index == 0 &&
                is_expr_head(elements, "EBNF:ElementsNil", 0u)) {
                node = make_expr3(arena, "EBNF:OptionalAbsent", occurrence,
                                  (Atom *)frame->origin_span, input);
            } else if (frame->alt_index == 1 &&
                       is_expr_head(elements, "EBNF:ElementsCons", 2u) &&
                       is_expr_head(elements->expr.elems[2],
                                    "EBNF:ElementsNil", 0u)) {
                node = make_expr4(arena, "EBNF:OptionalPresent", occurrence,
                                  (Atom *)frame->origin_span, input,
                                  elements->expr.elems[1]);
            }
        } else if (strcmp(kind, "zero-or-more") == 0 ||
                   strcmp(kind, "one-or-more") == 0) {
            bool star = strcmp(kind, "zero-or-more") == 0;
            Atom *kind_atom = atom_symbol(arena, kind);
            if (!kind_atom)
                return proj_error(machine->error, machine->error_size,
                                  "ebnf derivation projection: kind symbol allocation failed");
            if (frame->alt_index == 0) {
                if (star && is_expr_head(elements, "EBNF:ElementsNil", 0u)) {
                    Atom *nil = make_expr0(arena, "EBNF:IterationsNil");
                    node = nil
                        ? make_expr5(arena, "EBNF:Repetition", kind_atom,
                                     occurrence,
                                     (Atom *)frame->origin_span, input, nil)
                        : NULL;
                } else if (!star &&
                           is_expr_head(elements, "EBNF:ElementsCons", 2u) &&
                           is_expr_head(elements->expr.elems[2],
                                        "EBNF:ElementsNil", 0u)) {
                    Atom *nil = make_expr0(arena, "EBNF:IterationsNil");
                    Atom *single = nil
                        ? make_expr2(arena, "EBNF:IterationsCons",
                                     elements->expr.elems[1], nil)
                        : NULL;
                    node = single
                        ? make_expr5(arena, "EBNF:Repetition", kind_atom,
                                     occurrence,
                                     (Atom *)frame->origin_span, input, single)
                        : NULL;
                }
            } else if (frame->alt_index == 1 &&
                       is_expr_head(elements, "EBNF:ElementsCons", 2u) &&
                       is_expr_head(elements->expr.elems[2],
                                    "EBNF:ElementsCons", 2u) &&
                       is_expr_head(elements->expr.elems[2]->expr.elems[2],
                                    "EBNF:ElementsNil", 0u)) {
                const Atom *previous = elements->expr.elems[1];
                const Atom *body = elements->expr.elems[2]->expr.elems[1];
                if (is_expr_head(previous, "EBNF:Repetition", 5u) &&
                    previous->expr.elems[1] &&
                    previous->expr.elems[1]->kind == ATOM_SYMBOL &&
                    strcmp(atom_name_cstr(previous->expr.elems[1]), kind) ==
                        0 &&
                    is_ground_int(previous->expr.elems[2]) &&
                    previous->expr.elems[2]->ground.ival ==
                        frame->origin_index &&
                    atom_eq((Atom *)previous->expr.elems[3],
                            (Atom *)frame->origin_span)) {
                    Atom *chain =
                        make_expr2(arena, "EBNF:IterationsCons", (Atom *)body,
                                   (Atom *)previous->expr.elems[5]);
                    node = chain
                        ? make_expr5(arena, "EBNF:Repetition", kind_atom,
                                     occurrence,
                                     (Atom *)frame->origin_span, input, chain)
                        : NULL;
                }
            }
        }
    }
    if (!node)
        return proj_error(machine->error, machine->error_size,
                          "ebnf derivation projection: node shape does not match its helper kind and alternative");
    *frame->target = node;
    machine_pop(machine);
    return true;
}

/* ----------------------------------------------------------- source order */

static bool machine_so_map_enter(ProjMachine *machine, ProjFrame *frame) {
    const Atom *src = frame->so_src;
    if (!src || src->kind != ATOM_EXPR)
        return proj_error(machine->error, machine->error_size,
                          "ebnf derivation projection: malformed derivation for source ordering");

    if (is_expr_head(src, "EBNF:OptionalAbsent", 3u) ||
        is_expr_head(src, "EBNF:Literal", 3u) ||
        is_expr_head(src, "EBNF:Lexical", 5u) ||
        is_expr_head(src, "EBNF:ElementsNil", 0u) ||
        is_expr_head(src, "EBNF:IterationsNil", 0u) ||
        is_expr_head(src, "EBNF:DerivationsNil", 0u)) {
        *frame->target = (Atom *)src;
        machine_pop(machine);
        return true;
    }

    if (is_expr_head(src, "EBNF:Rule", 6u) ||
        is_expr_head(src, "EBNF:Group", 6u) ||
        is_expr_head(src, "EBNF:Reference", 4u) ||
        is_expr_head(src, "EBNF:OptionalPresent", 4u)) {
        uint32_t arity = is_expr_head(src, "EBNF:Rule", 6u) ||
                                 is_expr_head(src, "EBNF:Group", 6u)
                             ? 6u
                             : 4u;
        frame->so_child = arity;
        frame->so_phase = 1u;
        frame->so_slot0 = NULL;
        ProjFrame *child = machine_push(machine, FR_SO_MAP, &frame->so_slot0);
        if (!child)
            return proj_error(machine->error, machine->error_size,
                              "ebnf derivation projection: frame allocation failed");
        child->so_src = src->expr.elems[arity];
        return true;
    }

    if (is_expr_head(src, "EBNF:ElementsCons", 2u) ||
        is_expr_head(src, "EBNF:DerivationsCons", 2u)) {
        frame->kind = FR_SO_PAIR;
        frame->pair_phase = 0u;
        frame->so_slot0 = NULL;
        frame->so_slot1 = NULL;
        ProjFrame *child = machine_push(machine, FR_SO_MAP, &frame->so_slot0);
        if (!child)
            return proj_error(machine->error, machine->error_size,
                              "ebnf derivation projection: frame allocation failed");
        child->so_src = src->expr.elems[1];
        return true;
    }

    if (is_expr_head(src, "EBNF:Repetition", 5u)) {
        const Atom *cursor = src->expr.elems[5];
        frame->kind = FR_SO_ITER;
        frame->iter_next = 0u;
        for (;;) {
            if (is_expr_head(cursor, "EBNF:IterationsNil", 0u))
                break;
            if (!is_expr_head(cursor, "EBNF:IterationsCons", 2u))
                return proj_error(machine->error, machine->error_size,
                                  "ebnf derivation projection: malformed iteration list");
            if (!vec_push(&frame->bodies, cursor->expr.elems[1]))
                return proj_error(machine->error, machine->error_size,
                                  "ebnf derivation projection: iteration collection failed");
            cursor = cursor->expr.elems[2];
        }
        if (frame->bodies.len == 0u) {
            *frame->target = (Atom *)src;
            machine_pop(machine);
            return true;
        }
        frame->mapped.items =
            calloc(frame->bodies.len, sizeof(*frame->mapped.items));
        if (!frame->mapped.items)
            return proj_error(machine->error, machine->error_size,
                              "ebnf derivation projection: iteration map allocation failed");
        frame->mapped.len = frame->bodies.len;
        frame->mapped.cap = frame->bodies.len;
        ProjFrame *child =
            machine_push(machine, FR_SO_MAP, &frame->mapped.items[0]);
        if (!child)
            return proj_error(machine->error, machine->error_size,
                              "ebnf derivation projection: frame allocation failed");
        child->so_src = frame->bodies.items[0];
        frame->iter_next = 1u;
        return true;
    }

    return proj_error(machine->error, machine->error_size,
                      "ebnf derivation projection: unrecognized derivation node for source ordering");
}

static bool machine_so_map_resume(ProjMachine *machine, ProjFrame *frame) {
    const Atom *src = frame->so_src;
    uint32_t child = frame->so_child;
    Atom *mapped = frame->so_slot0;
    if (!mapped)
        return proj_error(machine->error, machine->error_size,
                          "ebnf derivation projection: source-order child missing");
    if (mapped == src->expr.elems[child]) {
        *frame->target = (Atom *)src;
        machine_pop(machine);
        return true;
    }
    Atom *node;
    if (child == 6u)
        node = make_expr6(machine->arena,
                          atom_name_cstr(src->expr.elems[0]),
                          src->expr.elems[1], src->expr.elems[2],
                          src->expr.elems[3], src->expr.elems[4],
                          src->expr.elems[5], mapped);
    else
        node = make_expr4(machine->arena,
                          atom_name_cstr(src->expr.elems[0]),
                          src->expr.elems[1], src->expr.elems[2],
                          src->expr.elems[3], mapped);
    if (!node)
        return proj_error(machine->error, machine->error_size,
                          "ebnf derivation projection: source-order node allocation failed");
    *frame->target = node;
    machine_pop(machine);
    return true;
}

static bool machine_so_pair_step(ProjMachine *machine, ProjFrame *frame) {
    const Atom *src = frame->so_src;
    if (frame->pair_phase == 0u) {
        frame->pair_phase = 1u;
        ProjFrame *child = machine_push(machine, FR_SO_MAP, &frame->so_slot1);
        if (!child)
            return proj_error(machine->error, machine->error_size,
                              "ebnf derivation projection: frame allocation failed");
        child->so_src = src->expr.elems[2];
        return true;
    }
    Atom *first = frame->so_slot0;
    Atom *second = frame->so_slot1;
    if (!first || !second)
        return proj_error(machine->error, machine->error_size,
                          "ebnf derivation projection: source-order pair child missing");
    if (first == src->expr.elems[1] && second == src->expr.elems[2]) {
        *frame->target = (Atom *)src;
        machine_pop(machine);
        return true;
    }
    Atom *node = make_expr2(machine->arena,
                            atom_name_cstr(src->expr.elems[0]), first, second);
    if (!node)
        return proj_error(machine->error, machine->error_size,
                          "ebnf derivation projection: source-order pair allocation failed");
    *frame->target = node;
    machine_pop(machine);
    return true;
}

static bool machine_so_iter_step(ProjMachine *machine, ProjFrame *frame) {
    if (frame->iter_next < frame->bodies.len) {
        uint32_t index = frame->iter_next++;
        ProjFrame *child =
            machine_push(machine, FR_SO_MAP, &frame->mapped.items[index]);
        if (!child)
            return proj_error(machine->error, machine->error_size,
                              "ebnf derivation projection: frame allocation failed");
        child->so_src = frame->bodies.items[index];
        return true;
    }
    const Atom *src = frame->so_src;
    bool all_same = true;
    for (uint32_t index = 0u; index < frame->bodies.len; index++) {
        if (!frame->mapped.items[index])
            return proj_error(machine->error, machine->error_size,
                              "ebnf derivation projection: source-order iteration child missing");
        if (frame->mapped.items[index] != frame->bodies.items[index])
            all_same = false;
    }
    if (all_same && frame->bodies.len <= 1u) {
        *frame->target = (Atom *)src;
        machine_pop(machine);
        return true;
    }
    Atom *chain = make_expr0(machine->arena, "EBNF:IterationsNil");
    for (uint32_t index = 0u; index < frame->bodies.len && chain; index++)
        chain = make_expr2(machine->arena, "EBNF:IterationsCons",
                           frame->mapped.items[index], chain);
    if (!chain)
        return proj_error(machine->error, machine->error_size,
                          "ebnf derivation projection: iteration chain allocation failed");
    Atom *node = make_expr5(machine->arena, "EBNF:Repetition",
                            (Atom *)src->expr.elems[1], (Atom *)src->expr.elems[2],
                            (Atom *)src->expr.elems[3], (Atom *)src->expr.elems[4],
                            chain);
    if (!node)
        return proj_error(machine->error, machine->error_size,
                          "ebnf derivation projection: repetition node allocation failed");
    *frame->target = node;
    machine_pop(machine);
    return true;
}

/* ------------------------------------------------------------- root walker */

static bool machine_roots_step(ProjMachine *machine, ProjFrame *frame) {
    if (frame->roots_phase == ROOTS_WAIT_TREE) {
        if (!frame->root_scratch)
            return proj_error(machine->error, machine->error_size,
                              "ebnf derivation projection: missing projected root");
        Atom *raw = frame->root_scratch;
        frame->root_scratch = NULL;
        frame->root_final = NULL;
        frame->roots_phase = ROOTS_WAIT_SO;
        ProjFrame *so = machine_push(machine, FR_SO_MAP, &frame->root_final);
        if (!so)
            return proj_error(machine->error, machine->error_size,
                              "ebnf derivation projection: frame allocation failed");
        so->so_src = raw;
        return true;
    }
    if (frame->roots_phase == ROOTS_WAIT_SO) {
        if (!frame->root_final ||
            !vec_push(&machine->roots, frame->root_final))
            return proj_error(machine->error, machine->error_size,
                              "ebnf derivation projection: root collection failed");
        frame->root_final = NULL;
        frame->roots_phase = ROOTS_STEP;
    }
    if (!frame->trees || frame->trees->kind != ATOM_EXPR)
        return proj_error(machine->error, machine->error_size,
                          "ebnf derivation projection: CST tuple is not an expression");
    if (frame->next_root < frame->trees->expr.len) {
        const Atom *child = frame->trees->expr.elems[frame->next_root++];
        frame->root_scratch = NULL;
        frame->roots_phase = ROOTS_WAIT_TREE;
        ProjFrame *tree = machine_push(machine, FR_TREE, &frame->root_scratch);
        if (!tree)
            return proj_error(machine->error, machine->error_size,
                              "ebnf derivation projection: frame allocation failed");
        tree->name = frame->start;
        tree->tree = child;
        return true;
    }
    Atom *chain = make_expr0(machine->arena, "EBNF:DerivationsNil");
    for (uint32_t index = machine->roots.len; index > 0u && chain; index--)
        chain = make_expr2(machine->arena, "EBNF:DerivationsCons",
                           machine->roots.items[index - 1u], chain);
    if (!chain)
        return proj_error(machine->error, machine->error_size,
                          "ebnf derivation projection: derivations chain allocation failed");
    *frame->target = chain;
    machine_pop(machine);
    return true;
}

/* -------------------------------------------------------------- main loop */

bool cetta_ebnf_derivation_projection_native_v1(
    const Atom *document, const Atom *origins, const Atom *authority,
    const Atom *trees, Arena *arena, Atom **out, char *error,
    size_t error_size) {
    ProjTables tables;
    memset(&tables, 0u, sizeof(tables));
    tables.arena = arena;

    ProjMachine machine;
    memset(&machine, 0u, sizeof(machine));
    machine.arena = arena;
    machine.error = error;
    machine.error_size = error_size;
    machine.tables = &tables;

    bool ok = false;
    Atom *result = NULL;

    if (!is_expr_head(document, "bnf-v1:document", 2u)) {
        proj_error(error, error_size,
                   "ebnf derivation projection: receipt document is malformed");
        goto done;
    }
    if (!is_expr_head(authority, "bnf-v1:grammar-authority", 2u) ||
        !is_expr_head(authority->expr.elems[1], "bnf-v1:start", 1u) ||
        !is_expr_head(authority->expr.elems[2], "bnf-v1:lexical-environment",
                      1u) ||
        !text_well_formed(authority->expr.elems[1]->expr.elems[1])) {
        proj_error(error, error_size,
                   "ebnf derivation projection: receipt authority is malformed");
        goto done;
    }

    if (!prepare_definitions(&tables, document->expr.elems[1], error,
                             error_size))
        goto done;
    if (!prepare_lexicals(&tables, authority->expr.elems[2]->expr.elems[1],
                          error, error_size))
        goto done;
    if (!prepare_origins(&tables, origins, error, error_size))
        goto done;

    {
        ProjFrame *roots = machine_push(&machine, FR_ROOTS, &result);
        if (!roots) {
            proj_error(error, error_size,
                       "ebnf derivation projection: frame allocation failed");
            goto done;
        }
        roots->trees = trees;
        roots->start = authority->expr.elems[1]->expr.elems[1];
        roots->roots_phase = ROOTS_STEP;

        while (machine.frame_count > 0u) {
            ProjFrame *top = machine_top(&machine);
            bool step_ok;
            switch (top->kind) {
            case FR_ROOTS:
                step_ok = machine_roots_step(&machine, top);
                break;
            case FR_TREE:
                step_ok = machine_tree_enter(&machine, top);
                break;
            case FR_ELEMENTS:
                step_ok = top->waiting_ref
                    ? machine_elements_resume(&machine, top)
                    : machine_elements_step(&machine, top);
                break;
            case FR_FINISH:
                step_ok = machine_finish_enter(&machine, top);
                break;
            case FR_SO_MAP:
                step_ok = top->so_phase ? machine_so_map_resume(&machine, top)
                                        : machine_so_map_enter(&machine, top);
                break;
            case FR_SO_ITER:
                step_ok = machine_so_iter_step(&machine, top);
                break;
            case FR_SO_PAIR:
                step_ok = machine_so_pair_step(&machine, top);
                break;
            default:
                step_ok = proj_error(error, error_size,
                                     "ebnf derivation projection: internal frame kind");
                break;
            }
            if (!step_ok)
                goto done;
        }
        if (!result)
            proj_error(error, error_size,
                       "ebnf derivation projection: no result produced");
        else
            ok = true;
    }

done:
    while (machine.frame_count > 0u)
        machine_pop(&machine);
    free(machine.frames);
    free(machine.roots.items);
    tables_free(&tables);
    if (ok && out)
        *out = result;
    return ok;
}
