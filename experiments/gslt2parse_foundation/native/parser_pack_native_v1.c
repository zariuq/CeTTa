#include "parser_pack_native_v1.h"

#include "finite_horn_ground_term_v1.h"
#include "native_sha256.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum { PPNATIVE_V1_MAX_TERM_DEPTH = 4096u };
enum { PPNATIVE_V1_CANONICAL_LIST_MATERIALIZE_LIMIT = 3000u };

typedef struct {
    CettaLpNativeUnicodeRange *data;
    uint32_t len;
    uint32_t cap;
} PPNativeV1RangeVec;

typedef struct {
    Atom **data;
    uint32_t len;
    uint32_t cap;
} PPNativeV1AtomVec;

typedef struct {
    Atom *atom;
    char *canonical;
} PPNativeV1CanonicalAtom;

typedef struct {
    PPNativeV1CanonicalAtom *data;
    uint32_t len;
    uint32_t cap;
} PPNativeV1CanonicalAtomVec;

typedef struct {
    uint32_t *nodes;
    uint32_t len;
} PPNativeV1NodeSequence;

typedef struct {
    PPNativeV1NodeSequence *data;
    uint32_t len;
    uint32_t cap;
} PPNativeV1SequenceVec;

typedef struct {
    uint32_t *data;
    uint32_t len;
    uint32_t cap;
} PPNativeV1U32Vec;

typedef struct {
    bool ready;
    bool visiting;
    PPNativeV1CanonicalAtomVec values;
} PPNativeV1ReplayMemo;

typedef struct {
    Atom *action;
    uint32_t item_len;
} PPNativeV1ReplayProduction;

typedef struct {
    uint32_t node_index;
    PPNativeV1U32Vec dependencies;
    uint32_t next_dependency;
} PPNativeV1ReplayFrame;

typedef struct {
    PPNativeV1ReplayFrame *data;
    uint32_t len;
    uint32_t cap;
} PPNativeV1ReplayStack;

typedef enum {
    PPNATIVE_V1_REPLAY_OK = 0,
    PPNATIVE_V1_REPLAY_DEPTH_HIT = 1,
    PPNATIVE_V1_REPLAY_RESULT_HIT = 2,
    PPNATIVE_V1_REPLAY_MALFORMED = 3
} PPNativeV1ReplayStatus;

typedef struct {
    const PPABIV1Pack *pack;
    const CettaLpNativeUtf8Forest *forest;
    const PPNativeV1ForestExtension *extension;
    Arena *arena;
    PPNativeV1ReplayMemo *memo;
    uint32_t result_limit;
    PPNativeV1ReplayStatus status;
} PPNativeV1ReplayContext;

static bool ppnative_v1_extension_valid(
    const PPABIV1Pack *pack,
    const PPNativeV1ForestExtension *extension,
    char *error_buf,
    size_t error_buf_size);

static void ppnative_v1_set_error(char *buf, size_t size, const char *fmt, ...) {
    va_list args;

    if (!buf || size == 0u)
        return;
    va_start(args, fmt);
    (void)vsnprintf(buf, size, fmt, args);
    va_end(args);
}

static bool ppnative_v1_expr_head(Atom *atom,
                               const char *head,
                               CettaExprLen argument_len) {
    return atom && atom->kind == ATOM_EXPR &&
        atom->expr.len == argument_len + 1u &&
        atom_is_symbol(atom->expr.elems[0], head);
}

static bool ppnative_v1_range_push(PPNativeV1RangeVec *ranges,
                                uint32_t low,
                                uint32_t high) {
    if (low > high)
        return true;
    if (ranges->len == ranges->cap) {
        uint32_t cap = ranges->cap ? ranges->cap * 2u : 16u;
        if (cap < ranges->cap)
            return false;
        ranges->data = cetta_realloc(
            ranges->data, sizeof(*ranges->data) * cap);
        ranges->cap = cap;
    }
    ranges->data[ranges->len].low = low;
    ranges->data[ranges->len].high = high;
    ranges->len++;
    return true;
}

static int ppnative_v1_range_compare(const void *lhs, const void *rhs) {
    const CettaLpNativeUnicodeRange *left = lhs;
    const CettaLpNativeUnicodeRange *right = rhs;
    if (left->low != right->low)
        return left->low < right->low ? -1 : 1;
    if (left->high != right->high)
        return left->high < right->high ? -1 : 1;
    return 0;
}

static void ppnative_v1_ranges_normalize(PPNativeV1RangeVec *ranges) {
    uint32_t read;
    uint32_t write = 0u;

    if (ranges->len > 1u) {
        qsort(ranges->data, ranges->len, sizeof(*ranges->data),
              ppnative_v1_range_compare);
    }
    for (read = 0u; read < ranges->len; read++) {
        CettaLpNativeUnicodeRange current = ranges->data[read];
        if (write > 0u &&
            current.low <= ranges->data[write - 1u].high + 1u) {
            if (current.high > ranges->data[write - 1u].high)
                ranges->data[write - 1u].high = current.high;
        } else {
            ranges->data[write++] = current;
        }
    }
    ranges->len = write;
}

static bool ppnative_v1_except_segment(PPNativeV1RangeVec *ranges,
                                    uint32_t low,
                                    uint32_t high,
                                    const uint32_t *excluded,
                                    uint32_t excluded_len) {
    uint32_t start = low;
    uint32_t index;

    for (index = 0u; index < excluded_len; index++) {
        uint32_t point = excluded[index];
        if (point < low || point > high)
            continue;
        if (start < point && !ppnative_v1_range_push(ranges, start, point - 1u))
            return false;
        if (point == high)
            return true;
        start = point + 1u;
    }
    return ppnative_v1_range_push(ranges, start, high);
}

static bool ppnative_v1_class_ranges(const PPABIV1Pack *pack,
                                  Atom *expression,
                                  PPNativeV1RangeVec *ranges,
                                  unsigned depth,
                                  char *error_buf,
                                  size_t error_buf_size) {
    uint32_t index;
    bool found = false;

    if (depth > PPNATIVE_V1_MAX_TERM_DEPTH) {
        ppnative_v1_set_error(error_buf, error_buf_size,
                           "terminal class expression is too deep");
        return false;
    }
    if (ppnative_v1_expr_head(expression, "c-union", 2u)) {
        return ppnative_v1_class_ranges(
                   pack, expression->expr.elems[1], ranges, depth + 1u,
                   error_buf, error_buf_size) &&
               ppnative_v1_class_ranges(
                   pack, expression->expr.elems[2], ranges, depth + 1u,
                   error_buf, error_buf_size);
    }
    for (index = 0u; index < pack->class_clause_len; index++) {
        const PPABIV1ClassClause *clause = &pack->class_clauses[index];
        if (!atom_eq(expression, clause->class_identity))
            continue;
        found = true;
        if (clause->kind == PPABI_V1_CLASS_POINT) {
            if (!ppnative_v1_range_push(ranges, clause->point, clause->point))
                return false;
        } else if (!ppnative_v1_except_segment(
                       ranges, 0u, UINT32_C(0xd7ff),
                       clause->excluded_points, clause->excluded_len) ||
                   !ppnative_v1_except_segment(
                       ranges, UINT32_C(0xe000), UINT32_C(0x10ffff),
                       clause->excluded_points, clause->excluded_len)) {
            return false;
        }
    }
    if (!found) {
        ppnative_v1_set_error(error_buf, error_buf_size,
                           "terminal class expression has no clauses");
        return false;
    }
    return true;
}

bool ppnative_v1_class_ranges_build(
    const PPABIV1Pack *pack,
    Atom *class_expression,
    CettaLpNativeUnicodeRange **out_ranges,
    uint32_t *out_range_len,
    char *error_buf,
    size_t error_buf_size) {
    PPNativeV1RangeVec ranges = {0};

    if (!pack || !class_expression || !out_ranges || !out_range_len) {
        ppnative_v1_set_error(error_buf, error_buf_size,
                              "bad terminal class range arguments");
        return false;
    }
    *out_ranges = NULL;
    *out_range_len = 0u;
    if (!ppnative_v1_class_ranges(
            pack, class_expression, &ranges, 0u,
            error_buf, error_buf_size)) {
        free(ranges.data);
        return false;
    }
    ppnative_v1_ranges_normalize(&ranges);
    if (ranges.len == 0u) {
        free(ranges.data);
        ppnative_v1_set_error(error_buf, error_buf_size,
                              "terminal class normalized to empty");
        return false;
    }
    *out_ranges = ranges.data;
    *out_range_len = ranges.len;
    return true;
}

void ppnative_v1_terminal_plan_free(PPNativeV1TerminalPlan *plan) {
    uint32_t index;
    if (!plan)
        return;
    for (index = 0u; index < plan->len; index++)
        free(plan->owned_ranges ? plan->owned_ranges[index] : NULL);
    free(plan->owned_ranges);
    free(plan->terminals);
    memset(plan, 0, sizeof(*plan));
}

bool ppnative_v1_terminal_plan_build(const PPABIV1Pack *pack,
                                         PPNativeV1TerminalPlan *plan,
                                         char *error_buf,
                                         size_t error_buf_size) {
    uint32_t index;

    memset(plan, 0, sizeof(*plan));
    plan->len = pack->terminal_len;
    plan->terminals = calloc(plan->len ? plan->len : 1u,
                             sizeof(*plan->terminals));
    plan->owned_ranges = calloc(plan->len ? plan->len : 1u,
                                sizeof(*plan->owned_ranges));
    if (!plan->terminals || !plan->owned_ranges)
        return false;
    for (index = 0u; index < pack->terminal_len; index++) {
        const PPABIV1Terminal *source = &pack->terminals[index];
        CettaLpNativeUtf8Terminal *target = &plan->terminals[index];
        target->terminal_id = source->dense_id;
        if (source->kind == PPABI_V1_TERMINAL_ANY) {
            target->kind = CETTA_LP_NATIVE_UTF8_TERMINAL_ANY;
        } else if (source->kind == PPABI_V1_TERMINAL_EOF) {
            target->kind = CETTA_LP_NATIVE_UTF8_TERMINAL_EOF;
        } else if (source->kind == PPABI_V1_TERMINAL_CHAR) {
            target->kind = CETTA_LP_NATIVE_UTF8_TERMINAL_SCALAR;
            target->scalar = source->codepoint;
        } else {
            CettaLpNativeUnicodeRange *ranges = NULL;
            uint32_t range_len = 0u;
            if (!ppnative_v1_class_ranges_build(
                    pack, source->class_expression,
                    &ranges, &range_len,
                    error_buf, error_buf_size)) {
                return false;
            }
            target->kind = CETTA_LP_NATIVE_UTF8_TERMINAL_RANGES;
            target->ranges = ranges;
            target->range_len = range_len;
            plan->owned_ranges[index] = ranges;
        }
    }
    return true;
}

bool ppnative_v1_grammar_build(const PPABIV1Pack *pack,
                                   CettaLpNativeGrammar *grammar) {
    uint32_t production_index;

    cetta_lp_native_grammar_init(grammar);
    grammar->productions = calloc(
        pack->production_len ? pack->production_len : 1u,
        sizeof(*grammar->productions));
    if (!grammar->productions)
        return false;
    grammar->production_len = pack->production_len;
    for (production_index = 0u;
         production_index < pack->production_len; production_index++) {
        const PPABIV1Production *source =
            &pack->productions[production_index];
        CettaLpNativeProduction *target =
            &grammar->productions[production_index];
        uint32_t item_index;
        target->label = source->dense_id;
        target->lhs = source->lhs_state_id;
        target->rhs_len = source->item_len;
        if (source->item_len > 0u) {
            target->rhs = calloc(source->item_len, sizeof(*target->rhs));
            if (!target->rhs)
                return false;
        }
        for (item_index = 0u; item_index < source->item_len; item_index++) {
            target->rhs[item_index].kind =
                source->items[item_index].kind == PPABI_V1_ITEM_TERMINAL
                ? CETTA_LP_NATIVE_SYMBOL_TM
                : CETTA_LP_NATIVE_SYMBOL_HL;
            target->rhs[item_index].name = source->items[item_index].dense_id;
            target->rhs[item_index].scope = 0u;
        }
    }
    return true;
}

int32_t ppnative_v1_state_find(const PPABIV1Pack *pack,
                                   const Atom *state) {
    uint32_t index;
    for (index = 0u; index < pack->state_len; index++) {
        if (atom_eq(pack->states[index].identity, (Atom *)state))
            return (int32_t)index;
    }
    return -1;
}

