#include "native/tptp_official_snapshot_v1.h"
#include "native/grammar_canonical_term_v1.h"

#include "atom.h"
#include "lib_parse_native_grammar.h"
#include "native_sha256.h"
#include "parser.h"
#include "regular_span_dfa_v1.h"
#include "symbol.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define TPTP_UNICODE_MAX 0x10FFFFu
#define TPTP_NFA_STATE_LIMIT 8192u
#define TPTP_NFA_EDGE_LIMIT 65536u
#define TPTP_DFA_STATE_LIMIT 8192u
#define TPTP_DFA_TRANS_LIMIT 131072u



static bool is_app(const Atom *a, const char *head, uint32_t arity) {
    return a && a->kind == ATOM_EXPR && a->expr.len == arity + 1u &&
           a->expr.elems[0] && a->expr.elems[0]->kind == ATOM_SYMBOL &&
           atom_is_symbol(a->expr.elems[0], head);
}

static bool decode_digest(const Atom *atom, char out[65]) {
    size_t i;
    const char *value;
    if (!atom || atom->kind != ATOM_GROUNDED ||
        atom->ground.gkind != GV_STRING || !atom->ground.sval)
        return false;
    value = atom->ground.sval;
    if (strlen(value) != 64u)
        return false;
    for (i = 0u; i < 64u; i++) {
        const char c = value[i];
        if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f')))
            return false;
    }
    memcpy(out, value, 65u);
    return true;
}

static bool decode_text(const Atom *text, char *buf, size_t bufsz) {
    size_t n = 0u;
    const Atom *cur = text;
    if (!buf || bufsz == 0u)
        return false;
    buf[0] = '\0';
    while (is_app(cur, "bnf-v1:text-cons", 2u)) {
        const Atom *sc = cur->expr.elems[1];
        int64_t v;
        if (!sc || sc->kind != ATOM_GROUNDED || sc->ground.gkind != GV_INT)
            return false;
        v = sc->ground.ival;
        if (v < 0 || v > 127 || n + 1u >= bufsz)
            return false;
        buf[n++] = (char)v;
        cur = cur->expr.elems[2];
    }
    if (!is_app(cur, "bnf-v1:text-nil", 0u) &&
        !(cur && cur->kind == ATOM_SYMBOL &&
          atom_is_symbol((Atom *)cur, "bnf-v1:text-nil")))
        return false;
    buf[n] = '\0';
    return true;
}

/* The canonical table of a snapshot's parser, its helpers named by the
 * lowering origins the snapshot carries. */
static bool snapshot_canonical_table(const PPTableSnapshotV1 *snap,
                                     const CettaGrammarCanonicalTerminalsV1 *terminals,
                                     CettaGrammarCanonicalTableV1 **out, char *error,
                                     size_t error_size) {
    CettaGrammarCanonicalOriginV1 *origins =
        calloc(snap->origin_len ? snap->origin_len : 1u, sizeof(*origins));
    bool ok;
    uint32_t i;
    if (!origins) {
        if (error && error_size)
            snprintf(error, error_size, "canonical table: out of memory");
        return false;
    }
    for (i = 0u; i < snap->origin_len; i++) {
        origins[i].helper = snap->origins[i].helper;
        origins[i].owner = snap->origins[i].owner;
        origins[i].kind = (CettaGrammarCanonicalRuleKindV1)snap->origins[i].kind;
        origins[i].repeat = (char)snap->origins[i].repeat;
    }
    ok = cetta_grammar_canonical_table_from_slr_v1(&snap->slr, terminals, origins,
                                                   snap->origin_len, out, error, error_size);
    free(origins);
    return ok;
}


static bool name_is_generated(const char *name) {
    return name && name[0] == '#' && strncmp(name, "#token:", 7) != 0 &&
           strcmp(name, "#layout") != 0 && strcmp(name, "#entry") != 0 &&
           strcmp(name, "#white") != 0;
}

static bool is_wrapper_name(const char *name) {
    return name && strncmp(name, "#token:", 7) == 0;
}

static bool is_layout_name(const char *name) {
    return name && (strcmp(name, "#layout") == 0 ||
                    strcmp(name, "#entry") == 0 ||
                    strcmp(name, "#white") == 0);
}

/* A token carries a lexeme when its tag's language holds more than one
 * string (derived from the lexer DFA); skipped layout never reaches values. */
static bool snapshot_tag_is_value(const PPTableSnapshotV1 *snap, uint32_t tag) {
    uint32_t i;
    if (!snap || !snap->tag_carries_lexeme || tag >= snap->tag_name_len ||
        !snap->tag_carries_lexeme[tag])
        return false;
    for (i = 0u; i < snap->skip_tag_len; i++) {
        if (snap->skip_tags[i] == tag)
            return false;
    }
    return true;
}

static bool snapshot_symbol_is_value(const PPTableSnapshotV1 *snap,
                                     SymbolId symbol) {
    uint32_t tag;
    if (!snap || !snap->tag_symbol_ids || symbol == SYMBOL_ID_NONE)
        return false;
    for (tag = 0u; tag < snap->tag_name_len; tag++) {
        if (snap->tag_symbol_ids[tag] == symbol)
            return snapshot_tag_is_value(snap, tag);
    }
    return false;
}

static bool canonical_terminal_is_value(void *context, SymbolId terminal) {
    return snapshot_symbol_is_value(context, terminal);
}

static const char *canonical_terminal_fixed_text(void *context, SymbolId terminal) {
    const PPTableSnapshotV1 *snap = context;
    uint32_t tag;
    if (!snap || !snap->tag_symbol_ids || !snap->tag_fixed_texts)
        return NULL;
    for (tag = 0u; tag < snap->tag_name_len; tag++) {
        if (snap->tag_symbol_ids[tag] == terminal)
            return snap->tag_fixed_texts[tag];
    }
    return NULL;
}

static Atom *source_token_string(Arena *arena, const char *text,
                                 size_t text_len,
                                 const CettaTptpLexTokenV1 *token);

/* Two adjacent texts need layout when the longest token starting at the
 * first one runs past it. */
static bool snapshot_adjacent_needs_space(void *context, const char *left, size_t left_len,
                                          const char *right, size_t right_len) {
    const PPTableSnapshotV1 *snap = context;
    char buffer[256], local[128];
    RSDFAV1Token hits[128];
    RSDFAV1CursorScanResult scan;
    CettaLpNativeUtf8ScalarBuffer scalars;
    uint32_t left_scalars = 0u, best_end = 0u, i;
    bool needs;
    size_t k;
    if (left_len + right_len > sizeof(buffer) || snap->dfa.tag_len > 128u)
        return true;
    for (k = 0u; k < left_len; k++) {
        if (((unsigned char)left[k] & 0xC0u) != 0x80u)
            left_scalars++;
    }
    memcpy(buffer, left, left_len);
    memcpy(buffer + left_len, right, right_len);
    cetta_lp_native_utf8_scalar_buffer_init(&scalars);
    if (!cetta_lp_native_utf8_scalar_buffer_prepare(&scalars, (const uint8_t *)buffer,
                                                    left_len + right_len, local,
                                                    sizeof(local))) {
        cetta_lp_native_utf8_scalar_buffer_free(&scalars);
        return true;
    }
    memset(&scan, 0, sizeof(scan));
    if (!rsdfa_v1_program_scan_cursor_longest_prevalidated(&snap->dfa, &scalars.view, 0u,
                                                           1000000ull, hits, snap->dfa.tag_len,
                                                           &scan, local, sizeof(local))) {
        cetta_lp_native_utf8_scalar_buffer_free(&scalars);
        return true;
    }
    for (i = 0u; i < scan.accept_len; i++) {
        if (hits[i].end_scalar > best_end)
            best_end = hits[i].end_scalar;
    }
    needs = best_end != left_scalars;
    cetta_lp_native_utf8_scalar_buffer_free(&scalars);
    return needs;
}

bool cetta_tptp_snapshot_canonical_print_v1(const PPTableSnapshotV1 *snap,
                                            const CettaGrammarCanonicalTableV1 *table,
                                            const Atom *term,
                                            const char *sort, char **out, size_t *out_len,
                                            char *error, size_t error_size) {
    CettaGrammarCanonicalTerminalsV1 terminals = {
        canonical_terminal_is_value, canonical_terminal_fixed_text, (void *)snap};
    CettaGrammarCanonicalTableV1 *built = NULL;
    bool ok;
    if (out)
        *out = NULL;
    if (!snap || snap->slr.production_len == 0u || !sort) {
        if (error && error_size)
            snprintf(error, error_size, "canonical print: missing parser tables");
        return false;
    }
    if (!table) {
        if (!snapshot_canonical_table(snap, &terminals, &built, error, error_size))
            return false;
        table = built;
    }
    ok = cetta_grammar_canonical_print_v1(table, term, symbol_intern_cstr(g_symbols, sort),
                                          snapshot_adjacent_needs_space, (void *)snap,
                                          out, out_len, error, error_size);
    cetta_grammar_canonical_table_free_v1(built);
    return ok;
}

bool cetta_tptp_snapshot_canonical_table_v1(const PPTableSnapshotV1 *snap,
                                            CettaGrammarCanonicalTableV1 **out,
                                            char *error, size_t error_size) {
    CettaGrammarCanonicalTerminalsV1 terminals = {
        canonical_terminal_is_value, canonical_terminal_fixed_text, (void *)snap};
    if (out)
        *out = NULL;
    if (!snap || snap->slr.production_len == 0u || !out) {
        if (error && error_size)
            snprintf(error, error_size, "canonical table: missing parser tables");
        return false;
    }
    return snapshot_canonical_table(snap, &terminals, out,
                                                     error, error_size);
}

bool cetta_tptp_snapshot_canonical_print_wrapped_v1(
    const PPTableSnapshotV1 *snap, const CettaGrammarCanonicalTableV1 *table,
    const Atom *term, const char *sort,
    const SymbolId *wrappers, uint32_t wrapper_len, char **out, size_t *out_len,
    char *error, size_t error_size) {
    CettaGrammarCanonicalTerminalsV1 terminals = {
        canonical_terminal_is_value, canonical_terminal_fixed_text, (void *)snap};
    CettaGrammarCanonicalTableV1 *built = NULL;
    bool ok;
    if (out)
        *out = NULL;
    if (!snap || snap->slr.production_len == 0u || !sort) {
        if (error && error_size)
            snprintf(error, error_size, "canonical print: missing parser tables");
        return false;
    }
    if (!table) {
        if (!snapshot_canonical_table(snap, &terminals, &built, error, error_size))
            return false;
        table = built;
    }
    ok = cetta_grammar_canonical_print_wrapped_v1(
        table, term, symbol_intern_cstr(g_symbols, sort), wrappers, wrapper_len,
        snapshot_adjacent_needs_space, (void *)snap, out, out_len, error, error_size);
    cetta_grammar_canonical_table_free_v1(built);
    return ok;
}

bool cetta_tptp_snapshot_canonical_language_v1(const PPTableSnapshotV1 *snap,
                                               Arena *arena, Atom **out,
                                               char *error, size_t error_size) {
    CettaGrammarCanonicalTerminalsV1 terminals = {
        canonical_terminal_is_value, canonical_terminal_fixed_text, (void *)snap};
    CettaGrammarCanonicalTableV1 *table = NULL;
    bool ok;
    if (out)
        *out = NULL;
    if (!snap || snap->slr.production_len == 0u) {
        if (error && error_size)
            snprintf(error, error_size, "canonical language: missing parser tables");
        return false;
    }
    if (!snapshot_canonical_table(snap, &terminals, &table,
                                                   error, error_size))
        return false;
    ok = cetta_grammar_canonical_language_v1(table, "TptpCanonicalV1", arena, out,
                                             error, error_size);
    cetta_grammar_canonical_table_free_v1(table);
    return ok;
}

typedef struct {
    const CettaTptpLexTokenV1 *tokens;
    uint32_t token_len;
    const char *text;
    size_t text_len;
} CanonicalLexemeContext;

/* A token leaf, labelled #token:T, covers exactly one lexer token: token T
 * with that token's source bytes. */
static bool canonical_leaf(void *context, Arena *arena, const Atom *node, const char *label,
                           SymbolId *token, Atom **text) {
    static const char prefix[] = "#token:";
    const CanonicalLexemeContext *lexemes = context;
    uint32_t low = 0u, high = lexemes->token_len;
    const Atom *start_atom;
    int64_t start;
    if (strncmp(label, prefix, sizeof(prefix) - 1u) != 0 || node->expr.len < 4u)
        return false;
    start_atom = node->expr.elems[2];
    if (start_atom->kind != ATOM_GROUNDED || start_atom->ground.gkind != GV_INT)
        return false;
    start = start_atom->ground.ival;
    while (low < high) {
        uint32_t middle = low + (high - low) / 2u;
        int64_t probe = (int64_t)lexemes->tokens[middle].start_scalar;
        if (probe == start) {
            *token = symbol_intern_cstr(g_symbols, label + sizeof(prefix) - 1u);
            *text = source_token_string(arena, lexemes->text, lexemes->text_len,
                                        &lexemes->tokens[middle]);
            return *text != NULL;
        }
        if (probe < start)
            low = middle + 1u;
        else
            high = middle;
    }
    return false;
}

static bool grow(void **p, uint32_t *len, uint32_t *cap, size_t sz,
                 uint32_t add) {
    uint32_t need;
    uint32_t ncap;
    void *n;
    if (add > UINT32_MAX - *len)
        return false;
    need = *len + add;
    if (need <= *cap)
        return true;
    ncap = *cap ? *cap : 64u;
    while (ncap < need) {
        if (ncap > UINT32_MAX / 2u)
            return false;
        ncap *= 2u;
    }
    if (sz && ncap > SIZE_MAX / sz)
        return false;
    n = realloc(*p, ncap * sz);
    if (!n)
        return false;
    if (ncap > *cap)
        memset((char *)n + (*cap) * sz, 0, (size_t)(ncap - *cap) * sz);
    *p = n;
    *cap = ncap;
    return true;
}

typedef struct {
    char name[96];
    Atom *expr;
} PackRule;

typedef struct {
    PackRule *rules;
    uint32_t rule_len;
    uint32_t rule_cap;
} PackIndex;

static bool pack_add_rule(PackIndex *idx, const char *name, Atom *expr) {
    if (!grow((void **)&idx->rules, &idx->rule_len, &idx->rule_cap,
              sizeof(*idx->rules), 1u))
        return false;
    snprintf(idx->rules[idx->rule_len].name,
             sizeof(idx->rules[idx->rule_len].name), "%s", name);
    idx->rules[idx->rule_len].expr = expr;
    idx->rule_len++;
    return true;
}

static Atom *pack_find(const PackIndex *idx, const char *name) {
    uint32_t i;
    for (i = 0u; i < idx->rule_len; i++) {
        if (strcmp(idx->rules[i].name, name) == 0)
            return idx->rules[i].expr;
    }
    return NULL;
}

static bool walk_entries(Atom *entries, PackIndex *idx) {
    while (is_app(entries, "bnf-v1:entries-cons", 2u)) {
        Atom *rule = entries->expr.elems[1];
        char name[96];
        if (is_app(rule, "bnf-v1:rule", 3u) &&
            decode_text(rule->expr.elems[1], name, sizeof(name))) {
            if (!pack_add_rule(idx, name, rule->expr.elems[2]))
                return false;
        }
        entries = entries->expr.elems[2];
    }
    return true;
}

typedef struct {
    char name[96];
    CettaLpNativeUnicodeRange *ranges;
    uint32_t range_len;
    bool except;
    uint32_t *excluded;
    uint32_t excluded_len;
} LexClass;

typedef struct {
    LexClass *classes;
    uint32_t len;
    uint32_t cap;
} LexIndex;

typedef struct {
    const PackIndex *pack;
    const LexIndex *lex;
    uint8_t *queued;
    uint8_t *token_rules;
    uint8_t *skip_rules;
    uint8_t *token_classes;
    uint8_t *skip_classes;
    uint32_t *queue;
    uint32_t queue_len;
    bool in_layout;
    bool in_entry;
    uint32_t entry_layout_count;
    uint32_t entry_payload_count;
    char content_start[96];
} PackTokenFrontier;

static int cmp_u32(const void *a, const void *b) {
    uint32_t x = *(const uint32_t *)a;
    uint32_t y = *(const uint32_t *)b;
    return (x > y) - (x < y);
}

static bool collect_scalars(Atom *node, uint32_t **out, uint32_t *len,
                            uint32_t *cap) {
    while (is_app(node, "bnf-v1:scalars-cons", 2u)) {
        const Atom *sc = node->expr.elems[1];
        int64_t v;
        if (!sc || sc->kind != ATOM_GROUNDED || sc->ground.gkind != GV_INT)
            return false;
        v = sc->ground.ival;
        if (v < 0 || (uint64_t)v > TPTP_UNICODE_MAX)
            return false;
        if (!grow((void **)out, len, cap, sizeof(**out), 1u))
            return false;
        (*out)[(*len)++] = (uint32_t)v;
        node = node->expr.elems[2];
    }
    return is_app(node, "bnf-v1:scalars-nil", 0u) ||
           (node && node->kind == ATOM_SYMBOL &&
            atom_is_symbol(node, "bnf-v1:scalars-nil"));
}

static bool emit_unicode_range(CettaLpNativeUnicodeRange *out, uint32_t *rlen,
                               uint32_t cap, uint32_t low, uint32_t high) {
    /* DFA rejects ranges that intersect the UTF-16 surrogate block. */
    if (low > high)
        return true;
    if (high < 0xD800u || low > 0xDFFFu) {
        if (*rlen >= cap)
            return false;
        out[*rlen].low = low;
        out[*rlen].high = high;
        (*rlen)++;
        return true;
    }
    if (low < 0xD800u) {
        if (*rlen >= cap)
            return false;
        out[*rlen].low = low;
        out[*rlen].high = 0xD7FFu;
        (*rlen)++;
    }
    if (high > 0xDFFFu) {
        if (*rlen >= cap)
            return false;
        out[*rlen].low = 0xE000u;
        out[*rlen].high = high;
        (*rlen)++;
    }
    return true;
}

static bool scalars_to_ranges(uint32_t *vals, uint32_t n,
                              CettaLpNativeUnicodeRange **ranges,
                              uint32_t *range_len) {
    uint32_t i;
    uint32_t rlen = 0u;
    if (n == 0u) {
        *ranges = NULL;
        *range_len = 0u;
        return true;
    }
    qsort(vals, n, sizeof(*vals), cmp_u32);
    *ranges = calloc(n, sizeof(**ranges));
    if (!*ranges)
        return false;
    (*ranges)[0].low = vals[0];
    (*ranges)[0].high = vals[0];
    rlen = 1u;
    for (i = 1u; i < n; i++) {
        if (vals[i] == vals[i - 1u])
            continue;
        if (vals[i] == (*ranges)[rlen - 1u].high + 1u)
            (*ranges)[rlen - 1u].high = vals[i];
        else {
            (*ranges)[rlen].low = vals[i];
            (*ranges)[rlen].high = vals[i];
            rlen++;
        }
    }
    *range_len = rlen;
    return true;
}

static bool except_to_ranges(uint32_t *ex, uint32_t n,
                             CettaLpNativeUnicodeRange **ranges,
                             uint32_t *range_len) {
    uint32_t i;
    uint32_t rlen = 0u;
    uint32_t prev;
    uint32_t *uniq = NULL;
    uint32_t un = 0u;
    if (n) {
        qsort(ex, n, sizeof(*ex), cmp_u32);
        uniq = malloc(n * sizeof(*uniq));
        if (!uniq)
            return false;
        uniq[un++] = ex[0];
        for (i = 1u; i < n; i++) {
            if (ex[i] != ex[i - 1u])
                uniq[un++] = ex[i];
        }
    }
    *ranges = calloc(un + 4u, sizeof(**ranges));
    if (!*ranges) {
        free(uniq);
        return false;
    }
    prev = 0u;
    for (i = 0u; i < un; i++) {
        if (uniq[i] > prev) {
            if (!emit_unicode_range(*ranges, &rlen, un + 4u, prev,
                                    uniq[i] - 1u)) {
                free(uniq);
                return false;
            }
        }
        if (uniq[i] < TPTP_UNICODE_MAX)
            prev = uniq[i] + 1u;
        else {
            prev = TPTP_UNICODE_MAX + 1u;
            break;
        }
    }
    if (prev <= TPTP_UNICODE_MAX) {
        if (!emit_unicode_range(*ranges, &rlen, un + 4u, prev,
                                TPTP_UNICODE_MAX)) {
            free(uniq);
            return false;
        }
    }
    *range_len = rlen;
    free(uniq);
    return true;
}

static bool walk_lex(Atom *node, LexIndex *idx) {
    while (is_app(node, "bnf-v1:lexical-declarations-cons", 2u)) {
        Atom *d = node->expr.elems[1];
        char name[96];
        uint32_t *vals = NULL;
        uint32_t vlen = 0u;
        uint32_t vcap = 0u;
        LexClass *cls;
        if (!is_app(d, "bnf-v1:lexical-declaration", 5u) ||
            !decode_text(d->expr.elems[1], name, sizeof(name)))
            return false;
        if (!grow((void **)&idx->classes, &idx->len, &idx->cap,
                  sizeof(*idx->classes), 1u))
            return false;
        cls = &idx->classes[idx->len];
        memset(cls, 0, sizeof(*cls));
        snprintf(cls->name, sizeof(cls->name), "%s", name);
        if (is_app(d->expr.elems[3], "bnf-v1:lexical-points", 1u)) {
            if (!collect_scalars(d->expr.elems[3]->expr.elems[1], &vals, &vlen,
                                 &vcap) ||
                !scalars_to_ranges(vals, vlen, &cls->ranges, &cls->range_len)) {
                free(vals);
                return false;
            }
        } else if (is_app(d->expr.elems[3], "bnf-v1:lexical-except", 1u)) {
            cls->except = true;
            if (!collect_scalars(d->expr.elems[3]->expr.elems[1], &vals, &vlen,
                                 &vcap) ||
                !except_to_ranges(vals, vlen, &cls->ranges, &cls->range_len)) {
                free(vals);
                return false;
            }
        } else {
            free(vals);
            return false;
        }
        free(vals);
        idx->len++;
        node = node->expr.elems[2];
    }
    return is_app(node, "bnf-v1:lexical-declarations-nil", 0u) ||
           (node && node->kind == ATOM_SYMBOL &&
            atom_is_symbol(node, "bnf-v1:lexical-declarations-nil"));
}

