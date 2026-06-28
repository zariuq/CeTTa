#include "lib_parse_native_grammar.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "parser.h"

static void grammar_set_error(char *buf, size_t size, const char *fmt, ...) {
    va_list args;

    if (!buf || size == 0)
        return;
    va_start(args, fmt);
    vsnprintf(buf, size, fmt, args);
    va_end(args);
}

static bool atom_expr_head_is(Atom *atom, const char *name) {
    return atom &&
           atom->kind == ATOM_EXPR &&
           atom->expr.len > 0 &&
           atom_is_symbol(atom->expr.elems[0], name);
}

static bool atom_to_symbol_id(Atom *atom, SymbolId *out) {
    if (!atom || !out || atom->kind != ATOM_SYMBOL)
        return false;
    *out = atom->sym_id;
    return true;
}

static bool grow_storage(void **ptr,
                         uint32_t *len,
                         uint32_t *cap,
                         size_t elem_size) {
    void *next;
    uint32_t next_cap;

    if (*len < *cap)
        return true;
    next_cap = (*cap == 0) ? 8u : (*cap * 2u);
    next = cetta_realloc(*ptr, elem_size * next_cap);
    *ptr = next;
    *cap = next_cap;
    return true;
}

void cetta_lp_native_grammar_init(CettaLpNativeGrammar *grammar) {
    if (!grammar)
        return;
    memset(grammar, 0, sizeof(*grammar));
}

void cetta_lp_native_grammar_free(CettaLpNativeGrammar *grammar) {
    uint32_t i;

    if (!grammar)
        return;
    for (i = 0; i < grammar->production_len; i++) {
        free(grammar->productions[i].rhs);
        grammar->productions[i].rhs = NULL;
        grammar->productions[i].rhs_len = 0;
    }
    free(grammar->productions);
    free(grammar->vars);
    free(grammar->lexes);
    memset(grammar, 0, sizeof(*grammar));
}

static bool parse_rhs(Atom *rhs_expr,
                      CettaLpNativeProduction *prod,
                      uint32_t *binder_hole_count,
                      char *error_buf,
                      size_t error_buf_size) {
    uint32_t i;

    if (!rhs_expr || rhs_expr->kind != ATOM_EXPR) {
        grammar_set_error(error_buf, error_buf_size,
                          "Prod rhs must be an expression list");
        return false;
    }
    prod->rhs_len = (uint32_t)rhs_expr->expr.len;
    if (prod->rhs_len == 0) {
        prod->rhs = NULL;
        return true;
    }
    prod->rhs = cetta_malloc(sizeof(CettaLpNativeSymbol) * prod->rhs_len);
    for (i = 0; i < prod->rhs_len; i++) {
        Atom *item = rhs_expr->expr.elems[i];
        CettaLpNativeSymbol *slot = &prod->rhs[i];
        memset(slot, 0, sizeof(*slot));
        if (atom_expr_head_is(item, "Tm") && item->expr.len == 2) {
            slot->kind = CETTA_LP_NATIVE_SYMBOL_TM;
            if (!atom_to_symbol_id(item->expr.elems[1], &slot->name)) {
                grammar_set_error(error_buf, error_buf_size,
                                  "Tm expects a symbol terminal");
                return false;
            }
            continue;
        }
        if (atom_expr_head_is(item, "Hl") && item->expr.len == 2) {
            slot->kind = CETTA_LP_NATIVE_SYMBOL_HL;
            if (!atom_to_symbol_id(item->expr.elems[1], &slot->name)) {
                grammar_set_error(error_buf, error_buf_size,
                                  "Hl expects a symbol nonterminal");
                return false;
            }
            continue;
        }
        if (atom_expr_head_is(item, "HlB") && item->expr.len == 3) {
            slot->kind = CETTA_LP_NATIVE_SYMBOL_HLB;
            if (!atom_to_symbol_id(item->expr.elems[1], &slot->name) ||
                !atom_to_symbol_id(item->expr.elems[2], &slot->scope)) {
                grammar_set_error(error_buf, error_buf_size,
                                  "HlB expects symbol nonterminal and scope");
                return false;
            }
            (*binder_hole_count)++;
            continue;
        }
        grammar_set_error(error_buf, error_buf_size,
                          "unsupported mkGram rhs item");
        return false;
    }
    return true;
}

static bool parse_entry(CettaLpNativeGrammar *grammar,
                        Atom *entry,
                        uint32_t *prod_cap,
                        uint32_t *var_cap,
                        uint32_t *lex_cap,
                        char *error_buf,
                        size_t error_buf_size) {
    if (!entry || entry->kind != ATOM_EXPR || entry->expr.len == 0) {
        grammar_set_error(error_buf, error_buf_size,
                          "mkGram entry must be a non-empty expression");
        return false;
    }
    if (atom_is_symbol(entry->expr.elems[0], "LexD")) {
        CettaLpNativeLexDecl *decl;
        if (entry->expr.len != 3) {
            grammar_set_error(error_buf, error_buf_size,
                              "LexD expects 2 arguments");
            return false;
        }
        grow_storage((void **)&grammar->lexes, &grammar->lex_len, lex_cap,
                     sizeof(*grammar->lexes));
        decl = &grammar->lexes[grammar->lex_len++];
        if (!atom_to_symbol_id(entry->expr.elems[1], &decl->klass) ||
            !atom_to_symbol_id(entry->expr.elems[2], &decl->nt)) {
            grammar_set_error(error_buf, error_buf_size,
                              "LexD expects symbol class and nonterminal");
            return false;
        }
        return true;
    }
    if (atom_is_symbol(entry->expr.elems[0], "VarD")) {
        CettaLpNativeVarDecl *decl;
        if (entry->expr.len != 3) {
            grammar_set_error(error_buf, error_buf_size,
                              "VarD expects 2 arguments");
            return false;
        }
        grow_storage((void **)&grammar->vars, &grammar->var_len, var_cap,
                     sizeof(*grammar->vars));
        decl = &grammar->vars[grammar->var_len++];
        if (!atom_to_symbol_id(entry->expr.elems[1], &decl->atom) ||
            !atom_to_symbol_id(entry->expr.elems[2], &decl->nt)) {
            grammar_set_error(error_buf, error_buf_size,
                              "VarD expects symbol atom and nonterminal");
            return false;
        }
        return true;
    }
    if (atom_is_symbol(entry->expr.elems[0], "Prod")) {
        CettaLpNativeProduction *prod;
        if (entry->expr.len != 4) {
            grammar_set_error(error_buf, error_buf_size,
                              "Prod expects 3 arguments");
            return false;
        }
        grow_storage((void **)&grammar->productions, &grammar->production_len,
                     prod_cap, sizeof(*grammar->productions));
        prod = &grammar->productions[grammar->production_len++];
        memset(prod, 0, sizeof(*prod));
        if (!atom_to_symbol_id(entry->expr.elems[1], &prod->label) ||
            !atom_to_symbol_id(entry->expr.elems[2], &prod->lhs)) {
            grammar_set_error(error_buf, error_buf_size,
                              "Prod expects symbol label and lhs");
            return false;
        }
        if (!parse_rhs(entry->expr.elems[3], prod, &grammar->binder_hole_count,
                       error_buf, error_buf_size)) {
            return false;
        }
        return true;
    }
    grammar_set_error(error_buf, error_buf_size,
                      "unsupported mkGram entry head");
    return false;
}

bool cetta_lp_native_grammar_load_forms(CettaLpNativeGrammar *out,
                                        Atom **forms,
                                        int form_count,
                                        const char *def_name,
                                        char *error_buf,
                                        size_t error_buf_size) {
    int i;
    uint32_t prod_cap = 0;
    uint32_t var_cap = 0;
    uint32_t lex_cap = 0;
    Atom *target = NULL;
    Atom *rhs = NULL;

    if (!out || !forms || form_count < 0 || !def_name || !*def_name) {
        grammar_set_error(error_buf, error_buf_size, "bad grammar loader args");
        return false;
    }

    cetta_lp_native_grammar_free(out);
    cetta_lp_native_grammar_init(out);

    for (i = 0; i < form_count; i++) {
        Atom *form = forms[i];
        if (!atom_expr_head_is(form, "=") || form->expr.len < 3)
            continue;
        target = form->expr.elems[1];
        if (atom_expr_head_is(target, def_name) && target->expr.len == 1) {
            rhs = form->expr.elems[2];
            break;
        }
    }
    if (!rhs) {
        grammar_set_error(error_buf, error_buf_size,
                          "definition %s not found", def_name);
        return false;
    }
    if (!atom_expr_head_is(rhs, "mkGram") || rhs->expr.len != 2) {
        grammar_set_error(error_buf, error_buf_size,
                          "%s is not a mkGram definition", def_name);
        return false;
    }
    if (!rhs->expr.elems[1] || rhs->expr.elems[1]->kind != ATOM_EXPR) {
        grammar_set_error(error_buf, error_buf_size,
                          "mkGram expects a list of entries");
        return false;
    }
    for (i = 0; i < (int)rhs->expr.elems[1]->expr.len; i++) {
        if (!parse_entry(out, rhs->expr.elems[1]->expr.elems[i],
                         &prod_cap, &var_cap, &lex_cap,
                         error_buf, error_buf_size)) {
            cetta_lp_native_grammar_free(out);
            return false;
        }
    }
    return true;
}

bool cetta_lp_native_grammar_load_file(CettaLpNativeGrammar *out,
                                       const char *filename,
                                       const char *def_name,
                                       char *error_buf,
                                       size_t error_buf_size) {
    Arena arena;
    Atom **forms = NULL;
    int form_count;
    bool ok;

    arena_init(&arena);
    form_count = parse_metta_file(filename, &arena, &forms);
    if (form_count < 0) {
        grammar_set_error(error_buf, error_buf_size,
                          "parse_metta_file failed for %s", filename);
        arena_free(&arena);
        return false;
    }
    ok = cetta_lp_native_grammar_load_forms(out, forms, form_count, def_name,
                                            error_buf, error_buf_size);
    arena_free(&arena);
    return ok;
}

void cetta_lp_native_grammar_summary(const CettaLpNativeGrammar *grammar,
                                     CettaLpNativeGrammarSummary *out) {
    if (!out)
        return;
    memset(out, 0, sizeof(*out));
    if (!grammar)
        return;
    out->production_len = grammar->production_len;
    out->var_len = grammar->var_len;
    out->lex_len = grammar->lex_len;
    out->binder_hole_count = grammar->binder_hole_count;
}

typedef struct {
    bool is_terminal;
    SymbolId name;
} CettaLpNativeTransitionSymbol;

typedef struct {
    SymbolId lhs;
    CettaLpNativeSymbol *rhs;
    uint32_t rhs_len;
} CettaLpNativeSlrProduction;

typedef struct {
    int32_t prod_idx;
    uint32_t dot;
} CettaLpNativeItem;

typedef struct {
    CettaLpNativeItem *items;
    uint32_t len;
    uint32_t cap;
} CettaLpNativeItemVec;

typedef struct {
    CettaLpNativeTransitionSymbol *data;
    uint32_t len;
    uint32_t cap;
} CettaLpNativeSymbolVec;

typedef struct {
    SymbolId *data;
    uint32_t len;
    uint32_t cap;
} CettaLpNativeIdVec;

typedef struct {
    uint64_t *words;
    uint32_t word_len;
} CettaLpNativeBitset;

typedef struct {
    CettaLpNativeItem *items;
    uint32_t len;
} CettaLpNativeState;

typedef struct {
    CettaLpNativeState *data;
    uint32_t len;
    uint32_t cap;
} CettaLpNativeStateVec;

typedef struct {
    int32_t *data;
    uint32_t len;
    uint32_t cap;
} CettaLpNativeProdIndexVec;

typedef struct {
    uint32_t state;
    uint32_t symbol;
    uint32_t target;
} CettaLpNativeEdge;

typedef struct {
    CettaLpNativeEdge *data;
    uint32_t len;
    uint32_t cap;
} CettaLpNativeEdgeVec;

typedef struct {
    uint32_t state;
    uint32_t token_idx;
    uint8_t kind;
    int32_t value;
} CettaLpNativeAction;

typedef struct {
    CettaLpNativeAction *data;
    uint32_t len;
    uint32_t cap;
} CettaLpNativeActionVec;

static void slr_summary_set_error(char *buf, size_t size, const char *fmt, ...) {
    va_list args;

    if (!buf || size == 0)
        return;
    va_start(args, fmt);
    vsnprintf(buf, size, fmt, args);
    va_end(args);
}

static bool idvec_push(CettaLpNativeIdVec *vec, SymbolId value) {
    if (!grow_storage((void **)&vec->data, &vec->len, &vec->cap,
                      sizeof(*vec->data))) {
        return false;
    }
    vec->data[vec->len++] = value;
    return true;
}

static bool idvec_push_unique(CettaLpNativeIdVec *vec, SymbolId value) {
    uint32_t i;

    for (i = 0; i < vec->len; i++) {
        if (vec->data[i] == value)
            return true;
    }
    return idvec_push(vec, value);
}

static int32_t idvec_find(const CettaLpNativeIdVec *vec, SymbolId value) {
    uint32_t i;

    for (i = 0; i < vec->len; i++) {
        if (vec->data[i] == value)
            return (int32_t)i;
    }
    return -1;
}

static bool symbolvec_push_unique(CettaLpNativeSymbolVec *vec,
                                  bool is_terminal,
                                  SymbolId name) {
    uint32_t i;

    for (i = 0; i < vec->len; i++) {
        if (vec->data[i].is_terminal == is_terminal &&
            vec->data[i].name == name) {
            return true;
        }
    }
    if (!grow_storage((void **)&vec->data, &vec->len, &vec->cap,
                      sizeof(*vec->data))) {
        return false;
    }
    vec->data[vec->len].is_terminal = is_terminal;
    vec->data[vec->len].name = name;
    vec->len++;
    return true;
}

static int32_t symbolvec_find(const CettaLpNativeSymbolVec *vec,
                              bool is_terminal,
                              SymbolId name) {
    uint32_t i;

    for (i = 0; i < vec->len; i++) {
        if (vec->data[i].is_terminal == is_terminal &&
            vec->data[i].name == name) {
            return (int32_t)i;
        }
    }
    return -1;
}

static bool itemvec_push_unique(CettaLpNativeItemVec *vec,
                                int32_t prod_idx,
                                uint32_t dot) {
    uint32_t i;

    for (i = 0; i < vec->len; i++) {
        if (vec->items[i].prod_idx == prod_idx && vec->items[i].dot == dot)
            return true;
    }
    if (!grow_storage((void **)&vec->items, &vec->len, &vec->cap,
                      sizeof(*vec->items))) {
        return false;
    }
    vec->items[vec->len].prod_idx = prod_idx;
    vec->items[vec->len].dot = dot;
    vec->len++;
    return true;
}

static int cmp_items(const void *lhs, const void *rhs) {
    const CettaLpNativeItem *a = lhs;
    const CettaLpNativeItem *b = rhs;

    if (a->prod_idx < b->prod_idx)
        return -1;
    if (a->prod_idx > b->prod_idx)
        return 1;
    if (a->dot < b->dot)
        return -1;
    if (a->dot > b->dot)
        return 1;
    return 0;
}

static void itemvec_sort(CettaLpNativeItemVec *vec) {
    if (!vec || vec->len < 2)
        return;
    qsort(vec->items, vec->len, sizeof(*vec->items), cmp_items);
}

static void bitset_zero(CettaLpNativeBitset *bitset) {
    if (!bitset || !bitset->words)
        return;
    memset(bitset->words, 0, sizeof(uint64_t) * bitset->word_len);
}

static bool bitset_init(CettaLpNativeBitset *bitset, uint32_t bit_count) {
    if (!bitset)
        return false;
    bitset->word_len = (bit_count + 63u) / 64u;
    bitset->words = NULL;
    if (bitset->word_len == 0)
        return true;
    bitset->words = cetta_malloc(sizeof(uint64_t) * bitset->word_len);
    bitset_zero(bitset);
    return true;
}

static void bitset_free(CettaLpNativeBitset *bitset) {
    if (!bitset)
        return;
    free(bitset->words);
    bitset->words = NULL;
    bitset->word_len = 0;
}

static bool bitset_set(CettaLpNativeBitset *bitset, uint32_t idx) {
    uint32_t word = idx / 64u;
    uint64_t mask = 1ull << (idx % 64u);
    bool changed;

    if (!bitset || word >= bitset->word_len)
        return false;
    changed = (bitset->words[word] & mask) == 0;
    bitset->words[word] |= mask;
    return changed;
}

static bool bitset_test(const CettaLpNativeBitset *bitset, uint32_t idx) {
    uint32_t word = idx / 64u;
    uint64_t mask = 1ull << (idx % 64u);

    if (!bitset || word >= bitset->word_len)
        return false;
    return (bitset->words[word] & mask) != 0;
}

static bool bitset_or_changed(CettaLpNativeBitset *dst,
                              const CettaLpNativeBitset *src) {
    bool changed = false;
    uint32_t i;

    for (i = 0; i < dst->word_len && i < src->word_len; i++) {
        uint64_t next = dst->words[i] | src->words[i];
        if (next != dst->words[i]) {
            dst->words[i] = next;
            changed = true;
        }
    }
    return changed;
}

static bool copy_rhs_symbol_array(CettaLpNativeSymbol **out,
                                  uint32_t *out_len,
                                  const CettaLpNativeSymbol *rhs,
                                  uint32_t rhs_len) {
    CettaLpNativeSymbol *copy = NULL;

    if (!out || !out_len)
        return false;
    *out = NULL;
    *out_len = rhs_len;
    if (rhs_len == 0)
        return true;
    copy = cetta_malloc(sizeof(*copy) * rhs_len);
    memcpy(copy, rhs, sizeof(*copy) * rhs_len);
    *out = copy;
    return true;
}

static void slr_productions_free(CettaLpNativeSlrProduction *productions,
                                 uint32_t production_len) {
    uint32_t i;

    for (i = 0; i < production_len; i++) {
        free(productions[i].rhs);
        productions[i].rhs = NULL;
        productions[i].rhs_len = 0;
    }
    free(productions);
}

static bool slr_build_productions(const CettaLpNativeGrammar *grammar,
                                  CettaLpNativeSlrProduction **out_productions,
                                  uint32_t *out_len,
                                  char *error_buf,
                                  size_t error_buf_size) {
    CettaLpNativeSlrProduction *productions = NULL;
    uint32_t total = grammar->production_len + grammar->lex_len + grammar->var_len;
    uint32_t idx = 0;
    uint32_t i;

    if (!out_productions || !out_len) {
        slr_summary_set_error(error_buf, error_buf_size,
                              "bad SLR production output args");
        return false;
    }
    *out_productions = NULL;
    *out_len = 0;
    if (total == 0) {
        slr_summary_set_error(error_buf, error_buf_size,
                              "mkGram has no productions");
        return false;
    }

    productions = cetta_malloc(sizeof(*productions) * total);
    memset(productions, 0, sizeof(*productions) * total);

    for (i = 0; i < grammar->production_len; i++) {
        productions[idx].lhs = grammar->productions[i].lhs;
        if (!copy_rhs_symbol_array(&productions[idx].rhs,
                                   &productions[idx].rhs_len,
                                   grammar->productions[i].rhs,
                                   grammar->productions[i].rhs_len)) {
            slr_summary_set_error(error_buf, error_buf_size,
                                  "failed to copy grammar productions");
            slr_productions_free(productions, total);
            return false;
        }
        idx++;
    }
    for (i = 0; i < grammar->lex_len; i++) {
        productions[idx].lhs = grammar->lexes[i].nt;
        productions[idx].rhs = cetta_malloc(sizeof(*productions[idx].rhs));
        productions[idx].rhs_len = 1;
        productions[idx].rhs[0].kind = CETTA_LP_NATIVE_SYMBOL_TM;
        productions[idx].rhs[0].name = grammar->lexes[i].klass;
        productions[idx].rhs[0].scope = 0;
        idx++;
    }
    for (i = 0; i < grammar->var_len; i++) {
        productions[idx].lhs = grammar->vars[i].nt;
        productions[idx].rhs = cetta_malloc(sizeof(*productions[idx].rhs));
        productions[idx].rhs_len = 1;
        productions[idx].rhs[0].kind = CETTA_LP_NATIVE_SYMBOL_TM;
        productions[idx].rhs[0].name = grammar->vars[i].atom;
        productions[idx].rhs[0].scope = 0;
        idx++;
    }
    *out_productions = productions;
    *out_len = total;
    return true;
}

static void slr_get_prod(const CettaLpNativeSlrProduction *productions,
                         uint32_t production_len,
                         SymbolId start_nt,
                         int32_t prod_idx,
                         SymbolId *out_lhs,
                         const CettaLpNativeSymbol **out_rhs,
                         uint32_t *out_rhs_len) {
    static CettaLpNativeSymbol augmented_rhs[1];

    if (prod_idx == -1) {
        augmented_rhs[0].kind = CETTA_LP_NATIVE_SYMBOL_HL;
        augmented_rhs[0].name = start_nt;
        augmented_rhs[0].scope = 0;
        *out_lhs = start_nt;
        *out_rhs = augmented_rhs;
        *out_rhs_len = 1;
        return;
    }
    if ((uint32_t)prod_idx >= production_len) {
        *out_lhs = 0;
        *out_rhs = NULL;
        *out_rhs_len = 0;
        return;
    }
    *out_lhs = productions[prod_idx].lhs;
    *out_rhs = productions[prod_idx].rhs;
    *out_rhs_len = productions[prod_idx].rhs_len;
}