int32_t ppnative_v1_state_find_extended(
    const PPABIV1Pack *pack,
    const PPNativeV1ForestExtension *extension,
    const Atom *state) {
    int32_t base;
    uint32_t index;

    if (!pack || !state)
        return -1;
    base = ppnative_v1_state_find(pack, state);
    if (base >= 0 || !extension)
        return base;
    for (index = 0u; index < extension->state_len; index++) {
        if (atom_eq(extension->states[index].identity, (Atom *)state))
            return (int32_t)extension->states[index].state_id;
    }
    return -1;
}

static Atom *ppnative_v1_copy(Arena *arena, Atom *atom) {
    return atom_deep_copy(arena, atom);
}

static Atom *ppnative_v1_expr(Arena *arena,
                           const char *head,
                           Atom **arguments,
                           uint32_t argument_len) {
    Atom **items = arena_alloc(
        arena, sizeof(*items) * ((size_t)argument_len + 1u));
    uint32_t index;
    items[0] = atom_symbol(arena, head);
    for (index = 0u; index < argument_len; index++)
        items[index + 1u] = arguments[index];
    return atom_expr(arena, items, argument_len + 1u);
}

static Atom *ppnative_v1_list(Arena *arena, Atom **items, uint32_t len) {
    Atom *list = atom_symbol(arena, "nil");
    while (len > 0u) {
        Atom *arguments[2];
        len--;
        arguments[0] = items[len];
        arguments[1] = list;
        list = ppnative_v1_expr(arena, "cons", arguments, 2u);
    }
    return list;
}

static char *ppnative_v1_render(Atom *atom) {
    uint8_t *bytes = NULL;
    size_t len = 0u;
    if (!fh_ground_term_v1_render(atom, &bytes, &len, NULL, 0u))
        return NULL;
    return (char *)bytes;
}

typedef struct {
    const PPABIV1Pack *pack;
    const Atom *start_state;
    uint32_t start_state_id;
    uint32_t state_len;
    uint32_t terminal_len;
    uint32_t production_len;
    uint32_t class_clause_len;
    uint32_t derivation_len;
    uint32_t table_build_count;
    char source_digest[65];
    char compiler_digest[65];
    char environment_digest[65];
    char pack_digest[65];
    char *start_canonical;
    PPNativeV1TerminalPlan terminal_plan;
    CettaLpNativeGllPrepared gll;
    CettaLpNativeGlrPrepared glr;
} PPNativeV1PreparedImpl;

static bool ppnative_v1_digest_is_frozen(const char *digest) {
    uint32_t index;

    if (!digest || digest[64] != '\0')
        return false;
    for (index = 0u; index < 64u; index++) {
        char ch = digest[index];
        if (!((ch >= '0' && ch <= '9') ||
              (ch >= 'a' && ch <= 'f'))) {
            return false;
        }
    }
    return true;
}

typedef struct {
    uint32_t *start_state_ids;
    CettaLpNativeGllPrepared *gll;
    CettaLpNativeGlrPrepared *glr;
    uint32_t start_state_len;
    uint32_t table_build_count;
    char binding_digest[65];
    char grammar_digest[65];
} PPNativeV1PreparedStartFamilyImpl;

static void ppnative_v1_family_sha_u32(
    CettaNativeSha256 *sha, uint32_t value) {
    const uint8_t bytes[4] = {
        (uint8_t)(value >> 24u),
        (uint8_t)(value >> 16u),
        (uint8_t)(value >> 8u),
        (uint8_t)value,
    };
    cetta_native_sha256_update(sha, bytes, sizeof(bytes));
}

static bool ppnative_v1_grammar_digest(
    const CettaLpNativeGrammar *grammar,
    char out[65],
    char *error_buf,
    size_t error_buf_size) {
    static const char prefix[] = "CettaLpNativeGrammarV1\n";
    CettaNativeSha256 sha;
    uint32_t index;

    if (!grammar ||
        (grammar->production_len > 0u && !grammar->productions) ||
        (grammar->var_len > 0u && !grammar->vars) ||
        (grammar->lex_len > 0u && !grammar->lexes) ||
        (grammar->entry_len > 0u && !grammar->entries)) {
        ppnative_v1_set_error(
            error_buf, error_buf_size,
            "cannot digest a malformed native grammar");
        return false;
    }
    cetta_native_sha256_init(&sha);
    cetta_native_sha256_update(
        &sha, (const uint8_t *)prefix, sizeof(prefix) - 1u);
    ppnative_v1_family_sha_u32(&sha, grammar->production_len);
    for (index = 0u; index < grammar->production_len; index++) {
        const CettaLpNativeProduction *production =
            &grammar->productions[index];
        uint32_t item_index;
        if (production->rhs_len > 0u && !production->rhs) {
            ppnative_v1_set_error(
                error_buf, error_buf_size,
                "cannot digest a malformed native production");
            return false;
        }
        ppnative_v1_family_sha_u32(&sha, production->label);
        ppnative_v1_family_sha_u32(&sha, production->lhs);
        ppnative_v1_family_sha_u32(&sha, production->rhs_len);
        for (item_index = 0u; item_index < production->rhs_len;
             item_index++) {
            const CettaLpNativeSymbol *item =
                &production->rhs[item_index];
            ppnative_v1_family_sha_u32(&sha, (uint32_t)item->kind);
            ppnative_v1_family_sha_u32(&sha, item->name);
            ppnative_v1_family_sha_u32(&sha, item->scope);
        }
    }
    ppnative_v1_family_sha_u32(&sha, grammar->var_len);
    for (index = 0u; index < grammar->var_len; index++) {
        ppnative_v1_family_sha_u32(&sha, grammar->vars[index].atom);
        ppnative_v1_family_sha_u32(&sha, grammar->vars[index].nt);
    }
    ppnative_v1_family_sha_u32(&sha, grammar->lex_len);
    for (index = 0u; index < grammar->lex_len; index++) {
        ppnative_v1_family_sha_u32(&sha, grammar->lexes[index].klass);
        ppnative_v1_family_sha_u32(&sha, grammar->lexes[index].nt);
    }
    ppnative_v1_family_sha_u32(&sha, grammar->entry_len);
    for (index = 0u; index < grammar->entry_len; index++) {
        ppnative_v1_family_sha_u32(
            &sha, (uint32_t)grammar->entries[index].kind);
        ppnative_v1_family_sha_u32(&sha, grammar->entries[index].index);
    }
    ppnative_v1_family_sha_u32(&sha, grammar->binder_hole_count);
    cetta_native_sha256_finish_hex(&sha, out);
    return true;
}

static int ppnative_v1_u32_compare(const void *lhs, const void *rhs) {
    uint32_t left = *(const uint32_t *)lhs;
    uint32_t right = *(const uint32_t *)rhs;
    return left < right ? -1 : left > right ? 1 : 0;
}

static void ppnative_v1_prepared_start_family_impl_free(
    PPNativeV1PreparedStartFamilyImpl *implementation) {
    uint32_t index;

    if (!implementation)
        return;
    for (index = 0u; index < implementation->start_state_len; index++) {
        cetta_lp_native_glr_prepared_free(&implementation->glr[index]);
        cetta_lp_native_gll_prepared_free(&implementation->gll[index]);
    }
    free(implementation->glr);
    free(implementation->gll);
    free(implementation->start_state_ids);
    memset(implementation, 0, sizeof(*implementation));
}

static bool ppnative_v1_prepared_start_family_binding_equal(
    const PPNativeV1PreparedStartFamilyImpl *implementation,
    const char *binding_digest,
    const char *grammar_digest,
    const uint32_t *start_state_ids,
    uint32_t start_state_len) {
    return implementation && binding_digest && grammar_digest &&
        implementation->start_state_len == start_state_len &&
        strcmp(implementation->binding_digest, binding_digest) == 0 &&
        strcmp(implementation->grammar_digest, grammar_digest) == 0 &&
        memcmp(implementation->start_state_ids, start_state_ids,
               sizeof(*start_state_ids) * (size_t)start_state_len) == 0;
}

void ppnative_v1_prepared_start_family_init(
    PPNativeV1PreparedStartFamily *family) {
    if (family)
        family->implementation = NULL;
}

void ppnative_v1_prepared_start_family_free(
    PPNativeV1PreparedStartFamily *family) {
    PPNativeV1PreparedStartFamilyImpl *implementation;

    if (!family)
        return;
    implementation = family->implementation;
    if (implementation) {
        ppnative_v1_prepared_start_family_impl_free(implementation);
        free(implementation);
    }
    family->implementation = NULL;
}

bool ppnative_v1_prepared_start_family_prepare(
    PPNativeV1PreparedStartFamily *family,
    const CettaLpNativeGrammar *grammar,
    const char *binding_digest,
    const uint32_t *start_state_ids,
    uint32_t start_state_len,
    char *error_buf,
    size_t error_buf_size) {
    PPNativeV1PreparedStartFamilyImpl *previous;
    PPNativeV1PreparedStartFamilyImpl *implementation = NULL;
    uint32_t *canonical_starts = NULL;
    uint32_t canonical_len = 0u;
    uint32_t index;
    uint32_t build_count;
    char grammar_digest[65] = {0};
    bool ok = false;

    if (error_buf && error_buf_size > 0u)
        error_buf[0] = '\0';
    if (!family || !grammar || !binding_digest ||
        !ppnative_v1_digest_is_frozen(binding_digest) ||
        start_state_len == 0u || !start_state_ids ||
        (size_t)start_state_len > SIZE_MAX / sizeof(*canonical_starts) ||
        !ppnative_v1_grammar_digest(
            grammar, grammar_digest, error_buf, error_buf_size)) {
        if (!error_buf || error_buf[0] == '\0') {
            ppnative_v1_set_error(
                error_buf, error_buf_size,
                "bad prepared start-state family inputs");
        }
        goto done;
    }
    canonical_starts = malloc(
        sizeof(*canonical_starts) * (size_t)start_state_len);
    if (!canonical_starts)
        goto done;
    memcpy(canonical_starts, start_state_ids,
           sizeof(*canonical_starts) * (size_t)start_state_len);
    qsort(canonical_starts, start_state_len,
          sizeof(*canonical_starts), ppnative_v1_u32_compare);
    for (index = 0u; index < start_state_len; index++) {
        if (canonical_len == 0u ||
            canonical_starts[index] != canonical_starts[canonical_len - 1u]) {
            canonical_starts[canonical_len++] = canonical_starts[index];
        }
    }
    previous = family->implementation;
    if (ppnative_v1_prepared_start_family_binding_equal(
            previous, binding_digest, grammar_digest,
            canonical_starts, canonical_len)) {
        ok = true;
        goto done;
    }
    build_count = previous ? previous->table_build_count : 0u;
    if (build_count == UINT32_MAX) {
        ppnative_v1_set_error(
            error_buf, error_buf_size,
            "prepared start-state family build count overflow");
        goto done;
    }
    implementation = calloc(1u, sizeof(*implementation));
    if (!implementation)
        goto done;
    implementation->start_state_ids = malloc(
        sizeof(*implementation->start_state_ids) * (size_t)canonical_len);
    implementation->gll = calloc(canonical_len, sizeof(*implementation->gll));
    implementation->glr = calloc(canonical_len, sizeof(*implementation->glr));
    if (!implementation->start_state_ids || !implementation->gll ||
        !implementation->glr) {
        goto done;
    }
    memcpy(implementation->start_state_ids, canonical_starts,
           sizeof(*canonical_starts) * (size_t)canonical_len);
    implementation->start_state_len = canonical_len;
    implementation->table_build_count = build_count + 1u;
    memcpy(implementation->binding_digest, binding_digest, 65u);
    memcpy(implementation->grammar_digest, grammar_digest, 65u);
    for (index = 0u; index < canonical_len; index++) {
        cetta_lp_native_gll_prepared_init(&implementation->gll[index]);
        cetta_lp_native_glr_prepared_init(&implementation->glr[index]);
    }
    for (index = 0u; index < canonical_len; index++) {
        if (!cetta_lp_native_gll_prepare(
                &implementation->gll[index], grammar,
                implementation->start_state_ids[index],
                error_buf, error_buf_size) ||
            !cetta_lp_native_glr_prepare(
                &implementation->glr[index], grammar,
                implementation->start_state_ids[index],
                error_buf, error_buf_size)) {
            goto done;
        }
    }
    ppnative_v1_prepared_start_family_free(family);
    family->implementation = implementation;
    implementation = NULL;
    ok = true;

done:
    free(canonical_starts);
    if (implementation) {
        ppnative_v1_prepared_start_family_impl_free(implementation);
        free(implementation);
    }
    if (!ok && error_buf && error_buf_size > 0u && error_buf[0] == '\0') {
        ppnative_v1_set_error(
            error_buf, error_buf_size,
            "failed to prepare start-state parser family");
    }
    return ok;
}