static const LexClass *lex_find(const LexIndex *idx, const char *name) {
    uint32_t i;
    for (i = 0u; i < idx->len; i++) {
        if (strcmp(idx->classes[i].name, name) == 0)
            return &idx->classes[i];
    }
    return NULL;
}

static int32_t pack_rule_index(const PackIndex *idx, const char *name) {
    uint32_t i;
    for (i = 0u; i < idx->rule_len; i++)
        if (strcmp(idx->rules[i].name, name) == 0)
            return (int32_t)i;
    return -1;
}

static int32_t lex_class_index(const LexIndex *idx, const char *name) {
    uint32_t i;
    for (i = 0u; i < idx->len; i++)
        if (strcmp(idx->classes[i].name, name) == 0)
            return (int32_t)i;
    return -1;
}

static void pack_token_frontier_free(PackTokenFrontier *frontier) {
    free(frontier->queued);
    free(frontier->token_rules);
    free(frontier->skip_rules);
    free(frontier->token_classes);
    free(frontier->skip_classes);
    free(frontier->queue);
    memset(frontier, 0, sizeof(*frontier));
}

static bool pack_token_frontier_reference(PackTokenFrontier *frontier,
                                          const char *name) {
    const bool wrapper = is_wrapper_name(name);
    const char *target = wrapper ? name + 7 : name;
    int32_t rule = pack_rule_index(frontier->pack, target);
    int32_t cls = lex_class_index(frontier->lex, target);
    if (frontier->in_entry) {
        if (strcmp(name, "#layout") == 0) {
            frontier->entry_layout_count++;
        } else if (!wrapper && rule >= 0) {
            frontier->entry_payload_count++;
            if (frontier->entry_payload_count == 1u)
                snprintf(frontier->content_start,
                         sizeof(frontier->content_start), "%s", name);
        }
    }
    if (frontier->in_layout || wrapper || cls >= 0) {
        if (rule >= 0) {
            (frontier->in_layout ? frontier->skip_rules
                                 : frontier->token_rules)[rule] = 1u;
            return true;
        }
        if (cls >= 0) {
            (frontier->in_layout ? frontier->skip_classes
                                 : frontier->token_classes)[cls] = 1u;
            return true;
        }
        return false;
    }
    if (rule < 0)
        return false;
    if (!frontier->queued[rule]) {
        frontier->queued[rule] = 1u;
        frontier->queue[frontier->queue_len++] = (uint32_t)rule;
    }
    return true;
}

static bool pack_token_frontier_references(Atom *node,
                                           PackTokenFrontier *frontier,
                                           uint32_t depth) {
    uint32_t i;
    char name[96];
    if (!node || depth > 1024u)
        return false;
    if (is_app(node, "bnf-v1:reference", 2u)) {
        return decode_text(node->expr.elems[1], name, sizeof(name)) &&
               pack_token_frontier_reference(frontier, name);
    }
    if (is_app(node, "bnf-v1:literal", 2u) || node->kind != ATOM_EXPR)
        return true;
    for (i = 1u; i < node->expr.len; i++) {
        if (!pack_token_frontier_references(node->expr.elems[i], frontier,
                                            depth + 1u))
            return false;
    }
    return true;
}

/* The source-composed grammar identifies scanner roots by reachable token
 * wrappers. Its #layout rule identifies the roots that consume but do not
 * produce parser terminals. No list of TPTP token spellings is consulted. */
static bool pack_token_frontier_build(const PackIndex *pack,
                                      const LexIndex *lex,
                                      const char *start_name,
                                      PackTokenFrontier *frontier) {
    int32_t start;
    uint32_t next;
    memset(frontier, 0, sizeof(*frontier));
    start = pack_rule_index(pack, start_name);
    if (start < 0 || pack->rule_len > INT32_MAX || lex->len > INT32_MAX)
        return false;
    frontier->pack = pack;
    frontier->lex = lex;
    snprintf(frontier->content_start, sizeof(frontier->content_start),
             "%s", start_name);
    frontier->queued = calloc(pack->rule_len, 1u);
    frontier->token_rules = calloc(pack->rule_len, 1u);
    frontier->skip_rules = calloc(pack->rule_len, 1u);
    frontier->token_classes = calloc(lex->len ? lex->len : 1u, 1u);
    frontier->skip_classes = calloc(lex->len ? lex->len : 1u, 1u);
    frontier->queue = calloc(pack->rule_len, sizeof(*frontier->queue));
    if (!frontier->queued || !frontier->token_rules ||
        !frontier->skip_rules || !frontier->token_classes ||
        !frontier->skip_classes || !frontier->queue)
        return false;
    frontier->queued[start] = 1u;
    frontier->queue[frontier->queue_len++] = (uint32_t)start;
    for (next = 0u; next < frontier->queue_len; next++) {
        uint32_t rule = frontier->queue[next];
        frontier->in_entry = rule == (uint32_t)start;
        frontier->in_layout =
            strcmp(pack->rules[rule].name, "#layout") == 0;
        if (!pack_token_frontier_references(pack->rules[rule].expr,
                                            frontier, 0u))
            return false;
    }
    if (frontier->entry_layout_count > 0u &&
        frontier->entry_payload_count != 1u)
        return false;
    return true;
}

static void lex_free(LexIndex *idx) {
    uint32_t i;
    if (!idx)
        return;
    for (i = 0u; i < idx->len; i++)
        free(idx->classes[i].ranges);
    free(idx->classes);
    memset(idx, 0, sizeof(*idx));
}

typedef struct {
    uint32_t from;
    uint32_t to;
    RSDFAV1NfaEdgeKind kind;
    uint32_t range_begin;
    uint32_t range_len;
} RawEdge;

typedef struct {
    uint32_t start;
    uint32_t accept;
} Frag;

typedef struct {
    char name[96];
    Frag frag;
    bool busy;
    bool done;
} NameMemo;

typedef struct {
    uint32_t state_len;
    RawEdge *edges;
    uint32_t edge_len;
    uint32_t edge_cap;
    CettaLpNativeUnicodeRange *ranges;
    uint32_t range_len;
    uint32_t range_cap;
    RSDFAV1NfaAccept *accepts;
    uint32_t accept_len;
    uint32_t accept_cap;
    NameMemo *memo;
    uint32_t memo_len;
    uint32_t memo_cap;
    const PackIndex *pack;
    const LexIndex *lex;
    char error[256];
} NfaBuild;

static void nfa_free(NfaBuild *b) {
    if (!b)
        return;
    free(b->edges);
    free(b->ranges);
    free(b->accepts);
    free(b->memo);
    memset(b, 0, sizeof(*b));
}

static bool nfa_new_state(NfaBuild *b, uint32_t *out) {
    if (b->state_len >= TPTP_NFA_STATE_LIMIT) {
        snprintf(b->error, sizeof(b->error), "NFA state limit");
        return false;
    }
    *out = b->state_len++;
    return true;
}

static bool nfa_edge(NfaBuild *b, uint32_t from, uint32_t to,
                     RSDFAV1NfaEdgeKind kind, uint32_t rb, uint32_t rl) {
    if (b->edge_len >= TPTP_NFA_EDGE_LIMIT) {
        snprintf(b->error, sizeof(b->error), "NFA edge limit");
        return false;
    }
    if (!grow((void **)&b->edges, &b->edge_len, &b->edge_cap,
              sizeof(*b->edges), 1u))
        return false;
    b->edges[b->edge_len].from = from;
    b->edges[b->edge_len].to = to;
    b->edges[b->edge_len].kind = kind;
    b->edges[b->edge_len].range_begin = rb;
    b->edges[b->edge_len].range_len = rl;
    b->edge_len++;
    return true;
}

static bool nfa_eps(NfaBuild *b, uint32_t from, uint32_t to) {
    return nfa_edge(b, from, to, RSDFA_V1_NFA_EPSILON, 0u, 0u);
}

static bool nfa_ranges(NfaBuild *b, uint32_t from, uint32_t to,
                       const CettaLpNativeUnicodeRange *rs, uint32_t n) {
    uint32_t begin = b->range_len;
    uint32_t i;
    if (!n)
        return nfa_eps(b, from, to);
    if (!grow((void **)&b->ranges, &b->range_len, &b->range_cap,
              sizeof(*b->ranges), n))
        return false;
    for (i = 0u; i < n; i++)
        b->ranges[b->range_len++] = rs[i];
    return nfa_edge(b, from, to, RSDFA_V1_NFA_RANGES, begin, n);
}

static bool nfa_empty(NfaBuild *b, Frag *out) {
    uint32_t s;
    if (!nfa_new_state(b, &s))
        return false;
    out->start = s;
    out->accept = s;
    return true;
}

static bool compile_expr(NfaBuild *b, Atom *expr, Frag *out);
static bool compile_name(NfaBuild *b, const char *name, Frag *out);
static bool compile_literal_string(NfaBuild *b, const char *s, Frag *out);

static bool compile_element(NfaBuild *b, Atom *el, Frag *out) {
    Atom *inner = el;
    int rep = 0;
    Frag one;
    char name[96];
    if (is_app(el, "ebnf-v1:zero-or-more", 2u)) {
        inner = el->expr.elems[1];
        rep = 1;
    } else if (is_app(el, "ebnf-v1:one-or-more", 2u)) {
        inner = el->expr.elems[1];
        rep = 2;
    } else if (is_app(el, "ebnf-v1:optional", 2u)) {
        inner = el->expr.elems[1];
        rep = 3;
    }
    if (is_app(inner, "ebnf-v1:group", 2u))
        inner = inner->expr.elems[1];
    if (is_app(inner, "bnf-v1:expression", 2u)) {
        if (!compile_expr(b, inner, &one))
            return false;
    } else if (is_app(inner, "bnf-v1:reference", 2u) &&
               decode_text(inner->expr.elems[1], name, sizeof(name))) {
        if (!compile_name(b, name, &one))
            return false;
    } else if (is_app(inner, "bnf-v1:literal", 2u) &&
               decode_text(inner->expr.elems[1], name, sizeof(name))) {
        if (!compile_literal_string(b, name, &one))
            return false;
    } else {
        snprintf(b->error, sizeof(b->error), "unhandled token element");
        return false;
    }
    if (rep == 0) {
        *out = one;
        return true;
    }
    {
        uint32_t ns;
        uint32_t na;
        if (!nfa_new_state(b, &ns) || !nfa_new_state(b, &na))
            return false;
        if (rep == 1 || rep == 3) {
            if (!nfa_eps(b, ns, na))
                return false;
        }
        if (!nfa_eps(b, ns, one.start) || !nfa_eps(b, one.accept, na))
            return false;
        if (rep != 3 && !nfa_eps(b, one.accept, one.start))
            return false;
        if (rep == 2) {
            /* A+ = A A* : require at least one by not epsilon ns->na */
        }
        out->start = ns;
        out->accept = na;
        return true;
    }
}

static bool compile_alternative(NfaBuild *b, Atom *elems, Frag *out) {
    bool first = true;
    Frag acc;
    memset(&acc, 0, sizeof(acc));
    while (is_app(elems, "bnf-v1:elements-cons", 2u)) {
        Frag one;
        if (!compile_element(b, elems->expr.elems[1], &one))
            return false;
        if (first) {
            acc = one;
            first = false;
        } else {
            if (!nfa_eps(b, acc.accept, one.start))
                return false;
            acc.accept = one.accept;
        }
        elems = elems->expr.elems[2];
    }
    if (first)
        return nfa_empty(b, out);
    *out = acc;
    return true;
}

static bool compile_expr(NfaBuild *b, Atom *expr, Frag *out) {
    Atom *alts;
    uint32_t ns;
    uint32_t na;
    bool any = false;
    if (!is_app(expr, "bnf-v1:expression", 2u)) {
        snprintf(b->error, sizeof(b->error), "expected bnf-v1:expression");
        return false;
    }
    if (!nfa_new_state(b, &ns) || !nfa_new_state(b, &na))
        return false;
    alts = expr->expr.elems[1];
    while (is_app(alts, "bnf-v1:alternatives-cons", 2u)) {
        Atom *alt = alts->expr.elems[1];
        Frag one;
        if (!is_app(alt, "bnf-v1:alternative", 2u))
            break;
        if (!compile_alternative(b, alt->expr.elems[1], &one))
            return false;
        if (!nfa_eps(b, ns, one.start) || !nfa_eps(b, one.accept, na))
            return false;
        any = true;
        alts = alts->expr.elems[2];
    }
    if (!any && !nfa_eps(b, ns, na))
        return false;
    out->start = ns;
    out->accept = na;
    return true;
}

static bool compile_literal_string(NfaBuild *b, const char *s, Frag *out) {
    uint32_t i;
    uint32_t prev;
    size_t n = strlen(s);
    if (!nfa_empty(b, out))
        return false;
    prev = out->start;
    for (i = 0u; i < n; i++) {
        uint32_t st;
        CettaLpNativeUnicodeRange r;
        r.low = (uint32_t)(unsigned char)s[i];
        r.high = r.low;
        if (!nfa_new_state(b, &st))
            return false;
        if (!nfa_ranges(b, prev, st, &r, 1u))
            return false;
        prev = st;
    }
    out->accept = prev;
    return true;
}

static bool compile_name(NfaBuild *b, const char *name, Frag *out) {
    const LexClass *cls;
    Atom *expr;
    uint32_t i;
    NameMemo *m;
    if (is_wrapper_name(name))
        name = name + 7;
    for (i = 0u; i < b->memo_len; i++) {
        if (strcmp(b->memo[i].name, name) == 0) {
            if (b->memo[i].busy) {
                snprintf(b->error, sizeof(b->error), "cyclic token %s", name);
                return false;
            }
            if (b->memo[i].done) {
                *out = b->memo[i].frag;
                return true;
            }
        }
    }
    cls = lex_find(b->lex, name);
    if (cls) {
        uint32_t s;
        uint32_t t;
        if (!nfa_new_state(b, &s) || !nfa_new_state(b, &t))
            return false;
        if (!nfa_ranges(b, s, t, cls->ranges, cls->range_len))
            return false;
        out->start = s;
        out->accept = t;
        return true;
    }
    expr = pack_find(b->pack, name);
    if (!expr) {
        snprintf(b->error, sizeof(b->error), "unknown token name %s", name);
        return false;
    }
    /* Do not share token fragments: tagging adds accept epsilons onto the
     * fragment accept, and a shared accept would fire every tag that inlined
     * the same rule. Clone per use; cycle detection still uses memo.busy. */
    if (!grow((void **)&b->memo, &b->memo_len, &b->memo_cap, sizeof(*b->memo),
              1u))
        return false;
    m = &b->memo[b->memo_len];
    snprintf(m->name, sizeof(m->name), "%s", name);
    m->busy = true;
    m->done = false;
    b->memo_len++;
    if (!compile_expr(b, expr, out))
        return false;
    m->busy = false;
    b->memo_len--;
    return true;
}

static bool nfa_add_tagged(NfaBuild *b, const char *name, uint32_t tag) {
    Frag f;
    uint32_t acc;
    if (!compile_name(b, name, &f))
        return false;
    if (!nfa_new_state(b, &acc))
        return false;
    if (!nfa_eps(b, f.accept, acc))
        return false;
    if (!nfa_eps(b, 0u, f.start))
        return false;
    if (!grow((void **)&b->accepts, &b->accept_len, &b->accept_cap,
              sizeof(*b->accepts), 1u))
        return false;
    b->accepts[b->accept_len].state = acc;
    b->accepts[b->accept_len].tag = tag;
    b->accept_len++;
    return true;
}

static bool nfa_add_plus_class(NfaBuild *b, const char *cls_name,
                               uint32_t tag) {
    Frag one;
    uint32_t ns;
    uint32_t na;
    uint32_t acc;
    if (!compile_name(b, cls_name, &one))
        return false;
    if (!nfa_new_state(b, &ns) || !nfa_new_state(b, &na) ||
        !nfa_new_state(b, &acc))
        return false;
    if (!nfa_eps(b, ns, one.start) || !nfa_eps(b, one.accept, na) ||
        !nfa_eps(b, one.accept, one.start) || !nfa_eps(b, na, acc) ||
        !nfa_eps(b, 0u, ns))
        return false;
    if (!grow((void **)&b->accepts, &b->accept_len, &b->accept_cap,
              sizeof(*b->accepts), 1u))
        return false;
    b->accepts[b->accept_len].state = acc;
    b->accepts[b->accept_len].tag = tag;
    b->accept_len++;
    return true;
}

static const char *const kManifestCommonRoles[] = {
    "tptp-prepared-reader",
    "lib-tptp",
    "lib-bnf",
    "tptp-extended-bnf-source",
    "tptp-extended-bnf-parser-profile",
    "tptp-extended-bnf-ast",
    "tptp-extended-bnf-ast-projection",
    "tptp-lexical-to-ebnf",
    "tptp-syntax-to-ebnf",
    "bnf-ebnf-declaration-view",
    "bnf-ebnf-reachable-view",
    "bnf-ebnf-lowering",
    "bnf-plain-denotation",
    "tptp-snapshot-specializer",
    "tptp-snapshot-specializer-header",
    "parser-pack-table-snapshot",
    "parser-pack-table-snapshot-header",
    "native-grammar-parser",
    "native-grammar-parser-header",
    NULL};

static const char *const kManifestCommonPaths[] = {
    "langdef/tptp/official_prepared_reader_v1.metta",
    "lib/lib_tptp.metta",
    "lib/lib_bnf.metta",
    "langdef/tptp/official_extended_bnf_source_v1.metta",
    "langdef/tptp/official_extended_bnf_parser_profile_v1.metta",
    "langdef/tptp/official_extended_bnf_ast_v1.metta",
    "langdef/tptp/official_extended_bnf_ast_projection_v1.metta",
    "langdef/tptp/official_lexical_to_ebnf_v1.metta",
    "langdef/tptp/official_syntax_to_ebnf_v1.metta",
    "langdef/bnf/ebnf_declaration_view_v1.metta",
    "langdef/bnf/ebnf_reachable_view_v1.metta",
    "langdef/bnf/ebnf_lowering_v1.metta",
    "langdef/bnf/plain_bnf_denotation_v1.metta",
    "native/tptp_official_snapshot_v1.c",
    "native/tptp_official_snapshot_v1.h",
    "experiments/gslt2parse_foundation/native/parser_pack_table_snapshot_v1.c",
    "experiments/gslt2parse_foundation/native/parser_pack_table_snapshot_v1.h",
    "src/lib_parse_native_grammar.c",
    "src/lib_parse_native_grammar.h",
    NULL};

static const char *const kManifestCorpusCompatibleRoles[] = {
    "tptp-corpus-compatibility-native-types",
    "tptp-corpus-compatibility",
    NULL};

static const char *const kManifestCorpusCompatiblePaths[] = {
    "langdef/tptp/official_corpus_compatibility_native_types_v1.metta",
    "langdef/tptp/official_corpus_compatibility_v1.metta",
    NULL};

_Static_assert(sizeof(kManifestCommonRoles) == sizeof(kManifestCommonPaths),
               "TPTP manifest common role/path mismatch");
_Static_assert(sizeof(kManifestCorpusCompatibleRoles) ==
                   sizeof(kManifestCorpusCompatiblePaths),
               "TPTP manifest profile role/path mismatch");

static size_t manifest_role_count(const char *const *roles) {
    size_t i;
    for (i = 0u; roles[i]; i++)
        ;
    return i;
}

static bool manifest_role_index(const char *profile, const char *role,
                                size_t *out_index, size_t *out_count) {
    const size_t common_count = manifest_role_count(kManifestCommonRoles);
    const bool compatible = strcmp(profile, "corpus-compatible") == 0;
    const size_t profile_count =
        compatible ? manifest_role_count(kManifestCorpusCompatibleRoles) : 0u;
    size_t i;
    *out_count = common_count + profile_count;
    for (i = 0u; i < common_count; i++) {
        if (strcmp(kManifestCommonRoles[i], role) == 0) {
            *out_index = i;
            return true;
        }
    }
    for (i = 0u; i < profile_count; i++) {
        if (strcmp(kManifestCorpusCompatibleRoles[i], role) == 0) {
            *out_index = common_count + i;
            return true;
        }
    }
    return false;
}

static const char *manifest_role_path(const char *profile,
                                      size_t role_index) {
    const size_t common_count = manifest_role_count(kManifestCommonRoles);
    if (role_index < common_count)
        return kManifestCommonPaths[role_index];
    if (strcmp(profile, "corpus-compatible") == 0) {
        role_index -= common_count;
        if (role_index <
            manifest_role_count(kManifestCorpusCompatibleRoles))
            return kManifestCorpusCompatiblePaths[role_index];
    }
    return NULL;
}