static bool slr_collect_symbol_sets(const CettaLpNativeSlrProduction *productions,
                                    uint32_t production_len,
                                    SymbolId start_nt,
                                    CettaLpNativeIdVec *nonterminals,
                                    CettaLpNativeIdVec *terminals,
                                    char *error_buf,
                                    size_t error_buf_size) {
    uint32_t i;
    uint32_t j;

    if (!idvec_push_unique(nonterminals, start_nt)) {
        slr_summary_set_error(error_buf, error_buf_size,
                              "failed to record start nonterminal");
        return false;
    }
    for (i = 0; i < production_len; i++) {
        if (!idvec_push_unique(nonterminals, productions[i].lhs)) {
            slr_summary_set_error(error_buf, error_buf_size,
                                  "failed to record nonterminal");
            return false;
        }
        for (j = 0; j < productions[i].rhs_len; j++) {
            if (productions[i].rhs[j].kind == CETTA_LP_NATIVE_SYMBOL_TM) {
                if (!idvec_push_unique(terminals, productions[i].rhs[j].name)) {
                    slr_summary_set_error(error_buf, error_buf_size,
                                          "failed to record terminal");
                    return false;
                }
            } else if (!idvec_push_unique(nonterminals, productions[i].rhs[j].name)) {
                slr_summary_set_error(error_buf, error_buf_size,
                                      "failed to record nonterminal reference");
                return false;
            }
        }
    }
    return true;
}

static bool slr_grammar_mentions_nonterminal(
    const CettaLpNativeSlrProduction *productions,
    uint32_t production_len,
    SymbolId target) {
    uint32_t i;
    uint32_t j;

    for (i = 0; i < production_len; i++) {
        if (productions[i].lhs == target)
            return true;
        for (j = 0; j < productions[i].rhs_len; j++) {
            if (productions[i].rhs[j].kind != CETTA_LP_NATIVE_SYMBOL_TM &&
                productions[i].rhs[j].name == target) {
                return true;
            }
        }
    }
    return false;
}

static bool slr_compute_nullable_first_follow(
    const CettaLpNativeSlrProduction *productions,
    uint32_t production_len,
    const CettaLpNativeIdVec *nonterminals,
    const CettaLpNativeIdVec *terminals,
    SymbolId start_nt,
    bool **out_nullable,
    CettaLpNativeBitset **out_first,
    CettaLpNativeBitset **out_follow,
    char *error_buf,
    size_t error_buf_size) {
    bool *nullable = NULL;
    CettaLpNativeBitset *first = NULL;
    CettaLpNativeBitset *follow = NULL;
    uint32_t nt_len = nonterminals->len;
    uint32_t bit_count = terminals->len + 1u;
    uint32_t eof_idx = terminals->len;
    uint32_t i;
    bool changed = true;

    *out_nullable = NULL;
    *out_first = NULL;
    *out_follow = NULL;

    nullable = cetta_malloc(sizeof(*nullable) * nt_len);
    memset(nullable, 0, sizeof(*nullable) * nt_len);
    first = cetta_malloc(sizeof(*first) * nt_len);
    follow = cetta_malloc(sizeof(*follow) * nt_len);
    for (i = 0; i < nt_len; i++) {
        memset(&first[i], 0, sizeof(*first));
        memset(&follow[i], 0, sizeof(*follow));
        if (!bitset_init(&first[i], bit_count) ||
            !bitset_init(&follow[i], bit_count)) {
            slr_summary_set_error(error_buf, error_buf_size,
                                  "failed to allocate first/follow sets");
            goto fail;
        }
    }

    while (changed) {
        changed = false;
        for (i = 0; i < production_len; i++) {
            uint32_t j;
            int32_t lhs_idx = idvec_find(nonterminals, productions[i].lhs);
            bool all_nullable = true;

            if (lhs_idx < 0) {
                slr_summary_set_error(error_buf, error_buf_size,
                                      "lhs missing from nonterminal set");
                goto fail;
            }
            for (j = 0; j < productions[i].rhs_len; j++) {
                int32_t nt_idx;
                if (productions[i].rhs[j].kind == CETTA_LP_NATIVE_SYMBOL_TM) {
                    all_nullable = false;
                    break;
                }
                nt_idx = idvec_find(nonterminals, productions[i].rhs[j].name);
                if (nt_idx < 0) {
                    slr_summary_set_error(error_buf, error_buf_size,
                                          "nullable rhs nonterminal missing from set");
                    goto fail;
                }
                if (!nullable[nt_idx]) {
                    all_nullable = false;
                    break;
                }
            }
            if (all_nullable && !nullable[lhs_idx]) {
                nullable[lhs_idx] = true;
                changed = true;
            }
        }
    }

    changed = true;
    while (changed) {
        changed = false;
        for (i = 0; i < production_len; i++) {
            uint32_t j;
            int32_t lhs_idx = idvec_find(nonterminals, productions[i].lhs);

            for (j = 0; j < productions[i].rhs_len; j++) {
                int32_t nt_idx;
                if (productions[i].rhs[j].kind == CETTA_LP_NATIVE_SYMBOL_TM) {
                    int32_t term_idx = idvec_find(terminals, productions[i].rhs[j].name);
                    if (term_idx >= 0 && bitset_set(&first[lhs_idx], (uint32_t)term_idx))
                        changed = true;
                    break;
                }
                nt_idx = idvec_find(nonterminals, productions[i].rhs[j].name);
                if (nt_idx < 0) {
                    slr_summary_set_error(error_buf, error_buf_size,
                                          "rhs nonterminal missing from set");
                    goto fail;
                }
                if (bitset_or_changed(&first[lhs_idx], &first[nt_idx]))
                    changed = true;
                if (!nullable[nt_idx])
                    break;
            }
        }
    }

    {
        int32_t start_idx = idvec_find(nonterminals, start_nt);
        if (start_idx < 0) {
            slr_summary_set_error(error_buf, error_buf_size,
                                  "start nonterminal missing from set");
            goto fail;
        }
        bitset_set(&follow[start_idx], eof_idx);
    }

    changed = true;
    while (changed) {
        changed = false;
        for (i = 0; i < production_len; i++) {
            uint32_t j;
            int32_t lhs_idx = idvec_find(nonterminals, productions[i].lhs);
            for (j = 0; j < productions[i].rhs_len; j++) {
                int32_t nt_idx;
                bool rest_nullable = true;
                uint32_t k;

                if (productions[i].rhs[j].kind == CETTA_LP_NATIVE_SYMBOL_TM)
                    continue;
                nt_idx = idvec_find(nonterminals, productions[i].rhs[j].name);
                if (nt_idx < 0) {
                    slr_summary_set_error(error_buf, error_buf_size,
                                          "follow target missing from set");
                    goto fail;
                }
                for (k = j + 1; k < productions[i].rhs_len; k++) {
                    if (productions[i].rhs[k].kind == CETTA_LP_NATIVE_SYMBOL_TM) {
                        int32_t term_idx = idvec_find(terminals, productions[i].rhs[k].name);
                        if (term_idx >= 0 && bitset_set(&follow[nt_idx], (uint32_t)term_idx))
                            changed = true;
                        rest_nullable = false;
                        break;
                    }
                    {
                        int32_t rest_idx =
                            idvec_find(nonterminals, productions[i].rhs[k].name);
                        if (rest_idx < 0) {
                            slr_summary_set_error(error_buf, error_buf_size,
                                                  "follow rest nonterminal missing from set");
                            goto fail;
                        }
                        if (bitset_or_changed(&follow[nt_idx], &first[rest_idx])) {
                            changed = true;
                        }
                        if (!nullable[rest_idx]) {
                            rest_nullable = false;
                            break;
                        }
                    }
                }
                if (rest_nullable &&
                    bitset_or_changed(&follow[nt_idx], &follow[lhs_idx])) {
                    changed = true;
                }
            }
        }
    }

    *out_nullable = nullable;
    *out_first = first;
    *out_follow = follow;
    return true;

fail:
    if (first) {
        for (i = 0; i < nt_len; i++)
            bitset_free(&first[i]);
    }
    if (follow) {
        for (i = 0; i < nt_len; i++)
            bitset_free(&follow[i]);
    }
    free(nullable);
    free(first);
    free(follow);
    return false;
}

static bool slr_item_closure(const CettaLpNativeSlrProduction *productions,
                             uint32_t production_len,
                             SymbolId start_nt,
                             CettaLpNativeItemVec *items,
                             char *error_buf,
                             size_t error_buf_size) {
    bool changed = true;

    while (changed) {
        uint32_t i;
        changed = false;
        for (i = 0; i < items->len; i++) {
            SymbolId lhs = 0;
            const CettaLpNativeSymbol *rhs = NULL;
            uint32_t rhs_len = 0;
            uint32_t prod_idx;
            uint32_t dot = items->items[i].dot;

            slr_get_prod(productions, production_len, start_nt,
                         items->items[i].prod_idx, &lhs, &rhs, &rhs_len);
            (void)lhs;
            if (!rhs || dot >= rhs_len || rhs[dot].kind == CETTA_LP_NATIVE_SYMBOL_TM)
                continue;
            for (prod_idx = 0; prod_idx < production_len; prod_idx++) {
                uint32_t before_len;
                if (productions[prod_idx].lhs != rhs[dot].name)
                    continue;
                before_len = items->len;
                if (!itemvec_push_unique(items, (int32_t)prod_idx, 0)) {
                    slr_summary_set_error(error_buf, error_buf_size,
                                          "failed to extend closure");
                    return false;
                }
                if (items->len != before_len) {
                    changed = true;
                }
            }
        }
    }
    itemvec_sort(items);
    return true;
}

static bool slr_state_equals(const CettaLpNativeState *lhs,
                             const CettaLpNativeItemVec *rhs) {
    if (lhs->len != rhs->len)
        return false;
    return memcmp(lhs->items, rhs->items,
                  sizeof(*lhs->items) * lhs->len) == 0;
}

static int32_t slr_state_find(const CettaLpNativeStateVec *states,
                              const CettaLpNativeItemVec *candidate) {
    uint32_t i;

    for (i = 0; i < states->len; i++) {
        if (slr_state_equals(&states->data[i], candidate))
            return (int32_t)i;
    }
    return -1;
}

static bool statevec_push_copy(CettaLpNativeStateVec *states,
                               const CettaLpNativeItemVec *items) {
    CettaLpNativeState *slot;
    if (!grow_storage((void **)&states->data, &states->len, &states->cap,
                      sizeof(*states->data))) {
        return false;
    }
    slot = &states->data[states->len++];
    slot->len = items->len;
    slot->items = NULL;
    if (items->len == 0)
        return true;
    slot->items = cetta_malloc(sizeof(*slot->items) * items->len);
    memcpy(slot->items, items->items, sizeof(*slot->items) * items->len);
    return true;
}

static void statevec_free(CettaLpNativeStateVec *states) {
    uint32_t i;
    if (!states)
        return;
    for (i = 0; i < states->len; i++)
        free(states->data[i].items);
    free(states->data);
    memset(states, 0, sizeof(*states));
}

static bool prodindexvec_push(CettaLpNativeProdIndexVec *vec, int32_t value) {
    if (!grow_storage((void **)&vec->data, &vec->len, &vec->cap,
                      sizeof(*vec->data))) {
        return false;
    }
    vec->data[vec->len++] = value;
    return true;
}

static bool edgevec_push(CettaLpNativeEdgeVec *edges,
                         uint32_t state,
                         uint32_t symbol,
                         uint32_t target) {
    if (!grow_storage((void **)&edges->data, &edges->len, &edges->cap,
                      sizeof(*edges->data))) {
        return false;
    }
    edges->data[edges->len].state = state;
    edges->data[edges->len].symbol = symbol;
    edges->data[edges->len].target = target;
    edges->len++;
    return true;
}

static int32_t edgevec_find(const CettaLpNativeEdgeVec *edges,
                            uint32_t state,
                            uint32_t symbol) {
    uint32_t i;

    for (i = 0; i < edges->len; i++) {
        if (edges->data[i].state == state && edges->data[i].symbol == symbol)
            return (int32_t)i;
    }
    return -1;
}

static bool slr_build_states(const CettaLpNativeSlrProduction *productions,
                             uint32_t production_len,
                             SymbolId start_nt,
                             CettaLpNativeStateVec *states,
                             CettaLpNativeSymbolVec *symbols,
                             CettaLpNativeEdgeVec *edges,
                             char *error_buf,
                             size_t error_buf_size) {
    CettaLpNativeItemVec start_state = {0};
    CettaLpNativeProdIndexVec work = {0};
    uint32_t work_idx = 0;
    uint32_t i;

    if (!itemvec_push_unique(&start_state, -1, 0) ||
        !slr_item_closure(productions, production_len, start_nt, &start_state,
                          error_buf, error_buf_size) ||
        !statevec_push_copy(states, &start_state) ||
        !prodindexvec_push(&work, 0)) {
        slr_summary_set_error(error_buf, error_buf_size,
                              "failed to initialize LR states");
        free(start_state.items);
        free(work.data);
        return false;
    }
    free(start_state.items);

    for (i = 0; i < production_len; i++) {
        uint32_t j;
        for (j = 0; j < productions[i].rhs_len; j++) {
            if (!symbolvec_push_unique(symbols,
                                       productions[i].rhs[j].kind == CETTA_LP_NATIVE_SYMBOL_TM,
                                       productions[i].rhs[j].name)) {
                slr_summary_set_error(error_buf, error_buf_size,
                                      "failed to collect transition symbols");
                free(work.data);
                return false;
            }
        }
    }
    if (!symbolvec_push_unique(symbols, false, start_nt)) {
        slr_summary_set_error(error_buf, error_buf_size,
                              "failed to collect start symbol");
        free(work.data);
        return false;
    }

    while (work_idx < work.len) {
        uint32_t state_idx = (uint32_t)work.data[work_idx++];
        uint32_t sym_idx;
        for (sym_idx = 0; sym_idx < symbols->len; sym_idx++) {
            CettaLpNativeItemVec moved = {0};
            uint32_t item_idx;
            bool ok = true;

            for (item_idx = 0; item_idx < states->data[state_idx].len; item_idx++) {
                SymbolId lhs = 0;
                const CettaLpNativeSymbol *rhs = NULL;
                uint32_t rhs_len = 0;
                CettaLpNativeItem item = states->data[state_idx].items[item_idx];

                slr_get_prod(productions, production_len, start_nt,
                             item.prod_idx, &lhs, &rhs, &rhs_len);
                (void)lhs;
                if (!rhs || item.dot >= rhs_len)
                    continue;
                if ((rhs[item.dot].kind == CETTA_LP_NATIVE_SYMBOL_TM) != symbols->data[sym_idx].is_terminal ||
                    rhs[item.dot].name != symbols->data[sym_idx].name) {
                    continue;
                }
                ok = itemvec_push_unique(&moved, item.prod_idx, item.dot + 1);
                if (!ok)
                    break;
            }
            if (!ok || moved.len == 0) {
                free(moved.items);
                if (!ok) {
                    slr_summary_set_error(error_buf, error_buf_size,
                                          "failed to build goto items");
                    free(work.data);
                    return false;
                }
                continue;
            }
            if (!slr_item_closure(productions, production_len, start_nt,
                                  &moved, error_buf, error_buf_size)) {
                free(moved.items);
                free(work.data);
                return false;
            }
            {
                int32_t found = slr_state_find(states, &moved);
                uint32_t target;
                if (found < 0) {
                    if (!statevec_push_copy(states, &moved) ||
                        !prodindexvec_push(&work, (int32_t)(states->len - 1))) {
                        free(moved.items);
                        free(work.data);
                        slr_summary_set_error(error_buf, error_buf_size,
                                              "failed to record goto state");
                        return false;
                    }
                    target = states->len - 1;
                } else {
                    target = (uint32_t)found;
                }
                if (!edgevec_push(edges, state_idx, sym_idx, target)) {
                    free(moved.items);
                    free(work.data);
                    slr_summary_set_error(error_buf, error_buf_size,
                                          "failed to record goto edge");
                    return false;
                }
            }
            free(moved.items);
        }
    }

    free(work.data);
    return true;
}

static bool actionvec_set(CettaLpNativeActionVec *actions,
                          uint32_t state,
                          uint32_t token_idx,
                          uint8_t kind,
                          int32_t value,
                          uint32_t *conflict_len) {
    uint32_t i;

    for (i = 0; i < actions->len; i++) {
        CettaLpNativeAction *cur = &actions->data[i];
        if (cur->state != state || cur->token_idx != token_idx)
            continue;
        if (cur->kind == kind && cur->value == value)
            return true;
        (*conflict_len)++;
        if (cur->kind == 's' || kind != 's')
            return true;
        cur->kind = kind;
        cur->value = value;
        return true;
    }
    if (!grow_storage((void **)&actions->data, &actions->len, &actions->cap,
                      sizeof(*actions->data))) {
        return false;
    }
    actions->data[actions->len].state = state;
    actions->data[actions->len].token_idx = token_idx;
    actions->data[actions->len].kind = kind;
    actions->data[actions->len].value = value;
    actions->len++;
    return true;
}

static bool actionvec_push_unique(CettaLpNativeActionVec *actions,
                                  uint32_t state,
                                  uint32_t token_idx,
                                  uint8_t kind,
                                  int32_t value,
                                  uint32_t *conflict_len) {
    uint32_t i;
    bool same_key_seen = false;

    for (i = 0; i < actions->len; i++) {
        CettaLpNativeAction *cur = &actions->data[i];
        if (cur->state != state || cur->token_idx != token_idx)
            continue;
        if (cur->kind == kind && cur->value == value)
            return true;
        same_key_seen = true;
    }
    if (same_key_seen && conflict_len)
        (*conflict_len)++;
    if (!grow_storage((void **)&actions->data, &actions->len, &actions->cap,
                      sizeof(*actions->data))) {
        return false;
    }
    actions->data[actions->len].state = state;
    actions->data[actions->len].token_idx = token_idx;
    actions->data[actions->len].kind = kind;
    actions->data[actions->len].value = value;
    actions->len++;
    return true;
}

bool cetta_lp_native_slr_summary(const CettaLpNativeGrammar *grammar,
                                 SymbolId start_nt,
                                 CettaLpNativeSlrSummary *out,
                                 char *error_buf,
                                 size_t error_buf_size) {
    CettaLpNativeSlrProduction *productions = NULL;
    uint32_t production_len = 0;
    CettaLpNativeIdVec nonterminals = {0};
    CettaLpNativeIdVec terminals = {0};
    bool *nullable = NULL;
    CettaLpNativeBitset *first = NULL;
    CettaLpNativeBitset *follow = NULL;
    CettaLpNativeStateVec states = {0};
    CettaLpNativeSymbolVec symbols = {0};
    CettaLpNativeEdgeVec edges = {0};
    CettaLpNativeActionVec actions = {0};
    uint32_t accept_len = 0;
    uint32_t conflict_len = 0;
    uint32_t goto_len = 0;
    uint32_t i;

    if (!out || !grammar) {
        slr_summary_set_error(error_buf, error_buf_size,
                              "bad SLR summary args");
        return false;
    }
    memset(out, 0, sizeof(*out));

    if (!slr_build_productions(grammar, &productions, &production_len,
                               error_buf, error_buf_size) ||
        !slr_collect_symbol_sets(productions, production_len, start_nt,
                                 &nonterminals, &terminals,
                                 error_buf, error_buf_size)) {
        goto fail;
    }
    if (!slr_grammar_mentions_nonterminal(productions, production_len, start_nt)) {
        slr_summary_set_error(error_buf, error_buf_size,
                              "start nonterminal not present in grammar");
        goto fail;
    }
    if (!slr_compute_nullable_first_follow(productions, production_len,
                                           &nonterminals, &terminals, start_nt,
                                           &nullable, &first, &follow,
                                           error_buf, error_buf_size) ||
        !slr_build_states(productions, production_len, start_nt,
                          &states, &symbols, &edges,
                          error_buf, error_buf_size)) {
        goto fail;
    }

    for (i = 0; i < edges.len; i++) {
        if (!symbols.data[edges.data[i].symbol].is_terminal)
            goto_len++;
    }

    for (i = 0; i < states.len; i++) {
        uint32_t item_idx;
        for (item_idx = 0; item_idx < states.data[i].len; item_idx++) {
            SymbolId lhs = 0;
            const CettaLpNativeSymbol *rhs = NULL;
            uint32_t rhs_len = 0;
            CettaLpNativeItem item = states.data[i].items[item_idx];

            slr_get_prod(productions, production_len, start_nt,
                         item.prod_idx, &lhs, &rhs, &rhs_len);
            if (rhs && item.dot < rhs_len) {
                if (rhs[item.dot].kind == CETTA_LP_NATIVE_SYMBOL_TM) {
                    int32_t sym_idx = symbolvec_find(&symbols, true, rhs[item.dot].name);
                    int32_t edge_idx;
                    int32_t term_idx = idvec_find(&terminals, rhs[item.dot].name);
                    if (sym_idx >= 0 && term_idx >= 0) {
                        edge_idx = edgevec_find(&edges, i, (uint32_t)sym_idx);
                    } else {
                        edge_idx = -1;
                    }
                    if (edge_idx >= 0 && term_idx >= 0) {
                        if (!actionvec_set(&actions, i, (uint32_t)term_idx, 's',
                                           (int32_t)edges.data[edge_idx].target,
                                           &conflict_len)) {
                            slr_summary_set_error(error_buf, error_buf_size,
                                                  "failed to record shift action");
                            goto fail;
                        }
                    }
                }
                continue;
            }
            if (item.prod_idx == -1) {
                accept_len++;
                if (!actionvec_set(&actions, i, terminals.len, 'a', 0,
                                   &conflict_len)) {
                    slr_summary_set_error(error_buf, error_buf_size,
                                          "failed to record accept action");
                    goto fail;
                }
                continue;
            }
            {
                int32_t lhs_idx = idvec_find(&nonterminals, lhs);
                uint32_t tok_idx;
                if (lhs_idx < 0) {
                    slr_summary_set_error(error_buf, error_buf_size,
                                          "reduce lhs missing from follow set");
                    goto fail;
                }
                for (tok_idx = 0; tok_idx <= terminals.len; tok_idx++) {
                    if (!bitset_test(&follow[lhs_idx], tok_idx))
                        continue;
                    if (!actionvec_set(&actions, i, tok_idx, 'r',
                                       item.prod_idx, &conflict_len)) {
                        slr_summary_set_error(error_buf, error_buf_size,
                                              "failed to record reduce action");
                        goto fail;
                    }
                }
            }
        }
    }

    out->state_len = states.len;
    out->shift_len = 0;
    out->reduce_len = 0;
    out->goto_len = goto_len;
    out->accept_len = accept_len;
    out->conflict_len = conflict_len;
    for (i = 0; i < actions.len; i++) {
        if (actions.data[i].kind == 's')
            out->shift_len++;
        else if (actions.data[i].kind == 'r')
            out->reduce_len++;
    }

    for (i = 0; i < nonterminals.len; i++) {
        bitset_free(&first[i]);
        bitset_free(&follow[i]);
    }
    free(nullable);
    free(first);
    free(follow);
    free(nonterminals.data);
    free(terminals.data);
    free(symbols.data);
    free(edges.data);
    free(actions.data);
    statevec_free(&states);
    slr_productions_free(productions, production_len);
    return true;

fail:
    if (first && follow) {
        for (i = 0; i < nonterminals.len; i++) {
            bitset_free(&first[i]);
            bitset_free(&follow[i]);
        }
    }
    free(nullable);
    free(first);
    free(follow);
    free(nonterminals.data);
    free(terminals.data);
    free(symbols.data);
    free(edges.data);
    free(actions.data);
    statevec_free(&states);
    slr_productions_free(productions, production_len);
    return false;
}