bool ppnative_v1_prepared_start_family_lookup(
    const PPNativeV1PreparedStartFamily *family,
    const char *binding_digest,
    uint32_t start_state_id,
    const CettaLpNativeGllPrepared **gll,
    const CettaLpNativeGlrPrepared **glr,
    char *error_buf,
    size_t error_buf_size) {
    const PPNativeV1PreparedStartFamilyImpl *implementation =
        family ? family->implementation : NULL;
    uint32_t low = 0u;
    uint32_t high = implementation ? implementation->start_state_len : 0u;

    if (error_buf && error_buf_size > 0u)
        error_buf[0] = '\0';
    if (gll)
        *gll = NULL;
    if (glr)
        *glr = NULL;
    if (!implementation || !binding_digest || !gll || !glr ||
        strcmp(implementation->binding_digest, binding_digest) != 0) {
        ppnative_v1_set_error(
            error_buf, error_buf_size,
            "prepared start-state family binding is absent or stale");
        return false;
    }
    while (low < high) {
        uint32_t middle = low + (high - low) / 2u;
        uint32_t candidate = implementation->start_state_ids[middle];
        if (candidate < start_state_id) {
            low = middle + 1u;
        } else {
            high = middle;
        }
    }
    if (low >= implementation->start_state_len ||
        implementation->start_state_ids[low] != start_state_id) {
        ppnative_v1_set_error(
            error_buf, error_buf_size,
            "prepared start-state family omits the requested state");
        return false;
    }
    *gll = &implementation->gll[low];
    *glr = &implementation->glr[low];
    return true;
}

uint32_t ppnative_v1_prepared_start_family_len(
    const PPNativeV1PreparedStartFamily *family) {
    const PPNativeV1PreparedStartFamilyImpl *implementation =
        family ? family->implementation : NULL;
    return implementation ? implementation->start_state_len : 0u;
}

uint32_t ppnative_v1_prepared_start_family_build_count(
    const PPNativeV1PreparedStartFamily *family) {
    const PPNativeV1PreparedStartFamilyImpl *implementation =
        family ? family->implementation : NULL;
    return implementation ? implementation->table_build_count : 0u;
}

static void ppnative_v1_prepared_impl_free(
    PPNativeV1PreparedImpl *implementation) {
    if (!implementation)
        return;
    ppnative_v1_terminal_plan_free(&implementation->terminal_plan);
    cetta_lp_native_gll_prepared_free(&implementation->gll);
    cetta_lp_native_glr_prepared_free(&implementation->glr);
    free(implementation->start_canonical);
    memset(implementation, 0, sizeof(*implementation));
}

static bool ppnative_v1_prepared_binding_equal(
    const PPNativeV1PreparedImpl *implementation,
    const PPABIV1Pack *pack,
    uint32_t start_state_id,
    const char *start_canonical) {
    return implementation && pack && start_canonical &&
        implementation->start_state_id == start_state_id &&
        implementation->state_len == pack->state_len &&
        implementation->terminal_len == pack->terminal_len &&
        implementation->production_len == pack->production_len &&
        implementation->class_clause_len == pack->class_clause_len &&
        implementation->derivation_len == pack->derivation_len &&
        strcmp(implementation->source_digest, pack->source_digest) == 0 &&
        strcmp(implementation->compiler_digest, pack->compiler_digest) == 0 &&
        strcmp(implementation->environment_digest,
               pack->environment_digest) == 0 &&
        strcmp(implementation->pack_digest, pack->pack_digest) == 0 &&
        strcmp(implementation->start_canonical, start_canonical) == 0;
}

void ppnative_v1_prepared_init(PPNativeV1Prepared *prepared) {
    if (!prepared)
        return;
    prepared->implementation = NULL;
}

void ppnative_v1_prepared_free(PPNativeV1Prepared *prepared) {
    PPNativeV1PreparedImpl *implementation;

    if (!prepared)
        return;
    implementation = prepared->implementation;
    if (implementation) {
        ppnative_v1_prepared_impl_free(implementation);
        free(implementation);
    }
    prepared->implementation = NULL;
}

bool ppnative_v1_prepare(PPNativeV1Prepared *prepared,
                         const PPABIV1Pack *pack,
                         const Atom *start_state,
                         char *error_buf,
                         size_t error_buf_size) {
    PPNativeV1PreparedImpl *previous;
    PPNativeV1PreparedImpl *implementation = NULL;
    CettaLpNativeGrammar grammar;
    char closure_error[512] = {0};
    char *start_canonical = NULL;
    int32_t start_state_id;
    uint32_t build_count;
    bool ok = false;

    cetta_lp_native_grammar_init(&grammar);
    if (error_buf && error_buf_size > 0u)
        error_buf[0] = '\0';
    if (!prepared || !pack || !start_state ||
        !ppnative_v1_digest_is_frozen(pack->source_digest) ||
        !ppnative_v1_digest_is_frozen(pack->compiler_digest) ||
        !ppnative_v1_digest_is_frozen(pack->environment_digest) ||
        !ppnative_v1_digest_is_frozen(pack->pack_digest)) {
        ppnative_v1_set_error(
            error_buf, error_buf_size,
            "bad prepared ParserPack arguments or provenance");
        goto done;
    }
    start_state_id = ppnative_v1_state_find(pack, start_state);
    if (start_state_id < 0 || !ppabi_v1_pack_start_is_closed(
            pack, start_state, closure_error, sizeof(closure_error))) {
        ppnative_v1_set_error(
            error_buf, error_buf_size, "%s",
            closure_error[0] ? closure_error : "start state is absent");
        goto done;
    }
    start_canonical = ppnative_v1_render((Atom *)start_state);
    if (!start_canonical) {
        ppnative_v1_set_error(
            error_buf, error_buf_size,
            "failed to canonicalize prepared ParserPack start state");
        goto done;
    }
    previous = prepared->implementation;
    if (ppnative_v1_prepared_binding_equal(
            previous, pack, (uint32_t)start_state_id,
            start_canonical)) {
        previous->pack = pack;
        previous->start_state = start_state;
        free(start_canonical);
        start_canonical = NULL;
        ok = true;
        goto done;
    }
    build_count = previous ? previous->table_build_count : 0u;
    if (build_count == UINT32_MAX) {
        ppnative_v1_set_error(
            error_buf, error_buf_size,
            "prepared ParserPack table build count overflow");
        goto done;
    }
    implementation = calloc(1u, sizeof(*implementation));
    if (!implementation) {
        ppnative_v1_set_error(
            error_buf, error_buf_size,
            "failed to allocate prepared ParserPack owner");
        goto done;
    }
    cetta_lp_native_gll_prepared_init(&implementation->gll);
    cetta_lp_native_glr_prepared_init(&implementation->glr);
    implementation->pack = pack;
    implementation->start_state = start_state;
    implementation->start_state_id = (uint32_t)start_state_id;
    implementation->state_len = pack->state_len;
    implementation->terminal_len = pack->terminal_len;
    implementation->production_len = pack->production_len;
    implementation->class_clause_len = pack->class_clause_len;
    implementation->derivation_len = pack->derivation_len;
    implementation->table_build_count = build_count + 1u;
    memcpy(implementation->source_digest, pack->source_digest, 65u);
    memcpy(implementation->compiler_digest, pack->compiler_digest, 65u);
    memcpy(implementation->environment_digest,
           pack->environment_digest, 65u);
    memcpy(implementation->pack_digest, pack->pack_digest, 65u);
    implementation->start_canonical = start_canonical;
    start_canonical = NULL;
    if (!ppnative_v1_terminal_plan_build(
            pack, &implementation->terminal_plan,
            error_buf, error_buf_size) ||
        !ppnative_v1_grammar_build(pack, &grammar) ||
        !cetta_lp_native_gll_prepare(
            &implementation->gll, &grammar,
            implementation->start_state_id,
            error_buf, error_buf_size) ||
        !cetta_lp_native_glr_prepare(
            &implementation->glr, &grammar,
            implementation->start_state_id,
            error_buf, error_buf_size)) {
        if (!error_buf || error_buf[0] == '\0') {
            ppnative_v1_set_error(
                error_buf, error_buf_size,
                "failed to prepare ParserPack recognition tables");
        }
        goto done;
    }
    ppnative_v1_prepared_free(prepared);
    prepared->implementation = implementation;
    implementation = NULL;
    ok = true;

done:
    free(start_canonical);
    cetta_lp_native_grammar_free(&grammar);
    if (implementation) {
        ppnative_v1_prepared_impl_free(implementation);
        free(implementation);
    }
    return ok;
}

bool ppnative_v1_prepared_view(
    const PPNativeV1Prepared *prepared,
    PPNativeV1PreparedView *view,
    char *error_buf,
    size_t error_buf_size) {
    const PPNativeV1PreparedImpl *implementation =
        prepared ? prepared->implementation : NULL;
    char *start_canonical = NULL;
    int32_t start_state_id;
    bool ok = false;

    if (error_buf && error_buf_size > 0u)
        error_buf[0] = '\0';
    if (view)
        memset(view, 0, sizeof(*view));
    if (!implementation || !view || !implementation->pack ||
        !implementation->start_state) {
        ppnative_v1_set_error(
            error_buf, error_buf_size,
            "prepared ParserPack is not ready");
        goto done;
    }
    start_state_id = ppnative_v1_state_find(
        implementation->pack, implementation->start_state);
    start_canonical = ppnative_v1_render(
        (Atom *)implementation->start_state);
    if (start_state_id < 0 || !start_canonical ||
        !ppnative_v1_prepared_binding_equal(
            implementation, implementation->pack,
            (uint32_t)start_state_id, start_canonical)) {
        ppnative_v1_set_error(
            error_buf, error_buf_size,
            "stale prepared ParserPack binding");
        goto done;
    }
    view->pack = implementation->pack;
    view->start_state = implementation->start_state;
    view->start_state_id = implementation->start_state_id;
    view->terminal_plan = &implementation->terminal_plan;
    view->gll = &implementation->gll;
    view->glr = &implementation->glr;
    ok = true;

done:
    free(start_canonical);
    return ok;
}

uint32_t ppnative_v1_prepared_table_build_count(
    const PPNativeV1Prepared *prepared) {
    const PPNativeV1PreparedImpl *implementation =
        prepared ? prepared->implementation : NULL;
    return implementation ? implementation->table_build_count : 0u;
}

static int ppnative_v1_canonical_atom_compare(const void *lhs,
                                           const void *rhs) {
    const PPNativeV1CanonicalAtom *left = lhs;
    const PPNativeV1CanonicalAtom *right = rhs;
    return strcmp(left->canonical, right->canonical);
}

static void ppnative_v1_canonical_entries_free(
    PPNativeV1CanonicalAtom *entries,
    uint32_t len) {
    uint32_t index;

    if (!entries)
        return;
    for (index = 0u; index < len; index++)
        free(entries[index].canonical);
    free(entries);
}

static bool ppnative_v1_canonical_entries_build(
    Atom **atoms,
    uint32_t len,
    bool require_unique,
    PPNativeV1CanonicalAtom **out,
    char *error_buf,
    size_t error_buf_size) {
    PPNativeV1CanonicalAtom *entries = NULL;
    uint32_t index;

    *out = NULL;
    if (len > 0u) {
        entries = calloc(len, sizeof(*entries));
        if (!entries)
            goto done;
    }
    for (index = 0u; index < len; index++) {
        entries[index].atom = atoms[index];
        entries[index].canonical = ppnative_v1_render(atoms[index]);
        if (!entries[index].canonical)
            goto done;
    }
    if (len > 1u)
        qsort(entries, len, sizeof(*entries), ppnative_v1_canonical_atom_compare);
    for (index = 0u; index < len; index++) {
        if (require_unique && index > 0u &&
            strcmp(entries[index - 1u].canonical,
                   entries[index].canonical) == 0) {
            ppnative_v1_set_error(error_buf, error_buf_size,
                               "neutral forest contains a duplicate term");
            goto done;
        }
    }
    *out = entries;
    return true;

done:
    ppnative_v1_canonical_entries_free(entries, len);
    return false;
}

static Atom *ppnative_v1_canonical_entries_list(
    Arena *arena,
    const PPNativeV1CanonicalAtom *entries,
    uint32_t len) {
    Atom **sorted = NULL;
    Atom *result;
    uint32_t index;

    if (len > 0u) {
        sorted = malloc(sizeof(*sorted) * len);
        if (!sorted)
            return NULL;
    }
    for (index = 0u; index < len; index++)
        sorted[index] = entries[index].atom;
    result = ppnative_v1_list(arena, sorted, len);
    free(sorted);
    return result;
}