static bool manifest_sources_valid(const Atom *sources,
                                   const char *profile) {
    const Atom *cursor = sources;
    bool seen[64] = {false};
    size_t role_len = 0u;
    const size_t expected_len =
        manifest_role_count(kManifestCommonRoles) +
        (strcmp(profile, "corpus-compatible") == 0
             ? manifest_role_count(kManifestCorpusCompatibleRoles)
             : 0u);
    while (is_app(cursor, "TptpReaderArtifactSourcesConsV1", 2u)) {
        const Atom *source = cursor->expr.elems[1];
        const Atom *role;
        const Atom *path;
        const char *role_text;
        char digest[65];
        size_t role_index;
        size_t role_count;
        const char *expected_path;
        if (!is_app(source, "TptpReaderArtifactSourceV1", 3u))
            return false;
        role = source->expr.elems[1];
        path = source->expr.elems[2];
        if (!role || role->kind != ATOM_SYMBOL ||
            !path || path->kind != ATOM_GROUNDED ||
            path->ground.gkind != GV_STRING || !path->ground.sval ||
            path->ground.sval[0] == '\0' ||
            !decode_digest(source->expr.elems[3], digest) ||
            role_len >= sizeof(seen) / sizeof(seen[0]))
            return false;
        role_text = atom_name_cstr((Atom *)role);
        if (!role_text || role_text[0] == '\0' ||
            !manifest_role_index(profile, role_text, &role_index,
                                 &role_count) ||
            !(expected_path = manifest_role_path(profile, role_index)) ||
            !atom_string_equals_cstr(path, expected_path) ||
            role_count > sizeof(seen) / sizeof(seen[0]) ||
            seen[role_index])
            return false;
        seen[role_index] = true;
        role_len++;
        cursor = cursor->expr.elems[2];
    }
    return role_len == expected_len &&
           cursor && cursor->kind == ATOM_SYMBOL &&
           atom_is_symbol((Atom *)cursor,
                          "TptpReaderArtifactSourcesNilV1");
}

static bool manifest_parts(Atom *manifest, char profile[32],
                           char syntax_digest[65]) {
    const Atom *profile_atom;
    const char *profile_text;
    size_t profile_len;
    if (!is_app(manifest, "TptpReaderArtifactManifestV2", 3u))
        return false;
    profile_atom = manifest->expr.elems[1];
    if (!profile_atom || profile_atom->kind != ATOM_SYMBOL)
        return false;
    profile_text = atom_name_cstr((Atom *)profile_atom);
    profile_len = profile_text ? strlen(profile_text) : 0u;
    if ((strcmp(profile_text ? profile_text : "", "strict") != 0 &&
         strcmp(profile_text ? profile_text : "", "corpus-compatible") != 0) ||
        profile_len == 0u || profile_len >= 32u ||
        !decode_digest(manifest->expr.elems[2], syntax_digest) ||
        !manifest_sources_valid(manifest->expr.elems[3], profile_text))
        return false;
    memcpy(profile, profile_text, profile_len + 1u);
    return true;
}

static bool artifact_digest(Atom *manifest, Atom *lowered,
                            char digest[65]) {
    static const char domain[] = "TptpReaderArtifactV2";
    Atom *terms[2] = {manifest, lowered};
    CettaNativeSha256 sha;
    Arena scratch;
    size_t i;
    arena_init(&scratch);
    cetta_native_sha256_init(&sha);
    cetta_native_sha256_update(
        &sha, (const uint8_t *)domain, sizeof(domain));
    for (i = 0u; i < 2u; i++) {
        const char *printed = atom_to_parseable_string(&scratch, terms[i]);
        uint8_t framed[8];
        uint64_t length;
        size_t byte;
        if (!printed) {
            arena_free(&scratch);
            return false;
        }
        length = (uint64_t)strlen(printed);
        for (byte = 0u; byte < sizeof(framed); byte++)
            framed[sizeof(framed) - 1u - byte] =
                (uint8_t)(length >> (byte * 8u));
        cetta_native_sha256_update(&sha, framed, sizeof(framed));
        cetta_native_sha256_update(
            &sha, (const uint8_t *)printed, (size_t)length);
    }
    cetta_native_sha256_finish_hex(&sha, digest);
    arena_free(&scratch);
    return true;
}

static bool pack_parts(Atom *pack, char syntax_digest[65],
                       char pack_digest[65], char profile[32],
                       char start_name[96], Atom **orig_entries,
                       Atom **lex_env) {
    Atom *manifest;
    Atom *lg;
    Atom *doc;
    Atom *auth;
    if (!is_app(pack, "TptpPreparedPackV2", 2u))
        return false;
    manifest = pack->expr.elems[1];
    lg = pack->expr.elems[2];
    if (!manifest_parts(manifest, profile, syntax_digest) ||
        !artifact_digest(manifest, lg, pack_digest))
        return false;
    if (!is_app(lg, "EBNF:LoweredGrammar", 4u))
        return false;
    doc = lg->expr.elems[1];
    auth = lg->expr.elems[2];
    if (!is_app(doc, "bnf-v1:document", 2u))
        return false;
    *orig_entries = doc->expr.elems[1];
    if (!is_app(auth, "bnf-v1:grammar-authority", 2u))
        return false;
    if (!is_app(auth->expr.elems[1], "bnf-v1:start", 1u) ||
        !decode_text(auth->expr.elems[1]->expr.elems[1], start_name, 96u))
        return false;
    *lex_env = auth->expr.elems[2];
    if (!is_app(*lex_env, "bnf-v1:lexical-environment", 1u))
        return false;
    *lex_env = (*lex_env)->expr.elems[1];
    return true;
}

static char *dup_cstr(const char *s) {
    size_t n = strlen(s);
    char *d = malloc(n + 1u);
    if (!d)
        return NULL;
    memcpy(d, s, n + 1u);
    return d;
}

static bool tag_add(char ***names, uint32_t *len, uint32_t *cap,
                    const char *name, uint32_t *out_tag) {
    uint32_t i;
    for (i = 0u; i < *len; i++) {
        if (strcmp((*names)[i], name) == 0) {
            *out_tag = i;
            return true;
        }
    }
    if (!grow((void **)names, len, cap, sizeof(**names), 1u))
        return false;
    (*names)[*len] = dup_cstr(name);
    if (!(*names)[*len])
        return false;
    *out_tag = *len;
    (*len)++;
    return true;
}

static bool collect_literals(Atom *expr, char ***lits, uint32_t *len,
                             uint32_t *cap) {
    uint32_t i;
    char name[96];
    if (!expr)
        return true;
    if (is_app(expr, "bnf-v1:literal", 2u) &&
        decode_text(expr->expr.elems[1], name, sizeof(name))) {
        uint32_t tag;
        return tag_add(lits, len, cap, name, &tag);
    }
    if (expr->kind != ATOM_EXPR)
        return true;
    for (i = 0u; i < expr->expr.len; i++) {
        if (!collect_literals(expr->expr.elems[i], lits, len, cap))
            return false;
    }
    return true;
}

static bool slr_add_prod(CettaLpNativeGrammar *g, uint32_t *cap, Arena *arena,
                         const char *label, const CettaLpNativeSymbol *rhs,
                         uint32_t rhs_len) {
    CettaLpNativeProduction *p;
    if (g->production_len >= *cap) {
        uint32_t ncap = *cap ? *cap * 2u : 256u;
        CettaLpNativeProduction *grown =
            realloc(g->productions, ncap * sizeof(*grown));
        if (!grown)
            return false;
        memset(grown + *cap, 0, (ncap - *cap) * sizeof(*grown));
        g->productions = grown;
        *cap = ncap;
    }
    p = &g->productions[g->production_len];
    p->label = atom_symbol(arena, label)->sym_id;
    p->lhs = p->label;
    /* label may include #alt suffix; lhs is the NT without suffix handled by
     * caller via the same symbol as the rule name for alt 0, but we set lhs
     * from the base name stored in label before suffix... callers pass both. */
    p->rhs_len = rhs_len;
    if (rhs_len) {
        p->rhs = calloc(rhs_len, sizeof(*p->rhs));
        if (!p->rhs)
            return false;
        memcpy(p->rhs, rhs, rhs_len * sizeof(*rhs));
    }
    g->production_len++;
    return true;
}

static void alt_label(char *buf, size_t bufsz, const char *name, uint32_t alt) {
    uint32_t i;
    size_t n = strlen(name);
    if (n + 1u + alt + 1u > bufsz) {
        snprintf(buf, bufsz, "%s", name);
        return;
    }
    memcpy(buf, name, n);
    buf[n] = '#';
    for (i = 0u; i < alt; i++)
        buf[n + 1u + i] = 'x';
    buf[n + 1u + alt] = '\0';
}

static bool slr_symbol_from_name(Arena *arena,
                                 const PackTokenFrontier *frontier,
                                 const char *name,
                                 CettaLpNativeSymbol *out, bool *omit) {
    const char *tm = is_wrapper_name(name) ? name + 7 : name;
    int32_t rule;
    int32_t cls;
    *omit = false;
    if (is_layout_name(name) || name_is_generated(name)) {
        *omit = true;
        return true;
    }
    rule = pack_rule_index(frontier->pack, tm);
    cls = lex_class_index(frontier->lex, tm);
    if ((rule >= 0 && frontier->skip_rules[rule]) ||
        (cls >= 0 && frontier->skip_classes[cls])) {
        *omit = true;
        return true;
    }
    if (rule >= 0 && frontier->token_rules[rule]) {
        out->kind = CETTA_LP_NATIVE_SYMBOL_TM;
        out->name = atom_symbol(arena, tm)->sym_id;
        out->scope = 0;
        return true;
    }
    if (cls >= 0 && frontier->token_classes[cls]) {
        out->kind = CETTA_LP_NATIVE_SYMBOL_TM;
        out->name = atom_symbol(arena, tm)->sym_id;
        out->scope = 0;
        return true;
    }
    if (name_is_generated(tm)) {
        *omit = true;
        return true;
    }
    if (rule < 0 || !frontier->queued[rule])
        return false;
    out->kind = CETTA_LP_NATIVE_SYMBOL_HL;
    out->name = atom_symbol(arena, tm)->sym_id;
    out->scope = 0;
    return true;
}

/* Record what made a helper nonterminal this lowering generates. */
typedef struct {
    PPTableSnapshotOriginV1 *items;
    uint32_t len;
    uint32_t cap;
} SlrOrigins;

static bool slr_origin_add(SlrOrigins *origins, SymbolId helper, SymbolId owner, uint32_t kind,
                           char repeat) {
    if (origins->len == origins->cap) {
        uint32_t cap = origins->cap ? origins->cap * 2u : 16u;
        PPTableSnapshotOriginV1 *grown = realloc(origins->items, (size_t)cap * sizeof(*grown));
        if (!grown)
            return false;
        origins->items = grown;
        origins->cap = cap;
    }
    origins->items[origins->len++] =
        (PPTableSnapshotOriginV1){helper, owner, kind, (uint32_t)(unsigned char)repeat};
    return true;
}

static bool slr_fill_from_pack(CettaLpNativeGrammar *g, uint32_t *cap,
                               Arena *arena, const PackIndex *idx,
                               const PackTokenFrontier *frontier, SlrOrigins *origins,
                               char *error, size_t error_size) {
    uint32_t i;
    for (i = 0u; i < idx->rule_len; i++) {
        const char *rname = idx->rules[i].name;
        Atom *expr = idx->rules[i].expr;
        Atom *alts;
        uint32_t alt_i = 0u;
        if (!frontier->queued[i] || name_is_generated(rname) ||
            is_wrapper_name(rname) || is_layout_name(rname))
            continue;
        if (!is_app(expr, "bnf-v1:expression", 2u))
            continue;
        alts = expr->expr.elems[1];
        while (is_app(alts, "bnf-v1:alternatives-cons", 2u)) {
            Atom *alt = alts->expr.elems[1];
            Atom *elems;
            CettaLpNativeSymbol rhs_buf[64];
            uint32_t rhs_len = 0u;
            char lab[128];
            if (!is_app(alt, "bnf-v1:alternative", 2u))
                break;
            elems = alt->expr.elems[1];
            while (is_app(elems, "bnf-v1:elements-cons", 2u) && rhs_len < 64u) {
                Atom *el = elems->expr.elems[1];
                Atom *inner = el;
                int rep = 0;
                char name[96];
                CettaLpNativeSymbol one;
                bool omit = false;
                if (is_app(el, "ebnf-v1:zero-or-more", 2u)) {
                    inner = el->expr.elems[1];
                    rep = 1;
                } else if (is_app(el, "ebnf-v1:one-or-more", 2u)) {
                    inner = el->expr.elems[1];
                    rep = 2;
                } else if (is_app(el, "ebnf-v1:optional", 2u)) {
                    inner = el->expr.elems[1];
                    rep = 3;
                }
                if (is_app(inner, "ebnf-v1:group", 2u))
                    inner = inner->expr.elems[1];
                if (is_app(inner, "bnf-v1:expression", 2u)) {
                    /* Nested group: aux NT with the group's alternatives. */
                    char aux[64];
                    SymbolId aux_id;
                    Atom *galts = inner->expr.elems[1];
                    uint32_t galti = 0u;
                    snprintf(aux, sizeof(aux), "#grp-%u", g->production_len);
                    aux_id = atom_symbol(arena, aux)->sym_id;
                    if (!slr_origin_add(origins, aux_id, atom_symbol(arena, rname)->sym_id,
                                        CETTA_GRAMMAR_CANONICAL_GROUP_V1, 0))
                        return false;
                    while (is_app(galts, "bnf-v1:alternatives-cons", 2u)) {
                        Atom *galt = galts->expr.elems[1];
                        Atom *gel;
                        CettaLpNativeSymbol grhs[32];
                        uint32_t gl = 0u;
                        if (!is_app(galt, "bnf-v1:alternative", 2u))
                            break;
                        gel = galt->expr.elems[1];
                        while (is_app(gel, "bnf-v1:elements-cons", 2u) &&
                               gl < 32u) {
                            Atom *ge = gel->expr.elems[1];
                            char gn[96];
                            bool gomit = false;
                            CettaLpNativeSymbol gs;
                            if (is_app(ge, "ebnf-v1:group", 2u))
                                ge = ge->expr.elems[1];
                            if (is_app(ge, "bnf-v1:reference", 2u) &&
                                decode_text(ge->expr.elems[1], gn,
                                            sizeof(gn))) {
                                if (!slr_symbol_from_name(arena, frontier, gn, &gs,
                                                          &gomit))
                                    return false;
                                if (!gomit)
                                    grhs[gl++] = gs;
                            } else if (is_app(ge, "bnf-v1:literal", 2u) &&
                                       decode_text(ge->expr.elems[1], gn,
                                                   sizeof(gn))) {
                                gs.kind = CETTA_LP_NATIVE_SYMBOL_TM;
                                gs.name = atom_symbol(arena, gn)->sym_id;
                                gs.scope = 0;
                                grhs[gl++] = gs;
                            }
                            gel = gel->expr.elems[2];
                        }
                        {
                            CettaLpNativeProduction *gp;
                            if (g->production_len >= *cap)
                                break;
                            gp = &g->productions[g->production_len];
                            gp->label = aux_id;
                            gp->lhs = aux_id;
                            gp->rhs_len = gl;
                            if (gl) {
                                gp->rhs = calloc(gl, sizeof(*gp->rhs));
                                if (!gp->rhs)
                                    return false;
                                memcpy(gp->rhs, grhs, gl * sizeof(*grhs));
                            }
                            g->production_len++;
                        }
                        (void)galti;
                        galts = galts->expr.elems[2];
                    }
                    one.kind = CETTA_LP_NATIVE_SYMBOL_HL;
                    one.name = aux_id;
                    one.scope = 0;
                    omit = false;
                } else if (is_app(inner, "bnf-v1:reference", 2u) &&
                           decode_text(inner->expr.elems[1], name,
                                       sizeof(name))) {
                    if (!slr_symbol_from_name(arena, frontier, name, &one, &omit))
                        return false;
                } else if (is_app(inner, "bnf-v1:literal", 2u) &&
                           decode_text(inner->expr.elems[1], name,
                                       sizeof(name))) {
                    one.kind = CETTA_LP_NATIVE_SYMBOL_TM;
                    one.name = atom_symbol(arena, name)->sym_id;
                    one.scope = 0;
                    omit = false;
                } else {
                    omit = true;
                }
                if (!omit) {
                    if (rep == 0) {
                        rhs_buf[rhs_len++] = one;
                    } else {
                        char aux[64];
                        SymbolId aux_id;
                        CettaLpNativeProduction *p0;
                        CettaLpNativeProduction *p1;
                        snprintf(aux, sizeof(aux), "#rep-%u",
                                 g->production_len);
                        aux_id = atom_symbol(arena, aux)->sym_id;
                        if (!slr_origin_add(
                                origins, aux_id, atom_symbol(arena, rname)->sym_id,
                                rep == 3 ? CETTA_GRAMMAR_CANONICAL_OPTION_V1
                                         : CETTA_GRAMMAR_CANONICAL_REPETITION_V1,
                                rep == 1 ? '*' : rep == 2 ? '+' : '?'))
                            return false;
                        if (g->production_len + 2u >= *cap) {
                            snprintf(error, error_size, "SLR production cap");
                            return false;
                        }
                        p0 = &g->productions[g->production_len++];
                        p0->label = aux_id;
                        p0->lhs = aux_id;
                        p0->rhs_len = 0u;
                        p0->rhs = NULL;
                        p1 = &g->productions[g->production_len++];
                        p1->label = aux_id;
                        p1->lhs = aux_id;
                        p1->rhs_len = (rep == 3) ? 1u : 2u;
                        p1->rhs = calloc(p1->rhs_len, sizeof(*p1->rhs));
                        if (!p1->rhs)
                            return false;
                        p1->rhs[0] = one;
                        if (rep != 3) {
                            p1->rhs[1].kind = CETTA_LP_NATIVE_SYMBOL_HL;
                            p1->rhs[1].name = aux_id;
                            p1->rhs[1].scope = 0;
                        }
                        if (rep == 2)
                            rhs_buf[rhs_len++] = one;
                        rhs_buf[rhs_len].kind = CETTA_LP_NATIVE_SYMBOL_HL;
                        rhs_buf[rhs_len].name = aux_id;
                        rhs_buf[rhs_len].scope = 0;
                        rhs_len++;
                    }
                }
                elems = elems->expr.elems[2];
            }
            alt_label(lab, sizeof(lab), rname, alt_i);
            if (!slr_add_prod(g, cap, arena, lab, rhs_buf, rhs_len))
                return false;
            /* slr_add_prod sets lhs from the full label including #alt.
             * Override lhs to the unsuffixed NT. */
            g->productions[g->production_len - 1u].lhs =
                atom_symbol(arena, rname)->sym_id;
            alt_i++;
            alts = alts->expr.elems[2];
        }
    }
    return true;
}

/* Helper nonterminals are the ones the lowering origins name. */
static bool snapshot_is_helper(const PPTableSnapshotV1 *snap, SymbolId sym) {
    uint32_t i;
    for (i = 0u; i < snap->origin_len; i++) {
        if (snap->origins[i].helper == sym)
            return true;
    }
    return false;
}

/* A correspondence between the baseline's helpers and the extension's:
 * lowering numbers helpers by position, so an extension renames them. */
typedef struct {
    SymbolId *base;
    SymbolId *ext;
    uint32_t len;
    uint32_t cap;
} TptpHelperMapV1;

static bool helper_map_bind(TptpHelperMapV1 *map, SymbolId base,
                            SymbolId ext) {
    uint32_t i;
    for (i = 0u; i < map->len; i++) {
        if (map->base[i] == base || map->ext[i] == ext)
            return map->base[i] == base && map->ext[i] == ext;
    }
    if (map->len == map->cap) {
        uint32_t cap = map->cap ? map->cap * 2u : 16u;
        SymbolId *b = realloc(map->base, cap * sizeof(*b));
        SymbolId *e;
        if (!b)
            return false;
        map->base = b;
        e = realloc(map->ext, cap * sizeof(*e));
        if (!e)
            return false;
        map->ext = e;
        map->cap = cap;
    }
    map->base[map->len] = base;
    map->ext[map->len] = ext;
    map->len++;
    return true;
}

/* Whether an extension production is a baseline one: the same lhs up to
 * the helper map, and the same right side with helpers corresponding. */
static bool production_extends(
    const PPTableSnapshotV1 *ext, const CettaLpNativeSlrProgramProduction *p,
    const PPTableSnapshotV1 *base, const CettaLpNativeSlrProgramProduction *q,
    TptpHelperMapV1 *map) {
    uint32_t k;
    if (p->rhs_len != q->rhs_len)
        return false;
    for (k = 0u; k < p->rhs_len; k++) {
        const CettaLpNativeSymbol *a = &ext->slr.rhs[p->rhs_begin + k];
        const CettaLpNativeSymbol *b = &base->slr.rhs[q->rhs_begin + k];
        bool a_helper, b_helper;
        if (a->kind != b->kind || a->scope != b->scope)
            return false;
        a_helper = a->kind != CETTA_LP_NATIVE_SYMBOL_TM &&
                   snapshot_is_helper(ext, a->name);
        b_helper = b->kind != CETTA_LP_NATIVE_SYMBOL_TM &&
                   snapshot_is_helper(base, b->name);
        if (a_helper != b_helper)
            return false;
        if (a_helper ? !helper_map_bind(map, b->name, a->name)
                     : a->name != b->name)
            return false;
    }
    return true;
}

static int32_t production_by_label(const PPTableSnapshotV1 *snap,
                                   SymbolId label) {
    uint32_t i;
    for (i = 0u; i < snap->slr.authored_production_len; i++) {
        if (snap->slr.productions[i].label == label)
            return (int32_t)i;
    }
    return -1;
}