typedef struct {
    Atom *token_atom;
    SymbolId term_kind;
    uint32_t pos;
} CettaLpNativeInputToken;

typedef struct {
    CettaLpNativeInputToken *data;
    uint32_t len;
    uint32_t cap;
} CettaLpNativeInputTokenVec;

typedef struct {
    bool is_cert;
    Atom *token_atom;
    SymbolId term_kind;
    uint32_t start;
    uint32_t end;
    uint32_t forest_idx;
    Atom *cert;
} CettaLpNativeParseValue;

typedef struct {
    CettaLpNativeParseValue *data;
    uint32_t len;
    uint32_t cap;
} CettaLpNativeParseValueVec;

typedef struct {
    uint32_t *data;
    uint32_t len;
    uint32_t cap;
} CettaLpNativeU32Vec;

typedef struct {
    uint32_t pos;
    CettaLpNativeU32Vec state_stack;
    CettaLpNativeParseValueVec value_stack;
    uint8_t ways_total;
    uint8_t ways_done;
    bool queued;
} CettaLpNativeBranch;

typedef struct {
    CettaLpNativeBranch *data;
    uint32_t len;
    uint32_t cap;
} CettaLpNativeBranchVec;

typedef enum {
    CETTA_LP_NATIVE_FOREST_OUT_SUMMARY = 0,
    CETTA_LP_NATIVE_FOREST_OUT_DATA = 1,
    CETTA_LP_NATIVE_FOREST_OUT_SIGNATURE = 2,
    CETTA_LP_NATIVE_FOREST_OUT_SIGNATURE_DIGEST = 3,
} CettaLpNativeForestOutput;

typedef struct {
    uint8_t kind;
    SymbolId symbol;
    uint32_t left;
    uint32_t right;
} CettaLpNativeForestSpan;

typedef struct {
    CettaLpNativeForestSpan *data;
    uint32_t len;
    uint32_t cap;
} CettaLpNativeForestSpanVec;

static bool input_tokenvec_push(CettaLpNativeInputTokenVec *vec,
                                Atom *token_atom,
                                SymbolId term_kind,
                                uint32_t pos) {
    if (!grow_storage((void **)&vec->data, &vec->len, &vec->cap,
                      sizeof(*vec->data))) {
        return false;
    }
    vec->data[vec->len].token_atom = token_atom;
    vec->data[vec->len].term_kind = term_kind;
    vec->data[vec->len].pos = pos;
    vec->len++;
    return true;
}

static bool parsevaluevec_push(CettaLpNativeParseValueVec *vec,
                               const CettaLpNativeParseValue *value) {
    if (!grow_storage((void **)&vec->data, &vec->len, &vec->cap,
                      sizeof(*vec->data))) {
        return false;
    }
    vec->data[vec->len++] = *value;
    return true;
}

static bool parsevaluevec_copy(CettaLpNativeParseValueVec *dst,
                               const CettaLpNativeParseValueVec *src) {
    if (!dst || !src)
        return false;
    memset(dst, 0, sizeof(*dst));
    if (src->len == 0)
        return true;
    dst->data = cetta_malloc(sizeof(*dst->data) * src->len);
    memcpy(dst->data, src->data, sizeof(*dst->data) * src->len);
    dst->len = src->len;
    dst->cap = src->len;
    return true;
}

static bool u32vec_push(CettaLpNativeU32Vec *vec, uint32_t value) {
    if (!grow_storage((void **)&vec->data, &vec->len, &vec->cap,
                      sizeof(*vec->data))) {
        return false;
    }
    vec->data[vec->len++] = value;
    return true;
}

static bool u32vec_copy(CettaLpNativeU32Vec *dst,
                        const CettaLpNativeU32Vec *src) {
    if (!dst || !src)
        return false;
    memset(dst, 0, sizeof(*dst));
    if (src->len == 0)
        return true;
    dst->data = cetta_malloc(sizeof(*dst->data) * src->len);
    memcpy(dst->data, src->data, sizeof(*dst->data) * src->len);
    dst->len = src->len;
    dst->cap = src->len;
    return true;
}

static bool u32vec_equals(const CettaLpNativeU32Vec *lhs,
                          const CettaLpNativeU32Vec *rhs) {
    if (lhs->len != rhs->len)
        return false;
    if (lhs->len == 0)
        return true;
    return memcmp(lhs->data, rhs->data, sizeof(*lhs->data) * lhs->len) == 0;
}

static bool parsevaluevec_forest_equals(const CettaLpNativeParseValueVec *lhs,
                                        const CettaLpNativeParseValueVec *rhs) {
    uint32_t i;

    if (lhs->len != rhs->len)
        return false;
    for (i = 0; i < lhs->len; i++) {
        const CettaLpNativeParseValue *lv = &lhs->data[i];
        const CettaLpNativeParseValue *rv = &rhs->data[i];
        if (lv->is_cert != rv->is_cert ||
            lv->term_kind != rv->term_kind ||
            lv->start != rv->start ||
            lv->end != rv->end ||
            lv->forest_idx != rv->forest_idx) {
            return false;
        }
    }
    return true;
}

static void branchvec_free(CettaLpNativeBranchVec *vec) {
    uint32_t i;

    if (!vec)
        return;
    for (i = 0; i < vec->len; i++) {
        free(vec->data[i].state_stack.data);
        free(vec->data[i].value_stack.data);
    }
    free(vec->data);
    memset(vec, 0, sizeof(*vec));
}

static int32_t branchvec_find(const CettaLpNativeBranchVec *vec,
                              uint32_t pos,
                              const CettaLpNativeU32Vec *state_stack) {
    uint32_t i;

    for (i = 0; i < vec->len; i++) {
        if (vec->data[i].pos != pos)
            continue;
        if (u32vec_equals(&vec->data[i].state_stack, state_stack))
            return (int32_t)i;
    }
    return -1;
}

static int32_t branchvec_find_with_values(
    const CettaLpNativeBranchVec *vec,
    uint32_t pos,
    const CettaLpNativeU32Vec *state_stack,
    const CettaLpNativeParseValueVec *value_stack) {
    uint32_t i;

    for (i = 0; i < vec->len; i++) {
        if (vec->data[i].pos != pos)
            continue;
        if (!u32vec_equals(&vec->data[i].state_stack, state_stack))
            continue;
        if (parsevaluevec_forest_equals(&vec->data[i].value_stack, value_stack))
            return (int32_t)i;
    }
    return -1;
}

static bool branchvec_push_copy(CettaLpNativeBranchVec *vec,
                                uint32_t pos,
                                const CettaLpNativeU32Vec *state_stack,
                                const CettaLpNativeParseValueVec *value_stack,
                                uint8_t ways_total,
                                bool queued) {
    CettaLpNativeBranch *slot;

    if (!grow_storage((void **)&vec->data, &vec->len, &vec->cap,
                      sizeof(*vec->data))) {
        return false;
    }
    slot = &vec->data[vec->len];
    memset(slot, 0, sizeof(*slot));
    if (!u32vec_copy(&slot->state_stack, state_stack) ||
        !parsevaluevec_copy(&slot->value_stack, value_stack))
        return false;
    slot->pos = pos;
    slot->ways_total = ways_total;
    slot->ways_done = 0;
    slot->queued = queued;
    vec->len++;
    return true;
}

static bool parse_input_token(Atom *atom,
                              CettaLpNativeInputToken *out,
                              char *error_buf,
                              size_t error_buf_size) {
    if (!atom || !out) {
        slr_summary_set_error(error_buf, error_buf_size,
                              "bad input token args");
        return false;
    }
    if (atom->kind == ATOM_SYMBOL) {
        out->token_atom = atom;
        out->term_kind = atom->sym_id;
        return true;
    }
    if (atom_expr_head_is(atom, "Lex") &&
        atom->expr.len == 3 &&
        atom->expr.elems[1] &&
        atom->expr.elems[1]->kind == ATOM_SYMBOL) {
        out->token_atom = atom;
        out->term_kind = atom->expr.elems[1]->sym_id;
        return true;
    }
    slr_summary_set_error(error_buf, error_buf_size,
                          "token list must contain symbols or (Lex cls raw)");
    return false;
}

static bool input_tokens_from_list(Atom *token_list,
                                   CettaLpNativeInputTokenVec *out,
                                   char *error_buf,
                                   size_t error_buf_size) {
    Atom *cursor = token_list;
    uint32_t pos = 0;

    memset(out, 0, sizeof(*out));
    while (cursor) {
        if (atom_is_symbol(cursor, "Nil"))
            return true;
        if (cursor->kind == ATOM_EXPR &&
            cursor->expr.len == 3 &&
            atom_is_symbol(cursor->expr.elems[0], "Cons")) {
            CettaLpNativeInputToken token;
            if (!parse_input_token(cursor->expr.elems[1], &token,
                                   error_buf, error_buf_size) ||
                !input_tokenvec_push(out, token.token_atom, token.term_kind, pos)) {
                free(out->data);
                memset(out, 0, sizeof(*out));
                return false;
            }
            cursor = cursor->expr.elems[2];
            pos++;
            continue;
        }
        if (cursor->kind == ATOM_EXPR && cursor->expr.len == 0)
            return true;
        slr_summary_set_error(error_buf, error_buf_size,
                              "token list must be a Cons/Nil list");
        free(out->data);
        memset(out, 0, sizeof(*out));
        return false;
    }
    return true;
}

static Atom *make_cons_list(Arena *arena, Atom **items, uint32_t len) {
    Atom *out = atom_symbol(arena, "Nil");
    uint32_t i;

    for (i = len; i > 0; i--) {
        out = atom_expr3(arena, atom_symbol(arena, "Cons"), items[i - 1], out);
    }
    return out;
}

static Atom *make_eps_cert(Arena *arena) {
    Atom **elems = arena_alloc(arena, sizeof(Atom *));
    elems[0] = atom_symbol(arena, "EpsC");
    return atom_expr(arena, elems, 1);
}

static Atom *make_tok_cert(Arena *arena, SymbolId token, uint32_t pos) {
    Atom **elems = arena_alloc(arena, sizeof(Atom *) * 3);
    elems[0] = atom_symbol(arena, "TokC");
    elems[1] = atom_symbol_id(arena, token);
    elems[2] = atom_int(arena, pos);
    return atom_expr(arena, elems, 3);
}

static Atom *make_leaf_cert(Arena *arena, Atom *token_atom,
                            uint32_t left, uint32_t right) {
    Atom **elems = arena_alloc(arena, sizeof(Atom *) * 4);
    elems[0] = atom_symbol(arena, "LeafC");
    elems[1] = token_atom;
    elems[2] = atom_int(arena, left);
    elems[3] = atom_int(arena, right);
    return atom_expr(arena, elems, 4);
}

static Atom *make_node_cert(Arena *arena, SymbolId label, SymbolId lhs,
                            uint32_t left, uint32_t right,
                            Atom **kids, uint32_t kid_len) {
    Atom **elems = arena_alloc(arena, sizeof(Atom *) * 6);
    elems[0] = atom_symbol(arena, "NodeC");
    elems[1] = atom_symbol_id(arena, label);
    elems[2] = atom_symbol_id(arena, lhs);
    elems[3] = atom_int(arena, left);
    elems[4] = atom_int(arena, right);
    elems[5] = make_cons_list(arena, kids, kid_len);
    return atom_expr(arena, elems, 6);
}

static const CettaLpNativeAction *actionvec_lookup(
    const CettaLpNativeActionVec *actions,
    uint32_t state,
    uint32_t token_idx) {
    uint32_t i;

    for (i = 0; i < actions->len; i++) {
        if (actions->data[i].state == state &&
            actions->data[i].token_idx == token_idx) {
            return &actions->data[i];
        }
    }
    return NULL;
}

Atom *cetta_lp_native_slr_parse_shared(const CettaLpNativeGrammar *grammar,
                                       SymbolId start_nt,
                                       Atom *token_list,
                                       Arena *arena,
                                       char *error_buf,
                                       size_t error_buf_size) {
    CettaLpNativeSlrProduction *productions = NULL;
    uint32_t production_len = 0;
    CettaLpNativeIdVec nonterminals = {0};
    CettaLpNativeIdVec terminals = {0};
    bool *nullable = NULL;
    CettaLpNativeBitset *first = NULL;
    CettaLpNativeBitset *follow = NULL;
    CettaLpNativeStateVec states = {0};
    CettaLpNativeSymbolVec symbols = {0};
    CettaLpNativeEdgeVec edges = {0};
    CettaLpNativeActionVec actions = {0};
    uint32_t accept_len = 0;
    uint32_t conflict_len = 0;
    uint32_t i;
    CettaLpNativeInputTokenVec tokens = {0};
    CettaLpNativeU32Vec state_stack = {0};
    CettaLpNativeParseValueVec value_stack = {0};
    uint32_t pos = 0;
    Atom *result = NULL;

    if (!grammar || !arena) {
        slr_summary_set_error(error_buf, error_buf_size,
                              "bad SLR parse args");
        return NULL;
    }
    if (!input_tokens_from_list(token_list, &tokens, error_buf, error_buf_size))
        return NULL;
    if (!slr_build_productions(grammar, &productions, &production_len,
                               error_buf, error_buf_size) ||
        !slr_collect_symbol_sets(productions, production_len, start_nt,
                                 &nonterminals, &terminals,
                                 error_buf, error_buf_size)) {
        goto fail;
    }
    if (!slr_grammar_mentions_nonterminal(productions, production_len, start_nt)) {
        slr_summary_set_error(error_buf, error_buf_size,
                              "start nonterminal not present in grammar");
        goto fail;
    }
    if (!slr_compute_nullable_first_follow(productions, production_len,
                                           &nonterminals, &terminals, start_nt,
                                           &nullable, &first, &follow,
                                           error_buf, error_buf_size) ||
        !slr_build_states(productions, production_len, start_nt,
                          &states, &symbols, &edges,
                          error_buf, error_buf_size)) {
        goto fail;
    }

    for (i = 0; i < states.len; i++) {
        uint32_t item_idx;
        for (item_idx = 0; item_idx < states.data[i].len; item_idx++) {
            SymbolId lhs = 0;
            const CettaLpNativeSymbol *rhs = NULL;
            uint32_t rhs_len = 0;
            CettaLpNativeItem item = states.data[i].items[item_idx];

            slr_get_prod(productions, production_len, start_nt,
                         item.prod_idx, &lhs, &rhs, &rhs_len);
            if (rhs && item.dot < rhs_len) {
                if (rhs[item.dot].kind == CETTA_LP_NATIVE_SYMBOL_TM) {
                    int32_t sym_idx = symbolvec_find(&symbols, true, rhs[item.dot].name);
                    int32_t edge_idx;
                    int32_t term_idx = idvec_find(&terminals, rhs[item.dot].name);
                    if (sym_idx >= 0 && term_idx >= 0) {
                        edge_idx = edgevec_find(&edges, i, (uint32_t)sym_idx);
                    } else {
                        edge_idx = -1;
                    }
                    if (edge_idx >= 0 && term_idx >= 0) {
                        if (!actionvec_set(&actions, i, (uint32_t)term_idx, 's',
                                           (int32_t)edges.data[edge_idx].target,
                                           &conflict_len)) {
                            slr_summary_set_error(error_buf, error_buf_size,
                                                  "failed to record shift action");
                            goto fail;
                        }
                    }
                }
                continue;
            }
            if (item.prod_idx == -1) {
                accept_len++;
                if (!actionvec_set(&actions, i, terminals.len, 'a', 0,
                                   &conflict_len)) {
                    slr_summary_set_error(error_buf, error_buf_size,
                                          "failed to record accept action");
                    goto fail;
                }
                continue;
            }
            {
                int32_t lhs_idx = idvec_find(&nonterminals, lhs);
                uint32_t tok_idx;
                if (lhs_idx < 0) {
                    slr_summary_set_error(error_buf, error_buf_size,
                                          "reduce lhs missing from follow set");
                    goto fail;
                }
                for (tok_idx = 0; tok_idx <= terminals.len; tok_idx++) {
                    if (!bitset_test(&follow[lhs_idx], tok_idx))
                        continue;
                    if (!actionvec_set(&actions, i, tok_idx, 'r',
                                       item.prod_idx, &conflict_len)) {
                        slr_summary_set_error(error_buf, error_buf_size,
                                              "failed to record reduce action");
                        goto fail;
                    }
                }
            }
        }
    }

    if (conflict_len > 0) {
        slr_summary_set_error(error_buf, error_buf_size,
                              "SLR table has conflicts");
        goto fail;
    }

    if (!u32vec_push(&state_stack, 0)) {
        slr_summary_set_error(error_buf, error_buf_size,
                              "failed to initialize parse stack");
        goto fail;
    }

    while (1) {
        uint32_t token_idx;
        const CettaLpNativeAction *action;
        uint32_t state = state_stack.data[state_stack.len - 1];

        if (pos < tokens.len) {
            int32_t term_idx = idvec_find(&terminals, tokens.data[pos].term_kind);
            if (term_idx < 0) {
                result = atom_symbol(arena, "NoParse");
                break;
            }
            token_idx = (uint32_t)term_idx;
        } else {
            token_idx = terminals.len;
        }
        action = actionvec_lookup(&actions, state, token_idx);
        if (!action) {
            result = atom_symbol(arena, "NoParse");
            break;
        }
        if (action->kind == 's') {
            CettaLpNativeParseValue value;
            if (pos >= tokens.len) {
                slr_summary_set_error(error_buf, error_buf_size,
                                      "shift past end of token stream");
                goto fail;
            }
            value.is_cert = false;
            value.token_atom = tokens.data[pos].token_atom;
            value.term_kind = tokens.data[pos].term_kind;
            value.start = pos;
            value.end = pos + 1;
            value.forest_idx = UINT32_MAX;
            value.cert = NULL;
            if (!u32vec_push(&state_stack, (uint32_t)action->value) ||
                !parsevaluevec_push(&value_stack, &value)) {
                slr_summary_set_error(error_buf, error_buf_size,
                                      "failed to push shift result");
                goto fail;
            }
            pos++;
            continue;
        }
        if (action->kind == 'r') {
            int32_t prod_idx = action->value;
            SymbolId lhs = 0;
            const CettaLpNativeSymbol *rhs = NULL;
            uint32_t rhs_len = 0;
            CettaLpNativeParseValue next_value;
            int32_t lhs_sym_idx;
            int32_t goto_edge_idx;

            slr_get_prod(productions, production_len, start_nt,
                         prod_idx, &lhs, &rhs, &rhs_len);
            if (rhs_len > value_stack.len || rhs_len >= state_stack.len) {
                slr_summary_set_error(error_buf, error_buf_size,
                                      "reduce arity exceeds parse stack");
                goto fail;
            }

            if ((uint32_t)prod_idx >= grammar->production_len) {
                CettaLpNativeParseValue *child = &value_stack.data[value_stack.len - 1];
                if (rhs_len != 1 || child->is_cert) {
                    slr_summary_set_error(error_buf, error_buf_size,
                                          "leaf reduction expected one shifted token");
                    goto fail;
                }
                next_value.is_cert = true;
                next_value.token_atom = NULL;
                next_value.term_kind = 0;
                next_value.start = child->start;
                next_value.end = child->end;
                next_value.forest_idx = UINT32_MAX;
                next_value.cert = make_leaf_cert(arena, child->token_atom,
                                                 child->start, child->end);
            } else if (rhs_len == 0) {
                Atom *eps = make_eps_cert(arena);
                Atom *kids[1] = {eps};
                next_value.is_cert = true;
                next_value.token_atom = NULL;
                next_value.term_kind = 0;
                next_value.start = pos;
                next_value.end = pos;
                next_value.forest_idx = UINT32_MAX;
                next_value.cert = make_node_cert(
                    arena, grammar->productions[prod_idx].label, lhs,
                    pos, pos, kids, 1);
            } else {
                uint32_t base = value_stack.len - rhs_len;
                uint32_t j;
                Atom **kids = arena_alloc(arena, sizeof(Atom *) * rhs_len);
                next_value.is_cert = true;
                next_value.token_atom = NULL;
                next_value.term_kind = 0;
                next_value.start = value_stack.data[base].start;
                next_value.end = value_stack.data[value_stack.len - 1].end;
                next_value.forest_idx = UINT32_MAX;
                for (j = 0; j < rhs_len; j++) {
                    CettaLpNativeParseValue *part = &value_stack.data[base + j];
                    if (rhs[j].kind == CETTA_LP_NATIVE_SYMBOL_TM) {
                        if (part->is_cert || part->term_kind != rhs[j].name) {
                            slr_summary_set_error(error_buf, error_buf_size,
                                                  "terminal reduction mismatch");
                            goto fail;
                        }
                        kids[j] = make_tok_cert(arena, rhs[j].name, part->start);
                    } else {
                        if (!part->is_cert || !part->cert) {
                            slr_summary_set_error(error_buf, error_buf_size,
                                                  "nonterminal reduction missing child cert");
                            goto fail;
                        }
                        kids[j] = part->cert;
                    }
                }
                next_value.cert = make_node_cert(
                    arena, grammar->productions[prod_idx].label, lhs,
                    next_value.start, next_value.end, kids, rhs_len);
            }

            value_stack.len -= rhs_len;
            state_stack.len -= rhs_len;
            lhs_sym_idx = symbolvec_find(&symbols, false, lhs);
            if (lhs_sym_idx < 0 || state_stack.len == 0) {
                slr_summary_set_error(error_buf, error_buf_size,
                                      "missing goto state for reduction");
                goto fail;
            }
            goto_edge_idx = edgevec_find(&edges,
                                         state_stack.data[state_stack.len - 1],
                                         (uint32_t)lhs_sym_idx);
            if (goto_edge_idx < 0 ||
                !u32vec_push(&state_stack, edges.data[goto_edge_idx].target) ||
                !parsevaluevec_push(&value_stack, &next_value)) {
                slr_summary_set_error(error_buf, error_buf_size,
                                      "failed to push reduced value");
                goto fail;
            }
            continue;
        }
        if (action->kind == 'a') {
            if (value_stack.len != 1 || !value_stack.data[0].is_cert ||
                !value_stack.data[0].cert ||
                value_stack.data[0].start != 0 ||
                value_stack.data[0].end != tokens.len) {
                slr_summary_set_error(error_buf, error_buf_size,
                                      "accept state missing full-span cert");
                goto fail;
            }
            result = atom_expr2(arena, atom_symbol(arena, "Unique"),
                                value_stack.data[0].cert);
            break;
        }
        slr_summary_set_error(error_buf, error_buf_size,
                              "unknown SLR action kind");
        goto fail;
    }

    if (first && follow) {
        for (i = 0; i < nonterminals.len; i++) {
            bitset_free(&first[i]);
            bitset_free(&follow[i]);
        }
    }
    free(tokens.data);
    free(value_stack.data);
    free(state_stack.data);
    free(nullable);
    free(first);
    free(follow);
    free(nonterminals.data);
    free(terminals.data);
    free(symbols.data);
    free(edges.data);
    free(actions.data);
    statevec_free(&states);
    slr_productions_free(productions, production_len);
    return result;

fail:
    if (first && follow) {
        for (i = 0; i < nonterminals.len; i++) {
            bitset_free(&first[i]);
            bitset_free(&follow[i]);
        }
    }
    free(tokens.data);
    free(value_stack.data);
    free(state_stack.data);
    free(nullable);
    free(first);
    free(follow);
    free(nonterminals.data);
    free(terminals.data);
    free(symbols.data);
    free(edges.data);
    free(actions.data);
    statevec_free(&states);
    slr_productions_free(productions, production_len);
    return NULL;
}