static void ppnative_v1_sha_text(CettaNativeSha256 *sha,
                                 const char *text) {
    cetta_native_sha256_update(
        sha, (const uint8_t *)text, strlen(text));
}

static void ppnative_v1_sha_canonical_list(
    CettaNativeSha256 *sha,
    const PPNativeV1CanonicalAtom *entries,
    uint32_t len) {
    uint32_t index;

    for (index = 0u; index < len; index++) {
        ppnative_v1_sha_text(sha, "(cons ");
        ppnative_v1_sha_text(sha, entries[index].canonical);
        ppnative_v1_sha_text(sha, " ");
    }
    ppnative_v1_sha_text(sha, "nil");
    for (index = 0u; index < len; index++)
        ppnative_v1_sha_text(sha, ")");
}

static bool ppnative_v1_forest_digest(
    const Atom *start_state,
    uint32_t scalar_len,
    const PPNativeV1CanonicalAtom *roots,
    uint32_t root_len,
    const PPNativeV1CanonicalAtom *nodes,
    uint32_t node_len,
    const PPNativeV1CanonicalAtom *choices,
    uint32_t choice_len,
    char out[65]) {
    CettaNativeSha256 sha;
    char scalar[32];
    char *start = ppnative_v1_render((Atom *)start_state);
    int scalar_text_len;

    if (!start)
        return false;
    scalar_text_len = snprintf(scalar, sizeof(scalar), "%u", scalar_len);
    if (scalar_text_len <= 0 || (size_t)scalar_text_len >= sizeof(scalar)) {
        free(start);
        return false;
    }
    cetta_native_sha256_init(&sha);
    ppnative_v1_sha_text(&sha, "(pf-v1 ");
    ppnative_v1_sha_text(&sha, start);
    ppnative_v1_sha_text(&sha, " ");
    ppnative_v1_sha_text(&sha, scalar);
    ppnative_v1_sha_text(&sha, " ");
    ppnative_v1_sha_canonical_list(&sha, roots, root_len);
    ppnative_v1_sha_text(&sha, " ");
    ppnative_v1_sha_canonical_list(&sha, nodes, node_len);
    ppnative_v1_sha_text(&sha, " ");
    ppnative_v1_sha_canonical_list(&sha, choices, choice_len);
    ppnative_v1_sha_text(&sha, ")");
    cetta_native_sha256_finish_hex(&sha, out);
    free(start);
    return true;
}

static const PPNativeV1ProductionExtension *
ppnative_v1_production_extension_find(
    const PPABIV1Pack *pack,
    const PPNativeV1ForestExtension *extension,
    uint32_t production_index) {
    uint32_t low = 0u;
    uint32_t high;

    if (!extension || production_index < pack->production_len)
        return NULL;
    high = extension->production_len;
    while (low < high) {
        uint32_t middle = low + (high - low) / 2u;
        uint32_t candidate =
            extension->productions[middle].production_id;
        if (candidate < production_index)
            low = middle + 1u;
        else
            high = middle;
    }
    if (low >= extension->production_len ||
        extension->productions[low].production_id != production_index) {
        return NULL;
    }
    return &extension->productions[low];
}

static Atom *ppnative_v1_production_label(
    Arena *arena,
    const PPABIV1Pack *pack,
    const PPNativeV1ForestExtension *extension,
    uint32_t production_index) {
    Atom *identity;

    if (production_index < pack->production_len) {
        identity = pack->productions[production_index].identity;
    } else {
        const PPNativeV1ProductionExtension *production =
            ppnative_v1_production_extension_find(
                pack, extension, production_index);
        if (!production)
            return NULL;
        identity = production->identity;
    }
    if (!ppnative_v1_expr_head(identity, "pp-production", 4u))
        return NULL;
    return ppnative_v1_copy(arena, identity->expr.elems[1]);
}

static Atom *ppnative_v1_state_identity(
    const PPABIV1Pack *pack,
    const PPNativeV1ForestExtension *extension,
    uint32_t state_id) {
    uint32_t low = 0u;
    uint32_t high;

    if (state_id < pack->state_len)
        return pack->states[state_id].identity;
    if (!extension)
        return NULL;
    high = extension->state_len;
    while (low < high) {
        uint32_t middle = low + (high - low) / 2u;
        uint32_t candidate = extension->states[middle].state_id;
        if (candidate < state_id)
            low = middle + 1u;
        else
            high = middle;
    }
    if (low >= extension->state_len ||
        extension->states[low].state_id != state_id) {
        return NULL;
    }
    return extension->states[low].identity;
}

static Atom *ppnative_v1_terminal_identity(
    const PPABIV1Pack *pack,
    const PPNativeV1ForestExtension *extension,
    uint32_t terminal_id) {
    uint32_t low;
    uint32_t high;

    if (terminal_id < pack->terminal_len)
        return pack->terminals[terminal_id].identity;
    if (!extension)
        return NULL;
    low = 0u;
    high = extension->terminal_len;
    while (low < high) {
        uint32_t middle = low + (high - low) / 2u;
        uint32_t candidate = extension->terminals[middle].terminal_id;
        if (candidate < terminal_id) {
            low = middle + 1u;
        } else {
            high = middle;
        }
    }
    if (low >= extension->terminal_len ||
        extension->terminals[low].terminal_id != terminal_id) {
        return NULL;
    }
    return extension->terminals[low].identity;
}

static Atom *ppnative_v1_forest_node_atom(
    Arena *arena,
    const PPABIV1Pack *pack,
    const PPNativeV1ForestExtension *extension,
    const CettaLpNativeUtf8Forest *forest,
    uint32_t node_index) {
    const CettaLpNativeUtf8ForestNode *node;
    Atom *arguments[4];

    if (node_index >= forest->node_len)
        return NULL;
    node = &forest->nodes[node_index];
    if (node->kind == CETTA_LP_NATIVE_UTF8_FOREST_SYMBOL) {
        Atom *identity = ppnative_v1_state_identity(
            pack, extension, node->symbol_id);
        if (!identity)
            return NULL;
        arguments[0] = ppnative_v1_copy(
            arena, identity);
        arguments[1] = atom_int(arena, node->scalar_left);
        arguments[2] = atom_int(arena, node->scalar_right);
        return ppnative_v1_expr(arena, "pf-symbol", arguments, 3u);
    }
    if (node->kind == CETTA_LP_NATIVE_UTF8_FOREST_INTERMEDIATE) {
        arguments[0] = ppnative_v1_production_label(
            arena, pack, extension, node->production_index);
        arguments[1] = atom_int(arena, node->dot);
        arguments[2] = atom_int(arena, node->scalar_left);
        arguments[3] = atom_int(arena, node->scalar_right);
        if (!arguments[0])
            return NULL;
        return ppnative_v1_expr(arena, "pf-intermediate", arguments, 4u);
    }
    if (node->kind == CETTA_LP_NATIVE_UTF8_FOREST_TERM) {
        Atom *identity;
        Atom *value;
        identity = ppnative_v1_terminal_identity(
            pack, extension, node->symbol_id);
        if (!identity)
            return NULL;
        if (node->terminal_value_kind ==
            CETTA_LP_NATIVE_UTF8_TERMINAL_VALUE_EOF) {
            value = atom_symbol(arena, "eof");
        } else if (node->terminal_value_kind ==
                   CETTA_LP_NATIVE_UTF8_TERMINAL_VALUE_SCALAR) {
            Atom *cp_argument = atom_int(arena, node->terminal_scalar);
            value = ppnative_v1_expr(arena, "cp", &cp_argument, 1u);
        } else if (node->terminal_value_kind ==
                   CETTA_LP_NATIVE_UTF8_TERMINAL_VALUE_WITNESS) {
            Atom *witness = atom_int(arena, node->terminal_witness_id);
            value = ppnative_v1_expr(arena, "pf-witness", &witness, 1u);
        } else {
            return NULL;
        }
        arguments[0] = ppnative_v1_copy(
            arena, identity);
        arguments[1] = value;
        arguments[2] = atom_int(arena, node->scalar_left);
        arguments[3] = atom_int(arena, node->scalar_right);
        return ppnative_v1_expr(arena, "pf-terminal", arguments, 4u);
    }
    arguments[0] = atom_int(arena, node->scalar_left);
    return ppnative_v1_expr(arena, "pf-epsilon", arguments, 1u);
}

static bool ppnative_v1_forest_canonicalize(
    PPNativeV1Result *result,
    const PPABIV1Pack *pack,
    const Atom *start_state,
    const PPNativeV1ForestExtension *extension,
    char *error_buf,
    size_t error_buf_size) {
    const CettaLpNativeUtf8Forest *forest = &result->forest;
    Atom **node_atoms = NULL;
    Atom **root_atoms = NULL;
    Atom **choice_atoms = NULL;
    PPNativeV1CanonicalAtom *root_entries = NULL;
    PPNativeV1CanonicalAtom *node_entries = NULL;
    PPNativeV1CanonicalAtom *choice_entries = NULL;
    Arena canonical_arena;
    Atom *arguments[5];
    uint32_t index;
    bool ok = false;

    arena_init(&canonical_arena);
    node_atoms = calloc(forest->node_len ? forest->node_len : 1u,
                        sizeof(*node_atoms));
    root_atoms = calloc(forest->root_len ? forest->root_len : 1u,
                        sizeof(*root_atoms));
    choice_atoms = calloc(forest->choice_len ? forest->choice_len : 1u,
                          sizeof(*choice_atoms));
    if (!node_atoms || !root_atoms || !choice_atoms)
        goto done;
    for (index = 0u; index < forest->node_len; index++) {
        node_atoms[index] = ppnative_v1_forest_node_atom(
            &canonical_arena, pack, extension, forest, index);
        if (!node_atoms[index])
            goto done;
    }
    for (index = 0u; index < forest->root_len; index++) {
        if (forest->roots[index] >= forest->node_len)
            goto done;
        root_atoms[index] = node_atoms[forest->roots[index]];
    }
    for (index = 0u; index < forest->choice_len; index++) {
        const CettaLpNativeUtf8ForestChoice *choice =
            &forest->choices[index];
        Atom *choice_arguments[5];
        if (choice->parent_node >= forest->node_len ||
            choice->child_node >= forest->node_len)
            goto done;
        choice_arguments[0] = node_atoms[choice->parent_node];
        choice_arguments[1] = ppnative_v1_production_label(
            &canonical_arena, pack, extension,
            choice->production_index);
        if (choice->prefix_node == CETTA_LP_NATIVE_UTF8_FOREST_NONE) {
            choice_arguments[2] = atom_symbol(&canonical_arena, "pf-none");
        } else {
            if (choice->prefix_node >= forest->node_len)
                goto done;
            choice_arguments[2] = node_atoms[choice->prefix_node];
        }
        choice_arguments[3] = node_atoms[choice->child_node];
        choice_arguments[4] = atom_int(
            &canonical_arena, choice->scalar_pivot);
        if (!choice_arguments[1])
            goto done;
        choice_atoms[index] = ppnative_v1_expr(
            &canonical_arena, "pf-choice", choice_arguments, 5u);
    }
    if (!ppnative_v1_canonical_entries_build(
            root_atoms, forest->root_len, true, &root_entries,
            error_buf, error_buf_size) ||
        !ppnative_v1_canonical_entries_build(
            node_atoms, forest->node_len, true, &node_entries,
            error_buf, error_buf_size) ||
        !ppnative_v1_canonical_entries_build(
            choice_atoms, forest->choice_len, true, &choice_entries,
            error_buf, error_buf_size) ||
        !ppnative_v1_forest_digest(
            start_state, forest->scalar_len,
            root_entries, forest->root_len,
            node_entries, forest->node_len,
            choice_entries, forest->choice_len,
            result->forest_digest)) {
        goto done;
    }
    if (forest->root_len <= PPNATIVE_V1_CANONICAL_LIST_MATERIALIZE_LIMIT &&
        forest->node_len <= PPNATIVE_V1_CANONICAL_LIST_MATERIALIZE_LIMIT &&
        forest->choice_len <= PPNATIVE_V1_CANONICAL_LIST_MATERIALIZE_LIMIT) {
        Atom *root_list = ppnative_v1_canonical_entries_list(
            &canonical_arena, root_entries, forest->root_len);
        Atom *node_list = ppnative_v1_canonical_entries_list(
            &canonical_arena, node_entries, forest->node_len);
        Atom *choice_list = ppnative_v1_canonical_entries_list(
            &canonical_arena, choice_entries, forest->choice_len);
        Atom *canonical_forest;
        uint8_t *rendered = NULL;
        size_t rendered_len = 0u;
        char materialized_digest[65];

        if (!root_list || !node_list || !choice_list)
            goto done;
        arguments[0] = ppnative_v1_copy(
            &canonical_arena, (Atom *)start_state);
        arguments[1] = atom_int(&canonical_arena, forest->scalar_len);
        arguments[2] = root_list;
        arguments[3] = node_list;
        arguments[4] = choice_list;
        canonical_forest = ppnative_v1_expr(
            &canonical_arena, "pf-v1", arguments, 5u);
        if (!canonical_forest ||
            !fh_ground_term_v1_render(
                canonical_forest, &rendered, &rendered_len,
                error_buf, error_buf_size)) {
            free(rendered);
            goto done;
        }
        cetta_native_sha256_hex(
            rendered, rendered_len, materialized_digest);
        free(rendered);
        if (strcmp(materialized_digest, result->forest_digest) != 0) {
            ppnative_v1_set_error(
                error_buf, error_buf_size,
                "streamed and materialized forest digests differ");
            goto done;
        }
        result->canonical_forest = atom_deep_copy(
            &result->arena, canonical_forest);
        if (!result->canonical_forest)
            goto done;
        result->canonical_forest_materialized = true;
    }
    ok = true;

done:
    ppnative_v1_canonical_entries_free(root_entries, forest->root_len);
    ppnative_v1_canonical_entries_free(node_entries, forest->node_len);
    ppnative_v1_canonical_entries_free(choice_entries, forest->choice_len);
    free(node_atoms);
    free(root_atoms);
    free(choice_atoms);
    arena_free(&canonical_arena);
    if (!ok && (!error_buf || error_buf[0] == '\0')) {
        ppnative_v1_set_error(error_buf, error_buf_size,
                           "failed to canonicalize neutral GLL forest");
    }
    return ok;
}