/* The next production with the given lhs after index from (or the first,
 * from = -1). */
static int32_t production_next_of(const PPTableSnapshotV1 *snap,
                                  SymbolId lhs, int32_t from) {
    uint32_t i;
    for (i = (uint32_t)(from + 1); i < snap->slr.authored_production_len;
         i++) {
        if (snap->slr.productions[i].lhs == lhs)
            return (int32_t)i;
    }
    return -1;
}

/*
 * Mark the productions an extension grammar adds to its baseline as avoided.
 * The extension must contain the baseline exactly: every baseline production
 * appears in it, named rules by label and helpers by their correspondence,
 * with the same right side.  The productions left over are the extension's
 * own named-rule productions, which are avoided, and helpers only they reach.
 * A derivation reducing no avoided production is then a baseline derivation.
 */
static bool snapshot_mark_extension(PPTableSnapshotV1 *ext,
                                    const PPTableSnapshotV1 *base,
                                    uint32_t *avoided_len,
                                    char *error, size_t error_size) {
    TptpHelperMapV1 map = {0};
    uint32_t i, done_len = 0u, marked = 0u;
    bool ok = false;
    const char *what = NULL;
    SymbolId at = SYMBOL_ID_NONE;

    for (i = 0u; i < ext->slr.authored_production_len; i++) {
        CettaLpNativeSlrProgramProduction *p = &ext->slr.productions[i];
        int32_t q;
        if (snapshot_is_helper(ext, p->lhs))
            continue;
        if (production_by_label(ext, p->label) != (int32_t)i) {
            what = "repeats the label";
            at = p->label;
            goto done;
        }
        q = production_by_label(base, p->label);
        if (q < 0) {
            p->avoided = true;
            marked++;
            continue;
        }
        if (base->slr.productions[q].lhs != p->lhs ||
            !production_extends(ext, p, base, &base->slr.productions[q],
                                &map)) {
            what = "changes the baseline production";
            at = p->label;
            goto done;
        }
    }
    for (i = 0u; i < base->slr.authored_production_len; i++) {
        const CettaLpNativeSlrProgramProduction *q = &base->slr.productions[i];
        if (!snapshot_is_helper(base, q->lhs) &&
            production_by_label(ext, q->label) < 0) {
            what = "lacks the baseline production";
            at = q->label;
            goto done;
        }
    }
    /* Each corresponding helper has the baseline helper's productions, in
     * order; their helpers correspond in turn. */
    while (done_len < map.len) {
        SymbolId b = map.base[done_len];
        SymbolId a = map.ext[done_len];
        int32_t qi = production_next_of(base, b, -1);
        int32_t pi = production_next_of(ext, a, -1);
        done_len++;
        while (qi >= 0 && pi >= 0) {
            if (!production_extends(ext, &ext->slr.productions[pi], base,
                                    &base->slr.productions[qi], &map)) {
                what = "changes the baseline helper";
                at = b;
                goto done;
            }
            qi = production_next_of(base, b, qi);
            pi = production_next_of(ext, a, pi);
        }
        if (qi >= 0 || pi >= 0) {
            what = "changes the baseline helper";
            at = b;
            goto done;
        }
    }
    for (i = 0u; i < base->origin_len; i++) {
        uint32_t m;
        bool mapped = false;
        for (m = 0u; m < map.len && !mapped; m++)
            mapped = map.base[m] == base->origins[i].helper;
        if (!mapped) {
            what = "lacks the baseline helper";
            at = base->origins[i].helper;
            goto done;
        }
    }
    *avoided_len = marked;
    ok = true;

done:
    if (!ok && error && error_size) {
        if (what)
            snprintf(error, error_size,
                     "the compatible grammar %s %s", what,
                     at == SYMBOL_ID_NONE ? "?" : symbol_bytes(g_symbols, at));
        else
            snprintf(error, error_size, "helper correspondence failed");
    }
    free(map.base);
    free(map.ext);
    return ok;
}

bool cetta_tptp_snapshot_construct_from_pack_v1(
    const char *pack_path,
    const char *baseline_path,
    const char *out_path,
    char *error,
    size_t error_size) {
    Arena arena;
    Atom *pack = NULL;
    Atom *entries = NULL;
    Atom *lex_env = NULL;
    PackIndex idx;
    LexIndex lex;
    PackTokenFrontier frontier;
    NfaBuild nfa;
    CettaLpNativeGrammar grammar;
    CettaLpNativeSlrPrepared prepared;
    CettaLpNativeSlrProgram slr;
    SlrOrigins slr_origins = {0};
    CettaLpNativeSlrSummary summary = {0};
    PPTableSnapshotV1 snap;
    PPTableSnapshotV1 base;
    RSDFAV1Plan dfa_plan;
    RSDFAV1Program dfa_prog;
    RSDFAV1BuildOutcome dfa_out = RSDFA_V1_BUILD_COMPLETED;
    RSDFAV1Nfa nfa_view;
    RSDFAV1NfaEdge *live_edges = NULL;
    uint32_t *starts = NULL;
    char **tag_names = NULL;
    uint32_t tag_len = 0u;
    uint32_t tag_cap = 0u;
    char **literals = NULL;
    uint32_t lit_len = 0u;
    uint32_t lit_cap = 0u;
    uint32_t *skip = NULL;
    uint32_t skip_len = 0u;
    Atom *start_sym;
    uint32_t i;
    uint32_t prod_cap = 0u;
    bool ok = false;
    char local[512] = {0};
    char syntax_digest[65] = {0};
    char pack_digest[65] = {0};
    char profile[32] = {0};
    char start_name[96] = {0};
    uint32_t tag;

    if (error && error_size)
        error[0] = '\0';
    memset(&idx, 0, sizeof(idx));
    memset(&lex, 0, sizeof(lex));
    memset(&frontier, 0, sizeof(frontier));
    memset(&nfa, 0, sizeof(nfa));
    arena_init(&arena);
    cetta_lp_native_grammar_init(&grammar);
    cetta_lp_native_slr_prepared_init(&prepared);
    cetta_lp_native_slr_program_init(&slr);
    pp_table_snapshot_v1_init(&snap);
    pp_table_snapshot_v1_init(&base);
    rsdfa_v1_plan_init(&dfa_plan);
    rsdfa_v1_program_init(&dfa_prog);

    if (!cetta_tptp_read_atom_v1(pack_path, &arena, &pack, local,
                                 sizeof(local))) {
        if (error && error_size)
            snprintf(error, error_size, "%s",
                     local[0] ? local : "pack read failed");
        goto done;
    }
    if (!pack_parts(pack, syntax_digest, pack_digest, profile, start_name,
                    &entries, &lex_env) ||
        !walk_entries(entries, &idx) || !walk_lex(lex_env, &lex) ||
        !pack_token_frontier_build(&idx, &lex, start_name, &frontier)) {
        snprintf(error ? error : local, error ? error_size : sizeof(local),
                 "pack document or lexical frontier failed");
        goto done;
    }
    /* The corpus-compatible profile is the strict grammar with extensions;
     * a reading uses them only where no reading does with fewer. */
    if ((strcmp(profile, "corpus-compatible") == 0) != (baseline_path != NULL)) {
        if (error && error_size)
            snprintf(error, error_size,
                     baseline_path
                         ? "only a corpus-compatible snapshot has a baseline"
                         : "a corpus-compatible snapshot needs its strict "
                           "snapshot as baseline");
        goto done;
    }
    if (baseline_path &&
        !cetta_tptp_snapshot_load_bound_v1(
            &base, baseline_path, syntax_digest, "strict", NULL,
            error, error_size))
        goto done;
    nfa.pack = &idx;
    nfa.lex = &lex;
    if (!nfa_new_state(&nfa, &tag)) /* state 0 = lexer start */
        goto done;
    (void)tag;

    for (i = 0u; i < idx.rule_len; i++) {
        const char *name;
        if (!frontier.token_rules[i] && !frontier.skip_rules[i])
            continue;
        name = idx.rules[i].name;
        if (!tag_add(&tag_names, &tag_len, &tag_cap, name, &tag) ||
            !nfa_add_tagged(&nfa, name, tag)) {
            snprintf(error ? error : local, error ? error_size : sizeof(local),
                     "token DFA %s: %s", name,
                     nfa.error[0] ? nfa.error : "failed");
            goto done;
        }
    }
    for (i = 0u; i < lex.len; i++) {
        const char *name;
        if (!frontier.token_classes[i] && !frontier.skip_classes[i])
            continue;
        name = lex.classes[i].name;
        if (!tag_add(&tag_names, &tag_len, &tag_cap, name, &tag) ||
            !(frontier.skip_classes[i]
                  ? nfa_add_plus_class(&nfa, name, tag)
                  : nfa_add_tagged(&nfa, name, tag))) {
            snprintf(error ? error : local, error ? error_size : sizeof(local),
                     "lexical DFA %s: %s", name,
                     nfa.error[0] ? nfa.error : "failed");
            goto done;
        }
    }
    for (i = 0u; i < idx.rule_len; i++) {
        if (!collect_literals(idx.rules[i].expr, &literals, &lit_len, &lit_cap))
            goto done;
    }
    for (i = 0u; i < lit_len; i++) {
        Frag litf;
        uint32_t acc;
        if (!tag_add(&tag_names, &tag_len, &tag_cap, literals[i], &tag))
            goto done;
        if (!compile_literal_string(&nfa, literals[i], &litf) ||
            !nfa_new_state(&nfa, &acc) || !nfa_eps(&nfa, litf.accept, acc) ||
            !nfa_eps(&nfa, 0u, litf.start)) {
            snprintf(error ? error : local, error ? error_size : sizeof(local),
                     "literal DFA %s: %s", literals[i],
                     nfa.error[0] ? nfa.error : "failed");
            goto done;
        }
        if (!grow((void **)&nfa.accepts, &nfa.accept_len, &nfa.accept_cap,
                  sizeof(*nfa.accepts), 1u))
            goto done;
        nfa.accepts[nfa.accept_len].state = acc;
        nfa.accepts[nfa.accept_len].tag = tag;
        nfa.accept_len++;
    }
    live_edges = calloc(nfa.edge_len ? nfa.edge_len : 1u, sizeof(*live_edges));
    starts = malloc(sizeof(*starts));
    if (!live_edges || !starts)
        goto done;
    starts[0] = 0u;
    for (i = 0u; i < nfa.edge_len; i++) {
        live_edges[i].from = nfa.edges[i].from;
        live_edges[i].to = nfa.edges[i].to;
        live_edges[i].kind = nfa.edges[i].kind;
        live_edges[i].range_len = nfa.edges[i].range_len;
        live_edges[i].ranges =
            nfa.edges[i].range_len
                ? &nfa.ranges[nfa.edges[i].range_begin]
                : NULL;
    }
    memset(&nfa_view, 0, sizeof(nfa_view));
    nfa_view.state_len = nfa.state_len;
    nfa_view.start_states = starts;
    nfa_view.start_len = 1u;
    nfa_view.edges = live_edges;
    nfa_view.edge_len = nfa.edge_len;
    nfa_view.accepts = nfa.accepts;
    nfa_view.accept_len = nfa.accept_len;
    nfa_view.tag_len = tag_len;
    if (!rsdfa_v1_plan_build(&nfa_view, TPTP_DFA_STATE_LIMIT,
                             TPTP_DFA_TRANS_LIMIT, &dfa_plan, &dfa_out, local,
                             sizeof(local)) ||
        dfa_out != RSDFA_V1_BUILD_COMPLETED ||
        !rsdfa_v1_plan_export_program(&dfa_plan, &dfa_prog, local,
                                      sizeof(local))) {
        if (error && error_size)
            snprintf(error, error_size, "DFA build: %s", local);
        goto done;
    }

    start_sym = atom_symbol(&arena, frontier.content_start);
    prod_cap = idx.rule_len * 8u + 64u;
    grammar.productions = calloc(prod_cap, sizeof(*grammar.productions));
    if (!grammar.productions)
        goto done;
    if (!slr_fill_from_pack(&grammar, &prod_cap, &arena, &idx, &frontier, &slr_origins, local,
                            sizeof(local))) {
        if (error && error_size)
            snprintf(error, error_size, "SLR fill: %s", local);
        goto done;
    }
    if (!cetta_lp_native_slr_summary(&grammar, start_sym->sym_id, &summary,
                                     local, sizeof(local))) {
        fprintf(stderr, "tptp snapshot: SLR summary failed: %s\n", local);
        memset(&summary, 0, sizeof(summary));
    } else {
        fprintf(stderr,
                "tptp snapshot: SLR summary states=%u conflicts=%u "
                "shifts=%u reduces=%u\n",
                summary.state_len, summary.conflict_len, summary.shift_len,
                summary.reduce_len);
    }
    memcpy(snap.syntax_digest, syntax_digest, 65u);
    memcpy(snap.artifact_digest, pack_digest, 65u);
    memcpy(snap.profile, profile, strlen(profile) + 1u);
    snap.conflict_len = summary.conflict_len;
    /* Unique-table conflict count decides the kernel: deterministic SLR
     * where the table permits, GLR action multimap otherwise. */
    snap.kernel = summary.conflict_len > 0u
                      ? PP_TABLE_SNAPSHOT_V1_KERNEL_GLR
                      : PP_TABLE_SNAPSHOT_V1_KERNEL_SLR;
    snap.dfa = dfa_prog;
    memset(&dfa_prog, 0, sizeof(dfa_prog));
    if (!(snap.kernel == PP_TABLE_SNAPSHOT_V1_KERNEL_GLR
              ? cetta_lp_native_slr_prepare_glr(
                    &prepared, &grammar, start_sym->sym_id, local,
                    sizeof(local))
              : cetta_lp_native_slr_prepare(
                    &prepared, &grammar, start_sym->sym_id, local,
                    sizeof(local)))) {
        if (error && error_size)
            snprintf(error, error_size, "parser prepare: %s", local);
        goto done;
    }
    if (!cetta_lp_native_slr_prepared_export_program(
            &prepared, &slr, local, sizeof(local))) {
        if (error && error_size)
            snprintf(error, error_size, "SLR export: %s", local);
        goto done;
    }
    snap.slr = slr;
    memset(&slr, 0, sizeof(slr));
    snap.slr.summary.conflict_len = summary.conflict_len;
    snap.conflict_len = summary.conflict_len;
    snap.tag_names = tag_names;
    snap.tag_name_len = tag_len;
    tag_names = NULL;
    tag_len = 0u;
    skip = calloc(snap.tag_name_len ? snap.tag_name_len : 1u,
                  sizeof(*skip));
    if (!skip)
        goto done;
    for (i = 0u; i < snap.tag_name_len; i++) {
        const int32_t rule = pack_rule_index(&idx, snap.tag_names[i]);
        const int32_t cls = lex_class_index(&lex, snap.tag_names[i]);
        if ((rule >= 0 && frontier.skip_rules[rule]) ||
            (cls >= 0 && frontier.skip_classes[cls])) {
            skip[skip_len++] = i;
        }
    }
    snap.skip_tags = skip;
    snap.skip_tag_len = skip_len;
    {
        /* The parser's helpers, each with what made it; a helper the parser
         * table does not reach has no symbol there. */
        uint32_t kept = 0u, o, n;
        for (o = 0u; o < slr_origins.len; o++) {
            bool parser = false;
            for (n = 0u; n < snap.slr.nonterminal_len && !parser; n++)
                parser = snap.slr.nonterminals[n] == slr_origins.items[o].helper;
            if (parser)
                slr_origins.items[kept++] = slr_origins.items[o];
        }
        snap.origins = slr_origins.items;
        snap.origin_len = kept;
        slr_origins.items = NULL;
    }
    skip = NULL;
    if (baseline_path) {
        uint32_t avoided_len = 0u;
        if (!snapshot_mark_extension(&snap, &base, &avoided_len, error,
                                     error_size))
            goto done;
        fprintf(stderr, "tptp snapshot: avoided=%u\n", avoided_len);
    }
    fprintf(stderr,
            "tptp snapshot: kernel=%s conflicts=%u tags=%u dfa_states=%u "
            "slr_prods=%u grammar_prods=%u skip=%u\n",
            snap.kernel == PP_TABLE_SNAPSHOT_V1_KERNEL_SLR ? "slr" : "glr",
            snap.conflict_len, snap.tag_name_len, snap.dfa.state_len,
            snap.slr.production_len, grammar.production_len, snap.skip_tag_len);
    if (!pp_table_snapshot_v1_write_path(&snap, out_path, local,
                                         sizeof(local))) {
        if (error && error_size)
            snprintf(error, error_size, "%s", local);
        goto done;
    }
    ok = true;

done:
    if (!ok && error && error_size && !error[0] && nfa.error[0])
        snprintf(error, error_size, "%s", nfa.error);
    if (!ok && error && error_size && !error[0])
        snprintf(error, error_size, "snapshot construct failed");
    if (!ok)
        fprintf(stderr, "tptp snapshot construct: %s (nfa=%s local=%s)\n",
                error && error[0] ? error : "?",
                nfa.error[0] ? nfa.error : "",
                local[0] ? local : "");
    pp_table_snapshot_v1_free(&snap);
    pp_table_snapshot_v1_free(&base);
    cetta_lp_native_slr_prepared_free(&prepared);
    cetta_lp_native_slr_program_free(&slr);
    cetta_lp_native_grammar_free(&grammar);
    rsdfa_v1_plan_free(&dfa_plan);
    rsdfa_v1_program_free(&dfa_prog);
    nfa_free(&nfa);
    pack_token_frontier_free(&frontier);
    lex_free(&lex);
    free(idx.rules);
    free(live_edges);
    free(starts);
    if (tag_names) {
        for (i = 0u; i < tag_len; i++)
            free(tag_names[i]);
        free(tag_names);
    }
    if (literals) {
        for (i = 0u; i < lit_len; i++)
            free(literals[i]);
        free(literals);
    }
    free(skip);
    free(slr_origins.items);
    arena_free(&arena);
    return ok;
}

bool cetta_tptp_snapshot_load_v1(
    PPTableSnapshotV1 *out,
    const char *path,
    const char *expected_digest,
    char *error,
    size_t error_size) {
    return cetta_tptp_snapshot_load_bound_v1(
        out, path, expected_digest, NULL, NULL, error, error_size);
}

bool cetta_tptp_snapshot_load_bound_v1(
    PPTableSnapshotV1 *out,
    const char *path,
    const char *expected_syntax_digest,
    const char *expected_profile,
    const char *expected_artifact_digest,
    char *error,
    size_t error_size) {
    if (!pp_table_snapshot_v1_read_path(out, path, error, error_size))
        return false;
    if ((expected_syntax_digest &&
         strcmp(out->syntax_digest, expected_syntax_digest) != 0) ||
        (expected_profile && strcmp(out->profile, expected_profile) != 0) ||
        (expected_artifact_digest &&
         strcmp(out->artifact_digest, expected_artifact_digest) != 0)) {
        pp_table_snapshot_v1_free(out);
        if (error && error_size)
            snprintf(error, error_size, "TPTP:DigestMismatch");
        return false;
    }
    return true;
}

static bool grammar_from_snapshot(
    const PPTableSnapshotV1 *snap,
    CettaLpNativeGrammar *grammar,
    char *error,
    size_t error_size) {
    uint32_t i;
    uint32_t count;

    if (!snap || !grammar ||
        !cetta_lp_native_slr_program_validate(
            &snap->slr, error, error_size)) {
        return false;
    }
    count = snap->slr.authored_production_len;
    if (count == 0u || count > snap->slr.production_len) {
        if (error && error_size)
            snprintf(error, error_size,
                     "TPTP snapshot has no authored grammar");
        return false;
    }
    grammar->productions = calloc(count, sizeof(*grammar->productions));
    if (!grammar->productions)
        return false;
    grammar->production_len = count;
    for (i = 0u; i < count; i++) {
        const CettaLpNativeSlrProgramProduction *source =
            &snap->slr.productions[i];
        CettaLpNativeProduction *target = &grammar->productions[i];

        if (!source->authored || source->rhs_begin > snap->slr.rhs_len ||
            source->rhs_len > snap->slr.rhs_len - source->rhs_begin) {
            if (error && error_size)
                snprintf(error, error_size,
                         "TPTP snapshot authored grammar is malformed");
            cetta_lp_native_grammar_free(grammar);
            return false;
        }
        target->label = source->label;
        target->lhs = source->lhs;
        target->rhs_len = source->rhs_len;
        if (source->rhs_len > 0u) {
            target->rhs = malloc(
                (size_t)source->rhs_len * sizeof(*target->rhs));
            if (!target->rhs) {
                cetta_lp_native_grammar_free(grammar);
                return false;
            }
            memcpy(target->rhs, &snap->slr.rhs[source->rhs_begin],
                   (size_t)source->rhs_len * sizeof(*target->rhs));
        }
    }
    return true;
}

void cetta_tptp_prepared_reader_init_v1(CettaTptpPreparedReaderV1 *reader) {
    if (!reader)
        return;
    pp_table_snapshot_v1_init(&reader->snapshot);
    cetta_lp_native_grammar_init(&reader->fallback_grammar);
    reader->canonical = NULL;
    rsdfa_v1_ascii_transition_index_init(&reader->ascii);
}

void cetta_tptp_prepared_reader_free_v1(CettaTptpPreparedReaderV1 *reader) {
    if (!reader)
        return;
    rsdfa_v1_ascii_transition_index_free(&reader->ascii);
    cetta_grammar_canonical_table_free_v1(reader->canonical);
    reader->canonical = NULL;
    cetta_lp_native_grammar_free(&reader->fallback_grammar);
    pp_table_snapshot_v1_free(&reader->snapshot);
}