#define CETTA_LP_NATIVE_NODE_NONE UINT32_MAX

typedef enum {
    CETTA_LP_NATIVE_GLL_NODE_TERM = 0,
    CETTA_LP_NATIVE_GLL_NODE_EPS = 1,
    CETTA_LP_NATIVE_GLL_NODE_SYM = 2,
    CETTA_LP_NATIVE_GLL_NODE_INTER = 3
} CettaLpNativeGllNodeKind;

typedef struct {
    uint32_t left_idx;
    uint32_t right_idx;
    uint32_t pivot;
    int32_t prod_idx;
} CettaLpNativeGllPackedChoice;

typedef struct {
    CettaLpNativeGllPackedChoice *data;
    uint32_t len;
    uint32_t cap;
} CettaLpNativeGllPackedVec;

typedef struct {
    CettaLpNativeGllNodeKind kind;
    SymbolId symbol;
    int32_t prod_idx;
    uint32_t dot;
    uint32_t left;
    uint32_t right;
    CettaLpNativeGllPackedVec packed;
} CettaLpNativeGllNode;

typedef struct {
    CettaLpNativeGllNode *data;
    uint32_t len;
    uint32_t cap;
} CettaLpNativeGllNodeVec;

typedef struct {
    uint32_t left_label;
    uint32_t parent_gss;
} CettaLpNativeGllGssEdge;

typedef struct {
    CettaLpNativeGllGssEdge *data;
    uint32_t len;
    uint32_t cap;
} CettaLpNativeGllGssEdgeVec;

typedef struct {
    bool is_root;
    int32_t prod_idx;
    uint32_t dot;
    uint32_t pos;
    CettaLpNativeGllGssEdgeVec edges;
    CettaLpNativeU32Vec popped;
} CettaLpNativeGllGssNode;

typedef struct {
    CettaLpNativeGllGssNode *data;
    uint32_t len;
    uint32_t cap;
} CettaLpNativeGllGssNodeVec;

typedef struct {
    int32_t prod_idx;
    uint32_t dot;
    uint32_t gss_idx;
    uint32_t left_label;
    uint32_t pos;
} CettaLpNativeGllDescriptor;

typedef struct {
    CettaLpNativeGllDescriptor *data;
    uint32_t len;
    uint32_t cap;
} CettaLpNativeGllDescriptorVec;

static bool u32vec_push_unique(CettaLpNativeU32Vec *vec, uint32_t value) {
    uint32_t i;

    for (i = 0; i < vec->len; i++) {
        if (vec->data[i] == value)
            return true;
    }
    return u32vec_push(vec, value);
}

static bool u32vec_contains(const CettaLpNativeU32Vec *vec, uint32_t value) {
    uint32_t i;

    for (i = 0; i < vec->len; i++) {
        if (vec->data[i] == value)
            return true;
    }
    return false;
}

static bool gll_packedvec_push_unique(CettaLpNativeGllPackedVec *vec,
                                      uint32_t left_idx,
                                      uint32_t right_idx,
                                      uint32_t pivot,
                                      int32_t prod_idx) {
    uint32_t i;

    for (i = 0; i < vec->len; i++) {
        CettaLpNativeGllPackedChoice *cur = &vec->data[i];
        if (cur->left_idx == left_idx &&
            cur->right_idx == right_idx &&
            cur->pivot == pivot &&
            cur->prod_idx == prod_idx) {
            return true;
        }
    }
    if (!grow_storage((void **)&vec->data, &vec->len, &vec->cap,
                      sizeof(*vec->data))) {
        return false;
    }
    vec->data[vec->len].left_idx = left_idx;
    vec->data[vec->len].right_idx = right_idx;
    vec->data[vec->len].pivot = pivot;
    vec->data[vec->len].prod_idx = prod_idx;
    vec->len++;
    return true;
}

static void gll_nodevec_free(CettaLpNativeGllNodeVec *vec) {
    uint32_t i;

    if (!vec)
        return;
    for (i = 0; i < vec->len; i++)
        free(vec->data[i].packed.data);
    free(vec->data);
    memset(vec, 0, sizeof(*vec));
}

static void gll_gssvec_free(CettaLpNativeGllGssNodeVec *vec) {
    uint32_t i;

    if (!vec)
        return;
    for (i = 0; i < vec->len; i++) {
        free(vec->data[i].edges.data);
        free(vec->data[i].popped.data);
    }
    free(vec->data);
    memset(vec, 0, sizeof(*vec));
}

static int32_t gll_node_find(const CettaLpNativeGllNodeVec *nodes,
                             CettaLpNativeGllNodeKind kind,
                             SymbolId symbol,
                             int32_t prod_idx,
                             uint32_t dot,
                             uint32_t left,
                             uint32_t right) {
    uint32_t i;

    for (i = 0; i < nodes->len; i++) {
        const CettaLpNativeGllNode *cur = &nodes->data[i];
        if (cur->kind == kind &&
            cur->symbol == symbol &&
            cur->prod_idx == prod_idx &&
            cur->dot == dot &&
            cur->left == left &&
            cur->right == right) {
            return (int32_t)i;
        }
    }
    return -1;
}

static int32_t gll_node_get(CettaLpNativeGllNodeVec *nodes,
                            CettaLpNativeGllNodeKind kind,
                            SymbolId symbol,
                            int32_t prod_idx,
                            uint32_t dot,
                            uint32_t left,
                            uint32_t right) {
    int32_t found = gll_node_find(nodes, kind, symbol, prod_idx, dot, left, right);

    if (found >= 0)
        return found;
    if (!grow_storage((void **)&nodes->data, &nodes->len, &nodes->cap,
                      sizeof(*nodes->data))) {
        return -1;
    }
    memset(&nodes->data[nodes->len], 0, sizeof(nodes->data[nodes->len]));
    nodes->data[nodes->len].kind = kind;
    nodes->data[nodes->len].symbol = symbol;
    nodes->data[nodes->len].prod_idx = prod_idx;
    nodes->data[nodes->len].dot = dot;
    nodes->data[nodes->len].left = left;
    nodes->data[nodes->len].right = right;
    nodes->len++;
    return (int32_t)(nodes->len - 1);
}

static int32_t gll_node_get_term(CettaLpNativeGllNodeVec *nodes,
                                 SymbolId symbol,
                                 uint32_t pos) {
    return gll_node_get(nodes, CETTA_LP_NATIVE_GLL_NODE_TERM,
                        symbol, -1, 0, pos, pos + 1);
}

static int32_t gll_node_get_eps(CettaLpNativeGllNodeVec *nodes,
                                uint32_t pos) {
    return gll_node_get(nodes, CETTA_LP_NATIVE_GLL_NODE_EPS,
                        0, -1, 0, pos, pos);
}

static int32_t gll_node_get_sym(CettaLpNativeGllNodeVec *nodes,
                                SymbolId symbol,
                                uint32_t left,
                                uint32_t right) {
    return gll_node_get(nodes, CETTA_LP_NATIVE_GLL_NODE_SYM,
                        symbol, -1, 0, left, right);
}

static int32_t gll_node_get_inter(CettaLpNativeGllNodeVec *nodes,
                                  int32_t prod_idx,
                                  uint32_t dot,
                                  uint32_t left,
                                  uint32_t right) {
    return gll_node_get(nodes, CETTA_LP_NATIVE_GLL_NODE_INTER,
                        0, prod_idx, dot, left, right);
}

static bool gll_nt_is_leaf(const CettaLpNativeGrammar *grammar,
                           SymbolId nt) {
    uint32_t i;

    for (i = 0; i < grammar->lex_len; i++) {
        if (grammar->lexes[i].nt == nt)
            return true;
    }
    for (i = 0; i < grammar->var_len; i++) {
        if (grammar->vars[i].nt == nt)
            return true;
    }
    return false;
}

static SymbolId gll_prod_label(const CettaLpNativeGrammar *grammar,
                               int32_t prod_idx) {
    if (prod_idx < 0 || (uint32_t)prod_idx >= grammar->production_len)
        return 0;
    return grammar->productions[prod_idx].label;
}

static const CettaLpNativeGllPackedChoice *gll_pick_choice(
    const CettaLpNativeGllPackedVec *choices) {
    const CettaLpNativeGllPackedChoice *best;
    uint32_t i;

    if (!choices || choices->len == 0)
        return NULL;
    best = &choices->data[0];
    for (i = 1; i < choices->len; i++) {
        const CettaLpNativeGllPackedChoice *cur = &choices->data[i];
        if (cur->left_idx < best->left_idx ||
            (cur->left_idx == best->left_idx &&
             (cur->right_idx < best->right_idx ||
              (cur->right_idx == best->right_idx &&
               (cur->pivot < best->pivot ||
                (cur->pivot == best->pivot &&
                 cur->prod_idx < best->prod_idx)))))) {
            best = cur;
        }
    }
    return best;
}

static int32_t gll_sppf_get_node(CettaLpNativeGllNodeVec *nodes,
                                 CettaLpNativeSlrProduction *productions,
                                 uint32_t production_len,
                                 SymbolId start_nt,
                                 int32_t prod_idx,
                                 uint32_t dot,
                                 uint32_t left_idx,
                                 uint32_t right_idx,
                                 char *error_buf,
                                 size_t error_buf_size) {
    SymbolId lhs = 0;
    const CettaLpNativeSymbol *rhs = NULL;
    uint32_t rhs_len = 0;
    uint32_t left;
    uint32_t right;
    uint32_t pivot;
    bool is_end;
    int32_t node_idx;

    if (right_idx == CETTA_LP_NATIVE_NODE_NONE) {
        slr_summary_set_error(error_buf, error_buf_size,
                              "GLL SPPF node missing right child");
        return -1;
    }
    slr_get_prod(productions, production_len, start_nt,
                 prod_idx, &lhs, &rhs, &rhs_len);
    left = (left_idx == CETTA_LP_NATIVE_NODE_NONE)
        ? nodes->data[right_idx].left
        : nodes->data[left_idx].left;
    right = nodes->data[right_idx].right;
    pivot = nodes->data[right_idx].left;
    is_end = dot == rhs_len;
    if (is_end) {
        node_idx = gll_node_get_sym(nodes, lhs, left, right);
        if (node_idx < 0 ||
            !gll_packedvec_push_unique(&nodes->data[node_idx].packed,
                                       left_idx, right_idx, pivot, prod_idx)) {
            slr_summary_set_error(error_buf, error_buf_size,
                                  "failed to record GLL SPPF symbol node");
            return -1;
        }
        return node_idx;
    }
    node_idx = gll_node_get_inter(nodes, prod_idx, dot, left, right);
    if (node_idx < 0 ||
        !gll_packedvec_push_unique(&nodes->data[node_idx].packed,
                                   left_idx, right_idx, pivot, -1)) {
        slr_summary_set_error(error_buf, error_buf_size,
                              "failed to record GLL SPPF intermediate node");
        return -1;
    }
    return node_idx;
}

static int32_t gll_gss_find(const CettaLpNativeGllGssNodeVec *nodes,
                            bool is_root,
                            int32_t prod_idx,
                            uint32_t dot,
                            uint32_t pos) {
    uint32_t i;

    for (i = 0; i < nodes->len; i++) {
        const CettaLpNativeGllGssNode *cur = &nodes->data[i];
        if (cur->is_root == is_root &&
            cur->prod_idx == prod_idx &&
            cur->dot == dot &&
            cur->pos == pos) {
            return (int32_t)i;
        }
    }
    return -1;
}

static int32_t gll_gss_get(CettaLpNativeGllGssNodeVec *nodes,
                           bool is_root,
                           int32_t prod_idx,
                           uint32_t dot,
                           uint32_t pos) {
    int32_t found = gll_gss_find(nodes, is_root, prod_idx, dot, pos);

    if (found >= 0)
        return found;
    if (!grow_storage((void **)&nodes->data, &nodes->len, &nodes->cap,
                      sizeof(*nodes->data))) {
        return -1;
    }
    memset(&nodes->data[nodes->len], 0, sizeof(nodes->data[nodes->len]));
    nodes->data[nodes->len].is_root = is_root;
    nodes->data[nodes->len].prod_idx = prod_idx;
    nodes->data[nodes->len].dot = dot;
    nodes->data[nodes->len].pos = pos;
    nodes->len++;
    return (int32_t)(nodes->len - 1);
}

static bool gll_gss_add_edge(CettaLpNativeGllGssNodeVec *nodes,
                             uint32_t gss_idx,
                             uint32_t left_label,
                             uint32_t parent_gss) {
    CettaLpNativeGllGssEdgeVec *edges;
    uint32_t i;

    edges = &nodes->data[gss_idx].edges;
    for (i = 0; i < edges->len; i++) {
        if (edges->data[i].left_label == left_label &&
            edges->data[i].parent_gss == parent_gss) {
            return true;
        }
    }
    if (!grow_storage((void **)&edges->data, &edges->len, &edges->cap,
                      sizeof(*edges->data))) {
        return false;
    }
    edges->data[edges->len].left_label = left_label;
    edges->data[edges->len].parent_gss = parent_gss;
    edges->len++;
    return true;
}

static bool gll_desc_equals(const CettaLpNativeGllDescriptor *lhs,
                            const CettaLpNativeGllDescriptor *rhs) {
    return lhs->prod_idx == rhs->prod_idx &&
        lhs->dot == rhs->dot &&
        lhs->gss_idx == rhs->gss_idx &&
        lhs->left_label == rhs->left_label &&
        lhs->pos == rhs->pos;
}

static bool gll_desc_enqueue(CettaLpNativeGllDescriptorVec *seen,
                             CettaLpNativeGllDescriptorVec *work,
                             int32_t prod_idx,
                             uint32_t dot,
                             uint32_t gss_idx,
                             uint32_t left_label,
                             uint32_t pos) {
    CettaLpNativeGllDescriptor desc;
    uint32_t i;

    desc.prod_idx = prod_idx;
    desc.dot = dot;
    desc.gss_idx = gss_idx;
    desc.left_label = left_label;
    desc.pos = pos;
    for (i = 0; i < seen->len; i++) {
        if (gll_desc_equals(&seen->data[i], &desc))
            return true;
    }
    if (!grow_storage((void **)&seen->data, &seen->len, &seen->cap,
                      sizeof(*seen->data)) ||
        !grow_storage((void **)&work->data, &work->len, &work->cap,
                      sizeof(*work->data))) {
        return false;
    }
    seen->data[seen->len++] = desc;
    work->data[work->len++] = desc;
    return true;
}

static int32_t gll_count_node(const CettaLpNativeGllNodeVec *nodes,
                              uint32_t node_idx,
                              uint8_t *memo_seen,
                              uint8_t *memo_value) {
    const CettaLpNativeGllNode *node;
    int32_t total = 0;
    uint32_t i;

    node = &nodes->data[node_idx];
    if (node->kind == CETTA_LP_NATIVE_GLL_NODE_TERM ||
        node->kind == CETTA_LP_NATIVE_GLL_NODE_EPS) {
        return 1;
    }
    if (memo_seen[node_idx])
        return memo_value[node_idx];
    memo_seen[node_idx] = 1;
    memo_value[node_idx] = 0;
    for (i = 0; i < node->packed.len; i++) {
        const CettaLpNativeGllPackedChoice *choice = &node->packed.data[i];
        int32_t left_count = 1;
        int32_t right_count;
        if (choice->left_idx != CETTA_LP_NATIVE_NODE_NONE) {
            left_count = gll_count_node(nodes, choice->left_idx,
                                        memo_seen, memo_value);
        }
        right_count = gll_count_node(nodes, choice->right_idx,
                                     memo_seen, memo_value);
        total += left_count * right_count;
        if (total >= 2) {
            total = 2;
            break;
        }
    }
    memo_value[node_idx] = (uint8_t)total;
    return total;
}

static bool gll_collect_spine(const CettaLpNativeGllNodeVec *nodes,
                              uint32_t left_idx,
                              uint32_t right_idx,
                              CettaLpNativeU32Vec *out) {
    const CettaLpNativeGllPackedChoice *choice;

    if (left_idx != CETTA_LP_NATIVE_NODE_NONE) {
        const CettaLpNativeGllNode *left = &nodes->data[left_idx];
        if (left->kind == CETTA_LP_NATIVE_GLL_NODE_INTER) {
            choice = gll_pick_choice(&left->packed);
            if (!choice ||
                !gll_collect_spine(nodes, choice->left_idx, choice->right_idx, out)) {
                return false;
            }
        } else if (!u32vec_push(out, left_idx)) {
            return false;
        }
    }
    return u32vec_push(out, right_idx);
}

static Atom *gll_cert_from_node(const CettaLpNativeGrammar *grammar,
                                CettaLpNativeSlrProduction *productions,
                                uint32_t production_len,
                                SymbolId start_nt,
                                const CettaLpNativeInputTokenVec *tokens,
                                const CettaLpNativeGllNodeVec *nodes,
                                uint32_t node_idx,
                                Arena *arena,
                                char *error_buf,
                                size_t error_buf_size) {
    const CettaLpNativeGllNode *node = &nodes->data[node_idx];
    const CettaLpNativeGllPackedChoice *choice;
    CettaLpNativeU32Vec kids = {0};
    Atom *result = NULL;
    uint32_t i;

    if (node->kind == CETTA_LP_NATIVE_GLL_NODE_TERM) {
        return make_tok_cert(arena, node->symbol, node->left);
    }
    if (node->kind == CETTA_LP_NATIVE_GLL_NODE_EPS) {
        return make_eps_cert(arena);
    }

    choice = gll_pick_choice(&node->packed);
    if (!choice) {
        slr_summary_set_error(error_buf, error_buf_size,
                              "GLL cert extraction found node without packed choice");
        return NULL;
    }
    if (!gll_collect_spine(nodes, choice->left_idx, choice->right_idx, &kids)) {
        slr_summary_set_error(error_buf, error_buf_size,
                              "failed to flatten GLL cert spine");
        goto fail;
    }

    if (node->kind == CETTA_LP_NATIVE_GLL_NODE_SYM) {
        SymbolId lhs = 0;
        const CettaLpNativeSymbol *rhs = NULL;
        uint32_t rhs_len = 0;
        slr_get_prod(productions, production_len, start_nt,
                     choice->prod_idx, &lhs, &rhs, &rhs_len);
        if (gll_nt_is_leaf(grammar, lhs) &&
            rhs_len == 1 &&
            rhs[0].kind == CETTA_LP_NATIVE_SYMBOL_TM) {
            uint32_t child_idx;
            const CettaLpNativeGllNode *child;
            if (kids.len != 1) {
                slr_summary_set_error(error_buf, error_buf_size,
                                      "leaf production did not flatten to exactly one terminal");
                goto fail;
            }
            child_idx = kids.data[0];
            child = &nodes->data[child_idx];
            if (child->kind != CETTA_LP_NATIVE_GLL_NODE_TERM ||
                child->left >= tokens->len) {
                slr_summary_set_error(error_buf, error_buf_size,
                                      "leaf production child was not a terminal token");
                goto fail;
            }
            result = make_leaf_cert(arena, tokens->data[child->left].token_atom,
                                    node->left, node->right);
            goto fail;
        }
        if (rhs_len == 0) {
            Atom *eps = make_eps_cert(arena);
            Atom **items = arena_alloc(arena, sizeof(Atom *));
            items[0] = eps;
            result = make_node_cert(arena, gll_prod_label(grammar, choice->prod_idx), lhs,
                                    node->left, node->right, items, 1);
            goto fail;
        }
        {
            Atom **items = arena_alloc(arena, sizeof(Atom *) * kids.len);
            for (i = 0; i < kids.len; i++) {
                items[i] = gll_cert_from_node(grammar, productions, production_len,
                                              start_nt, tokens, nodes, kids.data[i],
                                              arena, error_buf, error_buf_size);
                if (!items[i])
                    goto fail;
            }
            result = make_node_cert(arena, gll_prod_label(grammar, choice->prod_idx), lhs,
                                    node->left, node->right, items, kids.len);
            goto fail;
        }
    }

    slr_summary_set_error(error_buf, error_buf_size,
                          "GLL cert extraction expected symbol node");

fail:
    free(kids.data);
    return result;
}