static bool ppnative_v1_canonical_atomvec_push_unique(
    PPNativeV1CanonicalAtomVec *vec,
    Atom *atom,
    uint32_t limit,
    PPNativeV1ReplayStatus *status) {
    char *canonical = NULL;
    uint32_t index;

    if (vec->len > 0u) {
        canonical = ppnative_v1_render(atom);
        if (!canonical) {
            *status = PPNATIVE_V1_REPLAY_MALFORMED;
            return false;
        }
    }
    for (index = 0u; index < vec->len; index++) {
        if (!vec->data[index].canonical) {
            vec->data[index].canonical =
                ppnative_v1_render(vec->data[index].atom);
            if (!vec->data[index].canonical) {
                free(canonical);
                *status = PPNATIVE_V1_REPLAY_MALFORMED;
                return false;
            }
        }
        if (strcmp(vec->data[index].canonical, canonical) == 0) {
            free(canonical);
            return true;
        }
    }
    if (vec->len >= limit) {
        free(canonical);
        *status = PPNATIVE_V1_REPLAY_RESULT_HIT;
        return false;
    }
    if (vec->len == vec->cap) {
        uint32_t cap = vec->cap ? vec->cap * 2u : 8u;
        if (cap < vec->cap) {
            free(canonical);
            *status = PPNATIVE_V1_REPLAY_MALFORMED;
            return false;
        }
        vec->data = cetta_realloc(vec->data, sizeof(*vec->data) * cap);
        vec->cap = cap;
    }
    vec->data[vec->len++] = (PPNativeV1CanonicalAtom){
        .atom = atom,
        .canonical = canonical,
    };
    return true;
}

static void ppnative_v1_canonical_atomvec_free(
    PPNativeV1CanonicalAtomVec *vec) {
    uint32_t index;

    for (index = 0u; index < vec->len; index++)
        free(vec->data[index].canonical);
    free(vec->data);
    memset(vec, 0, sizeof(*vec));
}

static bool ppnative_v1_atomvec_push(PPNativeV1AtomVec *vec, Atom *atom) {
    if (vec->len == vec->cap) {
        uint32_t cap = vec->cap ? vec->cap * 2u : 8u;
        if (cap < vec->cap)
            return false;
        vec->data = cetta_realloc(vec->data, sizeof(*vec->data) * cap);
        vec->cap = cap;
    }
    vec->data[vec->len++] = atom;
    return true;
}

static bool ppnative_v1_sequence_push(PPNativeV1SequenceVec *vec,
                                   const uint32_t *nodes,
                                   uint32_t len,
                                   uint32_t append,
                                   uint32_t limit,
                                   PPNativeV1ReplayStatus *status) {
    PPNativeV1NodeSequence *sequence;
    if (vec->len >= limit) {
        *status = PPNATIVE_V1_REPLAY_RESULT_HIT;
        return false;
    }
    if (vec->len == vec->cap) {
        uint32_t cap = vec->cap ? vec->cap * 2u : 8u;
        if (cap < vec->cap)
            return false;
        vec->data = cetta_realloc(vec->data, sizeof(*vec->data) * cap);
        vec->cap = cap;
    }
    sequence = &vec->data[vec->len++];
    sequence->len = len + 1u;
    sequence->nodes = malloc(sizeof(*sequence->nodes) * sequence->len);
    if (!sequence->nodes)
        return false;
    if (len > 0u)
        memcpy(sequence->nodes, nodes, sizeof(*nodes) * len);
    sequence->nodes[len] = append;
    return true;
}

static void ppnative_v1_sequence_vec_free(PPNativeV1SequenceVec *vec) {
    uint32_t index;
    for (index = 0u; index < vec->len; index++)
        free(vec->data[index].nodes);
    free(vec->data);
    memset(vec, 0, sizeof(*vec));
}

static bool ppnative_v1_u32_push_unique(PPNativeV1U32Vec *vec,
                                      uint32_t value) {
    uint32_t index;
    for (index = 0u; index < vec->len; index++) {
        if (vec->data[index] == value)
            return true;
    }
    if (vec->len == vec->cap) {
        uint32_t cap = vec->cap ? vec->cap * 2u : 8u;
        if (cap < vec->cap)
            return false;
        vec->data = cetta_realloc(vec->data, sizeof(*vec->data) * cap);
        vec->cap = cap;
    }
    vec->data[vec->len++] = value;
    return true;
}

static void ppnative_v1_replay_stack_free(PPNativeV1ReplayStack *stack) {
    uint32_t index;
    for (index = 0u; index < stack->len; index++)
        free(stack->data[index].dependencies.data);
    free(stack->data);
    memset(stack, 0, sizeof(*stack));
}

static bool ppnative_v1_expand_choice(PPNativeV1ReplayContext *context,
                                   uint32_t choice_index,
                                   uint32_t depth,
                                   PPNativeV1SequenceVec *out);

static bool ppnative_v1_expand_prefix(PPNativeV1ReplayContext *context,
                                   uint32_t node_index,
                                   uint32_t depth,
                                   PPNativeV1SequenceVec *out) {
    const CettaLpNativeUtf8ForestNode *node;
    uint32_t offset;
    if (depth == 0u) {
        context->status = PPNATIVE_V1_REPLAY_DEPTH_HIT;
        return false;
    }
    if (node_index >= context->forest->node_len) {
        context->status = PPNATIVE_V1_REPLAY_MALFORMED;
        return false;
    }
    node = &context->forest->nodes[node_index];
    if (node->kind != CETTA_LP_NATIVE_UTF8_FOREST_INTERMEDIATE) {
        context->status = PPNATIVE_V1_REPLAY_MALFORMED;
        return false;
    }
    for (offset = 0u; offset < node->choice_len; offset++) {
        if (!ppnative_v1_expand_choice(
                context, node->choice_begin + offset, depth - 1u, out)) {
            return false;
        }
    }
    return true;
}

static bool ppnative_v1_expand_choice(PPNativeV1ReplayContext *context,
                                   uint32_t choice_index,
                                   uint32_t depth,
                                   PPNativeV1SequenceVec *out) {
    const CettaLpNativeUtf8ForestChoice *choice;
    if (choice_index >= context->forest->choice_len) {
        context->status = PPNATIVE_V1_REPLAY_MALFORMED;
        return false;
    }
    choice = &context->forest->choices[choice_index];
    if (choice->prefix_node == CETTA_LP_NATIVE_UTF8_FOREST_NONE) {
        return ppnative_v1_sequence_push(
            out, NULL, 0u, choice->child_node, context->result_limit,
            &context->status);
    }
    {
        PPNativeV1SequenceVec prefixes = {0};
        uint32_t index;
        bool ok = ppnative_v1_expand_prefix(
            context, choice->prefix_node, depth, &prefixes);
        if (!ok) {
            ppnative_v1_sequence_vec_free(&prefixes);
            return false;
        }
        for (index = 0u; index < prefixes.len; index++) {
            if (!ppnative_v1_sequence_push(
                    out, prefixes.data[index].nodes,
                    prefixes.data[index].len, choice->child_node,
                    context->result_limit, &context->status)) {
                ppnative_v1_sequence_vec_free(&prefixes);
                return false;
            }
        }
        ppnative_v1_sequence_vec_free(&prefixes);
    }
    return true;
}

static bool ppnative_v1_qindex(Atom *term, uint32_t *out) {
    uint32_t value = 0u;
    while (!atom_is_symbol(term, "q-zero")) {
        if (!ppnative_v1_expr_head(term, "q-succ", 1u) ||
            value == UINT32_MAX)
            return false;
        value++;
        term = term->expr.elems[1];
    }
    *out = value;
    return true;
}

static Atom *ppnative_v1_apply_action(PPNativeV1ReplayContext *context,
                                   Atom *action,
                                   Atom **values,
                                   uint32_t value_len,
                                   unsigned depth);

static bool ppnative_v1_apply_action_list(PPNativeV1ReplayContext *context,
                                       Atom *list,
                                       Atom **values,
                                       uint32_t value_len,
                                       unsigned depth,
                                       PPNativeV1AtomVec *out) {
    while (!atom_is_symbol(list, "pa-nil")) {
        Atom *value;
        if (depth > PPNATIVE_V1_MAX_TERM_DEPTH ||
            !ppnative_v1_expr_head(list, "pa-cons", 2u)) {
            context->status = PPNATIVE_V1_REPLAY_MALFORMED;
            return false;
        }
        value = ppnative_v1_apply_action(
            context, list->expr.elems[1], values, value_len, depth + 1u);
        if (!value || !ppnative_v1_atomvec_push(out, value)) {
            return false;
        }
        list = list->expr.elems[2];
        depth++;
    }
    return true;
}

static Atom *ppnative_v1_apply_action(PPNativeV1ReplayContext *context,
                                   Atom *action,
                                   Atom **values,
                                   uint32_t value_len,
                                   unsigned depth) {
    uint32_t slot;
    if (depth > PPNATIVE_V1_MAX_TERM_DEPTH) {
        context->status = PPNATIVE_V1_REPLAY_DEPTH_HIT;
        return NULL;
    }
    if (ppnative_v1_expr_head(action, "pa-slot", 1u) &&
        ppnative_v1_qindex(action->expr.elems[1], &slot) && slot < value_len) {
        return values[slot];
    }
    if (ppnative_v1_expr_head(action, "pa-const", 1u))
        return ppnative_v1_copy(context->arena, action->expr.elems[1]);
    if (ppnative_v1_expr_head(action, "pa-apply", 2u) &&
        action->expr.elems[1]->kind == ATOM_SYMBOL) {
        PPNativeV1AtomVec arguments = {0};
        Atom *head;
        Atom **items;
        Atom *result;
        uint32_t index;
        if (!ppnative_v1_apply_action_list(
                context, action->expr.elems[2], values, value_len,
                depth + 1u, &arguments)) {
            free(arguments.data);
            return NULL;
        }
        head = ppnative_v1_copy(context->arena, action->expr.elems[1]);
        items = arena_alloc(
            context->arena,
            sizeof(*items) * ((size_t)arguments.len + 1u));
        items[0] = head;
        for (index = 0u; index < arguments.len; index++)
            items[index + 1u] = arguments.data[index];
        result = atom_expr(context->arena, items, arguments.len + 1u);
        free(arguments.data);
        return result;
    }
    context->status = PPNATIVE_V1_REPLAY_MALFORMED;
    return NULL;
}

/*
 * CstRuleV1 is the span-aware node action of the authored scannerless
 * presentation.  Its action term carries the label and retained children;
 * the exact half-open span is supplied by the ambient forest symbol whose
 * production action is being replayed.
 */