bool cetta_tptp_prepared_reader_load_v1(
    CettaTptpPreparedReaderV1 *reader,
    const char *path,
    const char *expected_digest,
    char *error,
    size_t error_size) {
    return cetta_tptp_prepared_reader_load_bound_v1(
        reader, path, expected_digest, NULL, NULL, error, error_size);
}

bool cetta_tptp_prepared_reader_load_bound_v1(
    CettaTptpPreparedReaderV1 *reader,
    const char *path,
    const char *expected_syntax_digest,
    const char *expected_profile,
    const char *expected_artifact_digest,
    char *error,
    size_t error_size) {
    if (!reader)
        return false;
    cetta_tptp_prepared_reader_free_v1(reader);
    cetta_tptp_prepared_reader_init_v1(reader);
    if (!cetta_tptp_snapshot_load_bound_v1(
            &reader->snapshot, path, expected_syntax_digest,
            expected_profile, expected_artifact_digest,
            error, error_size) ||
        !grammar_from_snapshot(
            &reader->snapshot, &reader->fallback_grammar,
            error, error_size) ||
        !cetta_tptp_snapshot_canonical_table_v1(
            &reader->snapshot, &reader->canonical, error, error_size) ||
        !rsdfa_v1_ascii_transition_index_build(
            &reader->snapshot.dfa, &reader->ascii, error, error_size)) {
        cetta_tptp_prepared_reader_free_v1(reader);
        cetta_tptp_prepared_reader_init_v1(reader);
        return false;
    }
    return true;
}

static bool tag_is_skip(const PPTableSnapshotV1 *snap, uint32_t tag) {
    uint32_t i;
    for (i = 0u; i < snap->skip_tag_len; i++) {
        if (snap->skip_tags[i] == tag)
            return true;
    }
    return false;
}

/* Among equal-length matches a literal of the grammar precedes a value
 * token; the lattice below reads the literal as a word where it must. */
static int tag_priority(const PPTableSnapshotV1 *snap, uint32_t tag) {
    if (tag >= snap->tag_name_len || !snap->tag_names)
        return 0;
    if (snapshot_tag_is_value(snap, tag))
        return 1;
    return 2;
}

/* prebuilt, when given, is the snapshot's ASCII transition index, built
 * once; otherwise the lexer builds one for an ASCII text. */
static bool snapshot_lex_text(
    const PPTableSnapshotV1 *snap,
    const RSDFAV1AsciiTransitionIndex *prebuilt,
    const char *text,
    size_t text_len,
    CettaTptpLexTokenV1 *out,
    uint32_t cap,
    uint32_t *out_len,
    char *error,
    size_t error_size) {
    CettaLpNativeUtf8ScalarBuffer buf;
    RSDFAV1AsciiTransitionIndex ascii;
    const RSDFAV1AsciiTransitionIndex *index = &ascii;
    RSDFAV1Token *hits = NULL;
    uint32_t pos = 0u;
    uint32_t n = 0u;
    char local[256] = {0};

    if (out_len)
        *out_len = 0u;
    if (!snap || !text || !out_len) {
        if (error && error_size)
            snprintf(error, error_size, "lex: missing arguments");
        return false;
    }
    cetta_lp_native_utf8_scalar_buffer_init(&buf);
    if (!cetta_lp_native_utf8_scalar_buffer_prepare(
            &buf, (const uint8_t *)text, text_len, local, sizeof(local))) {
        cetta_lp_native_utf8_scalar_buffer_free(&buf);
        if (error && error_size)
            snprintf(error, error_size, "lex utf8: %s", local);
        return false;
    }
    rsdfa_v1_ascii_transition_index_init(&ascii);
    if (buf.view.ascii_bytes && prebuilt && prebuilt->targets)
        index = prebuilt;
    else if (buf.view.ascii_bytes &&
        !rsdfa_v1_ascii_transition_index_build(&snap->dfa, &ascii, local,
                                               sizeof(local))) {
        rsdfa_v1_ascii_transition_index_free(&ascii);
        cetta_lp_native_utf8_scalar_buffer_free(&buf);
        if (error && error_size)
            snprintf(error, error_size, "lex ascii index: %s", local);
        return false;
    }
    hits = calloc(snap->dfa.tag_len ? snap->dfa.tag_len : 1u, sizeof(*hits));
    if (!hits) {
        rsdfa_v1_ascii_transition_index_free(&ascii);
        cetta_lp_native_utf8_scalar_buffer_free(&buf);
        return false;
    }
    while (pos < buf.view.scalar_len) {
        RSDFAV1CursorScanResult cur;
        uint32_t i;
        uint32_t best_end = pos;
        int best_pri = -1;
        uint32_t best_tag = UINT32_MAX;
        bool skip = false;
        bool scanned;
        memset(&cur, 0, sizeof(cur));
        scanned = index->targets
                      ? rsdfa_v1_program_scan_cursor_longest_indexed_prevalidated(
                            &snap->dfa, index, &buf.view, pos, 1000000ull,
                            hits, snap->dfa.tag_len, &cur, local,
                            sizeof(local))
                      : rsdfa_v1_program_scan_cursor_longest_prevalidated(
                            &snap->dfa, &buf.view, pos, 1000000ull, hits,
                            snap->dfa.tag_len, &cur, local, sizeof(local));
        if (!scanned) {
            free(hits);
            rsdfa_v1_ascii_transition_index_free(&ascii);
            cetta_lp_native_utf8_scalar_buffer_free(&buf);
            if (error && error_size)
                snprintf(error, error_size, "lex scan: %s", local);
            return false;
        }
        for (i = 0u; i < cur.accept_len; i++) {
            if (hits[i].end_scalar > best_end)
                best_end = hits[i].end_scalar;
        }
        if (best_end <= pos) {
            uint32_t byte = cetta_lp_native_utf8_scalar_view_byte_offset(
                &buf.view, pos);
            free(hits);
            rsdfa_v1_ascii_transition_index_free(&ascii);
            cetta_lp_native_utf8_scalar_buffer_free(&buf);
            if (error && error_size)
                snprintf(error, error_size,
                         "TPTP:LexReject byte=%u", byte);
            return false;
        }
        for (i = 0u; i < cur.accept_len; i++) {
            int pri;
            if (hits[i].end_scalar != best_end)
                continue;
            if (tag_is_skip(snap, hits[i].tag)) {
                skip = true;
                break;
            }
            pri = tag_priority(snap, hits[i].tag);
            if (pri > best_pri) {
                best_pri = pri;
                best_tag = hits[i].tag;
            }
        }
        if (skip) {
            pos = best_end;
            continue;
        }
        if (best_tag == UINT32_MAX) {
            uint32_t byte = cetta_lp_native_utf8_scalar_view_byte_offset(
                &buf.view, pos);
            free(hits);
            rsdfa_v1_ascii_transition_index_free(&ascii);
            cetta_lp_native_utf8_scalar_buffer_free(&buf);
            if (error && error_size)
                snprintf(error, error_size,
                         "TPTP:LexReject byte=%u", byte);
            return false;
        }
        if (n >= cap) {
            free(hits);
            rsdfa_v1_ascii_transition_index_free(&ascii);
            cetta_lp_native_utf8_scalar_buffer_free(&buf);
            if (error && error_size)
                snprintf(error, error_size, "lex token cap");
            return false;
        }
        if (out) {
            out[n].tag = (uint16_t)best_tag;
            out[n].start_scalar = pos;
            out[n].len = best_end - pos;
            out[n].start_byte = cetta_lp_native_utf8_scalar_view_byte_offset(
                &buf.view, pos);
            out[n].end_byte = cetta_lp_native_utf8_scalar_view_byte_offset(
                &buf.view, best_end);
            if (out[n].end_byte < out[n].start_byte ||
                out[n].end_byte > text_len) {
                free(hits);
                rsdfa_v1_ascii_transition_index_free(&ascii);
                cetta_lp_native_utf8_scalar_buffer_free(&buf);
                if (error && error_size)
                    snprintf(error, error_size,
                             "lex: invalid scalar-to-byte span");
                return false;
            }
        }
        n++;
        pos = best_end;
    }
    free(hits);
    rsdfa_v1_ascii_transition_index_free(&ascii);
    cetta_lp_native_utf8_scalar_buffer_free(&buf);
    *out_len = n;
    return true;
}

bool cetta_tptp_snapshot_lex_text_v1(
    const PPTableSnapshotV1 *snap,
    const char *text,
    size_t text_len,
    CettaTptpLexTokenV1 *out,
    uint32_t cap,
    uint32_t *out_len,
    char *error,
    size_t error_size) {
    return snapshot_lex_text(snap, NULL, text, text_len, out, cap, out_len,
                             error, error_size);
}

static void map_token_span(const CettaTptpLexTokenV1 *toks, uint32_t ntok,
                           int64_t left, int64_t right, int64_t *start,
                           int64_t *stop) {
    if (left < 0)
        left = 0;
    if (right < left)
        right = left;
    if (ntok == 0u) {
        *start = 0;
        *stop = 0;
        return;
    }
    if ((uint32_t)left < ntok)
        *start = (int64_t)toks[left].start_scalar;
    else
        *start = (int64_t)cetta_tptp_lex_end(&toks[ntok - 1u]);
    if (right > left && (uint32_t)right <= ntok)
        *stop = (int64_t)cetta_tptp_lex_end(&toks[right - 1u]);
    else
        *stop = *start;
}

static void split_cst_label(const char *label, char *name, size_t name_size,
                            uint32_t *alternative) {
    size_t length;
    size_t suffix;

    if (alternative)
        *alternative = 0u;
    if (!name || name_size == 0u)
        return;
    if (!label) {
        name[0] = '\0';
        return;
    }
    length = strlen(label);
    suffix = length;
    while (suffix > 0u && label[suffix - 1u] == 'x')
        suffix--;
    if (suffix > 0u && label[suffix - 1u] == '#') {
        size_t base = suffix - 1u;
        if (alternative)
            *alternative = (uint32_t)(length - suffix);
        if (base >= name_size)
            base = name_size - 1u;
        memcpy(name, label, base);
        name[base] = '\0';
        return;
    }
    snprintf(name, name_size, "%s", label);
}

bool cetta_tptp_lex_token_bytes_v1(
    const CettaTptpLexTokenV1 *token,
    const char *text,
    size_t text_len,
    const char **bytes,
    size_t *byte_len) {
    if (bytes)
        *bytes = NULL;
    if (byte_len)
        *byte_len = 0u;
    if (!token || !text || !bytes || !byte_len ||
        token->start_byte > token->end_byte ||
        token->end_byte > text_len)
        return false;
    *bytes = text + token->start_byte;
    *byte_len = token->end_byte - token->start_byte;
    return true;
}

static Atom *source_token_string(Arena *arena, const char *text,
                                 size_t text_len,
                                 const CettaTptpLexTokenV1 *token) {
    const char *bytes;
    size_t byte_len;
    char *copy;
    Atom *result;

    if (!arena ||
        !cetta_tptp_lex_token_bytes_v1(token, text, text_len,
                                       &bytes, &byte_len) ||
        byte_len == SIZE_MAX || memchr(bytes, '\0', byte_len))
        return NULL;
    copy = malloc(byte_len + 1u);
    if (!copy)
        return NULL;
    memcpy(copy, bytes, byte_len);
    copy[byte_len] = '\0';
    result = atom_string(arena, copy);
    free(copy);
    return result;
}

static Atom *make_cst(Arena *arena, const char *name, int64_t start,
                      int64_t stop, Atom **kids, uint32_t kid_len,
                      bool observation) {
    Atom **elems;
    uint32_t i;
    if (observation) {
        Atom *children = atom_symbol(arena, "LNil");
        Atom *production_elements[3];
        Atom *node_elements[5];
        char base[192];
        uint32_t alternative = 0u;

        split_cst_label(name, base, sizeof(base), &alternative);
        for (i = kid_len; i > 0u; i--)
            children = atom_expr3(
                arena, atom_symbol(arena, "LCons"), kids[i - 1u], children);
        production_elements[0] =
            atom_symbol(arena, "tptp-cst:production");
        production_elements[1] = atom_string(arena, base);
        production_elements[2] = atom_int(arena, (int64_t)alternative);
        node_elements[0] = atom_symbol(arena, "tptp-cst:spanned-node");
        node_elements[1] = atom_expr(arena, production_elements, 3u);
        node_elements[2] = atom_int(arena, start);
        node_elements[3] = atom_int(arena, stop);
        node_elements[4] = children;
        return atom_expr(arena, node_elements, 5u);
    }
    elems = malloc(((size_t)kid_len + 4u) * sizeof(*elems));
    if (!elems)
        return NULL;
    elems[0] = atom_symbol(arena, "CstRuleV1");
    elems[1] = atom_string(arena, name ? name : "");
    elems[2] = atom_int(arena, start);
    elems[3] = atom_int(arena, stop);
    for (i = 0u; i < kid_len; i++)
        elems[4u + i] = kids[i];
    {
        Atom *out = atom_expr(arena, elems, kid_len + 4u);
        free(elems);
        return out;
    }
}

static Atom *tokc_to_cst(const PPTableSnapshotV1 *snap, Arena *arena, Atom *node,
                         const CettaTptpLexTokenV1 *toks, uint32_t ntok,
                         const char *text, size_t text_len,
                         bool observation) {
    const char *tname;
    bool value;
    char wrapped[128];
    int64_t pos;
    int64_t start = 0;
    int64_t stop = 0;
    if (!node || node->kind != ATOM_EXPR || node->expr.len < 3u)
        return NULL;
    tname = atom_name_cstr(node->expr.elems[1]);
    value = tname && snapshot_symbol_is_value(
        snap, symbol_intern_cstr(g_symbols, tname));
    pos = node->expr.elems[2]->kind == ATOM_GROUNDED
              ? node->expr.elems[2]->ground.ival
              : 0;
    map_token_span(toks, ntok, pos, pos + 1, &start, &stop);
    if (observation) {
        Atom *lexeme = pos >= 0 && (uint64_t)pos < ntok
                           ? source_token_string(
                                 arena, text, text_len, &toks[pos])
                           : NULL;
        if (value) {
            Atom *elements[5] = {
                atom_symbol(arena, "tptp-cst:token"),
                atom_string(arena, tname), atom_int(arena, start),
                atom_int(arena, stop), lexeme};
            return lexeme ? atom_expr(arena, elements, 5u) : NULL;
        } else {
            Atom *elements[4] = {
                atom_symbol(arena, "tptp-cst:literal"),
                atom_int(arena, start), atom_int(arena, stop), lexeme};
            return lexeme ? atom_expr(arena, elements, 4u) : NULL;
        }
    }
    if (!value)
        return NULL;
    snprintf(wrapped, sizeof(wrapped), "#token:%s", tname);
    return make_cst(arena, wrapped, start, stop, NULL, 0u, false);
}

typedef struct {
    Atom *node;
    Atom *kids_cons;
    Atom **kids;
    uint32_t kid_len;
    uint32_t kid_cap;
    const char *label;
    int64_t start;
    int64_t stop;
    bool expanded;
} CettaTptpNodeCFrameV1;

typedef struct {
    CettaTptpNodeCFrameV1 *data;
    uint32_t len;
    uint32_t cap;
} CettaTptpNodeCFrameVecV1;

static bool nodec_frame_push(CettaTptpNodeCFrameVecV1 *frames, Atom *node) {
    if (!grow((void **)&frames->data, &frames->len, &frames->cap,
              sizeof(*frames->data), 1u))
        return false;
    memset(&frames->data[frames->len], 0, sizeof(*frames->data));
    frames->data[frames->len].node = node;
    frames->len++;
    return true;
}

static void nodec_frames_free(CettaTptpNodeCFrameVecV1 *frames) {
    uint32_t i;
    for (i = 0u; i < frames->len; i++)
        free(frames->data[i].kids);
    free(frames->data);
    memset(frames, 0, sizeof(*frames));
}

static Atom *nodec_to_cst(const PPTableSnapshotV1 *snap, Arena *arena, Atom *node,
                          const CettaTptpLexTokenV1 *toks, uint32_t ntok,
                          const char *text, size_t text_len,
                          bool observation) {
    CettaTptpNodeCFrameVecV1 frames = {0};
    Atom *root = NULL;

    if (!node || !nodec_frame_push(&frames, node))
        return NULL;

    while (frames.len > 0u) {
        CettaTptpNodeCFrameV1 *frame = &frames.data[frames.len - 1u];
        Atom *completed = NULL;
        Atom **spliced = NULL;
        uint32_t spliced_len = 0u;

        if (!frame->expanded) {
            int64_t left = 0;
            int64_t right = 0;
            while (is_app(frame->node, "Unique", 1u) ||
                   is_app(frame->node, "LeafC", 3u))
                frame->node = frame->node->expr.elems[1];
            if (is_app(frame->node, "TokC", 2u)) {
                completed = tokc_to_cst(
                    snap, arena, frame->node, toks, ntok, text, text_len,
                    observation);
            } else if (is_app(frame->node, "EpsC", 0u) ||
                       (frame->node && frame->node->kind == ATOM_SYMBOL &&
                        atom_is_symbol(frame->node, "EpsC"))) {
                if (!observation)
                    completed = make_cst(
                        arena, "#eps", 0, 0, NULL, 0u, false);
            } else if (is_app(frame->node, "NodeC", 5u)) {
                frame->label = atom_name_cstr(frame->node->expr.elems[1]);
                left = frame->node->expr.elems[3] &&
                               frame->node->expr.elems[3]->kind == ATOM_GROUNDED
                           ? frame->node->expr.elems[3]->ground.ival
                           : 0;
                right = frame->node->expr.elems[4] &&
                                frame->node->expr.elems[4]->kind == ATOM_GROUNDED
                            ? frame->node->expr.elems[4]->ground.ival
                            : 0;
                map_token_span(toks, ntok, left, right,
                               &frame->start, &frame->stop);
                frame->kids_cons = frame->node->expr.elems[5];
                frame->expanded = true;
                continue;
            }
        } else if (is_app(frame->kids_cons, "Cons", 2u)) {
            Atom *child = frame->kids_cons->expr.elems[1];
            frame->kids_cons = frame->kids_cons->expr.elems[2];
            if (!nodec_frame_push(&frames, child)) {
                nodec_frames_free(&frames);
                return NULL;
            }
            continue;
        } else {
            if (observation && frame->label && frame->label[0] == '#') {
                spliced = frame->kids;
                spliced_len = frame->kid_len;
                frame->kids = NULL;
                frame->kid_len = 0u;
            } else {
                completed = make_cst(
                    arena, frame->label ? frame->label : "",
                    frame->start, frame->stop,
                    frame->kids, frame->kid_len, observation);
            }
        }

        free(frame->kids);
        frame->kids = NULL;
        frames.len--;
        if (frames.len == 0u) {
            if (completed)
                root = completed;
            else if (spliced_len == 1u)
                root = spliced[0];
            free(spliced);
            break;
        }
        if (spliced) {
            CettaTptpNodeCFrameV1 *parent = &frames.data[frames.len - 1u];
            uint32_t child_index;
            for (child_index = 0u; child_index < spliced_len;
                 child_index++) {
                if (!grow((void **)&parent->kids, &parent->kid_len,
                          &parent->kid_cap, sizeof(*parent->kids), 1u)) {
                    free(spliced);
                    nodec_frames_free(&frames);
                    return NULL;
                }
                parent->kids[parent->kid_len++] = spliced[child_index];
            }
            free(spliced);
            continue;
        }
        if (completed) {
            CettaTptpNodeCFrameV1 *parent = &frames.data[frames.len - 1u];
            if (!grow((void **)&parent->kids, &parent->kid_len,
                      &parent->kid_cap, sizeof(*parent->kids), 1u)) {
                nodec_frames_free(&frames);
                return NULL;
            }
            parent->kids[parent->kid_len++] = completed;
        }
    }
    free(frames.data);
    return root;
}

Atom *cetta_tptp_observe_derivation_v1(
    const PPTableSnapshotV1 *snap,
    Arena *arena, Atom *tree, const CettaTptpLexTokenV1 *tokens,
    uint32_t token_count, const char *source, size_t source_length) {
    if (!arena || !tree || (!tokens && token_count) || !source)
        return NULL;
    return nodec_to_cst(snap, arena, tree, tokens, token_count,
                        source, source_length, true);
}

static double monotonic_s(void) {
    struct timespec ts;
    if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0)
        return 0.0;
    return (double)ts.tv_sec + (double)ts.tv_nsec / 1e9;
}

static const char *tok_name(const PPTableSnapshotV1 *snap, uint32_t tag) {
    if (!snap || tag >= snap->tag_name_len || !snap->tag_names ||
        !snap->tag_names[tag])
        return "";
    return snap->tag_names[tag];
}

static int depth_delta(const char *name) {
    if (!name)
        return 0;
    if (strcmp(name, "(") == 0 || strcmp(name, "[") == 0 ||
        strcmp(name, "{") == 0)
        return 1;
    if (strcmp(name, ")") == 0 || strcmp(name, "]") == 0 ||
        strcmp(name, "}") == 0)
        return -1;
    return 0;
}

static bool is_input_dot(const char *name) {
    return name && (strcmp(name, ".") == 0 || strcmp(name, "dot") == 0);
}

static void read_outcome_init(CettaTptpReadOutcomeV1 *outcome) {
    if (!outcome)
        return;
    memset(outcome, 0, sizeof(*outcome));
    outcome->status = CETTA_TPTP_READ_ERROR_V1;
}

static bool text_is_ascii(const char *text, size_t text_len) {
    size_t i;
    for (i = 0u; i < text_len; i++) {
        if ((unsigned char)text[i] >= 0x80u)
            return false;
    }
    return true;
}