Atom *cetta_lp_native_gll_parse_shared(const CettaLpNativeGrammar *grammar,
                                       SymbolId start_nt,
                                       Atom *token_list,
                                       Arena *arena,
                                       char *error_buf,
                                       size_t error_buf_size) {
    CettaLpNativeSlrProduction *productions = NULL;
    uint32_t production_len = 0;
    CettaLpNativeInputTokenVec tokens = {0};
    CettaLpNativeGllNodeVec nodes = {0};
    CettaLpNativeGllGssNodeVec gss_nodes = {0};
    CettaLpNativeGllDescriptorVec seen = {0};
    CettaLpNativeGllDescriptorVec work = {0};
    uint32_t root_idx = CETTA_LP_NATIVE_NODE_NONE;
    Atom *result = NULL;

    if (!grammar || !arena) {
        slr_summary_set_error(error_buf, error_buf_size,
                              "bad GLL shared-parse args");
        return NULL;
    }
    if (!input_tokens_from_list(token_list, &tokens, error_buf, error_buf_size))
        return NULL;
    if (!slr_build_productions(grammar, &productions, &production_len,
                               error_buf, error_buf_size) ||
        !slr_grammar_mentions_nonterminal(productions, production_len, start_nt)) {
        goto fail;
    }
    if (gll_gss_get(&gss_nodes, true, -1, 0, 0) != 0) {
        slr_summary_set_error(error_buf, error_buf_size,
                              "failed to initialize GLL root");
        goto fail;
    }
    {
        uint32_t prod_idx;
        for (prod_idx = 0; prod_idx < production_len; prod_idx++) {
            SymbolId lhs = 0;
            const CettaLpNativeSymbol *rhs = NULL;
            uint32_t rhs_len = 0;
            slr_get_prod(productions, production_len, start_nt,
                         (int32_t)prod_idx, &lhs, &rhs, &rhs_len);
            (void)rhs;
            (void)rhs_len;
            if (lhs != start_nt)
                continue;
            if (!gll_desc_enqueue(&seen, &work, (int32_t)prod_idx, 0, 0,
                                  CETTA_LP_NATIVE_NODE_NONE, 0)) {
                slr_summary_set_error(error_buf, error_buf_size,
                                      "failed to seed GLL descriptors");
                goto fail;
            }
        }
    }

    while (work.len > 0) {
        CettaLpNativeGllDescriptor cur = work.data[work.len - 1];
        SymbolId lhs = 0;
        const CettaLpNativeSymbol *rhs = NULL;
        uint32_t rhs_len = 0;

        work.len--;
        slr_get_prod(productions, production_len, start_nt,
                     cur.prod_idx, &lhs, &rhs, &rhs_len);
        if (cur.dot < rhs_len) {
            CettaLpNativeSymbol sym = rhs[cur.dot];
            if (sym.kind == CETTA_LP_NATIVE_SYMBOL_TM) {
                int32_t term_idx;
                int32_t parent_idx;
                if (cur.pos >= tokens.len || tokens.data[cur.pos].term_kind != sym.name)
                    continue;
                term_idx = gll_node_get_term(&nodes, sym.name, cur.pos);
                if (term_idx < 0) {
                    slr_summary_set_error(error_buf, error_buf_size,
                                          "failed to allocate GLL terminal node");
                    goto fail;
                }
                parent_idx = gll_sppf_get_node(&nodes, productions, production_len,
                                               start_nt, cur.prod_idx, cur.dot + 1,
                                               cur.left_label, (uint32_t)term_idx,
                                               error_buf, error_buf_size);
                if (parent_idx < 0 ||
                    !gll_desc_enqueue(&seen, &work, cur.prod_idx, cur.dot + 1,
                                      cur.gss_idx, (uint32_t)parent_idx, cur.pos + 1)) {
                    slr_summary_set_error(error_buf, error_buf_size,
                                          "failed to advance GLL terminal descriptor");
                    goto fail;
                }
                continue;
            }
            {
                int32_t next_gss = gll_gss_get(&gss_nodes, false,
                                               cur.prod_idx, cur.dot + 1, cur.pos);
                uint32_t prod_idx;
                uint32_t popped_idx;
                if (next_gss < 0 ||
                    !gll_gss_add_edge(&gss_nodes, (uint32_t)next_gss,
                                      cur.left_label, cur.gss_idx)) {
                    slr_summary_set_error(error_buf, error_buf_size,
                                          "failed to create GLL continuation node");
                    goto fail;
                }
                for (popped_idx = 0; popped_idx < gss_nodes.data[next_gss].popped.len; popped_idx++) {
                    uint32_t done = gss_nodes.data[next_gss].popped.data[popped_idx];
                    int32_t parent_idx = gll_sppf_get_node(&nodes, productions, production_len,
                                                           start_nt, cur.prod_idx, cur.dot + 1,
                                                           cur.left_label, done,
                                                           error_buf, error_buf_size);
                    if (parent_idx < 0 ||
                        !gll_desc_enqueue(&seen, &work, cur.prod_idx, cur.dot + 1,
                                          cur.gss_idx, (uint32_t)parent_idx,
                                          nodes.data[done].right)) {
                        slr_summary_set_error(error_buf, error_buf_size,
                                              "failed to resume GLL continuation");
                        goto fail;
                    }
                }
                for (prod_idx = 0; prod_idx < production_len; prod_idx++) {
                    SymbolId child_lhs = 0;
                    const CettaLpNativeSymbol *child_rhs = NULL;
                    uint32_t child_rhs_len = 0;
                    slr_get_prod(productions, production_len, start_nt,
                                 (int32_t)prod_idx, &child_lhs, &child_rhs, &child_rhs_len);
                    (void)child_rhs;
                    (void)child_rhs_len;
                    if (child_lhs != sym.name)
                        continue;
                    if (!gll_desc_enqueue(&seen, &work, (int32_t)prod_idx, 0,
                                          (uint32_t)next_gss,
                                          CETTA_LP_NATIVE_NODE_NONE, cur.pos)) {
                        slr_summary_set_error(error_buf, error_buf_size,
                                              "failed to seed GLL child descriptor");
                        goto fail;
                    }
                }
                continue;
            }
        }
        {
            uint32_t done_idx;
            uint32_t edge_idx;
            if (rhs_len == 0) {
                int32_t sym_idx = gll_node_get_sym(&nodes, lhs, cur.pos, cur.pos);
                int32_t eps_idx = gll_node_get_eps(&nodes, cur.pos);
                if (sym_idx < 0 || eps_idx < 0 ||
                    !gll_packedvec_push_unique(&nodes.data[sym_idx].packed,
                                               CETTA_LP_NATIVE_NODE_NONE,
                                               (uint32_t)eps_idx,
                                               cur.pos,
                                               cur.prod_idx)) {
                    slr_summary_set_error(error_buf, error_buf_size,
                                          "failed to record GLL epsilon completion");
                    goto fail;
                }
                done_idx = (uint32_t)sym_idx;
            } else {
                done_idx = cur.left_label;
            }
            if (cur.gss_idx == 0) {
                if (!u32vec_push_unique(&gss_nodes.data[0].popped, done_idx)) {
                    slr_summary_set_error(error_buf, error_buf_size,
                                          "failed to record GLL root completion");
                    goto fail;
                }
                continue;
            }
            if (!u32vec_push_unique(&gss_nodes.data[cur.gss_idx].popped, done_idx)) {
                slr_summary_set_error(error_buf, error_buf_size,
                                      "failed to record GLL popped completion");
                goto fail;
            }
            for (edge_idx = 0; edge_idx < gss_nodes.data[cur.gss_idx].edges.len; edge_idx++) {
                CettaLpNativeGllGssEdge edge = gss_nodes.data[cur.gss_idx].edges.data[edge_idx];
                const CettaLpNativeGllGssNode *gss = &gss_nodes.data[cur.gss_idx];
                int32_t parent_idx = gll_sppf_get_node(&nodes, productions, production_len,
                                                       start_nt, gss->prod_idx, gss->dot,
                                                       edge.left_label, done_idx,
                                                       error_buf, error_buf_size);
                if (parent_idx < 0 ||
                    !gll_desc_enqueue(&seen, &work, gss->prod_idx, gss->dot,
                                      edge.parent_gss, (uint32_t)parent_idx,
                                      nodes.data[done_idx].right)) {
                    slr_summary_set_error(error_buf, error_buf_size,
                                          "failed to propagate GLL completion");
                    goto fail;
                }
            }
        }
    }

    {
        uint8_t *memo_seen = NULL;
        uint8_t *memo_value = NULL;
        int32_t count;
        root_idx = gll_node_find(&nodes, CETTA_LP_NATIVE_GLL_NODE_SYM,
                                 start_nt, -1, 0, 0, tokens.len);
        if (root_idx < 0) {
            result = atom_symbol(arena, "NoParse");
            goto cleanup;
        }
        if (!u32vec_contains(&gss_nodes.data[0].popped, (uint32_t)root_idx)) {
            result = atom_symbol(arena, "NoParse");
            goto cleanup;
        }
        memo_seen = cetta_malloc(sizeof(*memo_seen) * nodes.len);
        memo_value = cetta_malloc(sizeof(*memo_value) * nodes.len);
        memset(memo_seen, 0, sizeof(*memo_seen) * nodes.len);
        memset(memo_value, 0, sizeof(*memo_value) * nodes.len);
        count = gll_count_node(&nodes, (uint32_t)root_idx, memo_seen, memo_value);
        free(memo_seen);
        free(memo_value);
        if (count <= 0) {
            result = atom_symbol(arena, "NoParse");
            goto cleanup;
        }
        if (count > 1) {
            result = atom_symbol(arena, "Ambiguous");
            goto cleanup;
        }
        {
            Atom *cert = gll_cert_from_node(grammar, productions, production_len,
                                            start_nt, &tokens, &nodes, (uint32_t)root_idx,
                                            arena, error_buf, error_buf_size);
            if (!cert) {
                slr_summary_set_error(error_buf, error_buf_size,
                                      error_buf[0] ? error_buf : "failed to extract GLL cert");
                goto fail;
            }
            result = atom_expr2(arena, atom_symbol(arena, "Unique"), cert);
        }
    }

cleanup:
    free(tokens.data);
    gll_nodevec_free(&nodes);
    gll_gssvec_free(&gss_nodes);
    free(seen.data);
    free(work.data);
    slr_productions_free(productions, production_len);
    return result;

fail:
    free(tokens.data);
    gll_nodevec_free(&nodes);
    gll_gssvec_free(&gss_nodes);
    free(seen.data);
    free(work.data);
    slr_productions_free(productions, production_len);
    return NULL;
}

static uint32_t gll_total_packed_choices(const CettaLpNativeGllNodeVec *nodes) {
    uint32_t total = 0;
    uint32_t i;

    for (i = 0; i < nodes->len; i++)
        total += nodes->data[i].packed.len;
    return total;
}

static uint32_t gll_kind_count(const CettaLpNativeGllNodeVec *nodes,
                               CettaLpNativeGllNodeKind kind) {
    uint32_t total = 0;
    uint32_t i;

    for (i = 0; i < nodes->len; i++) {
        if (nodes->data[i].kind == kind)
            total++;
    }
    return total;
}

static Atom *gll_forest_summary_atom(Arena *arena,
                                     const char *status,
                                     uint32_t token_len,
                                     const CettaLpNativeGllNodeVec *nodes,
                                     const CettaLpNativeGllGssNodeVec *gss_nodes,
                                     uint32_t descriptor_len,
                                     uint32_t root_count) {
    Atom **elems = arena_alloc(arena, sizeof(Atom *) * 11);

    elems[0] = atom_symbol(arena, "GParseGLLForestSummary");
    elems[1] = atom_symbol(arena, status);
    elems[2] = atom_int(arena, token_len);
    elems[3] = atom_int(arena, nodes->len);
    elems[4] = atom_int(arena, gll_total_packed_choices(nodes));
    elems[5] = atom_int(arena, root_count);
    elems[6] = atom_int(arena,
                        gll_kind_count(nodes, CETTA_LP_NATIVE_GLL_NODE_TERM));
    elems[7] = atom_int(arena,
                        gll_kind_count(nodes, CETTA_LP_NATIVE_GLL_NODE_EPS));
    elems[8] = atom_int(arena,
                        gll_kind_count(nodes, CETTA_LP_NATIVE_GLL_NODE_SYM));
    elems[9] = atom_int(arena,
                        gll_kind_count(nodes, CETTA_LP_NATIVE_GLL_NODE_INTER));
    elems[10] = atom_int(arena, gss_nodes->len + descriptor_len);
    return atom_expr(arena, elems, 11);
}

static Atom *glr_forest_summary_atom(Arena *arena,
                                     const char *status,
                                     uint32_t token_len,
                                     const CettaLpNativeGllNodeVec *nodes,
                                     uint32_t branch_len,
                                     uint32_t root_count) {
    Atom **elems = arena_alloc(arena, sizeof(Atom *) * 11);

    elems[0] = atom_symbol(arena, "GParseGLRForestSummary");
    elems[1] = atom_symbol(arena, status);
    elems[2] = atom_int(arena, token_len);
    elems[3] = atom_int(arena, nodes->len);
    elems[4] = atom_int(arena, gll_total_packed_choices(nodes));
    elems[5] = atom_int(arena, root_count);
    elems[6] = atom_int(arena,
                        gll_kind_count(nodes, CETTA_LP_NATIVE_GLL_NODE_TERM));
    elems[7] = atom_int(arena,
                        gll_kind_count(nodes, CETTA_LP_NATIVE_GLL_NODE_EPS));
    elems[8] = atom_int(arena,
                        gll_kind_count(nodes, CETTA_LP_NATIVE_GLL_NODE_SYM));
    elems[9] = atom_int(arena,
                        gll_kind_count(nodes, CETTA_LP_NATIVE_GLL_NODE_INTER));
    elems[10] = atom_int(arena, branch_len);
    return atom_expr(arena, elems, 11);
}

static int forest_span_cmp(const void *lhs, const void *rhs) {
    const CettaLpNativeForestSpan *a = lhs;
    const CettaLpNativeForestSpan *b = rhs;

    if (a->kind != b->kind)
        return a->kind < b->kind ? -1 : 1;
    if (a->symbol != b->symbol)
        return a->symbol < b->symbol ? -1 : 1;
    if (a->left != b->left)
        return a->left < b->left ? -1 : 1;
    if (a->right != b->right)
        return a->right < b->right ? -1 : 1;
    return 0;
}

static bool forest_spanvec_push_unique(CettaLpNativeForestSpanVec *spans,
                                       uint8_t kind,
                                       SymbolId symbol,
                                       uint32_t left,
                                       uint32_t right) {
    uint32_t i;

    for (i = 0; i < spans->len; i++) {
        CettaLpNativeForestSpan *cur = &spans->data[i];
        if (cur->kind == kind &&
            cur->symbol == symbol &&
            cur->left == left &&
            cur->right == right) {
            return true;
        }
    }
    if (!grow_storage((void **)&spans->data, &spans->len, &spans->cap,
                      sizeof(*spans->data))) {
        return false;
    }
    spans->data[spans->len].kind = kind;
    spans->data[spans->len].symbol = symbol;
    spans->data[spans->len].left = left;
    spans->data[spans->len].right = right;
    spans->len++;
    return true;
}

static void gll_mark_reachable(const CettaLpNativeGllNodeVec *nodes,
                               uint32_t idx,
                               uint8_t *seen) {
    const CettaLpNativeGllNode *node;
    uint32_t i;

    if (!nodes || idx == CETTA_LP_NATIVE_NODE_NONE || idx >= nodes->len)
        return;
    if (seen[idx])
        return;
    seen[idx] = 1;
    node = &nodes->data[idx];
    if (node->kind != CETTA_LP_NATIVE_GLL_NODE_SYM &&
        node->kind != CETTA_LP_NATIVE_GLL_NODE_INTER) {
        return;
    }
    for (i = 0; i < node->packed.len; i++) {
        const CettaLpNativeGllPackedChoice *choice = &node->packed.data[i];
        if (choice->left_idx != CETTA_LP_NATIVE_NODE_NONE)
            gll_mark_reachable(nodes, choice->left_idx, seen);
        gll_mark_reachable(nodes, choice->right_idx, seen);
    }
}

static bool gll_collect_reachable_spans(const CettaLpNativeGllNodeVec *nodes,
                                        int32_t root_idx,
                                        CettaLpNativeForestSpanVec *spans) {
    uint8_t *seen;
    uint32_t i;
    bool ok = true;

    if (!nodes || root_idx < 0 || (uint32_t)root_idx >= nodes->len)
        return true;
    seen = cetta_malloc(sizeof(*seen) * nodes->len);
    memset(seen, 0, sizeof(*seen) * nodes->len);
    gll_mark_reachable(nodes, (uint32_t)root_idx, seen);
    for (i = 0; i < nodes->len; i++) {
        const CettaLpNativeGllNode *node;
        uint8_t kind;
        SymbolId symbol = 0;

        if (!seen[i])
            continue;
        node = &nodes->data[i];
        if (node->kind == CETTA_LP_NATIVE_GLL_NODE_TERM) {
            kind = 0;
            symbol = node->symbol;
        } else if (node->kind == CETTA_LP_NATIVE_GLL_NODE_EPS) {
            kind = 1;
        } else if (node->kind == CETTA_LP_NATIVE_GLL_NODE_SYM) {
            kind = 2;
            symbol = node->symbol;
        } else {
            continue;
        }
        if (!forest_spanvec_push_unique(spans, kind, symbol,
                                        node->left, node->right)) {
            ok = false;
            break;
        }
    }
    free(seen);
    if (ok && spans->len > 1) {
        qsort(spans->data, spans->len, sizeof(*spans->data),
              forest_span_cmp);
    }
    return ok;
}

static Atom *forest_span_atom(Arena *arena,
                              const CettaLpNativeForestSpan *span) {
    Atom **elems;

    if (span->kind == 0) {
        elems = arena_alloc(arena, sizeof(Atom *) * 4);
        elems[0] = atom_symbol(arena, "GSpanTerm");
        elems[1] = atom_symbol_id(arena, span->symbol);
        elems[2] = atom_int(arena, span->left);
        elems[3] = atom_int(arena, span->right);
        return atom_expr(arena, elems, 4);
    }
    if (span->kind == 1) {
        elems = arena_alloc(arena, sizeof(Atom *) * 3);
        elems[0] = atom_symbol(arena, "GSpanEps");
        elems[1] = atom_int(arena, span->left);
        elems[2] = atom_int(arena, span->right);
        return atom_expr(arena, elems, 3);
    }

    elems = arena_alloc(arena, sizeof(Atom *) * 4);
    elems[0] = atom_symbol(arena, "GSpanSym");
    elems[1] = atom_symbol_id(arena, span->symbol);
    elems[2] = atom_int(arena, span->left);
    elems[3] = atom_int(arena, span->right);
    return atom_expr(arena, elems, 4);
}

static Atom *forest_span_list_atom(Arena *arena,
                                   const CettaLpNativeForestSpanVec *spans) {
    Atom **items = NULL;
    uint32_t i;

    if (spans->len > 0)
        items = arena_alloc(arena, sizeof(Atom *) * spans->len);
    for (i = 0; i < spans->len; i++)
        items[i] = forest_span_atom(arena, &spans->data[i]);
    return make_cons_list(arena, items, spans->len);
}

static Atom *gll_forest_signature_atom(Arena *arena,
                                       const char *status,
                                       uint32_t token_len,
                                       uint32_t root_count,
                                       int32_t root_idx,
                                       const CettaLpNativeGllNodeVec *nodes) {
    CettaLpNativeForestSpanVec spans = {0};
    Atom **elems = arena_alloc(arena, sizeof(Atom *) * 5);

    if (!gll_collect_reachable_spans(nodes, root_idx, &spans)) {
        free(spans.data);
        return NULL;
    }
    elems[0] = atom_symbol(arena, "GParseForestSignature");
    elems[1] = atom_symbol(arena, status);
    elems[2] = atom_int(arena, token_len);
    elems[3] = atom_int(arena, root_count);
    elems[4] = forest_span_list_atom(arena, &spans);
    free(spans.data);
    return atom_expr(arena, elems, 5);
}

static uint64_t forest_digest_mix_byte(uint64_t hash, uint8_t byte) {
    hash ^= (uint64_t)byte;
    hash *= UINT64_C(1099511628211);
    return hash;
}

static uint64_t forest_digest_mix_u64(uint64_t hash, uint64_t value) {
    uint32_t i;

    for (i = 0; i < 8; i++)
        hash = forest_digest_mix_byte(hash, (uint8_t)((value >> (i * 8)) & 0xffu));
    return hash;
}

static uint64_t forest_digest_mix_text(uint64_t hash, const char *text) {
    const unsigned char *p = (const unsigned char *)text;

    while (p && *p)
        hash = forest_digest_mix_byte(hash, *p++);
    return forest_digest_mix_byte(hash, 0);
}

static uint64_t forest_spans_digest(const char *status,
                                    uint32_t token_len,
                                    uint32_t root_count,
                                    const CettaLpNativeForestSpanVec *spans) {
    uint64_t hash = UINT64_C(1469598103934665603);
    uint32_t i;

    hash = forest_digest_mix_text(hash, status);
    hash = forest_digest_mix_u64(hash, token_len);
    hash = forest_digest_mix_u64(hash, root_count);
    hash = forest_digest_mix_u64(hash, spans->len);
    for (i = 0; i < spans->len; i++) {
        const CettaLpNativeForestSpan *span = &spans->data[i];
        hash = forest_digest_mix_u64(hash, span->kind);
        if (span->kind != 1) {
            hash = forest_digest_mix_u64(hash, symbol_hash_value(g_symbols, span->symbol));
            hash = forest_digest_mix_u64(hash, symbol_len(g_symbols, span->symbol));
        }
        hash = forest_digest_mix_u64(hash, span->left);
        hash = forest_digest_mix_u64(hash, span->right);
    }
    return hash;
}