static Atom *ppnative_v1_materialize_cst_span(
    PPNativeV1ReplayContext *context,
    const CettaLpNativeUtf8ForestNode *node,
    Atom *value) {
    Atom **items;
    uint32_t index;

    if (!context || !node || !value || value->kind != ATOM_EXPR ||
        value->expr.len < 2u ||
        !atom_is_symbol(value->expr.elems[0], "CstRuleV1")) {
        return value;
    }
    if (node->scalar_left > node->scalar_right) {
        context->status = PPNATIVE_V1_REPLAY_MALFORMED;
        return NULL;
    }
    items = arena_alloc(
        context->arena, sizeof(*items) * ((size_t)value->expr.len + 2u));
    if (!items)
        return NULL;
    items[0] = value->expr.elems[0];
    items[1] = value->expr.elems[1];
    items[2] = atom_int(context->arena, (int64_t)node->scalar_left);
    items[3] = atom_int(context->arena, (int64_t)node->scalar_right);
    for (index = 2u; index < value->expr.len; index++)
        items[index + 2u] = value->expr.elems[index];
    return atom_expr(context->arena, items, value->expr.len + 2u);
}

bool ppnative_v1_apply_action_term(Arena *arena,
                                   Atom *action,
                                   Atom *const *values,
                                   uint32_t value_len,
                                   Atom **out,
                                   char *error_buf,
                                   size_t error_buf_size) {
    PPNativeV1ReplayContext context = {
        .arena = arena,
        .status = PPNATIVE_V1_REPLAY_OK,
    };
    Atom *result;

    if (error_buf && error_buf_size > 0u)
        error_buf[0] = '\0';
    if (out)
        *out = NULL;
    if (!arena || !action || !out || (value_len > 0u && !values)) {
        ppnative_v1_set_error(
            error_buf, error_buf_size,
            "bad ParserPack action execution arguments");
        return false;
    }
    result = ppnative_v1_apply_action(
        &context, action, (Atom **)values, value_len, 0u);
    if (!result || context.status != PPNATIVE_V1_REPLAY_OK) {
        ppnative_v1_set_error(
            error_buf, error_buf_size,
            "ParserPack action is malformed or exceeds its depth boundary");
        return false;
    }
    *out = result;
    return true;
}

static Atom *ppnative_v1_terminal_value(PPNativeV1ReplayContext *context,
                                     uint32_t node_index) {
    const CettaLpNativeUtf8ForestNode *node;
    Atom *argument;
    if (node_index >= context->forest->node_len)
        return NULL;
    node = &context->forest->nodes[node_index];
    if (node->kind != CETTA_LP_NATIVE_UTF8_FOREST_TERM)
        return NULL;
    if (node->terminal_value_kind ==
        CETTA_LP_NATIVE_UTF8_TERMINAL_VALUE_EOF) {
        return atom_symbol(context->arena, "eof");
    }
    if (node->terminal_value_kind ==
        CETTA_LP_NATIVE_UTF8_TERMINAL_VALUE_WITNESS) {
        if (!context->extension ||
            node->terminal_witness_id >=
                context->extension->witness_len ||
            !context->extension->witness_values[
                node->terminal_witness_id]) {
            context->status = PPNATIVE_V1_REPLAY_MALFORMED;
            return NULL;
        }
        return ppnative_v1_copy(
            context->arena,
            context->extension->witness_values[
                node->terminal_witness_id]);
    }
    if (node->terminal_value_kind !=
        CETTA_LP_NATIVE_UTF8_TERMINAL_VALUE_SCALAR) {
        context->status = PPNATIVE_V1_REPLAY_MALFORMED;
        return NULL;
    }
    argument = atom_int(context->arena, node->terminal_scalar);
    return ppnative_v1_expr(context->arena, "cp", &argument, 1u);
}

static bool ppnative_v1_replay_production(
    const PPNativeV1ReplayContext *context,
    uint32_t production_index,
    PPNativeV1ReplayProduction *out) {
    if (production_index < context->pack->production_len) {
        const PPABIV1Production *production =
            &context->pack->productions[production_index];
        out->action = production->action;
        out->item_len = production->item_len;
        return true;
    }
    {
        const PPNativeV1ProductionExtension *production =
            ppnative_v1_production_extension_find(
                context->pack, context->extension, production_index);
        if (!production)
            return false;
        out->action = production->action;
        out->item_len = production->item_len;
    }
    return true;
}

static bool ppnative_v1_sequence_dependencies(
    PPNativeV1ReplayContext *context,
    const PPNativeV1NodeSequence *sequence,
    PPNativeV1U32Vec *dependencies) {
    uint32_t index;

    for (index = 0u; index < sequence->len; index++) {
        uint32_t child_index = sequence->nodes[index];
        const CettaLpNativeUtf8ForestNode *child;
        if (child_index >= context->forest->node_len) {
            context->status = PPNATIVE_V1_REPLAY_MALFORMED;
            return false;
        }
        child = &context->forest->nodes[child_index];
        if (child->kind == CETTA_LP_NATIVE_UTF8_FOREST_TERM)
            continue;
        if (child->kind != CETTA_LP_NATIVE_UTF8_FOREST_SYMBOL ||
            !ppnative_v1_u32_push_unique(dependencies, child_index)) {
            context->status = PPNATIVE_V1_REPLAY_MALFORMED;
            return false;
        }
    }
    return true;
}

static bool ppnative_v1_symbol_dependencies(
    PPNativeV1ReplayContext *context,
    uint32_t node_index,
    uint32_t sequence_depth,
    PPNativeV1U32Vec *dependencies) {
    const CettaLpNativeUtf8ForestNode *node;
    uint32_t offset;

    if (node_index >= context->forest->node_len) {
        context->status = PPNATIVE_V1_REPLAY_MALFORMED;
        return false;
    }
    node = &context->forest->nodes[node_index];
    if (node->kind != CETTA_LP_NATIVE_UTF8_FOREST_SYMBOL) {
        context->status = PPNATIVE_V1_REPLAY_MALFORMED;
        return false;
    }
    for (offset = 0u; offset < node->choice_len; offset++) {
        uint32_t choice_index = node->choice_begin + offset;
        const CettaLpNativeUtf8ForestChoice *choice;
        PPNativeV1ReplayProduction production;
        PPNativeV1SequenceVec sequences = {0};
        uint32_t sequence_index;
        if (choice_index >= context->forest->choice_len) {
            context->status = PPNATIVE_V1_REPLAY_MALFORMED;
            return false;
        }
        choice = &context->forest->choices[choice_index];
        if (!ppnative_v1_replay_production(
                context, choice->production_index, &production)) {
            context->status = PPNATIVE_V1_REPLAY_MALFORMED;
            return false;
        }
        if (production.item_len == 0u)
            continue;
        if (!ppnative_v1_expand_choice(
                context, choice_index, sequence_depth, &sequences)) {
            ppnative_v1_sequence_vec_free(&sequences);
            return false;
        }
        for (sequence_index = 0u;
             sequence_index < sequences.len; sequence_index++) {
            const PPNativeV1NodeSequence *sequence =
                &sequences.data[sequence_index];
            if (sequence->len != production.item_len ||
                !ppnative_v1_sequence_dependencies(
                    context, sequence, dependencies)) {
                ppnative_v1_sequence_vec_free(&sequences);
                if (context->status == PPNATIVE_V1_REPLAY_OK)
                    context->status = PPNATIVE_V1_REPLAY_MALFORMED;
                return false;
            }
        }
        ppnative_v1_sequence_vec_free(&sequences);
    }
    return true;
}

static bool ppnative_v1_replay_stack_push(
    PPNativeV1ReplayContext *context,
    PPNativeV1ReplayStack *stack,
    uint32_t node_index,
    uint32_t sequence_depth) {
    PPNativeV1ReplayFrame frame;

    memset(&frame, 0, sizeof(frame));
    frame.node_index = node_index;
    if (!ppnative_v1_symbol_dependencies(
            context, node_index, sequence_depth, &frame.dependencies)) {
        free(frame.dependencies.data);
        return false;
    }
    if (stack->len == stack->cap) {
        uint32_t cap = stack->cap ? stack->cap * 2u : 64u;
        if (cap < stack->cap) {
            free(frame.dependencies.data);
            return false;
        }
        stack->data = cetta_realloc(
            stack->data, sizeof(*stack->data) * cap);
        stack->cap = cap;
    }
    stack->data[stack->len++] = frame;
    context->memo[node_index].visiting = true;
    return true;
}

static bool ppnative_v1_eval_sequence_ready(
    PPNativeV1ReplayContext *context,
    const CettaLpNativeUtf8ForestNode *parent,
    const PPNativeV1ReplayProduction *production,
    const PPNativeV1NodeSequence *sequence,
    PPNativeV1CanonicalAtomVec *out) {
    Atom **values = NULL;
    uint32_t *value_indices = NULL;
    uint32_t index;
    bool ok = false;

    if (sequence->len == 0u || sequence->len != production->item_len ||
        (size_t)sequence->len > SIZE_MAX / sizeof(*values) ||
        (size_t)sequence->len > SIZE_MAX / sizeof(*value_indices)) {
        context->status = PPNATIVE_V1_REPLAY_MALFORMED;
        goto done;
    }
    values = malloc(sizeof(*values) * sequence->len);
    value_indices = calloc(sequence->len, sizeof(*value_indices));
    if (!values || !value_indices)
        goto done;
    for (index = 0u; index < sequence->len; index++) {
        uint32_t child_index = sequence->nodes[index];
        const CettaLpNativeUtf8ForestNode *child;
        if (child_index >= context->forest->node_len) {
            context->status = PPNATIVE_V1_REPLAY_MALFORMED;
            goto done;
        }
        child = &context->forest->nodes[child_index];
        if (child->kind == CETTA_LP_NATIVE_UTF8_FOREST_TERM) {
            values[index] = ppnative_v1_terminal_value(
                context, child_index);
            if (!values[index])
                goto done;
        } else if (child->kind == CETTA_LP_NATIVE_UTF8_FOREST_SYMBOL) {
            PPNativeV1ReplayMemo *memo = &context->memo[child_index];
            if (!memo->ready) {
                context->status = PPNATIVE_V1_REPLAY_MALFORMED;
                goto done;
            }
            if (memo->values.len == 0u) {
                ok = true;
                goto done;
            }
            values[index] = memo->values.data[0].atom;
        } else {
            context->status = PPNATIVE_V1_REPLAY_MALFORMED;
            goto done;
        }
    }
    for (;;) {
        Atom *value = ppnative_v1_apply_action(
            context, production->action, values, sequence->len, 0u);
        bool advanced = false;
        uint32_t position;
        value = ppnative_v1_materialize_cst_span(context, parent, value);
        if (!value || !ppnative_v1_canonical_atomvec_push_unique(
                out, value, context->result_limit, &context->status)) {
            goto done;
        }
        for (position = sequence->len; position > 0u; position--) {
            uint32_t child_position = position - 1u;
            uint32_t child_index = sequence->nodes[child_position];
            const CettaLpNativeUtf8ForestNode *child =
                &context->forest->nodes[child_index];
            PPNativeV1ReplayMemo *memo;
            if (child->kind != CETTA_LP_NATIVE_UTF8_FOREST_SYMBOL)
                continue;
            memo = &context->memo[child_index];
            if (value_indices[child_position] + 1u < memo->values.len) {
                value_indices[child_position]++;
                values[child_position] =
                    memo->values.data[value_indices[child_position]].atom;
                advanced = true;
                break;
            }
            value_indices[child_position] = 0u;
            values[child_position] = memo->values.data[0].atom;
        }
        if (!advanced)
            break;
    }
    ok = true;

done:
    free(values);
    free(value_indices);
    return ok;
}

static bool ppnative_v1_eval_symbol_ready(
    PPNativeV1ReplayContext *context,
    uint32_t node_index,
    uint32_t sequence_depth) {
    const CettaLpNativeUtf8ForestNode *node =
        &context->forest->nodes[node_index];
    PPNativeV1ReplayMemo *memo = &context->memo[node_index];
    uint32_t offset;

    for (offset = 0u; offset < node->choice_len; offset++) {
        uint32_t choice_index = node->choice_begin + offset;
        const CettaLpNativeUtf8ForestChoice *choice;
        PPNativeV1ReplayProduction production;
        if (choice_index >= context->forest->choice_len) {
            context->status = PPNATIVE_V1_REPLAY_MALFORMED;
            return false;
        }
        choice = &context->forest->choices[choice_index];
        if (!ppnative_v1_replay_production(
                context, choice->production_index, &production)) {
            context->status = PPNATIVE_V1_REPLAY_MALFORMED;
            return false;
        }
        if (production.item_len == 0u) {
            Atom *value = ppnative_v1_apply_action(
                context, production.action, NULL, 0u, 0u);
            value = ppnative_v1_materialize_cst_span(context, node, value);
            if (!value || !ppnative_v1_canonical_atomvec_push_unique(
                    &memo->values, value, context->result_limit,
                    &context->status)) {
                return false;
            }
        } else {
            PPNativeV1SequenceVec sequences = {0};
            uint32_t sequence_index;
            if (!ppnative_v1_expand_choice(
                    context, choice_index, sequence_depth, &sequences)) {
                ppnative_v1_sequence_vec_free(&sequences);
                return false;
            }
            for (sequence_index = 0u;
                 sequence_index < sequences.len; sequence_index++) {
                const PPNativeV1NodeSequence *sequence =
                    &sequences.data[sequence_index];
                if (sequence->len != production.item_len ||
                    !ppnative_v1_eval_sequence_ready(
                        context, node, &production, sequence, &memo->values)) {
                    ppnative_v1_sequence_vec_free(&sequences);
                    if (context->status == PPNATIVE_V1_REPLAY_OK)
                        context->status = PPNATIVE_V1_REPLAY_MALFORMED;
                    return false;
                }
            }
            ppnative_v1_sequence_vec_free(&sequences);
        }
    }
    return true;
}