static uint64_t default_gll_descriptor_limit(uint32_t token_len) {
    uint64_t scaled = (uint64_t)token_len *
                          CETTA_TPTP_GLL_DESCRIPTORS_PER_TOKEN_V1 +
                      CETTA_TPTP_GLL_DESCRIPTOR_ALLOWANCE_V1;
    return scaled < CETTA_TPTP_GLL_DESCRIPTOR_LIMIT_V1
               ? CETTA_TPTP_GLL_DESCRIPTOR_LIMIT_V1
               : scaled;
}

typedef struct {
    uint32_t *data;
    uint32_t len;
    uint32_t cap;
} CettaTptpIndexVecV1;

typedef struct {
    CettaLpNativeUtf8Lattice lattice;
    uint32_t *terminal_ids;
    CettaLpNativeUtf8LatticeEdge *edges;
    uint32_t *start_offsets;
    uint32_t *codepoints;
    uint32_t *byte_offsets;
    bool has_alias;
} CettaTptpTokenLatticeV1;

typedef enum {
    CETTA_TPTP_LATTICE_NOT_APPLICABLE_V1 = 0,
    CETTA_TPTP_LATTICE_NO_PARSE_V1 = 1,
    CETTA_TPTP_LATTICE_UNIQUE_V1 = 2,
    CETTA_TPTP_LATTICE_AMBIGUOUS_V1 = 3,
    CETTA_TPTP_LATTICE_RESOURCE_LIMIT_V1 = 4,
    CETTA_TPTP_LATTICE_ERROR_V1 = 5
} CettaTptpLatticeResultV1;

/*
 * SyntaxBNF literals overlap value token classes: `include` is spelled
 * like a lower_word, `$let` like a dollar_word.  The deterministic lexer
 * keeps literals for the fast path.  When that path rejects, this lattice
 * offers each fixed token every value-class reading whose language contains
 * its spelling, read from the token classes themselves, and packed GLL lets
 * the grammar, rather than a filename or token heuristic, select the reading.
 */

static bool tptp_index_push(CettaTptpIndexVecV1 *values, uint32_t value) {
    if (!grow((void **)&values->data, &values->len, &values->cap,
              sizeof(*values->data), 1u))
        return false;
    values->data[values->len++] = value;
    return true;
}

static int tptp_u32_compare(const void *left, const void *right) {
    const uint32_t lhs = *(const uint32_t *)left;
    const uint32_t rhs = *(const uint32_t *)right;
    return lhs < rhs ? -1 : lhs > rhs ? 1 : 0;
}

/* The value token classes whose language contains one fixed token's
 * spelling: the lexer's DFA is scanned over the spelling, and every value
 * class that accepts exactly the whole spelling is a reading of it. */
static bool tptp_literal_value_readings(
    const PPTableSnapshotV1 *snap, const char *name,
    uint32_t *out_tags, uint32_t cap, uint32_t *out_len) {
    CettaLpNativeUtf8ScalarBuffer buf;
    RSDFAV1Token *hits;
    RSDFAV1CursorScanResult cur;
    char local[256] = {0};
    size_t len = name ? strlen(name) : 0u;
    uint32_t i;

    *out_len = 0u;
    if (len == 0u)
        return true;
    cetta_lp_native_utf8_scalar_buffer_init(&buf);
    if (!cetta_lp_native_utf8_scalar_buffer_prepare(
            &buf, (const uint8_t *)name, len, local, sizeof(local))) {
        cetta_lp_native_utf8_scalar_buffer_free(&buf);
        return false;
    }
    hits = calloc(snap->dfa.tag_len ? snap->dfa.tag_len : 1u, sizeof(*hits));
    if (!hits) {
        cetta_lp_native_utf8_scalar_buffer_free(&buf);
        return false;
    }
    memset(&cur, 0, sizeof(cur));
    if (!rsdfa_v1_program_scan_cursor_longest_prevalidated(
            &snap->dfa, &buf.view, 0u, 1000000ull, hits, snap->dfa.tag_len,
            &cur, local, sizeof(local))) {
        free(hits);
        cetta_lp_native_utf8_scalar_buffer_free(&buf);
        return false;
    }
    for (i = 0u; i < cur.accept_len; i++) {
        if (hits[i].end_scalar != buf.view.scalar_len ||
            !snapshot_tag_is_value(snap, hits[i].tag))
            continue;
        if (*out_len < cap)
            out_tags[(*out_len)++] = hits[i].tag;
    }
    free(hits);
    cetta_lp_native_utf8_scalar_buffer_free(&buf);
    return true;
}

/* The readings of every fixed token of the snapshot, tag by tag: `tags`
 * holds `tag_name_len` slots per tag, `lens` the count used. */
static bool tptp_literal_readings_table(
    const PPTableSnapshotV1 *snap, uint32_t **tags_out, uint32_t **lens_out,
    uint32_t *max_out) {
    uint32_t n = snap->tag_name_len;
    uint32_t *tags;
    uint32_t *lens;
    uint32_t tag;

    *tags_out = NULL;
    *lens_out = NULL;
    *max_out = 0u;
    if (n > 0xFFFFu)
        return false;
    tags = calloc(n ? (size_t)n * n : 1u, sizeof(*tags));
    lens = calloc(n ? n : 1u, sizeof(*lens));
    if (!tags || !lens) {
        free(tags);
        free(lens);
        return false;
    }
    for (tag = 0u; tag < n; tag++) {
        if (snapshot_tag_is_value(snap, tag) || tag_is_skip(snap, tag))
            continue;
        if (!tptp_literal_value_readings(
                snap, tok_name(snap, tag), tags + (size_t)tag * n, n,
                &lens[tag])) {
            free(tags);
            free(lens);
            return false;
        }
        if (lens[tag] > *max_out)
            *max_out = lens[tag];
    }
    *tags_out = tags;
    *lens_out = lens;
    return true;
}

static void tptp_token_lattice_free(CettaTptpTokenLatticeV1 *tokens) {
    if (!tokens)
        return;
    free(tokens->terminal_ids);
    free(tokens->edges);
    free(tokens->start_offsets);
    free(tokens->codepoints);
    free(tokens->byte_offsets);
    memset(tokens, 0, sizeof(*tokens));
}

static bool tptp_token_lattice_build(
    const PPTableSnapshotV1 *snap,
    const CettaTptpLexTokenV1 *tokens,
    uint32_t token_len,
    CettaTptpTokenLatticeV1 *out,
    char *error,
    size_t error_size) {
    CettaTptpTokenLatticeV1 result;
    uint32_t *reading_tags = NULL;
    uint32_t *reading_lens = NULL;
    uint32_t max_readings = 0u;
    uint32_t terminal_len = 0u;
    uint32_t edge_len = 0u;
    uint32_t index;

    if (!snap || (!tokens && token_len != 0u) || !out || !g_symbols)
        return false;
    memset(&result, 0, sizeof(result));
    if (!tptp_literal_readings_table(
            snap, &reading_tags, &reading_lens, &max_readings))
        return false;
    if (snap->slr.terminal_len > 0u) {
        result.terminal_ids = malloc(
            (size_t)snap->slr.terminal_len * sizeof(*result.terminal_ids));
        if (!result.terminal_ids)
            goto fail;
        memcpy(result.terminal_ids, snap->slr.terminals,
               (size_t)snap->slr.terminal_len * sizeof(*result.terminal_ids));
        qsort(result.terminal_ids, snap->slr.terminal_len,
              sizeof(*result.terminal_ids), tptp_u32_compare);
        for (index = 0u; index < snap->slr.terminal_len; index++) {
            if (terminal_len == 0u ||
                result.terminal_ids[terminal_len - 1u] !=
                    result.terminal_ids[index]) {
                result.terminal_ids[terminal_len++] =
                    result.terminal_ids[index];
            }
        }
    }
    if (token_len > (UINT32_MAX - 2u) ||
        (size_t)token_len >
            SIZE_MAX / ((1u + (size_t)max_readings) * sizeof(*result.edges)))
        goto fail;
    result.edges = calloc(
        token_len ? (size_t)token_len * (1u + (size_t)max_readings) : 1u,
        sizeof(*result.edges));
    result.start_offsets = calloc((size_t)token_len + 2u,
                                  sizeof(*result.start_offsets));
    result.codepoints = calloc(token_len ? token_len : 1u,
                               sizeof(*result.codepoints));
    result.byte_offsets = calloc((size_t)token_len + 1u,
                                 sizeof(*result.byte_offsets));
    if (!result.edges || !result.start_offsets || !result.codepoints ||
        !result.byte_offsets)
        goto fail;
    for (index = 0u; index < token_len; index++) {
        uint32_t tag = tokens[index].tag;
        const char *name = tok_name(snap, tag);
        SymbolId selected = symbol_intern_cstr(g_symbols, name);
        uint32_t readings = tag < snap->tag_name_len ? reading_lens[tag] : 0u;
        uint32_t position_begin = edge_len;
        CettaLpNativeUtf8LatticeEdge first;
        uint32_t r;

        if (selected == SYMBOL_ID_NONE)
            goto fail;
        result.start_offsets[index] = edge_len;
        result.codepoints[index] = (uint32_t)'x';
        result.byte_offsets[index] = index;
        first = (CettaLpNativeUtf8LatticeEdge){
            .terminal_id = selected,
            .scalar_left = index,
            .scalar_right = index + 1u,
            .byte_left = index,
            .byte_right = index + 1u,
            .value_kind = CETTA_LP_NATIVE_UTF8_TERMINAL_VALUE_WITNESS,
            .value = tokens[index].tag,
        };
        result.edges[edge_len++] = first;
        for (r = 0u; r < readings; r++) {
            CettaLpNativeUtf8LatticeEdge alias = first;
            SymbolId class_id = symbol_intern_cstr(
                g_symbols,
                tok_name(snap, reading_tags[(size_t)tag * snap->tag_name_len + r]));
            uint32_t at;

            if (class_id == SYMBOL_ID_NONE)
                goto fail;
            alias.terminal_id = class_id;
            alias.value = UINT32_MAX;
            /* The edges of one position stay in terminal order. */
            at = edge_len++;
            while (at > position_begin &&
                   result.edges[at - 1u].terminal_id > alias.terminal_id) {
                result.edges[at] = result.edges[at - 1u];
                at--;
            }
            result.edges[at] = alias;
            result.has_alias = true;
        }
    }
    free(reading_tags);
    free(reading_lens);
    reading_tags = NULL;
    reading_lens = NULL;
    result.start_offsets[token_len] = edge_len;
    result.start_offsets[token_len + 1u] = edge_len;
    result.byte_offsets[token_len] = token_len;
    /* Parser positions are token ordinals.  CST projection maps them back to
     * the original scalar spans after the unique derivation is selected. */
    result.lattice = (CettaLpNativeUtf8Lattice){
        .terminal_ids = result.terminal_ids,
        .terminal_len = terminal_len,
        .edges = result.edges,
        .edge_len = edge_len,
        .start_offsets = result.start_offsets,
        .start_offset_len = token_len + 2u,
        .codepoints = result.codepoints,
        .byte_offsets = result.byte_offsets,
        .scalar_len = token_len,
        .input_byte_len = token_len,
        .decoded_byte_len = 0u,
        .source_pass_count = 0u,
    };
    if (!cetta_lp_native_utf8_lattice_validate(
            &result.lattice, error, error_size))
        goto fail;
    tptp_token_lattice_free(out);
    *out = result;
    out->lattice.terminal_ids = out->terminal_ids;
    out->lattice.edges = out->edges;
    out->lattice.start_offsets = out->start_offsets;
    out->lattice.codepoints = out->codepoints;
    out->lattice.byte_offsets = out->byte_offsets;
    return true;

fail:
    free(reading_tags);
    free(reading_lens);
    tptp_token_lattice_free(&result);
    if (error && error_size && !error[0])
        snprintf(error, error_size, "TPTP token lattice allocation failed");
    return false;
}

/* The terminals at the leaves reachable from a forest node: whether a
 * value class and whether a fixed token occur among them. */
static bool tptp_forest_leaf_terminals(
    const PPTableSnapshotV1 *snap,
    const CettaLpNativeUtf8Forest *forest, uint32_t start,
    bool *has_value, bool *has_literal) {
    CettaTptpIndexVecV1 stack = {0};
    uint8_t *seen = calloc(forest->node_len ? forest->node_len : 1u, sizeof(*seen));
    bool ok = false;
    if (!seen || !tptp_index_push(&stack, start))
        goto done;
    while (stack.len > 0u) {
        uint32_t node_index = stack.data[--stack.len];
        const CettaLpNativeUtf8ForestNode *node;
        if (node_index == CETTA_LP_NATIVE_UTF8_FOREST_NONE)
            continue;
        if (node_index >= forest->node_len)
            goto done;
        if (seen[node_index])
            continue;
        seen[node_index] = 1u;
        node = &forest->nodes[node_index];
        if (node->kind == CETTA_LP_NATIVE_UTF8_FOREST_TERM) {
            if (snapshot_symbol_is_value(snap, node->symbol_id))
                *has_value = true;
            else
                *has_literal = true;
            continue;
        }
        for (uint32_t c = 0u; c < node->choice_len; c++) {
            const CettaLpNativeUtf8ForestChoice *choice;
            if (node->choice_begin + c >= forest->choice_len)
                goto done;
            choice = &forest->choices[node->choice_begin + c];
            if (!tptp_index_push(&stack, choice->prefix_node) ||
                !tptp_index_push(&stack, choice->child_node))
                goto done;
        }
    }
    ok = true;
done:
    free(stack.data);
    free(seen);
    return ok;
}

/* The productions a reading avoids, and for each forest node the fewest of
 * them a derivation of the node reduces (UINT32_MAX where none completes). */
typedef struct {
    const uint8_t *avoided;
    uint32_t avoided_len;
    uint32_t *cost;
} CettaTptpForestAvoidV1;

static uint32_t tptp_forest_choice_cost(
    const CettaTptpForestAvoidV1 *avoid,
    const CettaLpNativeUtf8Forest *forest,
    const CettaLpNativeUtf8ForestNode *node,
    const CettaLpNativeUtf8ForestChoice *choice) {
    uint64_t cost = 0u;
    if (choice->prefix_node != CETTA_LP_NATIVE_UTF8_FOREST_NONE)
        cost += avoid->cost[choice->prefix_node];
    if (choice->child_node != CETTA_LP_NATIVE_UTF8_FOREST_NONE)
        cost += avoid->cost[choice->child_node];
    if (node->kind == CETTA_LP_NATIVE_UTF8_FOREST_SYMBOL &&
        choice->production_index < avoid->avoided_len &&
        avoid->avoided[choice->production_index])
        cost++;
    (void)forest;
    return cost >= UINT32_MAX ? UINT32_MAX : (uint32_t)cost;
}

/* The costs of every node, children before parents.  A node on a cycle
 * back to itself has no finite derivation through that cycle. */
static bool tptp_forest_avoid_costs(const CettaLpNativeUtf8Forest *forest,
                                    CettaTptpForestAvoidV1 *avoid) {
    CettaTptpIndexVecV1 stack = {0};
    uint8_t *state = NULL;
    uint32_t start;
    bool ok = false;

    avoid->cost = malloc((forest->node_len ? forest->node_len : 1u) *
                         sizeof(*avoid->cost));
    state = calloc(forest->node_len ? forest->node_len : 1u, sizeof(*state));
    if (!avoid->cost || !state)
        goto done;
    for (start = 0u; start < forest->node_len; start++) {
        if (state[start] != 0u)
            continue;
        if (!tptp_index_push(&stack, start))
            goto done;
        while (stack.len > 0u) {
            uint32_t node_index = stack.data[stack.len - 1u];
            const CettaLpNativeUtf8ForestNode *node =
                &forest->nodes[node_index];
            uint32_t c;
            if (state[node_index] == 2u) {
                stack.len--;
                continue;
            }
            if (node->kind == CETTA_LP_NATIVE_UTF8_FOREST_TERM ||
                node->kind == CETTA_LP_NATIVE_UTF8_FOREST_EPSILON) {
                avoid->cost[node_index] = 0u;
                state[node_index] = 2u;
                stack.len--;
                continue;
            }
            if (node->choice_begin > forest->choice_len ||
                node->choice_len > forest->choice_len - node->choice_begin)
                goto done;
            if (state[node_index] == 0u) {
                state[node_index] = 1u;
                avoid->cost[node_index] = UINT32_MAX;
                for (c = 0u; c < node->choice_len; c++) {
                    const CettaLpNativeUtf8ForestChoice *choice =
                        &forest->choices[node->choice_begin + c];
                    uint32_t kids[2] = {choice->prefix_node,
                                        choice->child_node};
                    for (uint32_t k = 0u; k < 2u; k++) {
                        if (kids[k] == CETTA_LP_NATIVE_UTF8_FOREST_NONE)
                            continue;
                        if (kids[k] >= forest->node_len)
                            goto done;
                        if (state[kids[k]] == 0u &&
                            !tptp_index_push(&stack, kids[k]))
                            goto done;
                    }
                }
                continue;
            }
            for (c = 0u; c < node->choice_len; c++) {
                uint32_t cost = tptp_forest_choice_cost(
                    avoid, forest, node,
                    &forest->choices[node->choice_begin + c]);
                if (cost < avoid->cost[node_index])
                    avoid->cost[node_index] = cost;
            }
            state[node_index] = 2u;
            stack.len--;
        }
    }
    ok = true;
done:
    free(stack.data);
    free(state);
    return ok;
}

/*
 * The choice a forest node takes.  With avoided productions, only the
 * choices reducing the fewest of them are readings.  A fixed token of the
 * grammar spelled like a value reads either way in the lattice; where both
 * readings of that one token derive the node, the fixed token is the
 * reading: the grammar writes it out at that position.  Any other ambiguity
 * has no choice.
 */
static bool tptp_forest_choice(
    const PPTableSnapshotV1 *snap,
    const CettaLpNativeUtf8Forest *forest, uint32_t node_index,
    const CettaTptpForestAvoidV1 *avoid,
    uint32_t *choice_out, bool *ambiguous) {
    const CettaLpNativeUtf8ForestNode *node = &forest->nodes[node_index];
    uint32_t literal_choice = UINT32_MAX;
    uint32_t literal_count = 0u;
    uint32_t least = UINT32_MAX;
    uint32_t least_count = 0u;
    uint32_t least_choice = UINT32_MAX;
    *ambiguous = false;
    if (node->choice_len == 1u) {
        *choice_out = node->choice_begin;
        return true;
    }
    if (avoid && node->choice_len > 1u) {
        for (uint32_t c = 0u; c < node->choice_len; c++) {
            uint32_t cost = tptp_forest_choice_cost(
                avoid, forest, node, &forest->choices[node->choice_begin + c]);
            if (cost < least) {
                least = cost;
                least_count = 0u;
                least_choice = node->choice_begin + c;
            }
            if (cost == least)
                least_count++;
        }
        if (least != UINT32_MAX && least_count == 1u) {
            *choice_out = least_choice;
            return true;
        }
    }
    if (node->choice_len == 0u || node->scalar_right != node->scalar_left + 1u) {
        *ambiguous = node->choice_len > 1u;
        return false;
    }
    for (uint32_t c = 0u; c < node->choice_len; c++) {
        const CettaLpNativeUtf8ForestChoice *choice =
            &forest->choices[node->choice_begin + c];
        bool has_value = false;
        bool has_literal = false;
        if (avoid && least != UINT32_MAX &&
            tptp_forest_choice_cost(avoid, forest, node, choice) != least)
            continue;
        if (!tptp_forest_leaf_terminals(snap, forest, choice->prefix_node,
                                        &has_value, &has_literal) ||
            !tptp_forest_leaf_terminals(snap, forest, choice->child_node,
                                        &has_value, &has_literal))
            return false;
        if (has_literal && has_value) {
            *ambiguous = true;
            return false;
        }
        if (has_literal) {
            literal_choice = node->choice_begin + c;
            literal_count++;
        }
    }
    if (literal_count != 1u) {
        *ambiguous = true;
        return false;
    }
    *choice_out = literal_choice;
    return true;
}

static bool tptp_forest_unique_reachable(
    const PPTableSnapshotV1 *snap,
    const CettaLpNativeUtf8Forest *forest,
    uint32_t root,
    const CettaTptpForestAvoidV1 *avoid,
    bool *ambiguous,
    char *error,
    size_t error_size) {
    CettaTptpIndexVecV1 stack = {0};
    uint8_t *seen = NULL;
    bool ok = false;

    if (!forest || root >= forest->node_len || !ambiguous)
        return false;
    *ambiguous = false;
    seen = calloc(forest->node_len ? forest->node_len : 1u, sizeof(*seen));
    if (!seen || !tptp_index_push(&stack, root))
        goto done;
    while (stack.len > 0u) {
        uint32_t node_index = stack.data[--stack.len];
        const CettaLpNativeUtf8ForestNode *node;
        const CettaLpNativeUtf8ForestChoice *choice;
        if (node_index >= forest->node_len)
            goto malformed;
        if (seen[node_index])
            continue;
        seen[node_index] = 1u;
        node = &forest->nodes[node_index];
        if (node->kind == CETTA_LP_NATIVE_UTF8_FOREST_TERM ||
            node->kind == CETTA_LP_NATIVE_UTF8_FOREST_EPSILON) {
            if (node->choice_len != 0u)
                goto malformed;
            continue;
        }
        {
            uint32_t selected = UINT32_MAX;
            bool node_ambiguous = false;
            if (!tptp_forest_choice(snap, forest, node_index, avoid,
                                    &selected, &node_ambiguous)) {
                if (node_ambiguous) {
                    *ambiguous = true;
                    ok = true;
                    goto done;
                }
                goto malformed;
            }
            if (selected >= forest->choice_len)
                goto malformed;
            choice = &forest->choices[selected];
        }
        if (choice->parent_node != node_index)
            goto malformed;
        if (choice->prefix_node != CETTA_LP_NATIVE_UTF8_FOREST_NONE &&
            !tptp_index_push(&stack, choice->prefix_node))
            goto done;
        if (choice->child_node != CETTA_LP_NATIVE_UTF8_FOREST_NONE &&
            !tptp_index_push(&stack, choice->child_node))
            goto done;
    }
    ok = true;
    goto done;

malformed:
    if (error && error_size)
        snprintf(error, error_size, "TPTP token lattice forest is malformed");
done:
    free(seen);
    free(stack.data);
    return ok;
}