static Atom *gll_forest_signature_digest_atom(Arena *arena,
                                              const char *status,
                                              uint32_t token_len,
                                              uint32_t root_count,
                                              int32_t root_idx,
                                              const CettaLpNativeGllNodeVec *nodes) {
    CettaLpNativeForestSpanVec spans = {0};
    Atom **elems = arena_alloc(arena, sizeof(Atom *) * 6);
    char digest_text[17];
    uint64_t digest;

    if (!gll_collect_reachable_spans(nodes, root_idx, &spans)) {
        free(spans.data);
        return NULL;
    }
    digest = forest_spans_digest(status, token_len, root_count, &spans);
    snprintf(digest_text, sizeof(digest_text), "%016llx",
             (unsigned long long)digest);
    elems[0] = atom_symbol(arena, "GParseForestSignatureDigest");
    elems[1] = atom_symbol(arena, status);
    elems[2] = atom_int(arena, token_len);
    elems[3] = atom_int(arena, root_count);
    elems[4] = atom_int(arena, spans.len);
    elems[5] = atom_string(arena, digest_text);
    free(spans.data);
    return atom_expr(arena, elems, 6);
}

static Atom *gll_idx_atom(Arena *arena, uint32_t idx) {
    if (idx == CETTA_LP_NATIVE_NODE_NONE)
        return atom_symbol(arena, "GNone");
    return atom_int(arena, idx);
}

static Atom *gll_choice_atom(Arena *arena,
                             const CettaLpNativeGllPackedChoice *choice) {
    Atom **elems = arena_alloc(arena, sizeof(Atom *) * 5);

    elems[0] = atom_symbol(arena, "GChoice");
    elems[1] = gll_idx_atom(arena, choice->left_idx);
    elems[2] = gll_idx_atom(arena, choice->right_idx);
    elems[3] = atom_int(arena, choice->pivot);
    elems[4] = atom_int(arena, choice->prod_idx);
    return atom_expr(arena, elems, 5);
}

static Atom *gll_choices_list_atom(Arena *arena,
                                   const CettaLpNativeGllPackedVec *choices) {
    Atom **items = NULL;
    uint32_t i;

    if (choices->len > 0)
        items = arena_alloc(arena, sizeof(Atom *) * choices->len);
    for (i = 0; i < choices->len; i++)
        items[i] = gll_choice_atom(arena, &choices->data[i]);
    return make_cons_list(arena, items, choices->len);
}

static Atom *gll_node_data_atom(Arena *arena,
                                const CettaLpNativeGllNodeVec *nodes,
                                uint32_t idx) {
    const CettaLpNativeGllNode *node = &nodes->data[idx];
    Atom **elems;

    if (node->kind == CETTA_LP_NATIVE_GLL_NODE_TERM) {
        elems = arena_alloc(arena, sizeof(Atom *) * 5);
        elems[0] = atom_symbol(arena, "GNodeTerm");
        elems[1] = atom_int(arena, idx);
        elems[2] = atom_symbol_id(arena, node->symbol);
        elems[3] = atom_int(arena, node->left);
        elems[4] = atom_int(arena, node->right);
        return atom_expr(arena, elems, 5);
    }
    if (node->kind == CETTA_LP_NATIVE_GLL_NODE_EPS) {
        elems = arena_alloc(arena, sizeof(Atom *) * 4);
        elems[0] = atom_symbol(arena, "GNodeEps");
        elems[1] = atom_int(arena, idx);
        elems[2] = atom_int(arena, node->left);
        elems[3] = atom_int(arena, node->right);
        return atom_expr(arena, elems, 4);
    }
    if (node->kind == CETTA_LP_NATIVE_GLL_NODE_SYM) {
        elems = arena_alloc(arena, sizeof(Atom *) * 6);
        elems[0] = atom_symbol(arena, "GNodeSym");
        elems[1] = atom_int(arena, idx);
        elems[2] = atom_symbol_id(arena, node->symbol);
        elems[3] = atom_int(arena, node->left);
        elems[4] = atom_int(arena, node->right);
        elems[5] = gll_choices_list_atom(arena, &node->packed);
        return atom_expr(arena, elems, 6);
    }

    elems = arena_alloc(arena, sizeof(Atom *) * 7);
    elems[0] = atom_symbol(arena, "GNodeInter");
    elems[1] = atom_int(arena, idx);
    elems[2] = atom_int(arena, node->prod_idx);
    elems[3] = atom_int(arena, node->dot);
    elems[4] = atom_int(arena, node->left);
    elems[5] = atom_int(arena, node->right);
    elems[6] = gll_choices_list_atom(arena, &node->packed);
    return atom_expr(arena, elems, 7);
}

static Atom *gll_nodes_list_atom(Arena *arena,
                                 const CettaLpNativeGllNodeVec *nodes) {
    Atom **items = NULL;
    uint32_t i;

    if (nodes->len > 0)
        items = arena_alloc(arena, sizeof(Atom *) * nodes->len);
    for (i = 0; i < nodes->len; i++)
        items[i] = gll_node_data_atom(arena, nodes, i);
    return make_cons_list(arena, items, nodes->len);
}

static Atom *gll_forest_data_atom(Arena *arena,
                                  const char *status,
                                  uint32_t token_len,
                                  uint32_t root_count,
                                  int32_t root_idx,
                                  const CettaLpNativeGllNodeVec *nodes) {
    Atom **elems = arena_alloc(arena, sizeof(Atom *) * 6);

    elems[0] = atom_symbol(arena, "GParsePackedForest");
    elems[1] = atom_symbol(arena, status);
    elems[2] = atom_int(arena, token_len);
    elems[3] = atom_int(arena, root_count);
    elems[4] = root_idx < 0 ? atom_symbol(arena, "GNone") : atom_int(arena, root_idx);
    elems[5] = gll_nodes_list_atom(arena, nodes);
    return atom_expr(arena, elems, 6);
}

static Atom *cetta_lp_native_gll_forest_result(const CettaLpNativeGrammar *grammar,
                                               SymbolId start_nt,
                                               Atom *token_list,
                                               Arena *arena,
                                               char *error_buf,
                                               size_t error_buf_size,
                                               CettaLpNativeForestOutput output) {
    CettaLpNativeSlrProduction *productions = NULL;
    uint32_t production_len = 0;
    CettaLpNativeInputTokenVec tokens = {0};
    CettaLpNativeGllNodeVec nodes = {0};
    CettaLpNativeGllGssNodeVec gss_nodes = {0};
    CettaLpNativeGllDescriptorVec seen = {0};
    CettaLpNativeGllDescriptorVec work = {0};
    uint32_t root_count = 0;
    int32_t root_idx = -1;
    const char *status = "NoParse";
    Atom *result = NULL;

    if (!grammar || !arena) {
        slr_summary_set_error(error_buf, error_buf_size,
                              "bad GLL forest-summary args");
        return NULL;
    }
    if (!input_tokens_from_list(token_list, &tokens, error_buf, error_buf_size))
        return NULL;
    if (!slr_build_productions(grammar, &productions, &production_len,
                               error_buf, error_buf_size) ||
        !slr_grammar_mentions_nonterminal(productions, production_len, start_nt)) {
        goto fail;
    }
    if (gll_gss_get(&gss_nodes, true, -1, 0, 0) != 0) {
        slr_summary_set_error(error_buf, error_buf_size,
                              "failed to initialize GLL root");
        goto fail;
    }
    {
        uint32_t prod_idx;
        for (prod_idx = 0; prod_idx < production_len; prod_idx++) {
            SymbolId lhs = 0;
            const CettaLpNativeSymbol *rhs = NULL;
            uint32_t rhs_len = 0;
            slr_get_prod(productions, production_len, start_nt,
                         (int32_t)prod_idx, &lhs, &rhs, &rhs_len);
            (void)rhs;
            (void)rhs_len;
            if (lhs != start_nt)
                continue;
            if (!gll_desc_enqueue(&seen, &work, (int32_t)prod_idx, 0, 0,
                                  CETTA_LP_NATIVE_NODE_NONE, 0)) {
                slr_summary_set_error(error_buf, error_buf_size,
                                      "failed to seed GLL descriptors");
                goto fail;
            }
        }
    }

    while (work.len > 0) {
        CettaLpNativeGllDescriptor cur = work.data[work.len - 1];
        SymbolId lhs = 0;
        const CettaLpNativeSymbol *rhs = NULL;
        uint32_t rhs_len = 0;

        work.len--;
        slr_get_prod(productions, production_len, start_nt,
                     cur.prod_idx, &lhs, &rhs, &rhs_len);
        if (cur.dot < rhs_len) {
            CettaLpNativeSymbol sym = rhs[cur.dot];
            if (sym.kind == CETTA_LP_NATIVE_SYMBOL_TM) {
                int32_t term_idx;
                int32_t parent_idx;
                if (cur.pos >= tokens.len || tokens.data[cur.pos].term_kind != sym.name)
                    continue;
                term_idx = gll_node_get_term(&nodes, sym.name, cur.pos);
                if (term_idx < 0) {
                    slr_summary_set_error(error_buf, error_buf_size,
                                          "failed to allocate GLL terminal node");
                    goto fail;
                }
                parent_idx = gll_sppf_get_node(&nodes, productions, production_len,
                                               start_nt, cur.prod_idx, cur.dot + 1,
                                               cur.left_label, (uint32_t)term_idx,
                                               error_buf, error_buf_size);
                if (parent_idx < 0 ||
                    !gll_desc_enqueue(&seen, &work, cur.prod_idx, cur.dot + 1,
                                      cur.gss_idx, (uint32_t)parent_idx, cur.pos + 1)) {
                    slr_summary_set_error(error_buf, error_buf_size,
                                          "failed to advance GLL terminal descriptor");
                    goto fail;
                }
                continue;
            }
            {
                int32_t next_gss = gll_gss_get(&gss_nodes, false,
                                               cur.prod_idx, cur.dot + 1, cur.pos);
                uint32_t prod_idx;
                uint32_t popped_idx;
                if (next_gss < 0 ||
                    !gll_gss_add_edge(&gss_nodes, (uint32_t)next_gss,
                                      cur.left_label, cur.gss_idx)) {
                    slr_summary_set_error(error_buf, error_buf_size,
                                          "failed to create GLL continuation node");
                    goto fail;
                }
                for (popped_idx = 0; popped_idx < gss_nodes.data[next_gss].popped.len; popped_idx++) {
                    uint32_t done = gss_nodes.data[next_gss].popped.data[popped_idx];
                    int32_t parent_idx = gll_sppf_get_node(&nodes, productions, production_len,
                                                           start_nt, cur.prod_idx, cur.dot + 1,
                                                           cur.left_label, done,
                                                           error_buf, error_buf_size);
                    if (parent_idx < 0 ||
                        !gll_desc_enqueue(&seen, &work, cur.prod_idx, cur.dot + 1,
                                          cur.gss_idx, (uint32_t)parent_idx,
                                          nodes.data[done].right)) {
                        slr_summary_set_error(error_buf, error_buf_size,
                                              "failed to resume GLL continuation");
                        goto fail;
                    }
                }
                for (prod_idx = 0; prod_idx < production_len; prod_idx++) {
                    SymbolId child_lhs = 0;
                    const CettaLpNativeSymbol *child_rhs = NULL;
                    uint32_t child_rhs_len = 0;
                    slr_get_prod(productions, production_len, start_nt,
                                 (int32_t)prod_idx, &child_lhs, &child_rhs, &child_rhs_len);
                    (void)child_rhs;
                    (void)child_rhs_len;
                    if (child_lhs != sym.name)
                        continue;
                    if (!gll_desc_enqueue(&seen, &work, (int32_t)prod_idx, 0,
                                          (uint32_t)next_gss,
                                          CETTA_LP_NATIVE_NODE_NONE, cur.pos)) {
                        slr_summary_set_error(error_buf, error_buf_size,
                                              "failed to seed GLL child descriptor");
                        goto fail;
                    }
                }
                continue;
            }
        }
        {
            uint32_t done_idx;
            uint32_t edge_idx;
            if (rhs_len == 0) {
                int32_t sym_idx = gll_node_get_sym(&nodes, lhs, cur.pos, cur.pos);
                int32_t eps_idx = gll_node_get_eps(&nodes, cur.pos);
                if (sym_idx < 0 || eps_idx < 0 ||
                    !gll_packedvec_push_unique(&nodes.data[sym_idx].packed,
                                               CETTA_LP_NATIVE_NODE_NONE,
                                               (uint32_t)eps_idx,
                                               cur.pos,
                                               cur.prod_idx)) {
                    slr_summary_set_error(error_buf, error_buf_size,
                                          "failed to record GLL epsilon completion");
                    goto fail;
                }
                done_idx = (uint32_t)sym_idx;
            } else {
                done_idx = cur.left_label;
            }
            if (cur.gss_idx == 0) {
                if (!u32vec_push_unique(&gss_nodes.data[0].popped, done_idx)) {
                    slr_summary_set_error(error_buf, error_buf_size,
                                          "failed to record GLL root completion");
                    goto fail;
                }
                continue;
            }
            if (!u32vec_push_unique(&gss_nodes.data[cur.gss_idx].popped, done_idx)) {
                slr_summary_set_error(error_buf, error_buf_size,
                                      "failed to record GLL popped completion");
                goto fail;
            }
            for (edge_idx = 0; edge_idx < gss_nodes.data[cur.gss_idx].edges.len; edge_idx++) {
                CettaLpNativeGllGssEdge edge = gss_nodes.data[cur.gss_idx].edges.data[edge_idx];
                const CettaLpNativeGllGssNode *gss = &gss_nodes.data[cur.gss_idx];
                int32_t parent_idx = gll_sppf_get_node(&nodes, productions, production_len,
                                                       start_nt, gss->prod_idx, gss->dot,
                                                       edge.left_label, done_idx,
                                                       error_buf, error_buf_size);
                if (parent_idx < 0 ||
                    !gll_desc_enqueue(&seen, &work, gss->prod_idx, gss->dot,
                                      edge.parent_gss, (uint32_t)parent_idx,
                                      nodes.data[done_idx].right)) {
                    slr_summary_set_error(error_buf, error_buf_size,
                                          "failed to propagate GLL completion");
                    goto fail;
                }
            }
        }
    }

    {
        root_idx = gll_node_find(&nodes, CETTA_LP_NATIVE_GLL_NODE_SYM,
                                 start_nt, -1, 0, 0, tokens.len);
        if (root_idx >= 0 &&
            u32vec_contains(&gss_nodes.data[0].popped, (uint32_t)root_idx)) {
            uint8_t *memo_seen = cetta_malloc(sizeof(*memo_seen) * nodes.len);
            uint8_t *memo_value = cetta_malloc(sizeof(*memo_value) * nodes.len);
            int32_t count;
            memset(memo_seen, 0, sizeof(*memo_seen) * nodes.len);
            memset(memo_value, 0, sizeof(*memo_value) * nodes.len);
            count = gll_count_node(&nodes, (uint32_t)root_idx,
                                   memo_seen, memo_value);
            free(memo_seen);
            free(memo_value);
            root_count = count <= 0 ? 0 : (uint32_t)count;
            if (root_count == 1)
                status = "Unique";
            else if (root_count > 1)
                status = "Ambiguous";
        }
    }

    if (output == CETTA_LP_NATIVE_FOREST_OUT_DATA) {
        result = gll_forest_data_atom(arena, status, tokens.len, root_count,
                                      root_idx, &nodes);
    } else if (output == CETTA_LP_NATIVE_FOREST_OUT_SIGNATURE) {
        result = gll_forest_signature_atom(arena, status, tokens.len,
                                           root_count, root_idx, &nodes);
        if (!result) {
            slr_summary_set_error(error_buf, error_buf_size,
                                  "failed to build GLL forest signature");
            goto fail;
        }
    } else if (output == CETTA_LP_NATIVE_FOREST_OUT_SIGNATURE_DIGEST) {
        result = gll_forest_signature_digest_atom(arena, status, tokens.len,
                                                  root_count, root_idx, &nodes);
        if (!result) {
            slr_summary_set_error(error_buf, error_buf_size,
                                  "failed to build GLL forest signature digest");
            goto fail;
        }
    } else {
        result = gll_forest_summary_atom(arena, status, tokens.len, &nodes,
                                         &gss_nodes, seen.len, root_count);
    }

    free(tokens.data);
    gll_nodevec_free(&nodes);
    gll_gssvec_free(&gss_nodes);
    free(seen.data);
    free(work.data);
    slr_productions_free(productions, production_len);
    return result;

fail:
    free(tokens.data);
    gll_nodevec_free(&nodes);
    gll_gssvec_free(&gss_nodes);
    free(seen.data);
    free(work.data);
    slr_productions_free(productions, production_len);
    return NULL;
}

Atom *cetta_lp_native_gll_forest_summary(const CettaLpNativeGrammar *grammar,
                                         SymbolId start_nt,
                                         Atom *token_list,
                                         Arena *arena,
                                         char *error_buf,
                                         size_t error_buf_size) {
    return cetta_lp_native_gll_forest_result(grammar, start_nt, token_list,
                                             arena, error_buf, error_buf_size,
                                             CETTA_LP_NATIVE_FOREST_OUT_SUMMARY);
}

Atom *cetta_lp_native_gll_forest_signature(const CettaLpNativeGrammar *grammar,
                                           SymbolId start_nt,
                                           Atom *token_list,
                                           Arena *arena,
                                           char *error_buf,
                                           size_t error_buf_size) {
    return cetta_lp_native_gll_forest_result(grammar, start_nt, token_list,
                                             arena, error_buf, error_buf_size,
                                             CETTA_LP_NATIVE_FOREST_OUT_SIGNATURE);
}

Atom *cetta_lp_native_gll_forest_signature_digest(const CettaLpNativeGrammar *grammar,
                                                  SymbolId start_nt,
                                                  Atom *token_list,
                                                  Arena *arena,
                                                  char *error_buf,
                                                  size_t error_buf_size) {
    return cetta_lp_native_gll_forest_result(
        grammar, start_nt, token_list, arena, error_buf, error_buf_size,
        CETTA_LP_NATIVE_FOREST_OUT_SIGNATURE_DIGEST);
}

Atom *cetta_lp_native_gll_forest_data(const CettaLpNativeGrammar *grammar,
                                      SymbolId start_nt,
                                      Atom *token_list,
                                      Arena *arena,
                                      char *error_buf,
                                      size_t error_buf_size) {
    return cetta_lp_native_gll_forest_result(grammar, start_nt, token_list,
                                             arena, error_buf, error_buf_size,
                                             CETTA_LP_NATIVE_FOREST_OUT_DATA);
}