static bool ppnative_v1_eval_symbol(PPNativeV1ReplayContext *context,
                                 uint32_t node_index,
                                 uint32_t sequence_depth,
                                 PPNativeV1CanonicalAtomVec **out) {
    PPNativeV1ReplayStack stack = {0};
    bool ok = false;

    if (node_index >= context->forest->node_len ||
        context->forest->nodes[node_index].kind !=
            CETTA_LP_NATIVE_UTF8_FOREST_SYMBOL) {
        context->status = PPNATIVE_V1_REPLAY_MALFORMED;
        goto done;
    }
    if (context->memo[node_index].ready) {
        *out = &context->memo[node_index].values;
        return true;
    }
    if (!ppnative_v1_replay_stack_push(
            context, &stack, node_index, sequence_depth)) {
        goto done;
    }
    while (stack.len > 0u) {
        PPNativeV1ReplayFrame *frame = &stack.data[stack.len - 1u];
        PPNativeV1ReplayMemo *memo = &context->memo[frame->node_index];
        bool pushed = false;
        while (frame->next_dependency < frame->dependencies.len) {
            uint32_t dependency =
                frame->dependencies.data[frame->next_dependency];
            PPNativeV1ReplayMemo *dependency_memo =
                &context->memo[dependency];
            if (dependency_memo->ready) {
                frame->next_dependency++;
                continue;
            }
            if (dependency_memo->visiting) {
                context->status = PPNATIVE_V1_REPLAY_DEPTH_HIT;
                goto done;
            }
            if (!ppnative_v1_replay_stack_push(
                    context, &stack, dependency, sequence_depth)) {
                goto done;
            }
            pushed = true;
            break;
        }
        if (pushed)
            continue;
        if (!ppnative_v1_eval_symbol_ready(
                context, frame->node_index, sequence_depth)) {
            goto done;
        }
        memo->visiting = false;
        memo->ready = true;
        free(frame->dependencies.data);
        memset(frame, 0, sizeof(*frame));
        stack.len--;
    }
    *out = &context->memo[node_index].values;
    ok = true;

done:
    if (!ok) {
        uint32_t index;
        for (index = 0u; index < stack.len; index++)
            context->memo[stack.data[index].node_index].visiting = false;
    }
    ppnative_v1_replay_stack_free(&stack);
    return ok;
}

static Atom *ppnative_v1_input_suffix(Arena *arena,
                                   const CettaLpNativeUtf8Forest *forest,
                                   uint32_t position) {
    Atom *list = atom_symbol(arena, "nil");
    uint32_t index = forest->scalar_len;
    if (position > forest->scalar_len)
        return NULL;
    while (index > position) {
        Atom *cp_argument;
        Atom *cp;
        Atom *arguments[2];
        index--;
        cp_argument = atom_int(arena, forest->codepoints[index]);
        cp = ppnative_v1_expr(arena, "cp", &cp_argument, 1u);
        arguments[0] = cp;
        arguments[1] = list;
        list = ppnative_v1_expr(arena, "cons", arguments, 2u);
    }
    return list;
}

static bool ppnative_v1_results_sort(PPNativeV1Result *result,
                                  PPNativeV1CanonicalAtomVec *values) {
    uint32_t index;
    if (values->len == 0u)
        return true;
    for (index = 0u; index < values->len; index++) {
        if (!values->data[index].canonical) {
            values->data[index].canonical =
                ppnative_v1_render(values->data[index].atom);
            if (!values->data[index].canonical)
                return false;
        }
    }
    if (values->len > 1u) {
        qsort(values->data, values->len, sizeof(*values->data),
              ppnative_v1_canonical_atom_compare);
    }
    result->semantic_results = malloc(
        sizeof(*result->semantic_results) * values->len);
    if (!result->semantic_results)
        return false;
    for (index = 0u; index < values->len; index++)
        result->semantic_results[index] = values->data[index].atom;
    result->semantic_result_len = values->len;
    return true;
}

static bool ppnative_v1_replay(PPNativeV1Result *result,
                            const PPABIV1Pack *pack,
                            const PPNativeV1ForestExtension *extension,
                            uint32_t replay_depth,
                            uint32_t result_limit,
                            char *error_buf,
                            size_t error_buf_size) {
    PPNativeV1ReplayContext context;
    PPNativeV1CanonicalAtomVec results = {0};
    uint32_t root_index;
    uint32_t memo_index;
    bool ok = false;

    memset(&context, 0, sizeof(context));
    context.pack = pack;
    context.forest = &result->forest;
    context.extension = extension;
    context.arena = &result->arena;
    context.result_limit = result_limit;
    context.status = PPNATIVE_V1_REPLAY_OK;
    context.memo = calloc(result->forest.node_len ? result->forest.node_len : 1u,
                          sizeof(*context.memo));
    if (!context.memo)
        goto done;
    for (root_index = 0u; root_index < result->forest.root_len; root_index++) {
        uint32_t node_index = result->forest.roots[root_index];
        const CettaLpNativeUtf8ForestNode *root;
        PPNativeV1CanonicalAtomVec *values;
        Atom *rest;
        uint32_t value_index;
        if (node_index >= result->forest.node_len)
            goto malformed;
        root = &result->forest.nodes[node_index];
        if (!ppnative_v1_eval_symbol(
                &context, node_index, replay_depth, &values)) {
            goto resource_or_malformed;
        }
        rest = ppnative_v1_input_suffix(
            &result->arena, &result->forest, root->scalar_right);
        if (!rest)
            goto malformed;
        for (value_index = 0u; value_index < values->len; value_index++) {
            Atom *arguments[2];
            Atom *semantic_result;
            arguments[0] = values->data[value_index].atom;
            arguments[1] = rest;
            semantic_result = ppnative_v1_expr(
                &result->arena, "result", arguments, 2u);
            if (!ppnative_v1_canonical_atomvec_push_unique(
                    &results, semantic_result, result_limit,
                    &context.status)) {
                goto resource_or_malformed;
            }
        }
    }
    if (!ppnative_v1_results_sort(result, &results))
        goto done;
    ok = true;
    goto done;

resource_or_malformed:
    if (context.status == PPNATIVE_V1_REPLAY_DEPTH_HIT) {
        result->outcome = PPNATIVE_V1_REPLAY_DEPTH;
        (void)snprintf(result->detail, sizeof(result->detail),
                       "semantic replay depth %u", replay_depth);
        ok = true;
        goto done;
    }
    if (context.status == PPNATIVE_V1_REPLAY_RESULT_HIT) {
        result->outcome = PPNATIVE_V1_RESULT_LIMIT;
        (void)snprintf(result->detail, sizeof(result->detail),
                       "semantic result limit %u", result_limit);
        ok = true;
        goto done;
    }

malformed:
    ppnative_v1_set_error(error_buf, error_buf_size,
                       "neutral GLL forest could not be replayed");

done:
    if (context.memo) {
        for (memo_index = 0u; memo_index < result->forest.node_len;
             memo_index++) {
            ppnative_v1_canonical_atomvec_free(
                &context.memo[memo_index].values);
        }
    }
    free(context.memo);
    ppnative_v1_canonical_atomvec_free(&results);
    return ok;
}

void ppnative_v1_result_init(PPNativeV1Result *result) {
    if (!result)
        return;
    memset(result, 0, sizeof(*result));
    arena_init(&result->arena);
    cetta_lp_native_utf8_forest_init(&result->forest);
    result->outcome = PPNATIVE_V1_COMPLETED;
}

void ppnative_v1_result_free(PPNativeV1Result *result) {
    if (!result)
        return;
    cetta_lp_native_utf8_forest_free(&result->forest);
    free(result->semantic_results);
    arena_free(&result->arena);
    memset(result, 0, sizeof(*result));
}


static bool ppnative_v1_extension_action_list_valid(
    Atom *list,
    uint32_t arity,
    unsigned depth);

static bool ppnative_v1_extension_action_valid(
    Atom *action,
    uint32_t arity,
    unsigned depth) {
    uint32_t slot;

    if (!action || depth > PPNATIVE_V1_MAX_TERM_DEPTH)
        return false;
    if (ppnative_v1_expr_head(action, "pa-slot", 1u))
        return ppnative_v1_qindex(action->expr.elems[1], &slot) &&
            slot < arity;
    if (ppnative_v1_expr_head(action, "pa-const", 1u)) {
        char *canonical = ppnative_v1_render(action->expr.elems[1]);
        bool valid = canonical != NULL;
        free(canonical);
        return valid;
    }
    return ppnative_v1_expr_head(action, "pa-apply", 2u) &&
        action->expr.elems[1]->kind == ATOM_SYMBOL &&
        ppnative_v1_extension_action_list_valid(
            action->expr.elems[2], arity, depth + 1u);
}

static bool ppnative_v1_extension_action_list_valid(
    Atom *list,
    uint32_t arity,
    unsigned depth) {
    while (!atom_is_symbol(list, "pa-nil")) {
        if (depth > PPNATIVE_V1_MAX_TERM_DEPTH ||
            !ppnative_v1_expr_head(list, "pa-cons", 2u) ||
            !ppnative_v1_extension_action_valid(
                list->expr.elems[1], arity, depth + 1u)) {
            return false;
        }
        list = list->expr.elems[2];
        depth++;
    }
    return true;
}

static bool ppnative_v1_extension_production_matches(
    const PPABIV1Pack *pack,
    const PPNativeV1ForestExtension *extension,
    const PPNativeV1ProductionExtension *production) {
    Atom *items;
    Atom *lhs;
    uint32_t index;

    if (!ppnative_v1_expr_head(production->identity, "pp-production", 4u) ||
        !ppnative_v1_extension_action_valid(
            production->action, production->item_len, 0u) ||
        !atom_eq(production->identity->expr.elems[4], production->action)) {
        return false;
    }
    lhs = ppnative_v1_state_identity(
        pack, extension, production->lhs_state_id);
    if (!lhs || !atom_eq(production->identity->expr.elems[2], lhs))
        return false;
    items = production->identity->expr.elems[3];
    for (index = 0u; index < production->item_len; index++) {
        const PPABIV1Item *expected = &production->items[index];
        Atom *item;
        Atom *identity;
        const char *head;

        if (!ppnative_v1_expr_head(items, "pp-items-cons", 2u))
            return false;
        item = items->expr.elems[1];
        if (expected->kind == PPABI_V1_ITEM_NONTERMINAL) {
            head = "pp-nonterminal";
            identity = ppnative_v1_state_identity(
                pack, extension, expected->dense_id);
        } else if (expected->kind == PPABI_V1_ITEM_TERMINAL) {
            head = "pp-terminal";
            identity = ppnative_v1_terminal_identity(
                pack, extension, expected->dense_id);
        } else {
            return false;
        }
        if (!identity || !ppnative_v1_expr_head(item, head, 1u) ||
            !atom_eq(item->expr.elems[1], identity)) {
            return false;
        }
        items = items->expr.elems[2];
    }
    return atom_is_symbol(items, "pp-items-nil");
}