static bool tptp_forest_components(
    const CettaLpNativeUtf8Forest *forest,
    uint32_t prefix,
    uint32_t child,
    CettaTptpIndexVecV1 *components) {
    CettaTptpIndexVecV1 stack = {0};
    bool ok = false;

    if (child != CETTA_LP_NATIVE_UTF8_FOREST_NONE &&
        !tptp_index_push(&stack, child))
        goto done;
    if (prefix != CETTA_LP_NATIVE_UTF8_FOREST_NONE &&
        !tptp_index_push(&stack, prefix))
        goto done;
    while (stack.len > 0u) {
        uint32_t node_index = stack.data[--stack.len];
        const CettaLpNativeUtf8ForestNode *node;
        const CettaLpNativeUtf8ForestChoice *choice;
        if (node_index >= forest->node_len)
            goto done;
        node = &forest->nodes[node_index];
        if (node->kind != CETTA_LP_NATIVE_UTF8_FOREST_INTERMEDIATE) {
            if (!tptp_index_push(components, node_index))
                goto done;
            continue;
        }
        if (node->choice_len != 1u ||
            node->choice_begin >= forest->choice_len)
            goto done;
        choice = &forest->choices[node->choice_begin];
        if (choice->child_node != CETTA_LP_NATIVE_UTF8_FOREST_NONE &&
            !tptp_index_push(&stack, choice->child_node))
            goto done;
        if (choice->prefix_node != CETTA_LP_NATIVE_UTF8_FOREST_NONE &&
            !tptp_index_push(&stack, choice->prefix_node))
            goto done;
    }
    ok = true;

done:
    free(stack.data);
    return ok;
}

typedef struct {
    uint32_t node_index;
    CettaTptpIndexVecV1 components;
    uint32_t component_index;
    Atom **kids;
    uint32_t kid_len;
    uint32_t kid_cap;
    const char *label;
    int64_t start;
    int64_t stop;
    bool expanded;
} CettaTptpForestFrameV1;

typedef struct {
    CettaTptpForestFrameV1 *data;
    uint32_t len;
    uint32_t cap;
} CettaTptpForestFrameVecV1;

static bool tptp_forest_frame_push(CettaTptpForestFrameVecV1 *frames,
                                   uint32_t node_index) {
    if (!grow((void **)&frames->data, &frames->len, &frames->cap,
              sizeof(*frames->data), 1u))
        return false;
    memset(&frames->data[frames->len], 0, sizeof(*frames->data));
    frames->data[frames->len].node_index = node_index;
    frames->len++;
    return true;
}

static void tptp_forest_frames_free(CettaTptpForestFrameVecV1 *frames) {
    uint32_t index;
    for (index = 0u; index < frames->len; index++) {
        free(frames->data[index].components.data);
        free(frames->data[index].kids);
    }
    free(frames->data);
    memset(frames, 0, sizeof(*frames));
}

static bool tptp_forest_frame_add_child(CettaTptpForestFrameV1 *frame,
                                        Atom *child) {
    if (!child)
        return true;
    if (!grow((void **)&frame->kids, &frame->kid_len, &frame->kid_cap,
              sizeof(*frame->kids), 1u))
        return false;
    frame->kids[frame->kid_len++] = child;
    return true;
}

static Atom *tptp_forest_to_cst(
    const PPTableSnapshotV1 *snap,
    const CettaLpNativeGrammar *grammar,
    const CettaLpNativeUtf8Forest *forest,
    uint32_t root,
    const CettaTptpForestAvoidV1 *avoid,
    const CettaTptpLexTokenV1 *tokens,
    uint32_t token_len,
    Arena *arena,
    char *error,
    size_t error_size) {
    CettaTptpForestFrameVecV1 frames = {0};
    Atom *result = NULL;

    if (!grammar || !forest || root >= forest->node_len || !arena ||
        !tptp_forest_frame_push(&frames, root))
        return NULL;
    while (frames.len > 0u) {
        CettaTptpForestFrameV1 *frame = &frames.data[frames.len - 1u];
        const CettaLpNativeUtf8ForestNode *node;
        Atom *completed = NULL;

        if (frame->node_index >= forest->node_len)
            goto malformed;
        node = &forest->nodes[frame->node_index];
        if (!frame->expanded) {
            const CettaLpNativeUtf8ForestChoice *choice;
            uint32_t production_index;
            uint32_t selected = UINT32_MAX;
            bool node_ambiguous = false;
            if (node->kind != CETTA_LP_NATIVE_UTF8_FOREST_SYMBOL ||
                !tptp_forest_choice(snap, forest, frame->node_index, avoid,
                                    &selected, &node_ambiguous) ||
                selected >= forest->choice_len)
                goto malformed;
            choice = &forest->choices[selected];
            production_index = choice->production_index;
            if (production_index >= grammar->production_len)
                goto malformed;
            frame->label = symbol_bytes(
                g_symbols, grammar->productions[production_index].label);
            map_token_span(tokens, token_len,
                           node->scalar_left, node->scalar_right,
                           &frame->start, &frame->stop);
            if (!tptp_forest_components(
                    forest, choice->prefix_node, choice->child_node,
                    &frame->components))
                goto malformed;
            frame->expanded = true;
            continue;
        }
        if (frame->component_index < frame->components.len) {
            uint32_t child_index =
                frame->components.data[frame->component_index++];
            const CettaLpNativeUtf8ForestNode *child;
            if (child_index >= forest->node_len)
                goto malformed;
            child = &forest->nodes[child_index];
            if (child->kind == CETTA_LP_NATIVE_UTF8_FOREST_SYMBOL) {
                if (!tptp_forest_frame_push(&frames, child_index))
                    goto done;
                continue;
            }
            if (child->kind == CETTA_LP_NATIVE_UTF8_FOREST_EPSILON) {
                completed = make_cst(
                    arena, "#eps", 0, 0, NULL, 0u, false);
            } else if (child->kind == CETTA_LP_NATIVE_UTF8_FOREST_TERM) {
                const char *name = symbol_bytes(g_symbols, child->symbol_id);
                if (snapshot_symbol_is_value(snap, child->symbol_id)) {
                    char wrapped[128];
                    int64_t start = 0;
                    int64_t stop = 0;
                    map_token_span(tokens, token_len,
                                   child->scalar_left, child->scalar_right,
                                   &start, &stop);
                    snprintf(wrapped, sizeof(wrapped), "#token:%s", name);
                    completed = make_cst(
                        arena, wrapped, start, stop, NULL, 0u, false);
                }
            } else {
                goto malformed;
            }
            if (!tptp_forest_frame_add_child(frame, completed))
                goto done;
            continue;
        }
        completed = make_cst(
            arena, frame->label ? frame->label : "",
            frame->start, frame->stop, frame->kids, frame->kid_len,
            false);
        free(frame->components.data);
        free(frame->kids);
        frame->components.data = NULL;
        frame->kids = NULL;
        frames.len--;
        if (frames.len == 0u) {
            result = completed;
            break;
        }
        if (!tptp_forest_frame_add_child(
                &frames.data[frames.len - 1u], completed))
            goto done;
    }
    goto done;

malformed:
    if (error && error_size)
        snprintf(error, error_size,
                 "TPTP token lattice derivation is malformed");
done:
    tptp_forest_frames_free(&frames);
    return result;
}

static CettaTptpLatticeResultV1 tptp_keyword_lattice_cst(
    const PPTableSnapshotV1 *snap,
    const CettaLpNativeGrammar *grammar,
    const CettaTptpLexTokenV1 *tokens,
    uint32_t token_len,
    uint64_t descriptor_limit,
    const uint8_t *avoided,
    Arena *arena,
    Atom **out_cst,
    uint64_t *descriptors_used,
    char *error,
    size_t error_size) {
    CettaTptpTokenLatticeV1 token_lattice;
    CettaLpNativeUtf8Forest forest;
    uint32_t root = UINT32_MAX;
    uint32_t root_count = 0u;
    uint32_t index;
    bool ambiguous = false;
    CettaTptpLatticeResultV1 result = CETTA_TPTP_LATTICE_ERROR_V1;
    CettaTptpForestAvoidV1 avoid = {avoided, grammar->production_len, NULL};
    const CettaTptpForestAvoidV1 *avoiding = NULL;

    memset(&token_lattice, 0, sizeof(token_lattice));
    cetta_lp_native_utf8_forest_init(&forest);
    if (out_cst)
        *out_cst = NULL;
    if (descriptors_used)
        *descriptors_used = 0u;
    if (!tptp_token_lattice_build(
            snap, tokens, token_len, &token_lattice, error, error_size))
        goto done;
    if (!token_lattice.has_alias) {
        result = CETTA_TPTP_LATTICE_NOT_APPLICABLE_V1;
        goto done;
    }
    if (descriptor_limit == 0u)
        descriptor_limit = default_gll_descriptor_limit(token_len);
    if (descriptor_limit > UINT32_MAX)
        descriptor_limit = UINT32_MAX;
    if (!cetta_lp_native_gll_parse_utf8_lattice_forest(
            grammar, snap->slr.start_nonterminal,
            &token_lattice.lattice, (uint32_t)descriptor_limit,
            &forest, error, error_size))
        goto done;
    if (descriptors_used)
        *descriptors_used = forest.work_item_len;
    if (forest.outcome == CETTA_LP_NATIVE_UTF8_FOREST_RESOURCE_LIMIT) {
        result = CETTA_TPTP_LATTICE_RESOURCE_LIMIT_V1;
        goto done;
    }
    for (index = 0u; index < forest.root_len; index++) {
        uint32_t candidate = forest.roots[index];
        const CettaLpNativeUtf8ForestNode *node;
        if (candidate >= forest.node_len)
            goto done;
        node = &forest.nodes[candidate];
        if (node->kind == CETTA_LP_NATIVE_UTF8_FOREST_SYMBOL &&
            node->symbol_id == snap->slr.start_nonterminal &&
            node->scalar_left == 0u && node->scalar_right == token_len) {
            root = candidate;
            root_count++;
        }
    }
    if (root_count == 0u) {
        result = CETTA_TPTP_LATTICE_NO_PARSE_V1;
        goto done;
    }
    if (avoided && root_count == 1u) {
        if (!tptp_forest_avoid_costs(&forest, &avoid))
            goto done;
        avoiding = &avoid;
    }
    if (root_count > 1u ||
        !tptp_forest_unique_reachable(
            snap, &forest, root, avoiding, &ambiguous, error, error_size)) {
        if (root_count > 1u) {
            result = CETTA_TPTP_LATTICE_AMBIGUOUS_V1;
            goto done;
        }
        goto done;
    }
    if (ambiguous) {
        result = CETTA_TPTP_LATTICE_AMBIGUOUS_V1;
        goto done;
    }
    if (!out_cst ||
        !(*out_cst = tptp_forest_to_cst(
              snap, grammar, &forest, root, avoiding, tokens, token_len,
              arena, error, error_size)))
        goto done;
    result = CETTA_TPTP_LATTICE_UNIQUE_V1;

done:
    free(avoid.cost);
    cetta_lp_native_utf8_forest_free(&forest);
    tptp_token_lattice_free(&token_lattice);
    return result;
}

/* The avoided productions of the fallback grammar, which are the snapshot's
 * authored productions in order; NULL when the snapshot avoids none. */
static const uint8_t *snapshot_avoided_table(
    const PPTableSnapshotV1 *snap, const CettaLpNativeGrammar *grammar,
    Arena *arena) {
    uint8_t *table = NULL;
    uint32_t i;
    if (grammar->production_len > snap->slr.authored_production_len)
        return NULL;
    for (i = 0u; i < grammar->production_len; i++) {
        if (!snap->slr.productions[i].avoided)
            continue;
        if (!table) {
            table = arena_alloc(arena, grammar->production_len);
            memset(table, 0, grammar->production_len);
        }
        table[i] = 1u;
    }
    return table;
}

static bool parse_project_slice(const PPTableSnapshotV1 *snap,
                                const CettaLpNativeGrammar *fallback_grammar,
                                uint64_t gll_descriptor_limit,
                                const CettaTptpLexTokenV1 *toks, uint32_t lo,
                                uint32_t hi, const char *text, size_t text_len,
                                bool source_ascii,
                                CettaTptpCstProjectionV1 projection,
                                void *projection_context,
                                const CettaGrammarCanonicalTableV1 *canonical,
                                Arena *rec_arena, Atom **out_file,
                                CettaTptpReadCostV1 *cost,
                                CettaTptpReadOutcomeV1 *outcome,
                                uint64_t work_limit,
                                char *error, size_t error_size) {
    Arena parse;
    Atom *list;
    Atom *parsed;
    Atom *cst = NULL;
    Atom *trees;
    Atom *file = NULL;
    char local[512] = {0};
    uint32_t i;
    uint32_t n = hi - lo;
    double t0;
    double t1;

    if (out_file)
        *out_file = NULL;
    arena_init(&parse);
    list = atom_symbol(&parse, "Nil");
    for (i = hi; i > lo; i--) {
        const char *nm = tok_name(snap, toks[i - 1u].tag);
        Atom *sym = atom_symbol(&parse, nm[0] ? nm : "unknown");
        list = atom_expr3(&parse, atom_symbol(&parse, "Cons"), sym, list);
    }
    t0 = cost ? monotonic_s() : 0.0;
    parsed = cetta_lp_native_slr_program_parse_shared_counted(
        &snap->slr, list, work_limit, NULL, &parse, local, sizeof(local));
    if (parsed && fallback_grammar &&
        (is_app(parsed, "ResourceLimit", 2u) ||
         (parsed->kind == ATOM_SYMBOL && atom_is_symbol(parsed, "NoParse")))) {
        Atom *table_result = parsed;
        Atom *fallback_result;
        uint64_t descriptors_used = 0u;
        char table_local[sizeof(local)];

        const uint8_t *avoided =
            snapshot_avoided_table(snap, fallback_grammar, &parse);

        snprintf(table_local, sizeof(table_local), "%s", local);
        local[0] = '\0';
        if (gll_descriptor_limit == 0u)
            gll_descriptor_limit = default_gll_descriptor_limit(n);
        fallback_result = cetta_lp_native_gll_parse_avoiding_counted(
            fallback_grammar, snap->slr.start_nonterminal, list,
            gll_descriptor_limit, &descriptors_used, avoided,
            &parse, local, sizeof(local));
        if (cost)
            cost->gll_descriptor_count += descriptors_used;
        if ((fallback_result && is_app(fallback_result, "Unique", 1u)) ||
            (fallback_result && fallback_result->kind == ATOM_SYMBOL &&
             atom_is_symbol(fallback_result, "Ambiguous")))
            parsed = fallback_result;
        else {
            CettaTptpLatticeResultV1 lattice_result;
            Atom *lattice_cst = NULL;
            uint64_t lattice_descriptors = 0u;

            local[0] = '\0';
            lattice_result = tptp_keyword_lattice_cst(
                snap, fallback_grammar, toks + lo, n,
                gll_descriptor_limit, avoided, &parse, &lattice_cst,
                &lattice_descriptors, local, sizeof(local));
            if (cost)
                cost->gll_descriptor_count += lattice_descriptors;
            if (lattice_result == CETTA_TPTP_LATTICE_UNIQUE_V1) {
                cst = lattice_cst;
            } else if (lattice_result ==
                       CETTA_TPTP_LATTICE_AMBIGUOUS_V1) {
                parsed = atom_symbol(&parse, "Ambiguous");
            } else if (lattice_result == CETTA_TPTP_LATTICE_ERROR_V1) {
                parsed = NULL;
            } else {
                parsed = table_result;
                snprintf(local, sizeof(local), "%s", table_local);
            }
        }
    }
    t1 = cost ? monotonic_s() : 0.0;
    if (cost)
        cost->parse_s += t1 - t0;
    if (!cst && !parsed) {
        arena_free(&parse);
        if (error && error_size)
            snprintf(error, error_size, "parse: %s",
                     local[0] ? local : "NoParse");
        return false;
    }
    if (!cst && parsed->kind == ATOM_SYMBOL &&
        atom_is_symbol(parsed, "NoParse")) {
        unsigned tok = 0u;
        size_t byte = 0u;
        if (local[0] && sscanf(local, "token=%u", &tok) == 1 &&
            tok < n)
            byte = toks[lo + tok].start_byte;
        else if (hi > lo)
            byte = toks[hi - 1u].end_byte;
        if (outcome) {
            outcome->status = CETTA_TPTP_READ_NO_PARSE_V1;
            outcome->byte_offset = byte > UINT32_MAX
                                       ? UINT32_MAX : (uint32_t)byte;
        }
        arena_free(&parse);
        if (error && error_size)
            snprintf(error, error_size,
                     "TPTP:NoParse byte=%zu rule=TPTP_file", byte);
        return false;
    }
    if (!cst && parsed->kind == ATOM_SYMBOL &&
        atom_is_symbol(parsed, "Ambiguous")) {
        if (outcome)
            outcome->status = CETTA_TPTP_READ_AMBIGUOUS_V1;
        arena_free(&parse);
        if (error && error_size)
            snprintf(error, error_size,
                     "TPTP:Ambiguous rule=TPTP_file");
        return false;
    }
    if (!cst && is_app(parsed, "ResourceLimit", 2u)) {
        int64_t used = 0;
        int64_t limit = 0;
        if (parsed->expr.elems[1] &&
            parsed->expr.elems[1]->kind == ATOM_GROUNDED &&
            parsed->expr.elems[1]->ground.gkind == GV_INT)
            used = parsed->expr.elems[1]->ground.ival;
        if (parsed->expr.elems[2] &&
            parsed->expr.elems[2]->kind == ATOM_GROUNDED &&
            parsed->expr.elems[2]->ground.gkind == GV_INT)
            limit = parsed->expr.elems[2]->ground.ival;
        if (outcome) {
            outcome->status = CETTA_TPTP_READ_RESOURCE_LIMIT_V1;
            outcome->work = used < 0 ? 0u : (uint64_t)used;
            outcome->limit = limit < 0 ? 0u : (uint64_t)limit;
        }
        arena_free(&parse);
        if (error && error_size)
            snprintf(error, error_size,
                     "TPTP:ResourceLimit work=%lld limit=%lld rule=TPTP_file",
                     (long long)used, (long long)limit);
        return false;
    }
    if (!cst) {
        cst = nodec_to_cst(
            snap, &parse, parsed, toks + lo, n, text, text_len,
            projection != NULL);
        if (!cst) {
            arena_free(&parse);
            if (error && error_size)
                snprintf(error, error_size, "CST conversion failed");
            return false;
        }
    }
    trees = atom_expr(&parse, &cst, 1u);
    t0 = cost ? monotonic_s() : 0.0;
    if (projection) {
        if (!projection(
                trees, text, text_len, source_ascii, &parse, rec_arena,
                &file, projection_context, error, error_size)) {
            arena_free(&parse);
            return false;
        }
    } else {
        CanonicalLexemeContext lexemes = {toks + lo, n, text, text_len};
        if (!canonical ||
            !cetta_grammar_canonical_from_cst_v1(
                canonical, cst, "#eps", canonical_leaf, &lexemes,
                rec_arena, &file, error, error_size)) {
            arena_free(&parse);
            if (error && error_size && !error[0])
                snprintf(error, error_size, "TPTP canonical term failed");
            return false;
        }
    }
    t1 = cost ? monotonic_s() : 0.0;
    if (cost)
        cost->project_s += t1 - t0;
    arena_free(&parse);
    *out_file = file;
    return true;
}

/* out_starts, when given, receives the first byte of each input's first
 * token, one entry per input, in a malloc'ed array. */
static bool snapshot_read_text_spans_impl(
    const PPTableSnapshotV1 *snap,
    const CettaLpNativeGrammar *fallback_grammar,
    const CettaTptpPreparedReaderV1 *reader,
    uint64_t gll_descriptor_limit,
    const char *text,
    size_t text_len,
    Arena *arena,
    Atom **out_records,
    CettaTptpInputVisitV1 on_input,
    void *user,
    CettaTptpReadCostV1 *cost,
    CettaTptpReadOutcomeV1 *outcome,
    uint64_t work_limit,
    CettaTptpCstProjectionV1 projection,
    void *projection_context,
    size_t **out_starts,
    char *error,
    size_t error_size);

static bool snapshot_read_text_impl(
    const PPTableSnapshotV1 *snap,
    const CettaLpNativeGrammar *fallback_grammar,
    const CettaTptpPreparedReaderV1 *reader,
    uint64_t gll_descriptor_limit,
    const char *text,
    size_t text_len,
    Arena *arena,
    Atom **out_records,
    CettaTptpInputVisitV1 on_input,
    void *user,
    CettaTptpReadCostV1 *cost,
    CettaTptpReadOutcomeV1 *outcome,
    uint64_t work_limit,
    CettaTptpCstProjectionV1 projection,
    void *projection_context,
    char *error,
    size_t error_size) {
    return snapshot_read_text_spans_impl(
        snap, fallback_grammar, reader, gll_descriptor_limit, text, text_len, arena,
        out_records, on_input, user, cost, outcome, work_limit, projection,
        projection_context, NULL, error, error_size);
}