Atom *cetta_lp_native_glr_parse_class(const CettaLpNativeGrammar *grammar,
                                      SymbolId start_nt,
                                      Atom *token_list,
                                      Arena *arena,
                                      char *error_buf,
                                      size_t error_buf_size) {
    CettaLpNativeSlrProduction *productions = NULL;
    uint32_t production_len = 0;
    CettaLpNativeIdVec nonterminals = {0};
    CettaLpNativeIdVec terminals = {0};
    bool *nullable = NULL;
    CettaLpNativeBitset *first = NULL;
    CettaLpNativeBitset *follow = NULL;
    CettaLpNativeStateVec states = {0};
    CettaLpNativeSymbolVec symbols = {0};
    CettaLpNativeEdgeVec edges = {0};
    CettaLpNativeActionVec actions = {0};
    uint32_t conflict_len = 0;
    uint32_t i;
    CettaLpNativeInputTokenVec tokens = {0};
    CettaLpNativeBranchVec configs = {0};
    CettaLpNativeU32Vec work = {0};
    uint32_t accept_count = 0;
    Atom *result = NULL;

    if (!grammar || !arena) {
        slr_summary_set_error(error_buf, error_buf_size,
                              "bad GLR parse args");
        return NULL;
    }
    if (!input_tokens_from_list(token_list, &tokens, error_buf, error_buf_size))
        return NULL;
    if (!slr_build_productions(grammar, &productions, &production_len,
                               error_buf, error_buf_size) ||
        !slr_collect_symbol_sets(productions, production_len, start_nt,
                                 &nonterminals, &terminals,
                                 error_buf, error_buf_size)) {
        goto fail;
    }
    if (!slr_grammar_mentions_nonterminal(productions, production_len, start_nt)) {
        slr_summary_set_error(error_buf, error_buf_size,
                              "start nonterminal not present in grammar");
        goto fail;
    }
    if (!slr_compute_nullable_first_follow(productions, production_len,
                                           &nonterminals, &terminals, start_nt,
                                           &nullable, &first, &follow,
                                           error_buf, error_buf_size) ||
        !slr_build_states(productions, production_len, start_nt,
                          &states, &symbols, &edges,
                          error_buf, error_buf_size)) {
        goto fail;
    }

    for (i = 0; i < states.len; i++) {
        uint32_t item_idx;
        for (item_idx = 0; item_idx < states.data[i].len; item_idx++) {
            SymbolId lhs = 0;
            const CettaLpNativeSymbol *rhs = NULL;
            uint32_t rhs_len = 0;
            CettaLpNativeItem item = states.data[i].items[item_idx];

            slr_get_prod(productions, production_len, start_nt,
                         item.prod_idx, &lhs, &rhs, &rhs_len);
            if (rhs && item.dot < rhs_len) {
                if (rhs[item.dot].kind == CETTA_LP_NATIVE_SYMBOL_TM) {
                    int32_t sym_idx = symbolvec_find(&symbols, true, rhs[item.dot].name);
                    int32_t edge_idx;
                    int32_t term_idx = idvec_find(&terminals, rhs[item.dot].name);
                    if (sym_idx >= 0 && term_idx >= 0) {
                        edge_idx = edgevec_find(&edges, i, (uint32_t)sym_idx);
                    } else {
                        edge_idx = -1;
                    }
                    if (edge_idx >= 0 && term_idx >= 0) {
                        if (!actionvec_push_unique(&actions, i, (uint32_t)term_idx, 's',
                                                   (int32_t)edges.data[edge_idx].target,
                                                   &conflict_len)) {
                            slr_summary_set_error(error_buf, error_buf_size,
                                                  "failed to record GLR shift action");
                            goto fail;
                        }
                    }
                }
                continue;
            }
            if (item.prod_idx == -1) {
                if (!actionvec_push_unique(&actions, i, terminals.len, 'a', 0,
                                           &conflict_len)) {
                    slr_summary_set_error(error_buf, error_buf_size,
                                          "failed to record GLR accept action");
                    goto fail;
                }
                continue;
            }
            {
                int32_t lhs_idx = idvec_find(&nonterminals, lhs);
                uint32_t tok_idx;
                if (lhs_idx < 0) {
                    slr_summary_set_error(error_buf, error_buf_size,
                                          "reduce lhs missing from follow set");
                    goto fail;
                }
                for (tok_idx = 0; tok_idx <= terminals.len; tok_idx++) {
                    if (!bitset_test(&follow[lhs_idx], tok_idx))
                        continue;
                    if (!actionvec_push_unique(&actions, i, tok_idx, 'r',
                                               item.prod_idx, &conflict_len)) {
                        slr_summary_set_error(error_buf, error_buf_size,
                                              "failed to record GLR reduce action");
                        goto fail;
                    }
                }
            }
        }
    }

    {
        CettaLpNativeU32Vec init_stack = {0};
        CettaLpNativeParseValueVec empty_values = {0};
        if (!u32vec_push(&init_stack, 0) ||
            !branchvec_push_copy(&configs, 0, &init_stack, &empty_values, 1, true) ||
            !u32vec_push(&work, 0)) {
            free(init_stack.data);
            slr_summary_set_error(error_buf, error_buf_size,
                                  "failed to initialize GLR config queue");
            goto fail;
        }
        free(init_stack.data);
    }

    while (work.len > 0 && accept_count < 2) {
        uint32_t config_idx = work.data[work.len - 1];
        CettaLpNativeBranch *cur = &configs.data[config_idx];
        uint32_t delta;
        uint32_t state;
        uint32_t token_idx;
        uint32_t action_idx;

        work.len--;
        cur->queued = false;
        if (cur->state_stack.len == 0) {
            continue;
        }
        if (cur->ways_total <= cur->ways_done)
            continue;
        delta = (uint32_t)(cur->ways_total - cur->ways_done);
        cur->ways_done = cur->ways_total;
        state = cur->state_stack.data[cur->state_stack.len - 1];
        if (cur->pos < tokens.len) {
            int32_t term_idx = idvec_find(&terminals, tokens.data[cur->pos].term_kind);
            if (term_idx < 0) {
                continue;
            }
            token_idx = (uint32_t)term_idx;
        } else {
            token_idx = terminals.len;
        }

        for (action_idx = 0; action_idx < actions.len && accept_count < 2; action_idx++) {
            const CettaLpNativeAction *action = &actions.data[action_idx];
            CettaLpNativeU32Vec next_stack = {0};
            uint32_t next_pos = cur->pos;

            if (action->state != state || action->token_idx != token_idx)
                continue;
            if (action->kind == 'a') {
                if (cur->pos == tokens.len) {
                    accept_count += delta;
                    if (accept_count > 2)
                        accept_count = 2;
                }
                continue;
            }
            if (action->kind == 's') {
                if (cur->pos >= tokens.len ||
                    !u32vec_copy(&next_stack, &cur->state_stack) ||
                    !u32vec_push(&next_stack, (uint32_t)action->value)) {
                    free(next_stack.data);
                    slr_summary_set_error(error_buf, error_buf_size,
                                          "failed to fork GLR shift branch");
                    goto fail;
                }
                next_pos = cur->pos + 1;
            } else if (action->kind == 'r') {
                int32_t prod_idx = action->value;
                SymbolId lhs = 0;
                const CettaLpNativeSymbol *rhs = NULL;
                uint32_t rhs_len = 0;
                int32_t lhs_sym_idx;
                int32_t goto_edge_idx;

                slr_get_prod(productions, production_len, start_nt,
                             prod_idx, &lhs, &rhs, &rhs_len);
                if (!u32vec_copy(&next_stack, &cur->state_stack)) {
                    slr_summary_set_error(error_buf, error_buf_size,
                                          "failed to fork GLR reduce branch");
                    goto fail;
                }
                if (rhs_len >= next_stack.len) {
                    free(next_stack.data);
                    continue;
                }
                next_stack.len -= rhs_len;
                lhs_sym_idx = symbolvec_find(&symbols, false, lhs);
                if (lhs_sym_idx < 0 || next_stack.len == 0) {
                    free(next_stack.data);
                    continue;
                }
                goto_edge_idx = edgevec_find(&edges,
                                             next_stack.data[next_stack.len - 1],
                                             (uint32_t)lhs_sym_idx);
                if (goto_edge_idx < 0 ||
                    !u32vec_push(&next_stack, edges.data[goto_edge_idx].target)) {
                    free(next_stack.data);
                    slr_summary_set_error(error_buf, error_buf_size,
                                          "failed to advance GLR reduce branch");
                    goto fail;
                }
            } else {
                free(next_stack.data);
                slr_summary_set_error(error_buf, error_buf_size,
                                      "unknown GLR action kind");
                goto fail;
            }

            {
                int32_t next_idx = branchvec_find(&configs, next_pos, &next_stack);
                if (next_idx < 0) {
                    CettaLpNativeParseValueVec empty_values = {0};
                    if (!branchvec_push_copy(&configs, next_pos, &next_stack,
                                             &empty_values,
                                             (uint8_t)(delta > 2 ? 2 : delta), true) ||
                        !u32vec_push(&work, configs.len - 1)) {
                        free(next_stack.data);
                        slr_summary_set_error(error_buf, error_buf_size,
                                              "failed to enqueue GLR branch");
                        goto fail;
                    }
                } else {
                    uint32_t grown = configs.data[next_idx].ways_total + delta;
                    if (grown > 2)
                        grown = 2;
                    if (grown > configs.data[next_idx].ways_total) {
                        configs.data[next_idx].ways_total = (uint8_t)grown;
                        if (!configs.data[next_idx].queued) {
                            configs.data[next_idx].queued = true;
                            if (!u32vec_push(&work, (uint32_t)next_idx)) {
                                free(next_stack.data);
                                slr_summary_set_error(error_buf, error_buf_size,
                                                      "failed to schedule GLR branch");
                                goto fail;
                            }
                        }
                    }
                }
            }
            free(next_stack.data);
        }
    }

    if (accept_count == 0)
        result = atom_symbol(arena, "NoParse");
    else if (accept_count == 1)
        result = atom_symbol(arena, "Unique");
    else
        result = atom_symbol(arena, "Ambiguous");

    if (first && follow) {
        for (i = 0; i < nonterminals.len; i++) {
            bitset_free(&first[i]);
            bitset_free(&follow[i]);
        }
    }
    free(tokens.data);
    free(work.data);
    branchvec_free(&configs);
    free(nullable);
    free(first);
    free(follow);
    free(nonterminals.data);
    free(terminals.data);
    free(symbols.data);
    free(edges.data);
    free(actions.data);
    statevec_free(&states);
    slr_productions_free(productions, production_len);
    return result;

fail:
    if (first && follow) {
        for (i = 0; i < nonterminals.len; i++) {
            bitset_free(&first[i]);
            bitset_free(&follow[i]);
        }
    }
    free(tokens.data);
    free(work.data);
    branchvec_free(&configs);
    free(nullable);
    free(first);
    free(follow);
    free(nonterminals.data);
    free(terminals.data);
    free(symbols.data);
    free(edges.data);
    free(actions.data);
    statevec_free(&states);
    slr_productions_free(productions, production_len);
    return NULL;
}

Atom *cetta_lp_native_glr_parse_shared(const CettaLpNativeGrammar *grammar,
                                       SymbolId start_nt,
                                       Atom *token_list,
                                       Arena *arena,
                                       char *error_buf,
                                       size_t error_buf_size) {
    CettaLpNativeSlrProduction *productions = NULL;
    uint32_t production_len = 0;
    CettaLpNativeIdVec nonterminals = {0};
    CettaLpNativeIdVec terminals = {0};
    bool *nullable = NULL;
    CettaLpNativeBitset *first = NULL;
    CettaLpNativeBitset *follow = NULL;
    CettaLpNativeStateVec states = {0};
    CettaLpNativeSymbolVec symbols = {0};
    CettaLpNativeEdgeVec edges = {0};
    CettaLpNativeActionVec actions = {0};
    uint32_t conflict_len = 0;
    uint32_t i;
    CettaLpNativeInputTokenVec tokens = {0};
    CettaLpNativeBranchVec configs = {0};
    CettaLpNativeU32Vec work = {0};
    uint32_t accept_count = 0;
    Atom *accept_cert = NULL;
    Atom *result = NULL;

    if (!grammar || !arena) {
        slr_summary_set_error(error_buf, error_buf_size,
                              "bad GLR shared-parse args");
        return NULL;
    }
    if (!input_tokens_from_list(token_list, &tokens, error_buf, error_buf_size))
        return NULL;
    if (!slr_build_productions(grammar, &productions, &production_len,
                               error_buf, error_buf_size) ||
        !slr_collect_symbol_sets(productions, production_len, start_nt,
                                 &nonterminals, &terminals,
                                 error_buf, error_buf_size)) {
        goto fail;
    }
    if (!slr_grammar_mentions_nonterminal(productions, production_len, start_nt)) {
        slr_summary_set_error(error_buf, error_buf_size,
                              "start nonterminal not present in grammar");
        goto fail;
    }
    if (!slr_compute_nullable_first_follow(productions, production_len,
                                           &nonterminals, &terminals, start_nt,
                                           &nullable, &first, &follow,
                                           error_buf, error_buf_size) ||
        !slr_build_states(productions, production_len, start_nt,
                          &states, &symbols, &edges,
                          error_buf, error_buf_size)) {
        goto fail;
    }

    for (i = 0; i < states.len; i++) {
        uint32_t item_idx;
        for (item_idx = 0; item_idx < states.data[i].len; item_idx++) {
            SymbolId lhs = 0;
            const CettaLpNativeSymbol *rhs = NULL;
            uint32_t rhs_len = 0;
            CettaLpNativeItem item = states.data[i].items[item_idx];

            slr_get_prod(productions, production_len, start_nt,
                         item.prod_idx, &lhs, &rhs, &rhs_len);
            if (rhs && item.dot < rhs_len) {
                if (rhs[item.dot].kind == CETTA_LP_NATIVE_SYMBOL_TM) {
                    int32_t sym_idx = symbolvec_find(&symbols, true, rhs[item.dot].name);
                    int32_t edge_idx;
                    int32_t term_idx = idvec_find(&terminals, rhs[item.dot].name);
                    if (sym_idx >= 0 && term_idx >= 0) {
                        edge_idx = edgevec_find(&edges, i, (uint32_t)sym_idx);
                    } else {
                        edge_idx = -1;
                    }
                    if (edge_idx >= 0 && term_idx >= 0) {
                        if (!actionvec_push_unique(&actions, i, (uint32_t)term_idx, 's',
                                                   (int32_t)edges.data[edge_idx].target,
                                                   &conflict_len)) {
                            slr_summary_set_error(error_buf, error_buf_size,
                                                  "failed to record GLR shift action");
                            goto fail;
                        }
                    }
                }
                continue;
            }
            if (item.prod_idx == -1) {
                if (!actionvec_push_unique(&actions, i, terminals.len, 'a', 0,
                                           &conflict_len)) {
                    slr_summary_set_error(error_buf, error_buf_size,
                                          "failed to record GLR accept action");
                    goto fail;
                }
                continue;
            }
            {
                int32_t lhs_idx = idvec_find(&nonterminals, lhs);
                uint32_t tok_idx;
                if (lhs_idx < 0) {
                    slr_summary_set_error(error_buf, error_buf_size,
                                          "reduce lhs missing from follow set");
                    goto fail;
                }
                for (tok_idx = 0; tok_idx <= terminals.len; tok_idx++) {
                    if (!bitset_test(&follow[lhs_idx], tok_idx))
                        continue;
                    if (!actionvec_push_unique(&actions, i, tok_idx, 'r',
                                               item.prod_idx, &conflict_len)) {
                        slr_summary_set_error(error_buf, error_buf_size,
                                              "failed to record GLR reduce action");
                        goto fail;
                    }
                }
            }
        }
    }

    {
        CettaLpNativeU32Vec init_stack = {0};
        CettaLpNativeParseValueVec init_values = {0};
        if (!u32vec_push(&init_stack, 0) ||
            !branchvec_push_copy(&configs, 0, &init_stack, &init_values, 1, true) ||
            !u32vec_push(&work, 0)) {
            free(init_stack.data);
            slr_summary_set_error(error_buf, error_buf_size,
                                  "failed to initialize GLR shared config queue");
            goto fail;
        }
        free(init_stack.data);
    }

    while (work.len > 0 && accept_count < 2) {
        uint32_t config_idx = work.data[work.len - 1];
        CettaLpNativeBranch *cur = &configs.data[config_idx];
        uint32_t delta;
        uint32_t state;
        uint32_t token_idx;
        uint32_t action_idx;

        work.len--;
        cur->queued = false;
        if (cur->state_stack.len == 0)
            continue;
        if (cur->ways_total <= cur->ways_done)
            continue;
        delta = (uint32_t)(cur->ways_total - cur->ways_done);
        cur->ways_done = cur->ways_total;
        state = cur->state_stack.data[cur->state_stack.len - 1];
        if (cur->pos < tokens.len) {
            int32_t term_idx = idvec_find(&terminals, tokens.data[cur->pos].term_kind);
            if (term_idx < 0)
                continue;
            token_idx = (uint32_t)term_idx;
        } else {
            token_idx = terminals.len;
        }

        for (action_idx = 0; action_idx < actions.len && accept_count < 2; action_idx++) {
            const CettaLpNativeAction *action = &actions.data[action_idx];
            CettaLpNativeU32Vec next_stack = {0};
            CettaLpNativeParseValueVec next_values = {0};
            uint32_t next_pos = cur->pos;

            if (action->state != state || action->token_idx != token_idx)
                continue;
            if (action->kind == 'a') {
                if (cur->pos == tokens.len &&
                    cur->value_stack.len == 1 &&
                    cur->value_stack.data[0].is_cert &&
                    cur->value_stack.data[0].cert &&
                    cur->value_stack.data[0].start == 0 &&
                    cur->value_stack.data[0].end == tokens.len) {
                    if (accept_count == 0)
                        accept_cert = cur->value_stack.data[0].cert;
                    accept_count += delta;
                    if (accept_count > 2)
                        accept_count = 2;
                } else {
                    slr_summary_set_error(error_buf, error_buf_size,
                                          "GLR accept state missing full-span cert");
                    goto fail;
                }
                continue;
            }
            if (action->kind == 's') {
                CettaLpNativeParseValue value;

                if (cur->pos >= tokens.len ||
                    !u32vec_copy(&next_stack, &cur->state_stack) ||
                    !parsevaluevec_copy(&next_values, &cur->value_stack) ||
                    !u32vec_push(&next_stack, (uint32_t)action->value)) {
                    free(next_stack.data);
                    free(next_values.data);
                    slr_summary_set_error(error_buf, error_buf_size,
                                          "failed to fork GLR shared shift branch");
                    goto fail;
                }
                value.is_cert = false;
                value.token_atom = tokens.data[cur->pos].token_atom;
                value.term_kind = tokens.data[cur->pos].term_kind;
                value.start = cur->pos;
                value.end = cur->pos + 1;
                value.forest_idx = UINT32_MAX;
                value.cert = NULL;
                if (!parsevaluevec_push(&next_values, &value)) {
                    free(next_stack.data);
                    free(next_values.data);
                    slr_summary_set_error(error_buf, error_buf_size,
                                          "failed to push GLR shared shift value");
                    goto fail;
                }
                next_pos = cur->pos + 1;
            } else if (action->kind == 'r') {
                int32_t prod_idx = action->value;
                SymbolId lhs = 0;
                const CettaLpNativeSymbol *rhs = NULL;
                uint32_t rhs_len = 0;
                int32_t lhs_sym_idx;
                int32_t goto_edge_idx;
                CettaLpNativeParseValue next_value;

                slr_get_prod(productions, production_len, start_nt,
                             prod_idx, &lhs, &rhs, &rhs_len);
                if (!u32vec_copy(&next_stack, &cur->state_stack) ||
                    !parsevaluevec_copy(&next_values, &cur->value_stack)) {
                    free(next_stack.data);
                    free(next_values.data);
                    slr_summary_set_error(error_buf, error_buf_size,
                                          "failed to fork GLR shared reduce branch");
                    goto fail;
                }
                if (rhs_len > next_values.len || rhs_len >= next_stack.len) {
                    free(next_stack.data);
                    free(next_values.data);
                    continue;
                }

                if ((uint32_t)prod_idx >= grammar->production_len) {
                    CettaLpNativeParseValue *child = &next_values.data[next_values.len - 1];
                    if (rhs_len != 1 || child->is_cert) {
                        free(next_stack.data);
                        free(next_values.data);
                        slr_summary_set_error(error_buf, error_buf_size,
                                              "GLR leaf reduction expected one shifted token");
                        goto fail;
                    }
                    next_value.is_cert = true;
                    next_value.token_atom = NULL;
                    next_value.term_kind = 0;
                    next_value.start = child->start;
                    next_value.end = child->end;
                    next_value.forest_idx = UINT32_MAX;
                    next_value.cert = make_leaf_cert(arena, child->token_atom,
                                                     child->start, child->end);
                } else if (rhs_len == 0) {
                    Atom *eps = make_eps_cert(arena);
                    Atom *kids[1] = {eps};
                    next_value.is_cert = true;
                    next_value.token_atom = NULL;
                    next_value.term_kind = 0;
                    next_value.start = cur->pos;
                    next_value.end = cur->pos;
                    next_value.forest_idx = UINT32_MAX;
                    next_value.cert = make_node_cert(
                        arena, grammar->productions[prod_idx].label, lhs,
                        cur->pos, cur->pos, kids, 1);
                } else {
                    uint32_t base = next_values.len - rhs_len;
                    uint32_t j;
                    Atom **kids = arena_alloc(arena, sizeof(Atom *) * rhs_len);
                    next_value.is_cert = true;
                    next_value.token_atom = NULL;
                    next_value.term_kind = 0;
                    next_value.start = next_values.data[base].start;
                    next_value.end = next_values.data[next_values.len - 1].end;
                    next_value.forest_idx = UINT32_MAX;
                    for (j = 0; j < rhs_len; j++) {
                        CettaLpNativeParseValue *part = &next_values.data[base + j];
                        if (rhs[j].kind == CETTA_LP_NATIVE_SYMBOL_TM) {
                            if (part->is_cert || part->term_kind != rhs[j].name) {
                                free(next_stack.data);
                                free(next_values.data);
                                slr_summary_set_error(error_buf, error_buf_size,
                                                      "GLR terminal reduction mismatch");
                                goto fail;
                            }
                            kids[j] = make_tok_cert(arena, rhs[j].name, part->start);
                        } else {
                            if (!part->is_cert || !part->cert) {
                                free(next_stack.data);
                                free(next_values.data);
                                slr_summary_set_error(error_buf, error_buf_size,
                                                      "GLR nonterminal reduction missing child cert");
                                goto fail;
                            }
                            kids[j] = part->cert;
                        }
                    }
                    next_value.cert = make_node_cert(
                        arena, grammar->productions[prod_idx].label, lhs,
                        next_value.start, next_value.end, kids, rhs_len);
                }

                next_values.len -= rhs_len;
                next_stack.len -= rhs_len;
                lhs_sym_idx = symbolvec_find(&symbols, false, lhs);
                if (lhs_sym_idx < 0 || next_stack.len == 0) {
                    free(next_stack.data);
                    free(next_values.data);
                    continue;
                }
                goto_edge_idx = edgevec_find(&edges,
                                             next_stack.data[next_stack.len - 1],
                                             (uint32_t)lhs_sym_idx);
                if (goto_edge_idx < 0 ||
                    !u32vec_push(&next_stack, edges.data[goto_edge_idx].target) ||
                    !parsevaluevec_push(&next_values, &next_value)) {
                    free(next_stack.data);
                    free(next_values.data);
                    slr_summary_set_error(error_buf, error_buf_size,
                                          "failed to advance GLR shared reduce branch");
                    goto fail;
                }
            } else {
                free(next_stack.data);
                free(next_values.data);
                slr_summary_set_error(error_buf, error_buf_size,
                                      "unknown GLR shared action kind");
                goto fail;
            }

            {
                int32_t next_idx = branchvec_find(&configs, next_pos, &next_stack);
                if (next_idx < 0) {
                    if (!branchvec_push_copy(&configs, next_pos, &next_stack, &next_values,
                                             (uint8_t)(delta > 2 ? 2 : delta), true) ||
                        !u32vec_push(&work, configs.len - 1)) {
                        free(next_stack.data);
                        free(next_values.data);
                        slr_summary_set_error(error_buf, error_buf_size,
                                              "failed to enqueue GLR shared branch");
                        goto fail;
                    }
                } else {
                    uint32_t grown = configs.data[next_idx].ways_total + delta;
                    if (grown > 2)
                        grown = 2;
                    if (grown > configs.data[next_idx].ways_total) {
                        configs.data[next_idx].ways_total = (uint8_t)grown;
                        if (!configs.data[next_idx].queued) {
                            configs.data[next_idx].queued = true;
                            if (!u32vec_push(&work, (uint32_t)next_idx)) {
                                free(next_stack.data);
                                free(next_values.data);
                                slr_summary_set_error(error_buf, error_buf_size,
                                                      "failed to schedule GLR shared branch");
                                goto fail;
                            }
                        }
                    }
                }
            }
            free(next_stack.data);
            free(next_values.data);
        }
    }

    if (accept_count == 0)
        result = atom_symbol(arena, "NoParse");
    else if (accept_count == 1) {
        if (!accept_cert) {
            slr_summary_set_error(error_buf, error_buf_size,
                                  "GLR unique parse missing cert");
            goto fail;
        }
        result = atom_expr2(arena, atom_symbol(arena, "Unique"), accept_cert);
    } else {
        result = atom_symbol(arena, "Ambiguous");
    }

    if (first && follow) {
        for (i = 0; i < nonterminals.len; i++) {
            bitset_free(&first[i]);
            bitset_free(&follow[i]);
        }
    }
    free(tokens.data);
    free(work.data);
    branchvec_free(&configs);
    free(nullable);
    free(first);
    free(follow);
    free(nonterminals.data);
    free(terminals.data);
    free(symbols.data);
    free(edges.data);
    free(actions.data);
    statevec_free(&states);
    slr_productions_free(productions, production_len);
    return result;

fail:
    if (first && follow) {
        for (i = 0; i < nonterminals.len; i++) {
            bitset_free(&first[i]);
            bitset_free(&follow[i]);
        }
    }
    free(tokens.data);
    free(work.data);
    branchvec_free(&configs);
    free(nullable);
    free(first);
    free(follow);
    free(nonterminals.data);
    free(terminals.data);
    free(symbols.data);
    free(edges.data);
    free(actions.data);
    statevec_free(&states);
    slr_productions_free(productions, production_len);
    return NULL;
}