static bool ppnative_v1_extension_valid(
    const PPABIV1Pack *pack,
    const PPNativeV1ForestExtension *extension,
    char *error_buf,
    size_t error_buf_size) {
    uint32_t index;

    if (!extension)
        return true;
    if ((extension->state_len > 0u && !extension->states) ||
        (extension->terminal_len > 0u && !extension->terminals) ||
        (extension->production_len > 0u && !extension->productions) ||
        (extension->witness_len > 0u && !extension->witness_values)) {
        ppnative_v1_set_error(error_buf, error_buf_size,
                              "malformed ParserPack forest extension");
        return false;
    }
    if (extension->state_len > UINT32_MAX - pack->state_len ||
        extension->terminal_len > UINT32_MAX - pack->terminal_len ||
        extension->production_len > UINT32_MAX - pack->production_len) {
        ppnative_v1_set_error(error_buf, error_buf_size,
                              "ParserPack forest extension is too large");
        return false;
    }
    for (index = 0u; index < extension->state_len; index++) {
        const PPNativeV1StateExtension *state = &extension->states[index];
        char *canonical;
        uint32_t other;

        if (state->state_id != pack->state_len + index ||
            !state->identity) {
            ppnative_v1_set_error(
                error_buf, error_buf_size,
                "ParserPack forest states are not a contiguous extension");
            return false;
        }
        canonical = ppnative_v1_render(state->identity);
        if (!canonical) {
            ppnative_v1_set_error(
                error_buf, error_buf_size,
                "ParserPack forest state identity is not ground");
            return false;
        }
        free(canonical);
        for (other = 0u; other < pack->state_len; other++) {
            if (atom_eq(state->identity, pack->states[other].identity)) {
                ppnative_v1_set_error(
                    error_buf, error_buf_size,
                    "ParserPack forest state duplicates the base pack");
                return false;
            }
        }
        for (other = 0u; other < index; other++) {
            if (atom_eq(state->identity,
                        extension->states[other].identity)) {
                ppnative_v1_set_error(
                    error_buf, error_buf_size,
                    "ParserPack forest state identity is duplicated");
                return false;
            }
        }
    }
    for (index = 0u; index < extension->terminal_len; index++) {
        const PPNativeV1TerminalExtension *terminal =
            &extension->terminals[index];
        char *canonical;
        uint32_t other;
        if (terminal->terminal_id != pack->terminal_len + index ||
            !terminal->identity ||
            (index > 0u && extension->terminals[index - 1u].terminal_id >=
                terminal->terminal_id)) {
            ppnative_v1_set_error(
                error_buf, error_buf_size,
                "ParserPack forest terminals are not a contiguous extension");
            return false;
        }
        canonical = ppnative_v1_render(terminal->identity);
        if (!canonical) {
            ppnative_v1_set_error(
                error_buf, error_buf_size,
                "ParserPack forest terminal identity is not ground");
            return false;
        }
        free(canonical);
        for (other = 0u; other < pack->terminal_len; other++) {
            if (atom_eq(terminal->identity,
                        pack->terminals[other].identity)) {
                ppnative_v1_set_error(
                    error_buf, error_buf_size,
                    "ParserPack forest terminal duplicates the base pack");
                return false;
            }
        }
        for (other = 0u; other < index; other++) {
            if (atom_eq(terminal->identity,
                        extension->terminals[other].identity)) {
                ppnative_v1_set_error(
                    error_buf, error_buf_size,
                    "ParserPack forest terminal identity is duplicated");
                return false;
            }
        }
    }
    for (index = 0u; index < extension->production_len; index++) {
        const PPNativeV1ProductionExtension *production =
            &extension->productions[index];
        char *canonical;
        uint32_t other;

        if (production->production_id != pack->production_len + index ||
            !production->identity || !production->action ||
            (production->item_len > 0u && !production->items) ||
            !ppnative_v1_extension_production_matches(
                pack, extension, production)) {
            ppnative_v1_set_error(
                error_buf, error_buf_size,
                "ParserPack forest production is inconsistent");
            return false;
        }
        canonical = ppnative_v1_render(production->identity);
        if (!canonical) {
            ppnative_v1_set_error(
                error_buf, error_buf_size,
                "ParserPack forest production identity is not ground");
            return false;
        }
        free(canonical);
        for (other = 0u; other < pack->production_len; other++) {
            if (atom_eq(production->identity,
                        pack->productions[other].identity)) {
                ppnative_v1_set_error(
                    error_buf, error_buf_size,
                    "ParserPack forest production duplicates the base pack");
                return false;
            }
        }
        for (other = 0u; other < index; other++) {
            if (atom_eq(production->identity,
                        extension->productions[other].identity)) {
                ppnative_v1_set_error(
                    error_buf, error_buf_size,
                    "ParserPack forest production identity is duplicated");
                return false;
            }
        }
    }
    for (index = 0u; index < extension->witness_len; index++) {
        char *canonical;
        if (!extension->witness_values[index]) {
            ppnative_v1_set_error(error_buf, error_buf_size,
                                  "ParserPack witness table has a hole");
            return false;
        }
        canonical = ppnative_v1_render(extension->witness_values[index]);
        if (!canonical) {
            ppnative_v1_set_error(error_buf, error_buf_size,
                                  "ParserPack witness value is not ground");
            return false;
        }
        free(canonical);
    }
    return true;
}

bool ppnative_v1_start_is_closed_extended(
    const PPABIV1Pack *pack,
    const Atom *start_state,
    const PPNativeV1ForestExtension *extension,
    char *error_buf,
    size_t error_buf_size) {
    uint8_t *reachable = NULL;
    uint32_t *work = NULL;
    uint32_t total_states;
    uint32_t work_len = 0u;
    uint32_t work_head = 0u;
    int32_t start;
    bool ok = false;

    if (error_buf && error_buf_size > 0u)
        error_buf[0] = '\0';
    if (!pack || !start_state ||
        !ppnative_v1_extension_valid(
            pack, extension, error_buf, error_buf_size)) {
        if (error_buf && error_buf_size > 0u && error_buf[0] == '\0') {
            ppnative_v1_set_error(
                error_buf, error_buf_size,
                "bad extended ParserPack closure arguments");
        }
        return false;
    }
    total_states = pack->state_len +
        (extension ? extension->state_len : 0u);
    start = ppnative_v1_state_find_extended(pack, extension, start_state);
    if (start < 0 || (uint32_t)start >= total_states) {
        ppnative_v1_set_error(
            error_buf, error_buf_size,
            "start state is absent from extended ParserPack");
        return false;
    }
    reachable = calloc(total_states ? total_states : 1u, 1u);
    work = malloc(sizeof(*work) * (total_states ? total_states : 1u));
    if (!reachable || !work)
        goto done;
    reachable[start] = 1u;
    work[work_len++] = (uint32_t)start;
    while (work_head < work_len) {
        uint32_t state = work[work_head++];
        uint32_t production_index;
        bool defined = state < pack->state_len &&
            pack->states[state].defined;

        if (extension) {
            for (production_index = 0u;
                 production_index < extension->production_len;
                 production_index++) {
                if (extension->productions[production_index].lhs_state_id ==
                    state) {
                    defined = true;
                    break;
                }
            }
        }
        if (!defined) {
            Atom *identity = ppnative_v1_state_identity(
                pack, extension, state);
            char *canonical = identity ? ppnative_v1_render(identity) : NULL;
            ppnative_v1_set_error(
                error_buf, error_buf_size,
                "reachable extended ParserPack state is undefined%s%s",
                canonical ? ": " : "", canonical ? canonical : "");
            free(canonical);
            goto done;
        }
        for (production_index = 0u;
             production_index < pack->production_len;
             production_index++) {
            const PPABIV1Production *production =
                &pack->productions[production_index];
            uint32_t item_index;
            if (production->lhs_state_id != state)
                continue;
            for (item_index = 0u; item_index < production->item_len;
                 item_index++) {
                const PPABIV1Item *item = &production->items[item_index];
                if (item->kind == PPABI_V1_ITEM_NONTERMINAL &&
                    !reachable[item->dense_id]) {
                    reachable[item->dense_id] = 1u;
                    work[work_len++] = item->dense_id;
                }
            }
        }
        if (extension) {
            for (production_index = 0u;
                 production_index < extension->production_len;
                 production_index++) {
                const PPNativeV1ProductionExtension *production =
                    &extension->productions[production_index];
                uint32_t item_index;
                if (production->lhs_state_id != state)
                    continue;
                for (item_index = 0u; item_index < production->item_len;
                     item_index++) {
                    const PPABIV1Item *item = &production->items[item_index];
                    if (item->kind == PPABI_V1_ITEM_NONTERMINAL &&
                        !reachable[item->dense_id]) {
                        reachable[item->dense_id] = 1u;
                        work[work_len++] = item->dense_id;
                    }
                }
            }
        }
    }
    ok = true;

done:
    free(reachable);
    free(work);
    return ok;
}

bool ppnative_v1_grammar_extend(
    const PPABIV1Pack *pack,
    const PPNativeV1ForestExtension *extension,
    CettaLpNativeGrammar *grammar,
    char *error_buf,
    size_t error_buf_size) {
    CettaLpNativeProduction *productions = NULL;
    uint32_t total;
    uint32_t index;
    bool ok = false;

    if (error_buf && error_buf_size > 0u)
        error_buf[0] = '\0';
    if (!pack || !grammar || grammar->production_len != pack->production_len ||
        (grammar->production_len > 0u && !grammar->productions) ||
        !ppnative_v1_extension_valid(
            pack, extension, error_buf, error_buf_size)) {
        if (error_buf && error_buf_size > 0u && error_buf[0] == '\0') {
            ppnative_v1_set_error(
                error_buf, error_buf_size,
                "bad extended ParserPack grammar arguments");
        }
        return false;
    }
    for (index = 0u; index < grammar->production_len; index++) {
        if (grammar->productions[index].label != index) {
            ppnative_v1_set_error(
                error_buf, error_buf_size,
                "base ParserPack grammar labels are not dense");
            return false;
        }
    }
    if (!extension || extension->production_len == 0u)
        return true;
    total = pack->production_len + extension->production_len;
    productions = calloc(total, sizeof(*productions));
    if (!productions)
        goto done;
    if (pack->production_len > 0u) {
        memcpy(productions, grammar->productions,
               sizeof(*productions) * pack->production_len);
    }
    for (index = 0u; index < extension->production_len; index++) {
        const PPNativeV1ProductionExtension *source =
            &extension->productions[index];
        CettaLpNativeProduction *target =
            &productions[pack->production_len + index];
        uint32_t item_index;

        target->label = source->production_id;
        target->lhs = source->lhs_state_id;
        target->rhs_len = source->item_len;
        if (source->item_len > 0u) {
            target->rhs = calloc(source->item_len, sizeof(*target->rhs));
            if (!target->rhs)
                goto done;
        }
        for (item_index = 0u; item_index < source->item_len; item_index++) {
            target->rhs[item_index].kind =
                source->items[item_index].kind == PPABI_V1_ITEM_TERMINAL
                ? CETTA_LP_NATIVE_SYMBOL_TM
                : CETTA_LP_NATIVE_SYMBOL_HL;
            target->rhs[item_index].name =
                source->items[item_index].dense_id;
            target->rhs[item_index].scope = 0u;
        }
    }
    free(grammar->productions);
    grammar->productions = productions;
    grammar->production_len = total;
    productions = NULL;
    ok = true;

done:
    if (productions) {
        for (index = pack->production_len; index < total; index++)
            free(productions[index].rhs);
    }
    free(productions);
    if (!ok && error_buf && error_buf_size > 0u && error_buf[0] == '\0') {
        ppnative_v1_set_error(
            error_buf, error_buf_size,
            "failed to extend ParserPack grammar");
    }
    return ok;
}

bool ppnative_v1_finish_extended(
    PPNativeV1Result *result,
    const PPABIV1Pack *pack,
    const Atom *start_state,
    const PPNativeV1ForestExtension *extension,
    uint32_t replay_depth,
    uint32_t result_limit,
    char *error_buf,
    size_t error_buf_size) {
    if (!result || !pack || !start_state || replay_depth == 0u ||
        result_limit == 0u ||
        !ppnative_v1_extension_valid(
            pack, extension, error_buf, error_buf_size)) {
        if (error_buf && error_buf_size > 0u && error_buf[0] == '\0') {
            ppnative_v1_set_error(error_buf, error_buf_size,
                                  "bad ParserPack finish arguments");
        }
        return false;
    }
    return ppnative_v1_forest_canonicalize(
               result, pack, start_state, extension,
               error_buf, error_buf_size) &&
        ppnative_v1_replay(
               result, pack, extension, replay_depth, result_limit,
               error_buf, error_buf_size);
}

bool ppnative_v1_finish(PPNativeV1Result *result,
                        const PPABIV1Pack *pack,
                        const Atom *start_state,
                        uint32_t replay_depth,
                        uint32_t result_limit,
                        char *error_buf,
                        size_t error_buf_size) {
    return ppnative_v1_finish_extended(
        result, pack, start_state, NULL, replay_depth, result_limit,
        error_buf, error_buf_size);
}