static bool snapshot_read_text_spans_impl(
    const PPTableSnapshotV1 *snap,
    const CettaLpNativeGrammar *fallback_grammar,
    const CettaTptpPreparedReaderV1 *reader,
    uint64_t gll_descriptor_limit,
    const char *text,
    size_t text_len,
    Arena *arena,
    Atom **out_records,
    CettaTptpInputVisitV1 on_input,
    void *user,
    CettaTptpReadCostV1 *cost,
    CettaTptpReadOutcomeV1 *outcome,
    uint64_t work_limit,
    CettaTptpCstProjectionV1 projection,
    void *projection_context,
    size_t **out_starts,
    char *error,
    size_t error_size) {
    size_t *starts = NULL;
    uint32_t starts_cap = 0u;
    uint32_t starts_len = 0u;
    CettaTptpLexTokenV1 *toks = NULL;
    uint32_t ntok = 0u;
    uint32_t cap;
    uint32_t i;
    uint32_t lo;
    int depth = 0;
    Atom **inputs = NULL;
    uint32_t n_in = 0u;
    uint32_t cap_in = 0u;
    char local[512] = {0};
    double t0;
    bool ok = false;
    bool source_ascii;
    CettaGrammarCanonicalTableV1 *canonical = NULL; /* built for this read */
    const CettaGrammarCanonicalTableV1 *table = NULL;

    if (out_records)
        *out_records = NULL;
    if (cost)
        memset(cost, 0, sizeof(*cost));
    read_outcome_init(outcome);
    if (!snap || !text || !arena || (!out_records && !on_input)) {
        if (error && error_size)
            snprintf(error, error_size, "frozen read: missing arguments");
        return false;
    }
    if (snap->slr.production_len == 0u) {
        if (error && error_size)
            snprintf(error, error_size, "frozen read: missing parser tables");
        return false;
    }
    source_ascii = text_is_ascii(text, text_len);
    if (!projection && reader && reader->canonical) {
        table = reader->canonical;
    } else if (!projection) {
        CettaGrammarCanonicalTerminalsV1 terminals = {
            canonical_terminal_is_value, canonical_terminal_fixed_text,
            (void *)snap};
        if (!snapshot_canonical_table(snap, &terminals, &canonical, error, error_size))
            return false;
        table = canonical;
    }
    cap = 65536u;
    if (text_len + 8u < cap)
        cap = (uint32_t)text_len + 8u;
    if (cap < 8u)
        cap = 8u;
    toks = malloc((size_t)cap * sizeof(*toks));
    if (!toks) {
        cetta_grammar_canonical_table_free_v1(canonical);
        return false;
    }
    t0 = cost ? monotonic_s() : 0.0;
    for (;;) {
        if (snapshot_lex_text(snap, reader ? &reader->ascii : NULL,
                              text, text_len, toks, cap,
                                            &ntok, local, sizeof(local)))
            break;
        if (strcmp(local, "lex token cap") != 0) {
            unsigned byte = 0u;
            if (outcome &&
                sscanf(local, "TPTP:LexReject byte=%u", &byte) == 1) {
                outcome->status = CETTA_TPTP_READ_LEX_REJECT_V1;
                outcome->byte_offset = byte;
            }
            free(toks);
            cetta_grammar_canonical_table_free_v1(canonical);
            if (error && error_size)
                snprintf(error, error_size, "%s", local);
            return false;
        }
        if (cap > (UINT32_MAX / 2u)) {
            free(toks);
            cetta_grammar_canonical_table_free_v1(canonical);
            if (error && error_size)
                snprintf(error, error_size, "lex token cap");
            return false;
        }
        cap *= 2u;
        {
            CettaTptpLexTokenV1 *grown =
                realloc(toks, (size_t)cap * sizeof(*toks));
            if (!grown) {
                free(toks);
                cetta_grammar_canonical_table_free_v1(canonical);
                return false;
            }
            toks = grown;
        }
    }
    if (cost) {
        cost->lex_s = monotonic_s() - t0;
        cost->token_count = ntok;
    }
    lo = 0u;
    for (i = 0u; i <= ntok; i++) {
        const char *nm;
        int end_slice = 0;
        if (i < ntok) {
            nm = tok_name(snap, toks[i].tag);
            depth += depth_delta(nm);
            if (depth < 0)
                depth = 0;
            if (depth == 0 && is_input_dot(nm))
                end_slice = 1;
        } else if (lo < ntok) {
            end_slice = 1;
        }
        if (!end_slice)
            continue;
        {
            Atom *slice_file = NULL;
            Atom **got = NULL;
            uint32_t ng = 0u;
            uint32_t cg = 0u;
            uint32_t hi = (i < ntok) ? i + 1u : ntok;
            ArenaMark mark = arena_mark(arena);
            if (!parse_project_slice(snap, fallback_grammar,
                                     gll_descriptor_limit, toks, lo, hi,
                                     text, text_len, source_ascii,
                                     projection, projection_context,
                                     table, arena, &slice_file,
                                     cost, outcome, work_limit,
                                     error, error_size))
                goto done;
            Atom *const *slice_inputs = NULL;
            uint32_t input_len = 0u;
            bool shaped = false;
            if (slice_file && projection && slice_file->kind == ATOM_EXPR) {
                slice_inputs = slice_file->expr.elems;
                input_len = (uint32_t)slice_file->expr.len;
                shaped = true;
            } else if (slice_file && !projection) {
                shaped = cetta_grammar_canonical_list_elements_v1(slice_file, &slice_inputs,
                                                                  &input_len);
            }
            if (shaped) {
                /* A projection returns the inputs; a canonical file is the
                 * list node of its inputs. */
                for (uint32_t k = 0u; k < input_len; k++) {
                    if (!grow((void **)&got, &ng, &cg,
                              sizeof(*got), 1u)) {
                        free(got);
                        goto done;
                    }
                    got[ng++] = slice_inputs[k];
                }
            } else {
                free(got);
                if (error && error_size)
                    snprintf(error, error_size,
                             "tptp projection: per-input sequence shape");
                goto done;
            }
            if (cost)
                cost->input_count += ng;
            if (on_input) {
                uint32_t k;
                for (k = 0u; k < ng; k++) {
                    if (!on_input(got[k], user)) {
                        free(got);
                        if (error && error_size && !error[0])
                            snprintf(error, error_size,
                                     "tptp records: input visitor failed");
                        goto done;
                    }
                }
                arena_reset(arena, mark);
                free(got);
            } else {
                uint32_t k;
                for (k = 0u; k < ng; k++) {
                    if (!grow((void **)&inputs, &n_in, &cap_in,
                              sizeof(*inputs), 1u)) {
                        free(got);
                        goto done;
                    }
                    inputs[n_in++] = got[k];
                    if (out_starts) {
                        if (!grow((void **)&starts, &starts_len, &starts_cap,
                                  sizeof(*starts), 1u)) {
                            free(got);
                            goto done;
                        }
                        starts[starts_len++] = lo < ntok ? toks[lo].start_byte
                                                         : text_len;
                    }
                }
                free(got);
            }
            lo = hi;
        }
    }
    t0 = cost ? monotonic_s() : 0.0;
    if (!on_input) {
        *out_records = atom_expr(arena, inputs, (CettaExprLen)n_in);
        if (!*out_records)
            goto done;
    } else if (out_records) {
        *out_records = NULL;
    }
    if (cost)
        cost->combine_s = monotonic_s() - t0;
    if (outcome)
        outcome->status = CETTA_TPTP_READ_OK_V1;
    if (out_starts) {
        *out_starts = starts;
        starts = NULL;
    }
    ok = true;
done:
    free(starts);
    free(toks);
    free(inputs);
    cetta_grammar_canonical_table_free_v1(canonical);
    return ok;
}

bool cetta_tptp_snapshot_read_text_cost_v1(
    const PPTableSnapshotV1 *snap,
    const char *text,
    size_t text_len,
    Arena *arena,
    Atom **out_records,
    CettaTptpReadCostV1 *cost,
    char *error,
    size_t error_size) {
    return snapshot_read_text_impl(snap, NULL, NULL, 0u, text, text_len,
                                   arena, out_records,
                                   NULL, NULL, cost, NULL, 0u,
                                   NULL, NULL,
                                   error, error_size);
}

bool cetta_tptp_snapshot_read_text_v1(
    const PPTableSnapshotV1 *snap,
    const char *text,
    size_t text_len,
    Arena *arena,
    Atom **out_records,
    char *error,
    size_t error_size) {
    return snapshot_read_text_impl(snap, NULL, NULL, 0u, text, text_len,
                                   arena, out_records,
                                   NULL, NULL, NULL, NULL, 0u,
                                   NULL, NULL,
                                   error, error_size);
}

bool cetta_tptp_snapshot_read_text_outcome_v1(
    const PPTableSnapshotV1 *snap,
    const char *text,
    size_t text_len,
    Arena *arena,
    Atom **out_records,
    CettaTptpReadOutcomeV1 *outcome,
    char *error,
    size_t error_size) {
    return snapshot_read_text_impl(snap, NULL, NULL, 0u, text, text_len,
                                   arena, out_records,
                                   NULL, NULL, NULL, outcome, 0u,
                                   NULL, NULL,
                                   error, error_size);
}

bool cetta_tptp_snapshot_read_text_each_v1(
    const PPTableSnapshotV1 *snap,
    const char *text,
    size_t text_len,
    Arena *arena,
    CettaTptpInputVisitV1 on_input,
    void *user,
    CettaTptpReadCostV1 *cost,
    char *error,
    size_t error_size) {
    if (!on_input) {
        if (error && error_size)
            snprintf(error, error_size, "frozen read: missing visitor");
        return false;
    }
    return snapshot_read_text_impl(snap, NULL, NULL, 0u, text, text_len,
                                   arena, NULL, on_input, user, cost,
                                   NULL, 0u, NULL, NULL,
                                   error, error_size);
}

bool cetta_tptp_snapshot_read_text_each_with_work_limit_v1(
    const PPTableSnapshotV1 *snap,
    const char *text,
    size_t text_len,
    uint64_t work_limit,
    Arena *arena,
    CettaTptpInputVisitV1 on_input,
    void *user,
    CettaTptpReadCostV1 *cost,
    char *error,
    size_t error_size) {
    if (!on_input) {
        if (error && error_size)
            snprintf(error, error_size, "frozen read: missing visitor");
        return false;
    }
    return snapshot_read_text_impl(snap, NULL, NULL, 0u, text, text_len,
                                   arena, NULL, on_input, user, cost,
                                   NULL, work_limit,
                                   NULL, NULL,
                                   error, error_size);
}

bool cetta_tptp_prepared_reader_read_text_outcome_v1(
    const CettaTptpPreparedReaderV1 *reader,
    const char *text,
    size_t text_len,
    Arena *arena,
    Atom **out_records,
    CettaTptpReadOutcomeV1 *outcome,
    char *error,
    size_t error_size) {
    if (!reader)
        return false;
    return snapshot_read_text_impl(
        &reader->snapshot, &reader->fallback_grammar, reader, 0u, text, text_len,
        arena, out_records, NULL, NULL, NULL, outcome, 0u,
        NULL, NULL,
        error, error_size);
}

bool cetta_tptp_prepared_reader_read_text_spans_v1(
    const CettaTptpPreparedReaderV1 *reader,
    const char *text,
    size_t text_len,
    Arena *arena,
    Atom **out_records,
    size_t **out_starts,
    CettaTptpReadOutcomeV1 *outcome,
    char *error,
    size_t error_size) {
    if (!reader || !out_starts)
        return false;
    *out_starts = NULL;
    return snapshot_read_text_spans_impl(
        &reader->snapshot, &reader->fallback_grammar, reader, 0u, text, text_len,
        arena, out_records, NULL, NULL, NULL, outcome, 0u,
        NULL, NULL, out_starts,
        error, error_size);
}

bool cetta_tptp_prepared_reader_read_text_projected_outcome_v1(
    const CettaTptpPreparedReaderV1 *reader,
    const char *text,
    size_t text_len,
    CettaTptpCstProjectionV1 projection,
    void *projection_context,
    Arena *arena,
    Atom **out_records,
    CettaTptpReadOutcomeV1 *outcome,
    char *error,
    size_t error_size) {
    if (!reader || !projection)
        return false;
    return snapshot_read_text_impl(
        &reader->snapshot, &reader->fallback_grammar, reader, 0u, text, text_len,
        arena, out_records, NULL, NULL, NULL, outcome, 0u,
        projection, projection_context, error, error_size);
}

bool cetta_tptp_prepared_reader_read_text_each_with_work_limit_v1(
    const CettaTptpPreparedReaderV1 *reader,
    const char *text,
    size_t text_len,
    uint64_t work_limit,
    uint64_t gll_descriptor_limit,
    Arena *arena,
    CettaTptpInputVisitV1 on_input,
    void *user,
    CettaTptpReadCostV1 *cost,
    char *error,
    size_t error_size) {
    if (!reader || !on_input) {
        if (error && error_size)
            snprintf(error, error_size, "frozen read: missing visitor");
        return false;
    }
    return snapshot_read_text_impl(
        &reader->snapshot, &reader->fallback_grammar, reader,
        gll_descriptor_limit, text, text_len, arena, NULL,
        on_input, user, cost, NULL, work_limit, NULL, NULL,
        error, error_size);
}

static bool frozen_load(CettaTptpPreparedReaderV1 *reader, char *error,
                        size_t error_size) {
    cetta_tptp_prepared_reader_init_v1(reader);
    return cetta_tptp_prepared_reader_load_bound_v1(
        reader, CETTA_TPTP_OFFICIAL_SNAPSHOT_PATH_V1,
        CETTA_TPTP_OFFICIAL_SYNTAXBNF_DIGEST_V1, "strict", NULL,
        error, error_size);
}

bool cetta_tptp_read_text_frozen_v1(
    const char *text,
    Arena *arena,
    Atom **out_records,
    char *error,
    size_t error_size) {
    return cetta_tptp_read_text_frozen_outcome_v1(
        text, arena, out_records, NULL, error, error_size);
}

bool cetta_tptp_read_text_frozen_outcome_v1(
    const char *text,
    Arena *arena,
    Atom **out_records,
    CettaTptpReadOutcomeV1 *outcome,
    char *error,
    size_t error_size) {
    CettaTptpPreparedReaderV1 reader;
    bool ok;
    read_outcome_init(outcome);
    if (!text)
        text = "";
    if (!frozen_load(&reader, error, error_size))
        return false;
    ok = cetta_tptp_prepared_reader_read_text_outcome_v1(
        &reader, text, strlen(text), arena, out_records, outcome,
        error, error_size);
    cetta_tptp_prepared_reader_free_v1(&reader);
    return ok;
}

bool cetta_tptp_read_file_frozen_v1(
    const char *path,
    Arena *arena,
    Atom **out_records,
    char *error,
    size_t error_size) {
    return cetta_tptp_read_file_frozen_outcome_v1(
        path, arena, out_records, NULL, error, error_size);
}

bool cetta_tptp_read_file_frozen_outcome_v1(
    const char *path,
    Arena *arena,
    Atom **out_records,
    CettaTptpReadOutcomeV1 *outcome,
    char *error,
    size_t error_size) {
    CettaTptpPreparedReaderV1 reader;
    bool ok;
    read_outcome_init(outcome);
    if (!frozen_load(&reader, error, error_size))
        return false;
    ok = cetta_tptp_prepared_reader_read_file_outcome_v1(
        &reader, path, arena, out_records, outcome, error, error_size);
    cetta_tptp_prepared_reader_free_v1(&reader);
    return ok;
}

static bool snapshot_read_file_impl(
    const PPTableSnapshotV1 *snap,
    const CettaLpNativeGrammar *fallback_grammar,
    const CettaTptpPreparedReaderV1 *reader,
    uint64_t gll_descriptor_limit,
    const char *path,
    Arena *arena,
    Atom **out_records,
    CettaTptpReadOutcomeV1 *outcome,
    CettaTptpCstProjectionV1 projection,
    void *projection_context,
    char *error,
    size_t error_size) {
    FILE *file;
    long size;
    char *text;
    size_t got;
    bool ok;
    read_outcome_init(outcome);
    if (!snap || !path || !arena || !out_records)
        return false;
    file = fopen(path, "rb");
    if (!file) {
        if (error && error_size)
            snprintf(error, error_size, "cannot open %s", path);
        return false;
    }
    if (fseek(file, 0, SEEK_END) != 0 || (size = ftell(file)) < 0) {
        fclose(file);
        return false;
    }
    rewind(file);
    text = malloc((size_t)size + 1u);
    if (!text) {
        fclose(file);
        return false;
    }
    got = fread(text, 1u, (size_t)size, file);
    fclose(file);
    text[got] = '\0';
    ok = snapshot_read_text_impl(
        snap, fallback_grammar, reader, gll_descriptor_limit,
        text, got, arena, out_records, NULL, NULL, NULL, outcome, 0u,
        projection, projection_context,
        error, error_size);
    free(text);
    return ok;
}

bool cetta_tptp_snapshot_read_file_outcome_v1(
    const PPTableSnapshotV1 *snap,
    const char *path,
    Arena *arena,
    Atom **out_records,
    CettaTptpReadOutcomeV1 *outcome,
    char *error,
    size_t error_size) {
    return snapshot_read_file_impl(
        snap, NULL, NULL, 0u, path, arena, out_records, outcome, NULL, NULL,
        error, error_size);
}

bool cetta_tptp_prepared_reader_read_file_outcome_v1(
    const CettaTptpPreparedReaderV1 *reader,
    const char *path,
    Arena *arena,
    Atom **out_records,
    CettaTptpReadOutcomeV1 *outcome,
    char *error,
    size_t error_size) {
    if (!reader)
        return false;
    return snapshot_read_file_impl(
        &reader->snapshot, &reader->fallback_grammar, reader, 0u,
        path, arena, out_records, outcome, NULL, NULL,
        error, error_size);
}

bool cetta_tptp_prepared_reader_read_file_projected_outcome_v1(
    const CettaTptpPreparedReaderV1 *reader,
    const char *path,
    CettaTptpCstProjectionV1 projection,
    void *projection_context,
    Arena *arena,
    Atom **out_records,
    CettaTptpReadOutcomeV1 *outcome,
    char *error,
    size_t error_size) {
    if (!reader || !projection)
        return false;
    return snapshot_read_file_impl(
        &reader->snapshot, &reader->fallback_grammar, reader, 0u,
        path, arena, out_records, outcome, projection, projection_context,
        error, error_size);
}

bool cetta_tptp_file_sha256_hex_v1(const char *path, char out[65],
                                   char *error, size_t error_size) {
    FILE *file;
    uint8_t buffer[4096];
    size_t got;
    CettaNativeSha256 sha;
    if (!path || !out)
        return false;
    file = fopen(path, "rb");
    if (!file) {
        if (error && error_size)
            snprintf(error, error_size, "cannot open %s", path);
        return false;
    }
    cetta_native_sha256_init(&sha);
    while ((got = fread(buffer, 1u, sizeof(buffer), file)) > 0u)
        cetta_native_sha256_update(&sha, buffer, got);
    fclose(file);
    cetta_native_sha256_finish_hex(&sha, out);
    (void)error;
    (void)error_size;
    return true;
}

bool cetta_tptp_write_atom_v1(const char *path, Atom *atom, Arena *arena,
                              char *error, size_t error_size) {
    char *text;
    FILE *file;
    size_t len;
    if (!path || !atom || !arena)
        return false;
    text = atom_to_parseable_string(arena, atom);
    if (!text) {
        if (error && error_size)
            snprintf(error, error_size, "tptp cache: cannot print atom");
        return false;
    }
    file = fopen(path, "wb");
    if (!file) {
        if (error && error_size)
            snprintf(error, error_size, "tptp cache: cannot write %s", path);
        return false;
    }
    len = strlen(text);
    if (fwrite(text, 1u, len, file) != len) {
        fclose(file);
        if (error && error_size)
            snprintf(error, error_size, "tptp cache: short write %s", path);
        return false;
    }
    if (fputc('\n', file) == EOF) {
        fclose(file);
        return false;
    }
    fclose(file);
    return true;
}

bool cetta_tptp_read_atom_v1(const char *path, Arena *arena, Atom **out,
                             char *error, size_t error_size) {
    FILE *file;
    long size;
    char *text;
    size_t got;
    size_t pos = 0u;
    if (out)
        *out = NULL;
    if (!path || !arena || !out)
        return false;
    file = fopen(path, "rb");
    if (!file) {
        if (error && error_size)
            snprintf(error, error_size, "tptp cache: cannot open %s", path);
        return false;
    }
    if (fseek(file, 0, SEEK_END) != 0 || (size = ftell(file)) < 0) {
        fclose(file);
        if (error && error_size)
            snprintf(error, error_size, "tptp cache: cannot size %s", path);
        return false;
    }
    rewind(file);
    text = malloc((size_t)size + 1u);
    if (!text) {
        fclose(file);
        if (error && error_size)
            snprintf(error, error_size, "tptp cache: out of memory");
        return false;
    }
    got = fread(text, 1u, (size_t)size, file);
    fclose(file);
    text[got] = '\0';
    *out = parse_sexpr(arena, text, &pos);
    free(text);
    if (!*out) {
        if (error && error_size)
            snprintf(error, error_size, "tptp cache: cannot parse %s", path);
        return false;
    }
    return true;
}