static Atom *cetta_lp_native_glr_forest_result(const CettaLpNativeGrammar *grammar,
                                               SymbolId start_nt,
                                               Atom *token_list,
                                               Arena *arena,
                                               char *error_buf,
                                               size_t error_buf_size,
                                               CettaLpNativeForestOutput output) {
    CettaLpNativeSlrProduction *productions = NULL;
    uint32_t production_len = 0;
    CettaLpNativeIdVec nonterminals = {0};
    CettaLpNativeIdVec terminals = {0};
    bool *nullable = NULL;
    CettaLpNativeBitset *first = NULL;
    CettaLpNativeBitset *follow = NULL;
    CettaLpNativeStateVec states = {0};
    CettaLpNativeSymbolVec symbols = {0};
    CettaLpNativeEdgeVec edges = {0};
    CettaLpNativeActionVec actions = {0};
    uint32_t conflict_len = 0;
    uint32_t i;
    CettaLpNativeInputTokenVec tokens = {0};
    CettaLpNativeBranchVec configs = {0};
    CettaLpNativeU32Vec work = {0};
    CettaLpNativeGllNodeVec nodes = {0};
    uint32_t accept_count = 0;
    int32_t accept_root = -1;
    const char *status = "NoParse";
    Atom *result = NULL;

    if (!grammar || !arena) {
        slr_summary_set_error(error_buf, error_buf_size,
                              "bad GLR forest args");
        return NULL;
    }
    if (!input_tokens_from_list(token_list, &tokens, error_buf, error_buf_size))
        return NULL;
    if (!slr_build_productions(grammar, &productions, &production_len,
                               error_buf, error_buf_size) ||
        !slr_collect_symbol_sets(productions, production_len, start_nt,
                                 &nonterminals, &terminals,
                                 error_buf, error_buf_size)) {
        goto fail;
    }
    if (!slr_grammar_mentions_nonterminal(productions, production_len, start_nt)) {
        slr_summary_set_error(error_buf, error_buf_size,
                              "start nonterminal not present in grammar");
        goto fail;
    }
    if (!slr_compute_nullable_first_follow(productions, production_len,
                                           &nonterminals, &terminals, start_nt,
                                           &nullable, &first, &follow,
                                           error_buf, error_buf_size) ||
        !slr_build_states(productions, production_len, start_nt,
                          &states, &symbols, &edges,
                          error_buf, error_buf_size)) {
        goto fail;
    }

    for (i = 0; i < states.len; i++) {
        uint32_t item_idx;
        for (item_idx = 0; item_idx < states.data[i].len; item_idx++) {
            SymbolId lhs = 0;
            const CettaLpNativeSymbol *rhs = NULL;
            uint32_t rhs_len = 0;
            CettaLpNativeItem item = states.data[i].items[item_idx];

            slr_get_prod(productions, production_len, start_nt,
                         item.prod_idx, &lhs, &rhs, &rhs_len);
            if (rhs && item.dot < rhs_len) {
                if (rhs[item.dot].kind == CETTA_LP_NATIVE_SYMBOL_TM) {
                    int32_t sym_idx = symbolvec_find(&symbols, true, rhs[item.dot].name);
                    int32_t edge_idx;
                    int32_t term_idx = idvec_find(&terminals, rhs[item.dot].name);
                    if (sym_idx >= 0 && term_idx >= 0) {
                        edge_idx = edgevec_find(&edges, i, (uint32_t)sym_idx);
                    } else {
                        edge_idx = -1;
                    }
                    if (edge_idx >= 0 && term_idx >= 0) {
                        if (!actionvec_push_unique(&actions, i, (uint32_t)term_idx, 's',
                                                   (int32_t)edges.data[edge_idx].target,
                                                   &conflict_len)) {
                            slr_summary_set_error(error_buf, error_buf_size,
                                                  "failed to record GLR forest shift action");
                            goto fail;
                        }
                    }
                }
                continue;
            }
            if (item.prod_idx == -1) {
                if (!actionvec_push_unique(&actions, i, terminals.len, 'a', 0,
                                           &conflict_len)) {
                    slr_summary_set_error(error_buf, error_buf_size,
                                          "failed to record GLR forest accept action");
                    goto fail;
                }
                continue;
            }
            {
                int32_t lhs_idx = idvec_find(&nonterminals, lhs);
                uint32_t tok_idx;
                if (lhs_idx < 0) {
                    slr_summary_set_error(error_buf, error_buf_size,
                                          "reduce lhs missing from follow set");
                    goto fail;
                }
                for (tok_idx = 0; tok_idx <= terminals.len; tok_idx++) {
                    if (!bitset_test(&follow[lhs_idx], tok_idx))
                        continue;
                    if (!actionvec_push_unique(&actions, i, tok_idx, 'r',
                                               item.prod_idx, &conflict_len)) {
                        slr_summary_set_error(error_buf, error_buf_size,
                                              "failed to record GLR forest reduce action");
                        goto fail;
                    }
                }
            }
        }
    }

    {
        CettaLpNativeU32Vec init_stack = {0};
        CettaLpNativeParseValueVec init_values = {0};
        if (!u32vec_push(&init_stack, 0) ||
            !branchvec_push_copy(&configs, 0, &init_stack, &init_values, 1, true) ||
            !u32vec_push(&work, 0)) {
            free(init_stack.data);
            slr_summary_set_error(error_buf, error_buf_size,
                                  "failed to initialize GLR forest queue");
            goto fail;
        }
        free(init_stack.data);
    }

    while (work.len > 0 && accept_count < 2) {
        uint32_t config_idx = work.data[work.len - 1];
        CettaLpNativeBranch *cur = &configs.data[config_idx];
        uint32_t delta;
        uint32_t state;
        uint32_t token_idx;
        uint32_t action_idx;

        work.len--;
        cur->queued = false;
        if (cur->state_stack.len == 0)
            continue;
        if (cur->ways_total <= cur->ways_done)
            continue;
        delta = (uint32_t)(cur->ways_total - cur->ways_done);
        cur->ways_done = cur->ways_total;
        state = cur->state_stack.data[cur->state_stack.len - 1];
        if (cur->pos < tokens.len) {
            int32_t term_idx = idvec_find(&terminals, tokens.data[cur->pos].term_kind);
            if (term_idx < 0)
                continue;
            token_idx = (uint32_t)term_idx;
        } else {
            token_idx = terminals.len;
        }

        for (action_idx = 0; action_idx < actions.len && accept_count < 2; action_idx++) {
            const CettaLpNativeAction *action = &actions.data[action_idx];
            CettaLpNativeU32Vec next_stack = {0};
            CettaLpNativeParseValueVec next_values = {0};
            uint32_t next_pos = cur->pos;

            if (action->state != state || action->token_idx != token_idx)
                continue;
            if (action->kind == 'a') {
                if (cur->pos == tokens.len &&
                    cur->value_stack.len == 1 &&
                    cur->value_stack.data[0].forest_idx != UINT32_MAX) {
                    if (accept_count == 0)
                        accept_root = (int32_t)cur->value_stack.data[0].forest_idx;
                    accept_count += delta;
                    if (accept_count > 2)
                        accept_count = 2;
                } else {
                    slr_summary_set_error(error_buf, error_buf_size,
                                          "GLR forest accept missing full-span root");
                    goto fail;
                }
                continue;
            }
            if (action->kind == 's') {
                CettaLpNativeParseValue value;
                int32_t term_node;

                if (cur->pos >= tokens.len ||
                    !u32vec_copy(&next_stack, &cur->state_stack) ||
                    !parsevaluevec_copy(&next_values, &cur->value_stack) ||
                    !u32vec_push(&next_stack, (uint32_t)action->value)) {
                    free(next_stack.data);
                    free(next_values.data);
                    slr_summary_set_error(error_buf, error_buf_size,
                                          "failed to fork GLR forest shift branch");
                    goto fail;
                }
                term_node = gll_node_get_term(&nodes, tokens.data[cur->pos].term_kind,
                                              cur->pos);
                if (term_node < 0) {
                    free(next_stack.data);
                    free(next_values.data);
                    slr_summary_set_error(error_buf, error_buf_size,
                                          "failed to allocate GLR forest terminal");
                    goto fail;
                }
                value.is_cert = false;
                value.token_atom = tokens.data[cur->pos].token_atom;
                value.term_kind = tokens.data[cur->pos].term_kind;
                value.start = cur->pos;
                value.end = cur->pos + 1;
                value.forest_idx = (uint32_t)term_node;
                value.cert = NULL;
                if (!parsevaluevec_push(&next_values, &value)) {
                    free(next_stack.data);
                    free(next_values.data);
                    slr_summary_set_error(error_buf, error_buf_size,
                                          "failed to push GLR forest shift value");
                    goto fail;
                }
                next_pos = cur->pos + 1;
            } else if (action->kind == 'r') {
                int32_t prod_idx = action->value;
                SymbolId lhs = 0;
                const CettaLpNativeSymbol *rhs = NULL;
                uint32_t rhs_len = 0;
                int32_t lhs_sym_idx;
                int32_t goto_edge_idx;
                CettaLpNativeParseValue next_value;

                slr_get_prod(productions, production_len, start_nt,
                             prod_idx, &lhs, &rhs, &rhs_len);
                if (!u32vec_copy(&next_stack, &cur->state_stack) ||
                    !parsevaluevec_copy(&next_values, &cur->value_stack)) {
                    free(next_stack.data);
                    free(next_values.data);
                    slr_summary_set_error(error_buf, error_buf_size,
                                          "failed to fork GLR forest reduce branch");
                    goto fail;
                }
                if (rhs_len > next_values.len || rhs_len >= next_stack.len) {
                    free(next_stack.data);
                    free(next_values.data);
                    continue;
                }

                if ((uint32_t)prod_idx >= grammar->production_len) {
                    CettaLpNativeParseValue *child = &next_values.data[next_values.len - 1];
                    int32_t parent_idx;
                    if (rhs_len != 1 || child->is_cert ||
                        child->forest_idx == UINT32_MAX) {
                        free(next_stack.data);
                        free(next_values.data);
                        slr_summary_set_error(error_buf, error_buf_size,
                                              "GLR forest leaf reduction expected one shifted token");
                        goto fail;
                    }
                    parent_idx = gll_sppf_get_node(&nodes, productions, production_len,
                                                   start_nt, prod_idx, 1,
                                                   CETTA_LP_NATIVE_NODE_NONE,
                                                   child->forest_idx,
                                                   error_buf, error_buf_size);
                    if (parent_idx < 0) {
                        free(next_stack.data);
                        free(next_values.data);
                        goto fail;
                    }
                    next_value.is_cert = true;
                    next_value.token_atom = NULL;
                    next_value.term_kind = 0;
                    next_value.start = child->start;
                    next_value.end = child->end;
                    next_value.forest_idx = (uint32_t)parent_idx;
                    next_value.cert = make_leaf_cert(arena, child->token_atom,
                                                     child->start, child->end);
                } else if (rhs_len == 0) {
                    int32_t sym_idx = gll_node_get_sym(&nodes, lhs, cur->pos, cur->pos);
                    int32_t eps_idx = gll_node_get_eps(&nodes, cur->pos);
                    Atom *eps = make_eps_cert(arena);
                    Atom *kids[1] = {eps};
                    if (sym_idx < 0 || eps_idx < 0 ||
                        !gll_packedvec_push_unique(&nodes.data[sym_idx].packed,
                                                   CETTA_LP_NATIVE_NODE_NONE,
                                                   (uint32_t)eps_idx,
                                                   cur->pos,
                                                   prod_idx)) {
                        free(next_stack.data);
                        free(next_values.data);
                        slr_summary_set_error(error_buf, error_buf_size,
                                              "failed to record GLR forest epsilon");
                        goto fail;
                    }
                    next_value.is_cert = true;
                    next_value.token_atom = NULL;
                    next_value.term_kind = 0;
                    next_value.start = cur->pos;
                    next_value.end = cur->pos;
                    next_value.forest_idx = (uint32_t)sym_idx;
                    next_value.cert = make_node_cert(
                        arena, grammar->productions[prod_idx].label, lhs,
                        cur->pos, cur->pos, kids, 1);
                } else {
                    uint32_t base = next_values.len - rhs_len;
                    uint32_t j;
                    uint32_t parent_idx = CETTA_LP_NATIVE_NODE_NONE;
                    Atom **kids = arena_alloc(arena, sizeof(Atom *) * rhs_len);
                    next_value.is_cert = true;
                    next_value.token_atom = NULL;
                    next_value.term_kind = 0;
                    next_value.start = next_values.data[base].start;
                    next_value.end = next_values.data[next_values.len - 1].end;
                    for (j = 0; j < rhs_len; j++) {
                        CettaLpNativeParseValue *part = &next_values.data[base + j];
                        int32_t next_parent;
                        if (part->forest_idx == UINT32_MAX) {
                            free(next_stack.data);
                            free(next_values.data);
                            slr_summary_set_error(error_buf, error_buf_size,
                                                  "GLR forest reduction missing child node");
                            goto fail;
                        }
                        if (rhs[j].kind == CETTA_LP_NATIVE_SYMBOL_TM) {
                            if (part->is_cert || part->term_kind != rhs[j].name) {
                                free(next_stack.data);
                                free(next_values.data);
                                slr_summary_set_error(error_buf, error_buf_size,
                                                      "GLR forest terminal reduction mismatch");
                                goto fail;
                            }
                            kids[j] = make_tok_cert(arena, rhs[j].name, part->start);
                        } else {
                            if (!part->is_cert || !part->cert) {
                                free(next_stack.data);
                                free(next_values.data);
                                slr_summary_set_error(error_buf, error_buf_size,
                                                      "GLR forest nonterminal reduction missing child cert");
                                goto fail;
                            }
                            kids[j] = part->cert;
                        }
                        next_parent = gll_sppf_get_node(&nodes, productions,
                                                        production_len, start_nt,
                                                        prod_idx, j + 1,
                                                        parent_idx,
                                                        part->forest_idx,
                                                        error_buf,
                                                        error_buf_size);
                        if (next_parent < 0) {
                            free(next_stack.data);
                            free(next_values.data);
                            goto fail;
                        }
                        parent_idx = (uint32_t)next_parent;
                    }
                    next_value.forest_idx = parent_idx;
                    next_value.cert = make_node_cert(
                        arena, grammar->productions[prod_idx].label, lhs,
                        next_value.start, next_value.end, kids, rhs_len);
                }

                next_values.len -= rhs_len;
                next_stack.len -= rhs_len;
                lhs_sym_idx = symbolvec_find(&symbols, false, lhs);
                if (lhs_sym_idx < 0 || next_stack.len == 0) {
                    free(next_stack.data);
                    free(next_values.data);
                    continue;
                }
                goto_edge_idx = edgevec_find(&edges,
                                             next_stack.data[next_stack.len - 1],
                                             (uint32_t)lhs_sym_idx);
                if (goto_edge_idx < 0 ||
                    !u32vec_push(&next_stack, edges.data[goto_edge_idx].target) ||
                    !parsevaluevec_push(&next_values, &next_value)) {
                    free(next_stack.data);
                    free(next_values.data);
                    slr_summary_set_error(error_buf, error_buf_size,
                                          "failed to advance GLR forest reduce branch");
                    goto fail;
                }
            } else {
                free(next_stack.data);
                free(next_values.data);
                slr_summary_set_error(error_buf, error_buf_size,
                                      "unknown GLR forest action kind");
                goto fail;
            }

            {
                int32_t next_idx = branchvec_find_with_values(
                    &configs, next_pos, &next_stack, &next_values);
                if (next_idx < 0) {
                    if (!branchvec_push_copy(&configs, next_pos, &next_stack, &next_values,
                                             (uint8_t)(delta > 2 ? 2 : delta), true) ||
                        !u32vec_push(&work, configs.len - 1)) {
                        free(next_stack.data);
                        free(next_values.data);
                        slr_summary_set_error(error_buf, error_buf_size,
                                              "failed to enqueue GLR forest branch");
                        goto fail;
                    }
                } else {
                    uint32_t grown = configs.data[next_idx].ways_total + delta;
                    if (grown > 2)
                        grown = 2;
                    if (grown > configs.data[next_idx].ways_total) {
                        configs.data[next_idx].ways_total = (uint8_t)grown;
                        if (!configs.data[next_idx].queued) {
                            configs.data[next_idx].queued = true;
                            if (!u32vec_push(&work, (uint32_t)next_idx)) {
                                free(next_stack.data);
                                free(next_values.data);
                                slr_summary_set_error(error_buf, error_buf_size,
                                                      "failed to schedule GLR forest branch");
                                goto fail;
                            }
                        }
                    }
                }
            }
            free(next_stack.data);
            free(next_values.data);
        }
    }

    if (accept_count == 1)
        status = "Unique";
    else if (accept_count > 1)
        status = "Ambiguous";

    if (output == CETTA_LP_NATIVE_FOREST_OUT_DATA) {
        result = gll_forest_data_atom(arena, status, tokens.len, accept_count,
                                      accept_root, &nodes);
    } else if (output == CETTA_LP_NATIVE_FOREST_OUT_SIGNATURE) {
        result = gll_forest_signature_atom(arena, status, tokens.len,
                                           accept_count, accept_root, &nodes);
        if (!result) {
            slr_summary_set_error(error_buf, error_buf_size,
                                  "failed to build GLR forest signature");
            goto fail;
        }
    } else if (output == CETTA_LP_NATIVE_FOREST_OUT_SIGNATURE_DIGEST) {
        result = gll_forest_signature_digest_atom(arena, status, tokens.len,
                                                  accept_count, accept_root, &nodes);
        if (!result) {
            slr_summary_set_error(error_buf, error_buf_size,
                                  "failed to build GLR forest signature digest");
            goto fail;
        }
    } else {
        result = glr_forest_summary_atom(arena, status, tokens.len, &nodes,
                                         configs.len, accept_count);
    }

    if (first && follow) {
        for (i = 0; i < nonterminals.len; i++) {
            bitset_free(&first[i]);
            bitset_free(&follow[i]);
        }
    }
    free(tokens.data);
    free(work.data);
    branchvec_free(&configs);
    gll_nodevec_free(&nodes);
    free(nullable);
    free(first);
    free(follow);
    free(nonterminals.data);
    free(terminals.data);
    free(symbols.data);
    free(edges.data);
    free(actions.data);
    statevec_free(&states);
    slr_productions_free(productions, production_len);
    return result;

fail:
    if (first && follow) {
        for (i = 0; i < nonterminals.len; i++) {
            bitset_free(&first[i]);
            bitset_free(&follow[i]);
        }
    }
    free(tokens.data);
    free(work.data);
    branchvec_free(&configs);
    gll_nodevec_free(&nodes);
    free(nullable);
    free(first);
    free(follow);
    free(nonterminals.data);
    free(terminals.data);
    free(symbols.data);
    free(edges.data);
    free(actions.data);
    statevec_free(&states);
    slr_productions_free(productions, production_len);
    return NULL;
}

Atom *cetta_lp_native_glr_forest_summary(const CettaLpNativeGrammar *grammar,
                                         SymbolId start_nt,
                                         Atom *token_list,
                                         Arena *arena,
                                         char *error_buf,
                                         size_t error_buf_size) {
    return cetta_lp_native_glr_forest_result(grammar, start_nt, token_list,
                                             arena, error_buf, error_buf_size,
                                             CETTA_LP_NATIVE_FOREST_OUT_SUMMARY);
}

Atom *cetta_lp_native_glr_forest_signature(const CettaLpNativeGrammar *grammar,
                                           SymbolId start_nt,
                                           Atom *token_list,
                                           Arena *arena,
                                           char *error_buf,
                                           size_t error_buf_size) {
    return cetta_lp_native_glr_forest_result(grammar, start_nt, token_list,
                                             arena, error_buf, error_buf_size,
                                             CETTA_LP_NATIVE_FOREST_OUT_SIGNATURE);
}

Atom *cetta_lp_native_glr_forest_signature_digest(const CettaLpNativeGrammar *grammar,
                                                  SymbolId start_nt,
                                                  Atom *token_list,
                                                  Arena *arena,
                                                  char *error_buf,
                                                  size_t error_buf_size) {
    return cetta_lp_native_glr_forest_result(
        grammar, start_nt, token_list, arena, error_buf, error_buf_size,
        CETTA_LP_NATIVE_FOREST_OUT_SIGNATURE_DIGEST);
}

Atom *cetta_lp_native_glr_forest_data(const CettaLpNativeGrammar *grammar,
                                      SymbolId start_nt,
                                      Atom *token_list,
                                      Arena *arena,
                                      char *error_buf,
                                      size_t error_buf_size) {
    return cetta_lp_native_glr_forest_result(grammar, start_nt, token_list,
                                             arena, error_buf, error_buf_size,
                                             CETTA_LP_NATIVE_FOREST_OUT_DATA);
}
