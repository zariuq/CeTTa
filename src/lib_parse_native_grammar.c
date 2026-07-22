#include "lib_parse_native_grammar.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "parser.h"

#ifndef CETTA_LP_NATIVE_GLL_DISABLE_INDEX
#define CETTA_LP_NATIVE_GLL_DISABLE_INDEX 0
#endif

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
    free(grammar->entries);
    memset(grammar, 0, sizeof(*grammar));
}

static bool grammar_entry_push(CettaLpNativeGrammar *grammar,
                               uint32_t *entry_cap,
                               CettaLpNativeEntryKind kind,
                               uint32_t index) {
    CettaLpNativeEntry *entry;

    if (!grow_storage((void **)&grammar->entries, &grammar->entry_len,
                      entry_cap, sizeof(*grammar->entries))) {
        return false;
    }
    entry = &grammar->entries[grammar->entry_len++];
    entry->kind = kind;
    entry->index = index;
    return true;
}

static bool parse_rhs(Atom *rhs_expr,
                      CettaLpNativeProduction *prod,
                      uint32_t *binder_hole_count,
                      char *error_buf,
                      size_t error_buf_size) {
    Atom **items = NULL;
    bool owns_items = false;
    uint32_t i;

    if (!rhs_expr) {
        grammar_set_error(error_buf, error_buf_size,
                          "Prod rhs must be an expression list");
        return false;
    }
    if (atom_is_symbol(rhs_expr, "Nil")) {
        prod->rhs_len = 0;
    } else if (atom_expr_head_is(rhs_expr, "Cons") &&
               rhs_expr->expr.len == 3) {
        Atom *cursor = rhs_expr;
        uint32_t count = 0;

        while (atom_expr_head_is(cursor, "Cons") &&
               cursor->expr.len == 3) {
            if (count == UINT32_MAX) {
                grammar_set_error(error_buf, error_buf_size,
                                  "Prod rhs is too long");
                return false;
            }
            count++;
            cursor = cursor->expr.elems[2];
        }
        if (!atom_is_symbol(cursor, "Nil")) {
            grammar_set_error(error_buf, error_buf_size,
                              "Prod rhs must be a proper Cons/Nil list");
            return false;
        }
        prod->rhs_len = count;
        if (count > 0) {
            cursor = rhs_expr;
            items = cetta_malloc(sizeof(*items) * count);
            owns_items = true;
            for (i = 0; i < count; i++) {
                items[i] = cursor->expr.elems[1];
                cursor = cursor->expr.elems[2];
            }
        }
    } else if (rhs_expr->kind == ATOM_EXPR) {
        if ((uint64_t)rhs_expr->expr.len > UINT32_MAX) {
            grammar_set_error(error_buf, error_buf_size,
                              "Prod rhs is too long");
            return false;
        }
        prod->rhs_len = (uint32_t)rhs_expr->expr.len;
        items = rhs_expr->expr.elems;
    } else {
        grammar_set_error(error_buf, error_buf_size,
                          "Prod rhs must be an expression list");
        return false;
    }
    if (prod->rhs_len == 0) {
        prod->rhs = NULL;
        return true;
    }
    prod->rhs = cetta_malloc(sizeof(CettaLpNativeSymbol) * prod->rhs_len);
    for (i = 0; i < prod->rhs_len; i++) {
        Atom *item = items[i];
        CettaLpNativeSymbol *slot = &prod->rhs[i];
        memset(slot, 0, sizeof(*slot));
        if (atom_expr_head_is(item, "Tm") && item->expr.len == 2) {
            slot->kind = CETTA_LP_NATIVE_SYMBOL_TM;
            if (!atom_to_symbol_id(item->expr.elems[1], &slot->name)) {
                grammar_set_error(error_buf, error_buf_size,
                                  "Tm expects a symbol terminal");
                free(owns_items ? items : NULL);
                return false;
            }
            continue;
        }
        if (atom_expr_head_is(item, "Hl") && item->expr.len == 2) {
            slot->kind = CETTA_LP_NATIVE_SYMBOL_HL;
            if (!atom_to_symbol_id(item->expr.elems[1], &slot->name)) {
                grammar_set_error(error_buf, error_buf_size,
                                  "Hl expects a symbol nonterminal");
                free(owns_items ? items : NULL);
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
                free(owns_items ? items : NULL);
                return false;
            }
            (*binder_hole_count)++;
            continue;
        }
        grammar_set_error(error_buf, error_buf_size,
                          "unsupported mkGram rhs item");
        free(owns_items ? items : NULL);
        return false;
    }
    free(owns_items ? items : NULL);
    return true;
}

static bool parse_entry(CettaLpNativeGrammar *grammar,
                        Atom *entry,
                        uint32_t *prod_cap,
                        uint32_t *var_cap,
                        uint32_t *lex_cap,
                        uint32_t *entry_cap,
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
        return grammar_entry_push(grammar, entry_cap,
                                  CETTA_LP_NATIVE_ENTRY_LEX,
                                  grammar->lex_len - 1u);
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
        return grammar_entry_push(grammar, entry_cap,
                                  CETTA_LP_NATIVE_ENTRY_VAR,
                                  grammar->var_len - 1u);
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
        return grammar_entry_push(grammar, entry_cap,
                                  CETTA_LP_NATIVE_ENTRY_PRODUCTION,
                                  grammar->production_len - 1u);
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
    uint32_t entry_cap = 0;
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
                         &prod_cap, &var_cap, &lex_cap, &entry_cap,
                         error_buf, error_buf_size)) {
            cetta_lp_native_grammar_free(out);
            return false;
        }
    }
    return true;
}

bool cetta_lp_native_grammar_load_list(CettaLpNativeGrammar *out,
                                       Atom *grammar_list,
                                       char *error_buf,
                                       size_t error_buf_size) {
    uint32_t prod_cap = 0;
    uint32_t var_cap = 0;
    uint32_t lex_cap = 0;
    uint32_t entry_cap = 0;
    Atom *cursor;

    if (!out || !grammar_list) {
        grammar_set_error(error_buf, error_buf_size,
                          "bad grammar list loader args");
        return false;
    }

    cetta_lp_native_grammar_free(out);
    cetta_lp_native_grammar_init(out);
    cursor = grammar_list;
    while (!atom_is_symbol(cursor, "Nil")) {
        Atom *entry;

        if (!atom_expr_head_is(cursor, "Cons") || cursor->expr.len != 3) {
            grammar_set_error(error_buf, error_buf_size,
                              "grammar must be a proper Cons/Nil list");
            cetta_lp_native_grammar_free(out);
            return false;
        }
        entry = cursor->expr.elems[1];
        if (!parse_entry(out, entry, &prod_cap, &var_cap, &lex_cap,
                         &entry_cap, error_buf, error_buf_size)) {
            cetta_lp_native_grammar_free(out);
            return false;
        }
        cursor = cursor->expr.elems[2];
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
        free(forms);
        arena_free(&arena);
        return false;
    }
    ok = cetta_lp_native_grammar_load_forms(out, forms, form_count, def_name,
                                            error_buf, error_buf_size);
    free(forms);
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

typedef struct {
    SymbolId start_nt;
    SymbolId *production_labels;
    uint32_t grammar_production_len;
    CettaLpNativeSlrProduction *productions;
    uint32_t production_len;
    CettaLpNativeIdVec nonterminals;
    CettaLpNativeIdVec terminals;
    CettaLpNativeSymbolVec symbols;
    CettaLpNativeEdgeVec edges;
    CettaLpNativeActionVec actions;
    CettaLpNativeSlrSummary summary;
} CettaLpNativeSlrPreparedImpl;

static void slr_prepared_impl_free(
    CettaLpNativeSlrPreparedImpl *prepared) {
    if (!prepared)
        return;
    free(prepared->nonterminals.data);
    free(prepared->terminals.data);
    free(prepared->symbols.data);
    free(prepared->edges.data);
    free(prepared->actions.data);
    free(prepared->production_labels);
    slr_productions_free(
        prepared->productions, prepared->production_len);
    memset(prepared, 0, sizeof(*prepared));
}

static bool slr_prepared_impl_build(
    CettaLpNativeSlrPreparedImpl *prepared,
    const CettaLpNativeGrammar *grammar,
    SymbolId start_nt,
    char *error_buf,
    size_t error_buf_size) {
    bool *nullable = NULL;
    CettaLpNativeBitset *first = NULL;
    CettaLpNativeBitset *follow = NULL;
    CettaLpNativeStateVec states = {0};
    uint32_t accept_len = 0u;
    uint32_t conflict_len = 0u;
    uint32_t goto_len = 0u;
    uint32_t index;
    bool ok = false;

    if (!prepared || !grammar) {
        slr_summary_set_error(error_buf, error_buf_size,
                              "bad native SLR preparation arguments");
        return false;
    }
    memset(prepared, 0, sizeof(*prepared));
    prepared->start_nt = start_nt;
    prepared->grammar_production_len = grammar->production_len;
    if (grammar->production_len > 0u) {
        prepared->production_labels = cetta_malloc(
            sizeof(*prepared->production_labels) *
                grammar->production_len);
        for (index = 0u; index < grammar->production_len; index++) {
            prepared->production_labels[index] =
                grammar->productions[index].label;
        }
    }
    if (!slr_build_productions(
            grammar, &prepared->productions, &prepared->production_len,
            error_buf, error_buf_size) ||
        !slr_collect_symbol_sets(
            prepared->productions, prepared->production_len, start_nt,
            &prepared->nonterminals, &prepared->terminals,
            error_buf, error_buf_size)) {
        goto done;
    }
    if (!slr_grammar_mentions_nonterminal(
            prepared->productions, prepared->production_len, start_nt)) {
        slr_summary_set_error(error_buf, error_buf_size,
                              "start nonterminal not present in grammar");
        goto done;
    }
    if (!slr_compute_nullable_first_follow(
            prepared->productions, prepared->production_len,
            &prepared->nonterminals, &prepared->terminals, start_nt,
            &nullable, &first, &follow,
            error_buf, error_buf_size) ||
        !slr_build_states(
            prepared->productions, prepared->production_len, start_nt,
            &states, &prepared->symbols, &prepared->edges,
            error_buf, error_buf_size)) {
        goto done;
    }

    for (index = 0u; index < prepared->edges.len; index++) {
        if (!prepared->symbols.data[
                prepared->edges.data[index].symbol].is_terminal) {
            goto_len++;
        }
    }
    for (index = 0u; index < states.len; index++) {
        uint32_t item_index;
        for (item_index = 0u;
             item_index < states.data[index].len;
             item_index++) {
            SymbolId lhs = 0u;
            const CettaLpNativeSymbol *rhs = NULL;
            uint32_t rhs_len = 0u;
            CettaLpNativeItem item = states.data[index].items[item_index];

            slr_get_prod(
                prepared->productions, prepared->production_len, start_nt,
                item.prod_idx, &lhs, &rhs, &rhs_len);
            if (rhs && item.dot < rhs_len) {
                if (rhs[item.dot].kind == CETTA_LP_NATIVE_SYMBOL_TM) {
                    int32_t symbol_index = symbolvec_find(
                        &prepared->symbols, true, rhs[item.dot].name);
                    int32_t terminal_index = idvec_find(
                        &prepared->terminals, rhs[item.dot].name);
                    int32_t edge_index = -1;

                    if (symbol_index >= 0 && terminal_index >= 0) {
                        edge_index = edgevec_find(
                            &prepared->edges, index,
                            (uint32_t)symbol_index);
                    }
                    if (edge_index >= 0 &&
                        !actionvec_set(
                            &prepared->actions, index,
                            (uint32_t)terminal_index, 's',
                            (int32_t)prepared->edges.data[edge_index].target,
                            &conflict_len)) {
                        slr_summary_set_error(
                            error_buf, error_buf_size,
                            "failed to record shift action");
                        goto done;
                    }
                }
                continue;
            }
            if (item.prod_idx == -1) {
                accept_len++;
                if (!actionvec_set(
                        &prepared->actions, index,
                        prepared->terminals.len, 'a', 0,
                        &conflict_len)) {
                    slr_summary_set_error(
                        error_buf, error_buf_size,
                        "failed to record accept action");
                    goto done;
                }
                continue;
            }
            {
                int32_t lhs_index = idvec_find(
                    &prepared->nonterminals, lhs);
                uint32_t token_index;
                if (lhs_index < 0) {
                    slr_summary_set_error(
                        error_buf, error_buf_size,
                        "reduce lhs missing from follow set");
                    goto done;
                }
                for (token_index = 0u;
                     token_index <= prepared->terminals.len;
                     token_index++) {
                    if (!bitset_test(&follow[lhs_index], token_index))
                        continue;
                    if (!actionvec_set(
                            &prepared->actions, index, token_index, 'r',
                            item.prod_idx, &conflict_len)) {
                        slr_summary_set_error(
                            error_buf, error_buf_size,
                            "failed to record reduce action");
                        goto done;
                    }
                }
            }
        }
    }

    prepared->summary.state_len = states.len;
    prepared->summary.goto_len = goto_len;
    prepared->summary.accept_len = accept_len;
    prepared->summary.conflict_len = conflict_len;
    for (index = 0u; index < prepared->actions.len; index++) {
        if (prepared->actions.data[index].kind == 's')
            prepared->summary.shift_len++;
        else if (prepared->actions.data[index].kind == 'r')
            prepared->summary.reduce_len++;
    }
    ok = true;

done:
    if (first) {
        for (index = 0u; index < prepared->nonterminals.len; index++)
            bitset_free(&first[index]);
    }
    if (follow) {
        for (index = 0u; index < prepared->nonterminals.len; index++)
            bitset_free(&follow[index]);
    }
    free(nullable);
    free(first);
    free(follow);
    statevec_free(&states);
    if (!ok)
        slr_prepared_impl_free(prepared);
    return ok;
}

void cetta_lp_native_slr_prepared_init(
    CettaLpNativeSlrPrepared *prepared) {
    if (prepared)
        prepared->implementation = NULL;
}

void cetta_lp_native_slr_prepared_free(
    CettaLpNativeSlrPrepared *prepared) {
    CettaLpNativeSlrPreparedImpl *implementation;

    if (!prepared)
        return;
    implementation = prepared->implementation;
    if (implementation) {
        slr_prepared_impl_free(implementation);
        free(implementation);
    }
    prepared->implementation = NULL;
}

bool cetta_lp_native_slr_prepare(
    CettaLpNativeSlrPrepared *prepared,
    const CettaLpNativeGrammar *grammar,
    uint32_t start_nt,
    char *error_buf,
    size_t error_buf_size) {
    CettaLpNativeSlrPreparedImpl *implementation;

    if (error_buf && error_buf_size > 0u)
        error_buf[0] = '\0';
    if (!prepared || !grammar) {
        slr_summary_set_error(error_buf, error_buf_size,
                              "bad native SLR preparation arguments");
        return false;
    }
    implementation = cetta_malloc(sizeof(*implementation));
    if (!slr_prepared_impl_build(
            implementation, grammar, start_nt,
            error_buf, error_buf_size)) {
        free(implementation);
        return false;
    }
    if (implementation->summary.conflict_len > 0u) {
        slr_summary_set_error(error_buf, error_buf_size,
                              "SLR table has conflicts");
        slr_prepared_impl_free(implementation);
        free(implementation);
        return false;
    }
    cetta_lp_native_slr_prepared_free(prepared);
    prepared->implementation = implementation;
    return true;
}

bool cetta_lp_native_slr_prepared_summary(
    const CettaLpNativeSlrPrepared *prepared,
    CettaLpNativeSlrSummary *out,
    char *error_buf,
    size_t error_buf_size) {
    const CettaLpNativeSlrPreparedImpl *implementation =
        prepared ? prepared->implementation : NULL;

    if (error_buf && error_buf_size > 0u)
        error_buf[0] = '\0';
    if (!implementation || !out) {
        slr_summary_set_error(
            error_buf, error_buf_size,
            "bad prepared SLR summary arguments");
        return false;
    }
    *out = implementation->summary;
    return true;
}

void cetta_lp_native_slr_program_init(
    CettaLpNativeSlrProgram *program) {
    if (program)
        memset(program, 0, sizeof(*program));
}

void cetta_lp_native_slr_program_free(
    CettaLpNativeSlrProgram *program) {
    if (!program)
        return;
    free(program->terminals);
    free(program->nonterminals);
    free(program->productions);
    free(program->rhs);
    free(program->actions);
    free(program->gotos);
    memset(program, 0, sizeof(*program));
}

static bool slr_program_product_u32(
    uint32_t left, uint32_t right, uint32_t *out) {
    if (!out || (left > 0u && right > UINT32_MAX / left))
        return false;
    *out = left * right;
    return true;
}

static bool slr_program_id_unique(
    const SymbolId *ids, uint32_t len) {
    uint32_t left;

    if (len > 0u && !ids)
        return false;
    for (left = 0u; left < len; left++) {
        uint32_t right;
        for (right = left + 1u; right < len; right++) {
            if (ids[left] == ids[right])
                return false;
        }
    }
    return true;
}

static int32_t slr_program_id_find(
    const SymbolId *ids, uint32_t len, SymbolId value) {
    uint32_t index;

    for (index = 0u; index < len; index++) {
        if (ids[index] == value)
            return (int32_t)index;
    }
    return -1;
}

bool cetta_lp_native_slr_program_validate(
    const CettaLpNativeSlrProgram *program,
    char *error_buf,
    size_t error_buf_size) {
    uint32_t expected_actions;
    uint32_t expected_gotos;
    uint32_t rhs_end = 0u;
    uint32_t shift_len = 0u;
    uint32_t reduce_len = 0u;
    uint32_t accept_len = 0u;
    uint32_t goto_len = 0u;
    uint32_t index;

    if (error_buf && error_buf_size > 0u)
        error_buf[0] = '\0';
    if (!program || program->summary.state_len == 0u ||
        program->terminal_len == UINT32_MAX ||
        program->production_len == 0u ||
        program->authored_production_len > program->production_len ||
        !slr_program_product_u32(
            program->summary.state_len, program->terminal_len + 1u,
            &expected_actions) ||
        !slr_program_product_u32(
            program->summary.state_len, program->nonterminal_len,
            &expected_gotos) ||
        program->action_len != expected_actions ||
        program->goto_len != expected_gotos ||
        !slr_program_id_unique(program->terminals, program->terminal_len) ||
        !slr_program_id_unique(
            program->nonterminals, program->nonterminal_len) ||
        slr_program_id_find(
            program->nonterminals, program->nonterminal_len,
            program->start_nonterminal) < 0 ||
        !program->productions || !program->actions ||
        (program->rhs_len > 0u && !program->rhs) ||
        (program->goto_len > 0u && !program->gotos)) {
        slr_summary_set_error(error_buf, error_buf_size,
                              "bad native SLR program");
        return false;
    }
    for (index = 0u; index < program->production_len; index++) {
        const CettaLpNativeSlrProgramProduction *production =
            &program->productions[index];
        uint32_t rhs_index;

        if (rhs_end > program->rhs_len ||
            production->rhs_begin != rhs_end ||
            production->rhs_len > program->rhs_len - rhs_end ||
            production->authored !=
                (index < program->authored_production_len) ||
            (!production->authored && production->label != UINT32_MAX) ||
            slr_program_id_find(
                program->nonterminals, program->nonterminal_len,
                production->lhs) < 0) {
            slr_summary_set_error(error_buf, error_buf_size,
                                  "native SLR program production is malformed");
            return false;
        }
        for (rhs_index = 0u; rhs_index < production->rhs_len;
             rhs_index++) {
            const CettaLpNativeSymbol *symbol =
                &program->rhs[production->rhs_begin + rhs_index];
            bool valid = symbol->kind == CETTA_LP_NATIVE_SYMBOL_TM
                ? slr_program_id_find(
                      program->terminals, program->terminal_len,
                      symbol->name) >= 0
                : slr_program_id_find(
                      program->nonterminals, program->nonterminal_len,
                      symbol->name) >= 0;
            if (!valid) {
                slr_summary_set_error(
                    error_buf, error_buf_size,
                    "native SLR program rhs escaped its symbol sets");
                return false;
            }
        }
        rhs_end += production->rhs_len;
    }
    if (rhs_end != program->rhs_len) {
        slr_summary_set_error(error_buf, error_buf_size,
                              "native SLR program has trailing rhs storage");
        return false;
    }
    for (index = 0u; index < program->action_len; index++) {
        const CettaLpNativeSlrProgramAction *action =
            &program->actions[index];
        uint32_t column = index % (program->terminal_len + 1u);
        switch (action->kind) {
        case CETTA_LP_NATIVE_SLR_PROGRAM_ERROR:
            if (action->value != 0)
                goto invalid_action;
            break;
        case CETTA_LP_NATIVE_SLR_PROGRAM_SHIFT:
            if (action->value < 0 ||
                column == program->terminal_len ||
                (uint32_t)action->value >= program->summary.state_len) {
                goto invalid_action;
            }
            shift_len++;
            break;
        case CETTA_LP_NATIVE_SLR_PROGRAM_REDUCE:
            if (action->value < 0 ||
                (uint32_t)action->value >= program->production_len) {
                goto invalid_action;
            }
            reduce_len++;
            break;
        case CETTA_LP_NATIVE_SLR_PROGRAM_ACCEPT:
            if (action->value != 0 || column != program->terminal_len)
                goto invalid_action;
            accept_len++;
            break;
        default:
            goto invalid_action;
        }
    }
    for (index = 0u; index < program->goto_len; index++) {
        if (program->gotos[index] == UINT32_MAX)
            continue;
        if (program->gotos[index] >= program->summary.state_len) {
            slr_summary_set_error(error_buf, error_buf_size,
                                  "native SLR program goto is invalid");
            return false;
        }
        goto_len++;
    }
    if (shift_len != program->summary.shift_len ||
        reduce_len != program->summary.reduce_len ||
        accept_len != program->summary.accept_len ||
        goto_len != program->summary.goto_len ||
        program->summary.conflict_len != 0u) {
        slr_summary_set_error(error_buf, error_buf_size,
                              "native SLR program summary changed");
        return false;
    }
    return true;

invalid_action:
    slr_summary_set_error(error_buf, error_buf_size,
                          "native SLR program action is invalid");
    return false;
}

bool cetta_lp_native_slr_prepared_export_program(
    const CettaLpNativeSlrPrepared *prepared_owner,
    CettaLpNativeSlrProgram *out,
    char *error_buf,
    size_t error_buf_size) {
    const CettaLpNativeSlrPreparedImpl *prepared = prepared_owner
        ? prepared_owner->implementation : NULL;
    CettaLpNativeSlrProgram result;
    uint32_t action_columns;
    uint32_t rhs_write = 0u;
    uint32_t index;
    bool ok = false;

    cetta_lp_native_slr_program_init(&result);
    if (error_buf && error_buf_size > 0u)
        error_buf[0] = '\0';
    if (!prepared || !out || prepared->summary.conflict_len != 0u ||
        prepared->summary.state_len == 0u ||
        prepared->production_len == 0u ||
        prepared->grammar_production_len > prepared->production_len ||
        (prepared->grammar_production_len > 0u &&
         !prepared->production_labels) ||
        prepared->terminals.len == UINT32_MAX) {
        slr_summary_set_error(error_buf, error_buf_size,
                              "bad prepared SLR program export");
        goto done;
    }
    result.start_nonterminal = prepared->start_nt;
    result.terminal_len = prepared->terminals.len;
    result.nonterminal_len = prepared->nonterminals.len;
    result.production_len = prepared->production_len;
    result.authored_production_len = prepared->grammar_production_len;
    result.summary = prepared->summary;
    for (index = 0u; index < prepared->production_len; index++) {
        if (prepared->productions[index].rhs_len >
            UINT32_MAX - result.rhs_len) {
            slr_summary_set_error(error_buf, error_buf_size,
                                  "prepared SLR rhs size overflow");
            goto done;
        }
        result.rhs_len += prepared->productions[index].rhs_len;
    }
    action_columns = result.terminal_len + 1u;
    if (!slr_program_product_u32(
            result.summary.state_len, action_columns,
            &result.action_len) ||
        !slr_program_product_u32(
            result.summary.state_len, result.nonterminal_len,
            &result.goto_len) ||
        (size_t)result.terminal_len >
            SIZE_MAX / sizeof(*result.terminals) ||
        (size_t)result.nonterminal_len >
            SIZE_MAX / sizeof(*result.nonterminals) ||
        (size_t)result.production_len >
            SIZE_MAX / sizeof(*result.productions) ||
        (size_t)result.rhs_len > SIZE_MAX / sizeof(*result.rhs) ||
        (size_t)result.action_len > SIZE_MAX / sizeof(*result.actions) ||
        (size_t)result.goto_len > SIZE_MAX / sizeof(*result.gotos)) {
        slr_summary_set_error(error_buf, error_buf_size,
                              "prepared SLR program allocation overflow");
        goto done;
    }
    result.terminals = calloc(
        result.terminal_len ? result.terminal_len : 1u,
        sizeof(*result.terminals));
    result.nonterminals = calloc(
        result.nonterminal_len ? result.nonterminal_len : 1u,
        sizeof(*result.nonterminals));
    result.productions = calloc(
        result.production_len, sizeof(*result.productions));
    result.rhs = calloc(
        result.rhs_len ? result.rhs_len : 1u, sizeof(*result.rhs));
    result.actions = calloc(
        result.action_len, sizeof(*result.actions));
    result.gotos = malloc(
        sizeof(*result.gotos) *
        (size_t)(result.goto_len ? result.goto_len : 1u));
    if (!result.terminals || !result.nonterminals ||
        !result.productions || !result.rhs || !result.actions ||
        !result.gotos) {
        slr_summary_set_error(error_buf, error_buf_size,
                              "failed to allocate prepared SLR program");
        goto done;
    }
    if (result.terminal_len > 0u) {
        memcpy(result.terminals, prepared->terminals.data,
               sizeof(*result.terminals) * result.terminal_len);
    }
    if (result.nonterminal_len > 0u) {
        memcpy(result.nonterminals, prepared->nonterminals.data,
               sizeof(*result.nonterminals) * result.nonterminal_len);
    }
    for (index = 0u; index < result.goto_len; index++)
        result.gotos[index] = UINT32_MAX;
    for (index = 0u; index < result.production_len; index++) {
        const CettaLpNativeSlrProduction *source =
            &prepared->productions[index];
        CettaLpNativeSlrProgramProduction *target =
            &result.productions[index];

        target->label = index < result.authored_production_len
            ? prepared->production_labels[index] : UINT32_MAX;
        target->lhs = source->lhs;
        target->rhs_begin = rhs_write;
        target->rhs_len = source->rhs_len;
        target->authored = index < result.authored_production_len;
        if (source->rhs_len > 0u) {
            memcpy(&result.rhs[rhs_write], source->rhs,
                   sizeof(*result.rhs) * source->rhs_len);
            rhs_write += source->rhs_len;
        }
    }
    for (index = 0u; index < prepared->actions.len; index++) {
        const CettaLpNativeAction *source =
            &prepared->actions.data[index];
        CettaLpNativeSlrProgramAction *target;
        uint32_t offset;

        if (source->state >= result.summary.state_len ||
            source->token_idx > result.terminal_len) {
            slr_summary_set_error(error_buf, error_buf_size,
                                  "prepared SLR action escaped its table");
            goto done;
        }
        offset = source->state * action_columns + source->token_idx;
        target = &result.actions[offset];
        if (target->kind != CETTA_LP_NATIVE_SLR_PROGRAM_ERROR) {
            slr_summary_set_error(error_buf, error_buf_size,
                                  "prepared SLR action is duplicated");
            goto done;
        }
        target->kind = source->kind == 's'
            ? CETTA_LP_NATIVE_SLR_PROGRAM_SHIFT
            : source->kind == 'r'
                ? CETTA_LP_NATIVE_SLR_PROGRAM_REDUCE
                : source->kind == 'a'
                    ? CETTA_LP_NATIVE_SLR_PROGRAM_ACCEPT
                    : CETTA_LP_NATIVE_SLR_PROGRAM_ERROR;
        target->value = source->value;
        if (target->kind == CETTA_LP_NATIVE_SLR_PROGRAM_ERROR) {
            slr_summary_set_error(error_buf, error_buf_size,
                                  "prepared SLR action kind is unknown");
            goto done;
        }
    }
    for (index = 0u; index < prepared->edges.len; index++) {
        const CettaLpNativeEdge *edge = &prepared->edges.data[index];
        const CettaLpNativeTransitionSymbol *symbol;
        int32_t nonterminal;
        uint32_t offset;

        if (edge->state >= result.summary.state_len ||
            edge->symbol >= prepared->symbols.len ||
            edge->target >= result.summary.state_len) {
            slr_summary_set_error(error_buf, error_buf_size,
                                  "prepared SLR goto escaped its table");
            goto done;
        }
        symbol = &prepared->symbols.data[edge->symbol];
        if (symbol->is_terminal)
            continue;
        nonterminal = slr_program_id_find(
            result.nonterminals, result.nonterminal_len, symbol->name);
        if (nonterminal < 0) {
            slr_summary_set_error(error_buf, error_buf_size,
                                  "prepared SLR goto lost its nonterminal");
            goto done;
        }
        offset = edge->state * result.nonterminal_len +
            (uint32_t)nonterminal;
        if (result.gotos[offset] != UINT32_MAX &&
            result.gotos[offset] != edge->target) {
            slr_summary_set_error(error_buf, error_buf_size,
                                  "prepared SLR goto is conflicting");
            goto done;
        }
        result.gotos[offset] = edge->target;
    }
    if (rhs_write != result.rhs_len ||
        !cetta_lp_native_slr_program_validate(
            &result, error_buf, error_buf_size)) {
        goto done;
    }
    cetta_lp_native_slr_program_free(out);
    *out = result;
    memset(&result, 0, sizeof(result));
    ok = true;

done:
    cetta_lp_native_slr_program_free(&result);
    return ok;
}

bool cetta_lp_native_slr_summary(const CettaLpNativeGrammar *grammar,
                                 SymbolId start_nt,
                                 CettaLpNativeSlrSummary *out,
                                 char *error_buf,
                                 size_t error_buf_size) {
    CettaLpNativeSlrPreparedImpl prepared;

    if (!out || !grammar) {
        slr_summary_set_error(error_buf, error_buf_size,
                              "bad SLR summary args");
        return false;
    }
    memset(out, 0, sizeof(*out));
    if (!slr_prepared_impl_build(&prepared, grammar, start_nt,
                                 error_buf, error_buf_size)) {
        return false;
    }
    *out = prepared.summary;
    slr_prepared_impl_free(&prepared);
    return true;
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

static char *input_token_file_read_line(FILE *fp, size_t *out_len) {
    char *buf = NULL;
    size_t len = 0;
    size_t cap = 0;
    int ch;

    while ((ch = fgetc(fp)) != EOF) {
        if (len + 1u >= cap) {
            size_t next_cap = cap ? cap * 2u : 128u;
            if (next_cap <= len + 1u)
                next_cap = len + 2u;
            buf = cetta_realloc(buf, next_cap);
            cap = next_cap;
        }
        if (ch == '\n')
            break;
        buf[len++] = (char)ch;
    }
    if (!buf && ch == EOF)
        return NULL;
    if (len + 1u >= cap) {
        size_t next_cap = cap ? cap + 1u : 1u;
        buf = cetta_realloc(buf, next_cap);
        cap = next_cap;
    }
    if (len > 0 && buf[len - 1u] == '\r')
        len--;
    buf[len] = '\0';
    if (out_len)
        *out_len = len;
    return buf;
}

static char *input_token_file_take_field(char **cursor) {
    char *start;
    char *tab;

    if (!cursor || !*cursor)
        return NULL;
    start = *cursor;
    tab = strchr(start, '\t');
    if (!tab) {
        *cursor = NULL;
        return start;
    }
    *tab = '\0';
    *cursor = tab + 1;
    return start;
}

static bool input_tokens_from_file(Arena *arena,
                                   const char *filename,
                                   CettaLpNativeInputTokenVec *out,
                                   char *error_buf,
                                   size_t error_buf_size) {
    FILE *fp;
    char *line;
    size_t line_len;
    uint32_t line_no = 0;

    if (!arena || !filename || !out) {
        slr_summary_set_error(error_buf, error_buf_size,
                              "bad token-file args");
        return false;
    }
    memset(out, 0, sizeof(*out));
    fp = fopen(filename, "rb");
    if (!fp) {
        slr_summary_set_error(error_buf, error_buf_size,
                              "failed to open token file");
        return false;
    }
    while ((line = input_token_file_read_line(fp, &line_len)) != NULL) {
        char *cursor = line;
        char *kind;

        line_no++;
        if (line_len == 0 || line[0] == '#') {
            free(line);
            continue;
        }
        kind = input_token_file_take_field(&cursor);
        if (kind && strcmp(kind, "sym") == 0) {
            Atom *tok;
            if (!cursor || cursor[0] == '\0' || strchr(cursor, '\t')) {
                slr_summary_set_error(error_buf, error_buf_size,
                                      "bad sym token-file line %u", line_no);
                free(line);
                goto fail;
            }
            tok = atom_symbol(arena, cursor);
            if (!input_tokenvec_push(out, NULL, tok->sym_id, out->len)) {
                slr_summary_set_error(error_buf, error_buf_size,
                                      "failed to store sym token-file line %u", line_no);
                free(line);
                goto fail;
            }
        } else if (kind && (strcmp(kind, "lex") == 0 ||
                            strcmp(kind, "lex-text") == 0)) {
            char *klass = input_token_file_take_field(&cursor);
            Atom *klass_atom;

            if (!klass || klass[0] == '\0' || !cursor || cursor[0] == '\0' ||
                (strcmp(kind, "lex") == 0 && strchr(cursor, '\t'))) {
                slr_summary_set_error(error_buf, error_buf_size,
                                      "bad lex token-file line %u", line_no);
                free(line);
                goto fail;
            }
            klass_atom = atom_symbol(arena, klass);
            if (!input_tokenvec_push(out, NULL, klass_atom->sym_id, out->len)) {
                slr_summary_set_error(error_buf, error_buf_size,
                                      "failed to store lex token-file line %u", line_no);
                free(line);
                goto fail;
            }
        } else {
            slr_summary_set_error(error_buf, error_buf_size,
                                  "unknown token-file line %u", line_no);
            free(line);
            goto fail;
        }
        free(line);
    }
    fclose(fp);
    return true;

fail:
    fclose(fp);
    free(out->data);
    memset(out, 0, sizeof(*out));
    return false;
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

static Atom *slr_prepared_parse_shared_impl(
    const CettaLpNativeSlrPreparedImpl *prepared,
    Atom *token_list,
    Arena *arena,
    char *error_buf,
    size_t error_buf_size) {
    CettaLpNativeInputTokenVec tokens = {0};
    CettaLpNativeU32Vec state_stack = {0};
    CettaLpNativeParseValueVec value_stack = {0};
    uint32_t position = 0u;
    Atom *result = NULL;

    if (!prepared || !arena) {
        slr_summary_set_error(error_buf, error_buf_size,
                              "bad prepared SLR parse arguments");
        return NULL;
    }
    if (!input_tokens_from_list(
            token_list, &tokens, error_buf, error_buf_size)) {
        return NULL;
    }
    if (!u32vec_push(&state_stack, 0u)) {
        slr_summary_set_error(error_buf, error_buf_size,
                              "failed to initialize parse stack");
        goto done;
    }

    for (;;) {
        uint32_t token_index;
        uint32_t state = state_stack.data[state_stack.len - 1u];
        const CettaLpNativeAction *action;

        if (position < tokens.len) {
            int32_t terminal_index = idvec_find(
                &prepared->terminals,
                tokens.data[position].term_kind);
            if (terminal_index < 0) {
                result = atom_symbol(arena, "NoParse");
                goto done;
            }
            token_index = (uint32_t)terminal_index;
        } else {
            token_index = prepared->terminals.len;
        }
        action = actionvec_lookup(
            &prepared->actions, state, token_index);
        if (!action) {
            result = atom_symbol(arena, "NoParse");
            goto done;
        }
        if (action->kind == 's') {
            CettaLpNativeParseValue value;

            if (position >= tokens.len) {
                slr_summary_set_error(error_buf, error_buf_size,
                                      "shift past end of token stream");
                goto done;
            }
            value.is_cert = false;
            value.token_atom = tokens.data[position].token_atom;
            value.term_kind = tokens.data[position].term_kind;
            value.start = position;
            value.end = position + 1u;
            value.forest_idx = UINT32_MAX;
            value.cert = NULL;
            if (!u32vec_push(
                    &state_stack, (uint32_t)action->value) ||
                !parsevaluevec_push(&value_stack, &value)) {
                slr_summary_set_error(error_buf, error_buf_size,
                                      "failed to push shift result");
                goto done;
            }
            position++;
            continue;
        }
        if (action->kind == 'r') {
            int32_t production_index = action->value;
            SymbolId lhs = 0u;
            const CettaLpNativeSymbol *rhs = NULL;
            uint32_t rhs_len = 0u;
            CettaLpNativeParseValue next_value;
            int32_t lhs_symbol_index;
            int32_t goto_edge_index;

            slr_get_prod(
                prepared->productions, prepared->production_len,
                prepared->start_nt, production_index,
                &lhs, &rhs, &rhs_len);
            if (rhs_len > value_stack.len ||
                rhs_len >= state_stack.len) {
                slr_summary_set_error(error_buf, error_buf_size,
                                      "reduce arity exceeds parse stack");
                goto done;
            }
            if ((uint32_t)production_index >=
                prepared->grammar_production_len) {
                CettaLpNativeParseValue *child =
                    &value_stack.data[value_stack.len - 1u];
                if (rhs_len != 1u || child->is_cert) {
                    slr_summary_set_error(
                        error_buf, error_buf_size,
                        "leaf reduction expected one shifted token");
                    goto done;
                }
                next_value.is_cert = true;
                next_value.token_atom = NULL;
                next_value.term_kind = 0u;
                next_value.start = child->start;
                next_value.end = child->end;
                next_value.forest_idx = UINT32_MAX;
                next_value.cert = make_leaf_cert(
                    arena, child->token_atom,
                    child->start, child->end);
            } else if (rhs_len == 0u) {
                Atom *epsilon = make_eps_cert(arena);
                Atom *children[1] = {epsilon};

                next_value.is_cert = true;
                next_value.token_atom = NULL;
                next_value.term_kind = 0u;
                next_value.start = position;
                next_value.end = position;
                next_value.forest_idx = UINT32_MAX;
                next_value.cert = make_node_cert(
                    arena,
                    prepared->production_labels[production_index],
                    lhs, position, position, children, 1u);
            } else {
                uint32_t base = value_stack.len - rhs_len;
                uint32_t child_index;
                Atom **children = arena_alloc(
                    arena, sizeof(*children) * rhs_len);

                next_value.is_cert = true;
                next_value.token_atom = NULL;
                next_value.term_kind = 0u;
                next_value.start = value_stack.data[base].start;
                next_value.end =
                    value_stack.data[value_stack.len - 1u].end;
                next_value.forest_idx = UINT32_MAX;
                for (child_index = 0u;
                     child_index < rhs_len;
                     child_index++) {
                    CettaLpNativeParseValue *part =
                        &value_stack.data[base + child_index];
                    if (rhs[child_index].kind ==
                        CETTA_LP_NATIVE_SYMBOL_TM) {
                        if (part->is_cert ||
                            part->term_kind !=
                                rhs[child_index].name) {
                            slr_summary_set_error(
                                error_buf, error_buf_size,
                                "terminal reduction mismatch");
                            goto done;
                        }
                        children[child_index] = make_tok_cert(
                            arena, rhs[child_index].name,
                            part->start);
                    } else {
                        if (!part->is_cert || !part->cert) {
                            slr_summary_set_error(
                                error_buf, error_buf_size,
                                "nonterminal reduction missing child cert");
                            goto done;
                        }
                        children[child_index] = part->cert;
                    }
                }
                next_value.cert = make_node_cert(
                    arena,
                    prepared->production_labels[production_index],
                    lhs, next_value.start, next_value.end,
                    children, rhs_len);
            }

            value_stack.len -= rhs_len;
            state_stack.len -= rhs_len;
            lhs_symbol_index = symbolvec_find(
                &prepared->symbols, false, lhs);
            if (lhs_symbol_index < 0 || state_stack.len == 0u) {
                slr_summary_set_error(
                    error_buf, error_buf_size,
                    "missing goto state for reduction");
                goto done;
            }
            goto_edge_index = edgevec_find(
                &prepared->edges,
                state_stack.data[state_stack.len - 1u],
                (uint32_t)lhs_symbol_index);
            if (goto_edge_index < 0 ||
                !u32vec_push(
                    &state_stack,
                    prepared->edges
                        .data[goto_edge_index].target) ||
                !parsevaluevec_push(
                    &value_stack, &next_value)) {
                slr_summary_set_error(
                    error_buf, error_buf_size,
                    "failed to push reduced value");
                goto done;
            }
            continue;
        }
        if (action->kind == 'a') {
            if (value_stack.len != 1u ||
                !value_stack.data[0].is_cert ||
                !value_stack.data[0].cert ||
                value_stack.data[0].start != 0u ||
                value_stack.data[0].end != tokens.len) {
                slr_summary_set_error(
                    error_buf, error_buf_size,
                    "accept state missing full-span cert");
                goto done;
            }
            result = atom_expr2(
                arena, atom_symbol(arena, "Unique"),
                value_stack.data[0].cert);
            goto done;
        }
        slr_summary_set_error(error_buf, error_buf_size,
                              "unknown SLR action kind");
        goto done;
    }

done:
    free(tokens.data);
    free(value_stack.data);
    free(state_stack.data);
    return result;
}

Atom *cetta_lp_native_slr_prepared_parse_shared(
    const CettaLpNativeSlrPrepared *prepared,
    Atom *token_list,
    Arena *arena,
    char *error_buf,
    size_t error_buf_size) {
    const CettaLpNativeSlrPreparedImpl *implementation =
        prepared ? prepared->implementation : NULL;

    if (error_buf && error_buf_size > 0u)
        error_buf[0] = '\0';
    return slr_prepared_parse_shared_impl(
        implementation, token_list, arena,
        error_buf, error_buf_size);
}

Atom *cetta_lp_native_slr_parse_shared(
    const CettaLpNativeGrammar *grammar,
    SymbolId start_nt,
    Atom *token_list,
    Arena *arena,
    char *error_buf,
    size_t error_buf_size) {
    CettaLpNativeSlrPrepared prepared;
    Atom *result;

    cetta_lp_native_slr_prepared_init(&prepared);
    if (!cetta_lp_native_slr_prepare(
            &prepared, grammar, start_nt,
            error_buf, error_buf_size)) {
        return NULL;
    }
    result = cetta_lp_native_slr_prepared_parse_shared(
        &prepared, token_list, arena,
        error_buf, error_buf_size);
    cetta_lp_native_slr_prepared_free(&prepared);
    return result;
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
    uint32_t next_idx;
} CettaLpNativeGllPackedChoice;

typedef struct {
    CettaLpNativeGllPackedChoice *data;
    uint16_t *prod_idx16;
    int32_t *prod_idx32;
    uint32_t len;
    uint32_t cap;
    bool wide_prod_idx;
} CettaLpNativeGllPackedStore;

typedef struct {
    uint32_t *slots;
    uint32_t cap;
} CettaLpNativeGllIndex;

#define CETTA_LP_NATIVE_GLL_NODE_KIND_SHIFT 30u
#define CETTA_LP_NATIVE_GLL_NODE_VALUE_MASK 0x3fffffffu

typedef struct {
    uint32_t key0;
    uint32_t key1_kind;
    uint32_t left;
    uint32_t right;
    uint32_t terminal_value_kind;
    uint32_t terminal_value;
    uint32_t first_choice;
} CettaLpNativeGllNode;

typedef struct {
    CettaLpNativeGllNode *data;
    uint32_t len;
    uint32_t cap;
    CettaLpNativeGllIndex index;
    CettaLpNativeGllPackedStore packed;
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
    CettaLpNativeGllIndex index;
} CettaLpNativeGllGssNodeVec;

typedef struct {
    uint64_t **wide_chunks;
    uint32_t **lo_chunks;
    uint16_t **hi_chunks;
    uint32_t len;
    uint32_t cap;
    bool wide;
} CettaLpNativeGllRecLinkStore;

typedef CettaLpNativeGllRecLinkStore CettaLpNativeGllRecGssEdgeStore;
typedef CettaLpNativeGllRecLinkStore CettaLpNativeGllRecGssPoppedStore;

typedef struct {
    uint8_t prod_bits;
    uint8_t dot_bits;
    uint8_t pos_bits;
    uint8_t dot_shift;
    uint8_t prod_shift;
    uint64_t prod_mask;
    uint64_t dot_mask;
    uint64_t pos_mask;
} CettaLpNativeGllRecGssPacking;

typedef struct {
    uint64_t key;
    uint32_t first_edge;
    uint32_t first_popped;
} CettaLpNativeGllRecGssNode;

typedef struct {
    CettaLpNativeGllRecGssNode *data;
    uint32_t len;
    uint32_t cap;
    CettaLpNativeGllIndex index;
    CettaLpNativeGllRecGssPacking packing;
    CettaLpNativeGllRecGssEdgeStore edges;
    CettaLpNativeGllRecGssPoppedStore popped;
} CettaLpNativeGllRecGssNodeVec;

typedef struct {
    int32_t prod_idx;
    uint32_t dot;
    uint32_t gss_idx;
    uint32_t left_label;
    uint32_t pos;
} CettaLpNativeGllDescriptor;

typedef struct {
    uint8_t prod_bits;
    uint8_t dot_bits;
    uint8_t pos_bits;
    uint8_t gss_bits;
    uint8_t dot_shift;
    uint8_t pos_shift;
    uint8_t left_shift;
    uint8_t gss_shift;
    uint64_t prod_mask;
    uint64_t dot_mask;
    uint64_t pos_mask;
    uint64_t gss_mask;
} CettaLpNativeGllDescCompactPacking;

typedef struct {
    CettaLpNativeGllDescriptor *data;
    uint64_t *compact_lo;
    uint32_t *compact_hi;
    uint32_t len;
    uint32_t cap;
    CettaLpNativeGllIndex index;
    CettaLpNativeGllDescCompactPacking packing;
    bool compact;
} CettaLpNativeGllDescriptorVec;

typedef struct {
    uint64_t **slot_chunks;
    uint32_t **slot_lo_chunks;
    uint16_t **slot_hi_chunks;
    uint32_t len;
    uint32_t cap;
    bool wide_slots;
} CettaLpNativeGllRecDescSet;

typedef struct {
    uint64_t *data;
    uint32_t len;
    uint32_t cap;
} CettaLpNativeU64Vec;

typedef struct {
    uint64_t **blocks;
    uint32_t len;
    uint32_t block_len;
    uint32_t block_cap;
} CettaLpNativeU64Stack;

typedef struct {
    uint8_t prod_bits;
    uint8_t dot_bits;
    uint8_t gss_bits;
    uint8_t pos_bits;
    uint8_t dot_shift;
    uint8_t gss_shift;
    uint8_t prod_shift;
    uint64_t dot_mask;
    uint64_t gss_mask;
    uint64_t pos_mask;
} CettaLpNativeGllRecDescPacking;

typedef struct {
    uint8_t prod_bits;
    uint8_t pos_bits;
    uint8_t origin_shift;
    uint8_t end_shift;
    uint64_t prod_mask;
    uint64_t pos_mask;
} CettaLpNativeGllSpanPacking;

static uint64_t gll_hash_u32(uint64_t hash, uint32_t value) {
    hash ^= (uint64_t)value;
    return hash * 1099511628211ull;
}

static uint64_t gll_hash_u64(uint64_t hash, uint64_t value) {
    hash ^= value;
    return hash * 1099511628211ull;
}

static uint32_t gll_hash_slot(uint64_t hash, uint32_t cap) {
    hash ^= hash >> 32;
    return (uint32_t)hash & (cap - 1u);
}

static uint32_t gll_hash_slot_mod(uint64_t hash, uint32_t cap) {
    hash ^= hash >> 32;
    return (uint32_t)(hash % cap);
}

static uint32_t gll_index_next_slot(uint32_t slot, uint32_t cap) {
    return (slot + 1u) & (cap - 1u);
}

static bool gll_index_capacity_for(uint32_t min_len, uint32_t *out) {
    uint32_t cap = 16u;
    uint64_t needed = ((uint64_t)min_len * 4u + 2u) / 3u;

    if (!out)
        return false;
    while ((uint64_t)cap < needed) {
        if (cap > UINT32_MAX / 2u)
            return false;
        cap *= 2u;
    }
    *out = cap;
    return true;
}

static bool gll_index_needs_grow(const CettaLpNativeGllIndex *index,
                                 uint32_t next_len) {
    if (CETTA_LP_NATIVE_GLL_DISABLE_INDEX)
        return false;
    if (!index || index->cap == 0)
        return true;
    return ((uint64_t)next_len * 10u) >= ((uint64_t)index->cap * 9u);
}

static CettaLpNativeGllNodeKind gll_node_kind_value(
    const CettaLpNativeGllNode *node) {
    if (!node)
        return CETTA_LP_NATIVE_GLL_NODE_TERM;
    return (CettaLpNativeGllNodeKind)
        (node->key1_kind >> CETTA_LP_NATIVE_GLL_NODE_KIND_SHIFT);
}

static bool gll_node_set_key(CettaLpNativeGllNode *node,
                             CettaLpNativeGllNodeKind kind,
                             SymbolId symbol,
                             int32_t prod_idx,
                             uint32_t dot,
                             CettaLpNativeUtf8TerminalValueKind
                                 terminal_value_kind,
                             uint32_t terminal_value) {
    uint32_t key1;

    if (!node || (uint32_t)kind > (uint32_t)CETTA_LP_NATIVE_GLL_NODE_INTER)
        return false;
    key1 = ((uint32_t)kind) << CETTA_LP_NATIVE_GLL_NODE_KIND_SHIFT;
    if (kind == CETTA_LP_NATIVE_GLL_NODE_INTER) {
        if (prod_idx < 0 || dot > CETTA_LP_NATIVE_GLL_NODE_VALUE_MASK)
            return false;
        node->key0 = (uint32_t)prod_idx;
        node->key1_kind = key1 | dot;
    } else {
        node->key0 = symbol;
        node->key1_kind = key1;
    }
    if (kind == CETTA_LP_NATIVE_GLL_NODE_TERM) {
        if ((uint32_t)terminal_value_kind >
            (uint32_t)CETTA_LP_NATIVE_UTF8_TERMINAL_VALUE_WITNESS) {
            return false;
        }
        node->terminal_value_kind = (uint32_t)terminal_value_kind;
        node->terminal_value = terminal_value;
    } else {
        node->terminal_value_kind =
            (uint32_t)CETTA_LP_NATIVE_UTF8_TERMINAL_VALUE_SCALAR;
        node->terminal_value = 0u;
    }
    return true;
}

static SymbolId gll_node_symbol_value(const CettaLpNativeGllNode *node) {
    if (!node || gll_node_kind_value(node) == CETTA_LP_NATIVE_GLL_NODE_INTER)
        return 0;
    return node->key0;
}

static int32_t gll_node_prod_idx_value(const CettaLpNativeGllNode *node) {
    if (!node || gll_node_kind_value(node) != CETTA_LP_NATIVE_GLL_NODE_INTER)
        return -1;
    return (int32_t)node->key0;
}

static uint32_t gll_node_dot_value(const CettaLpNativeGllNode *node) {
    if (!node || gll_node_kind_value(node) != CETTA_LP_NATIVE_GLL_NODE_INTER)
        return 0;
    return node->key1_kind & CETTA_LP_NATIVE_GLL_NODE_VALUE_MASK;
}

static CettaLpNativeUtf8TerminalValueKind gll_node_terminal_value_kind(
    const CettaLpNativeGllNode *node) {
    if (!node || gll_node_kind_value(node) != CETTA_LP_NATIVE_GLL_NODE_TERM)
        return CETTA_LP_NATIVE_UTF8_TERMINAL_VALUE_SCALAR;
    return (CettaLpNativeUtf8TerminalValueKind)node->terminal_value_kind;
}

static uint32_t gll_node_terminal_value(
    const CettaLpNativeGllNode *node) {
    if (!node || gll_node_kind_value(node) != CETTA_LP_NATIVE_GLL_NODE_TERM)
        return 0u;
    return node->terminal_value;
}

static bool gll_node_key_equals(const CettaLpNativeGllNode *node,
                                CettaLpNativeGllNodeKind kind,
                                SymbolId symbol,
                                int32_t prod_idx,
                                uint32_t dot,
                                uint32_t left,
                                uint32_t right,
                                CettaLpNativeUtf8TerminalValueKind
                                    terminal_value_kind,
                                uint32_t terminal_value) {
    return gll_node_kind_value(node) == kind &&
        gll_node_symbol_value(node) == symbol &&
        gll_node_prod_idx_value(node) == prod_idx &&
        gll_node_dot_value(node) == dot &&
        node->left == left &&
        node->right == right &&
        (kind != CETTA_LP_NATIVE_GLL_NODE_TERM ||
         (gll_node_terminal_value_kind(node) == terminal_value_kind &&
          gll_node_terminal_value(node) == terminal_value));
}

static uint64_t gll_node_key_hash(CettaLpNativeGllNodeKind kind,
                                  SymbolId symbol,
                                  int32_t prod_idx,
                                  uint32_t dot,
                                  uint32_t left,
                                  uint32_t right,
                                  CettaLpNativeUtf8TerminalValueKind
                                      terminal_value_kind,
                                  uint32_t terminal_value) {
    uint64_t hash = 1469598103934665603ull;

    hash = gll_hash_u32(hash, (uint32_t)kind);
    hash = gll_hash_u32(hash, symbol);
    hash = gll_hash_u32(hash, (uint32_t)prod_idx);
    hash = gll_hash_u32(hash, dot);
    hash = gll_hash_u32(hash, left);
    hash = gll_hash_u32(hash, right);
    if (kind == CETTA_LP_NATIVE_GLL_NODE_TERM) {
        hash = gll_hash_u32(hash, (uint32_t)terminal_value_kind);
        hash = gll_hash_u32(hash, terminal_value);
    }
    return hash;
}

static void gll_node_index_insert_existing(CettaLpNativeGllNodeVec *nodes,
                                           uint32_t node_idx) {
    const CettaLpNativeGllNode *node = &nodes->data[node_idx];
    uint32_t slot;

    if (!nodes->index.slots || nodes->index.cap == 0)
        return;
    slot = gll_hash_slot(
        gll_node_key_hash(gll_node_kind_value(node), gll_node_symbol_value(node),
                          gll_node_prod_idx_value(node),
                          gll_node_dot_value(node), node->left, node->right,
                          gll_node_terminal_value_kind(node),
                          gll_node_terminal_value(node)),
        nodes->index.cap);
    while (nodes->index.slots[slot] != 0)
        slot = gll_index_next_slot(slot, nodes->index.cap);
    nodes->index.slots[slot] = node_idx + 1u;
}

static bool gll_node_index_rebuild(CettaLpNativeGllNodeVec *nodes,
                                   uint32_t min_len) {
    uint32_t cap;
    uint32_t *slots;
    uint32_t i;

    if (!gll_index_capacity_for(min_len, &cap))
        return false;
    slots = cetta_malloc(sizeof(*slots) * cap);
    memset(slots, 0, sizeof(*slots) * cap);
    free(nodes->index.slots);
    nodes->index.slots = slots;
    nodes->index.cap = cap;
    for (i = 0; i < nodes->len; i++)
        gll_node_index_insert_existing(nodes, i);
    return true;
}

static bool gll_node_index_ensure(CettaLpNativeGllNodeVec *nodes,
                                  uint32_t next_len) {
    if (!gll_index_needs_grow(&nodes->index, next_len))
        return true;
    return gll_node_index_rebuild(nodes, next_len);
}

static bool gll_gss_key_equals(const CettaLpNativeGllGssNode *node,
                               bool is_root,
                               int32_t prod_idx,
                               uint32_t dot,
                               uint32_t pos) {
    return node->is_root == is_root &&
        node->prod_idx == prod_idx &&
        node->dot == dot &&
        node->pos == pos;
}

static uint64_t gll_gss_key_hash(bool is_root,
                                 int32_t prod_idx,
                                 uint32_t dot,
                                 uint32_t pos) {
    uint64_t hash = 1469598103934665603ull;

    hash = gll_hash_u32(hash, is_root ? 1u : 0u);
    hash = gll_hash_u32(hash, (uint32_t)prod_idx);
    hash = gll_hash_u32(hash, dot);
    hash = gll_hash_u32(hash, pos);
    return hash;
}

static void gll_gss_index_insert_existing(CettaLpNativeGllGssNodeVec *nodes,
                                          uint32_t node_idx) {
    const CettaLpNativeGllGssNode *node = &nodes->data[node_idx];
    uint32_t slot;

    if (!nodes->index.slots || nodes->index.cap == 0)
        return;
    slot = gll_hash_slot(
        gll_gss_key_hash(node->is_root, node->prod_idx, node->dot, node->pos),
        nodes->index.cap);
    while (nodes->index.slots[slot] != 0)
        slot = gll_index_next_slot(slot, nodes->index.cap);
    nodes->index.slots[slot] = node_idx + 1u;
}

static bool gll_gss_index_rebuild(CettaLpNativeGllGssNodeVec *nodes,
                                  uint32_t min_len) {
    uint32_t cap;
    uint32_t *slots;
    uint32_t i;

    if (!gll_index_capacity_for(min_len, &cap))
        return false;
    slots = cetta_malloc(sizeof(*slots) * cap);
    memset(slots, 0, sizeof(*slots) * cap);
    free(nodes->index.slots);
    nodes->index.slots = slots;
    nodes->index.cap = cap;
    for (i = 0; i < nodes->len; i++)
        gll_gss_index_insert_existing(nodes, i);
    return true;
}

static bool gll_gss_index_ensure(CettaLpNativeGllGssNodeVec *nodes,
                                 uint32_t next_len) {
    if (!gll_index_needs_grow(&nodes->index, next_len))
        return true;
    return gll_gss_index_rebuild(nodes, next_len);
}

static bool gll_rec_gss_key_pack(
    const CettaLpNativeGllRecGssPacking *packing,
    bool is_root,
    int32_t prod_idx,
    uint32_t dot,
    uint32_t pos,
    uint64_t *out) {
    uint64_t prod_field;
    uint64_t key;

    if (!packing || !out)
        return false;
    if (is_root) {
        if (prod_idx != -1)
            return false;
        prod_field = 0;
    } else {
        if (prod_idx < 0)
            return false;
        prod_field = (uint64_t)(uint32_t)prod_idx + 1u;
    }
    if ((prod_field >> packing->prod_bits) != 0 ||
        ((uint64_t)dot >> packing->dot_bits) != 0 ||
        ((uint64_t)pos >> packing->pos_bits) != 0) {
        return false;
    }
    key = (uint64_t)pos;
    key |= ((uint64_t)dot << packing->dot_shift);
    key |= (prod_field << packing->prod_shift);
    *out = key;
    return true;
}

static bool gll_rec_gss_key_equals(const CettaLpNativeGllRecGssNode *node,
                                   uint64_t key) {
    return node->key == key;
}

static void gll_rec_gss_index_insert_existing(
    CettaLpNativeGllRecGssNodeVec *nodes,
    uint32_t node_idx) {
    const CettaLpNativeGllRecGssNode *node = &nodes->data[node_idx];
    uint32_t slot;

    if (!nodes->index.slots || nodes->index.cap == 0)
        return;
    slot = gll_hash_slot(gll_hash_u64(UINT64_C(1469598103934665603),
                                      node->key),
                         nodes->index.cap);
    while (nodes->index.slots[slot] != 0)
        slot = gll_index_next_slot(slot, nodes->index.cap);
    nodes->index.slots[slot] = node_idx + 1u;
}

static bool gll_rec_gss_index_rebuild(CettaLpNativeGllRecGssNodeVec *nodes,
                                      uint32_t min_len) {
    uint32_t cap;
    uint32_t *slots;
    uint32_t i;

    if (!gll_index_capacity_for(min_len, &cap))
        return false;
    slots = cetta_malloc(sizeof(*slots) * cap);
    memset(slots, 0, sizeof(*slots) * cap);
    free(nodes->index.slots);
    nodes->index.slots = slots;
    nodes->index.cap = cap;
    for (i = 0; i < nodes->len; i++)
        gll_rec_gss_index_insert_existing(nodes, i);
    return true;
}

static bool gll_rec_gss_index_ensure(CettaLpNativeGllRecGssNodeVec *nodes,
                                     uint32_t next_len) {
    if (!gll_index_needs_grow(&nodes->index, next_len))
        return true;
    return gll_rec_gss_index_rebuild(nodes, next_len);
}

static bool gll_desc_key_equals(const CettaLpNativeGllDescriptor *desc,
                                int32_t prod_idx,
                                uint32_t dot,
                                uint32_t gss_idx,
                                uint32_t left_label,
                                uint32_t pos) {
    return desc->prod_idx == prod_idx &&
        desc->dot == dot &&
        desc->gss_idx == gss_idx &&
        desc->left_label == left_label &&
        desc->pos == pos;
}

static uint8_t gll_desc_bits_needed(uint32_t max_value) {
    uint8_t bits = 0;

    do {
        bits++;
        max_value >>= 1;
    } while (max_value != 0);
    return bits;
}

static uint64_t gll_desc_bits_mask(uint8_t bits) {
    if (bits >= 64)
        return UINT64_MAX;
    return (UINT64_C(1) << bits) - UINT64_C(1);
}

static void gll_descvec_init_compact(CettaLpNativeGllDescriptorVec *vec,
                                     uint32_t production_len,
                                     uint32_t max_rhs_len,
                                     uint32_t token_len) {
    uint32_t used;

    if (!vec || production_len == 0)
        return;
    vec->packing.prod_bits = gll_desc_bits_needed(production_len - 1u);
    vec->packing.dot_bits = gll_desc_bits_needed(max_rhs_len + 1u);
    vec->packing.pos_bits = gll_desc_bits_needed(token_len);
    used = (uint32_t)vec->packing.prod_bits +
        (uint32_t)vec->packing.dot_bits +
        (uint32_t)vec->packing.pos_bits + 32u;
    if (used >= 96u)
        return;
    vec->packing.gss_bits = (uint8_t)(96u - used);
    vec->packing.dot_shift = vec->packing.prod_bits;
    vec->packing.pos_shift = (uint8_t)(vec->packing.dot_shift +
                                       vec->packing.dot_bits);
    vec->packing.left_shift = (uint8_t)(vec->packing.pos_shift +
                                        vec->packing.pos_bits);
    vec->packing.gss_shift = (uint8_t)(vec->packing.left_shift + 32u);
    vec->packing.prod_mask = gll_desc_bits_mask(vec->packing.prod_bits);
    vec->packing.dot_mask = gll_desc_bits_mask(vec->packing.dot_bits);
    vec->packing.pos_mask = gll_desc_bits_mask(vec->packing.pos_bits);
    vec->packing.gss_mask = gll_desc_bits_mask(vec->packing.gss_bits);
    vec->compact = true;
}

static bool gll_descvec_pack_compact(
    const CettaLpNativeGllDescriptorVec *vec,
    const CettaLpNativeGllDescriptor *desc,
    uint64_t *lo,
    uint32_t *hi) {
    __uint128_t raw;

    if (!vec || !desc || !lo || !hi || !vec->compact || desc->prod_idx < 0)
        return false;
    if (((uint64_t)(uint32_t)desc->prod_idx >> vec->packing.prod_bits) != 0 ||
        ((uint64_t)desc->dot >> vec->packing.dot_bits) != 0 ||
        ((uint64_t)desc->pos >> vec->packing.pos_bits) != 0 ||
        ((uint64_t)desc->gss_idx >> vec->packing.gss_bits) != 0) {
        return false;
    }
    raw = (__uint128_t)(uint32_t)desc->prod_idx;
    raw |= (__uint128_t)desc->dot << vec->packing.dot_shift;
    raw |= (__uint128_t)desc->pos << vec->packing.pos_shift;
    raw |= (__uint128_t)desc->left_label << vec->packing.left_shift;
    raw |= (__uint128_t)desc->gss_idx << vec->packing.gss_shift;
    *lo = (uint64_t)raw;
    *hi = (uint32_t)(raw >> 64);
    return true;
}

static bool gll_descvec_get(const CettaLpNativeGllDescriptorVec *vec,
                            uint32_t idx,
                            CettaLpNativeGllDescriptor *out) {
    __uint128_t raw;

    if (!vec || !out || idx >= vec->len)
        return false;
    if (!vec->compact) {
        if (!vec->data)
            return false;
        *out = vec->data[idx];
        return true;
    }
    if (!vec->compact_lo || !vec->compact_hi)
        return false;
    raw = (__uint128_t)vec->compact_lo[idx] |
        ((__uint128_t)vec->compact_hi[idx] << 64);
    out->prod_idx = (int32_t)((uint64_t)raw & vec->packing.prod_mask);
    out->dot = (uint32_t)((uint64_t)(raw >> vec->packing.dot_shift) &
                          vec->packing.dot_mask);
    out->pos = (uint32_t)((uint64_t)(raw >> vec->packing.pos_shift) &
                          vec->packing.pos_mask);
    out->left_label = (uint32_t)((raw >> vec->packing.left_shift) &
                                 UINT32_MAX);
    out->gss_idx = (uint32_t)((uint64_t)(raw >> vec->packing.gss_shift) &
                              vec->packing.gss_mask);
    return true;
}

static bool gll_descvec_promote(CettaLpNativeGllDescriptorVec *vec) {
    CettaLpNativeGllDescriptor *data;
    uint32_t i;

    if (!vec || !vec->compact)
        return true;
    data = cetta_malloc(sizeof(*data) * vec->cap);
    for (i = 0; i < vec->len; i++) {
        if (!gll_descvec_get(vec, i, &data[i])) {
            free(data);
            return false;
        }
    }
    free(vec->compact_lo);
    free(vec->compact_hi);
    vec->compact_lo = NULL;
    vec->compact_hi = NULL;
    vec->data = data;
    vec->compact = false;
    return true;
}

static bool gll_descvec_ensure_cap(CettaLpNativeGllDescriptorVec *vec,
                                   uint32_t next_len) {
    uint32_t next_cap;

    if (!vec)
        return false;
    if (vec->cap >= next_len)
        return true;
    next_cap = vec->cap == 0 ? 8u : vec->cap;
    while (next_cap < next_len) {
        if (next_cap > UINT32_MAX / 2u)
            return false;
        next_cap *= 2u;
    }
    if (vec->compact) {
        vec->compact_lo =
            cetta_realloc(vec->compact_lo, sizeof(*vec->compact_lo) * next_cap);
        vec->compact_hi =
            cetta_realloc(vec->compact_hi, sizeof(*vec->compact_hi) * next_cap);
    } else {
        vec->data = cetta_realloc(vec->data, sizeof(*vec->data) * next_cap);
    }
    vec->cap = next_cap;
    return true;
}

static bool gll_descvec_append(CettaLpNativeGllDescriptorVec *vec,
                               const CettaLpNativeGllDescriptor *desc,
                               uint32_t *out_idx) {
    uint64_t lo = 0;
    uint32_t hi = 0;

    if (!vec || !desc || !out_idx ||
        !gll_descvec_ensure_cap(vec, vec->len + 1u)) {
        return false;
    }
    if (vec->compact) {
        if (!gll_descvec_pack_compact(vec, desc, &lo, &hi)) {
            if (!gll_descvec_promote(vec))
                return false;
        }
    }
    *out_idx = vec->len;
    if (vec->compact) {
        vec->compact_lo[vec->len] = lo;
        vec->compact_hi[vec->len] = hi;
    } else {
        vec->data[vec->len] = *desc;
    }
    vec->len++;
    return true;
}

static uint64_t gll_desc_key_hash(int32_t prod_idx,
                                  uint32_t dot,
                                  uint32_t gss_idx,
                                  uint32_t left_label,
                                  uint32_t pos) {
    uint64_t hash = 1469598103934665603ull;

    hash = gll_hash_u32(hash, (uint32_t)prod_idx);
    hash = gll_hash_u32(hash, dot);
    hash = gll_hash_u32(hash, gss_idx);
    hash = gll_hash_u32(hash, left_label);
    hash = gll_hash_u32(hash, pos);
    return hash;
}

static void gll_desc_index_insert_existing(CettaLpNativeGllDescriptorVec *descs,
                                           uint32_t desc_idx) {
    CettaLpNativeGllDescriptor desc;
    uint32_t slot;

    if (!gll_descvec_get(descs, desc_idx, &desc) ||
        !descs->index.slots || descs->index.cap == 0)
        return;
    slot = gll_hash_slot(
        gll_desc_key_hash(desc.prod_idx, desc.dot, desc.gss_idx,
                          desc.left_label, desc.pos),
        descs->index.cap);
    while (descs->index.slots[slot] != 0)
        slot = gll_index_next_slot(slot, descs->index.cap);
    descs->index.slots[slot] = desc_idx + 1u;
}

static bool gll_desc_index_rebuild(CettaLpNativeGllDescriptorVec *descs,
                                   uint32_t min_len) {
    uint32_t cap;
    uint32_t *slots;
    uint32_t i;

    if (!gll_index_capacity_for(min_len, &cap))
        return false;
    slots = cetta_malloc(sizeof(*slots) * cap);
    memset(slots, 0, sizeof(*slots) * cap);
    free(descs->index.slots);
    descs->index.slots = slots;
    descs->index.cap = cap;
    for (i = 0; i < descs->len; i++)
        gll_desc_index_insert_existing(descs, i);
    return true;
}

static bool gll_desc_index_ensure(CettaLpNativeGllDescriptorVec *descs,
                                  uint32_t next_len) {
    if (!gll_index_needs_grow(&descs->index, next_len))
        return true;
    return gll_desc_index_rebuild(descs, next_len);
}

static bool gll_packed_store_ensure(CettaLpNativeGllPackedStore *store) {
    uint32_t next_cap;

    if (!store)
        return false;
    if (store->len < store->cap)
        return true;
    next_cap = store->cap == 0 ? 8u : store->cap * 2u;
    store->data = cetta_realloc(store->data,
                                sizeof(*store->data) * next_cap);
    if (store->wide_prod_idx) {
        store->prod_idx32 = cetta_realloc(store->prod_idx32,
                                          sizeof(*store->prod_idx32) * next_cap);
    } else {
        store->prod_idx16 = cetta_realloc(store->prod_idx16,
                                          sizeof(*store->prod_idx16) * next_cap);
    }
    store->cap = next_cap;
    return true;
}

static bool gll_packed_store_promote_prod(CettaLpNativeGllPackedStore *store) {
    int32_t *prod_idx32;
    uint32_t i;

    if (!store)
        return false;
    if (store->wide_prod_idx)
        return true;
    prod_idx32 = cetta_malloc(sizeof(*prod_idx32) * store->cap);
    for (i = 0; i < store->len; i++)
        prod_idx32[i] = store->prod_idx16 && store->prod_idx16[i] != 0
            ? (int32_t)store->prod_idx16[i] - 1
            : -1;
    free(store->prod_idx16);
    store->prod_idx16 = NULL;
    store->prod_idx32 = prod_idx32;
    store->wide_prod_idx = true;
    return true;
}

static bool gll_packed_store_set_prod(CettaLpNativeGllPackedStore *store,
                                      uint32_t choice_idx,
                                      int32_t prod_idx) {
    if (!store || choice_idx >= store->cap || prod_idx < -1)
        return false;
    if (prod_idx >= (int32_t)UINT16_MAX &&
        !gll_packed_store_promote_prod(store)) {
        return false;
    }
    if (store->wide_prod_idx) {
        store->prod_idx32[choice_idx] = prod_idx;
    } else {
        store->prod_idx16[choice_idx] = (uint16_t)(prod_idx + 1);
    }
    return true;
}

static int32_t gll_choice_prod_idx(const CettaLpNativeGllNodeVec *nodes,
                                   const CettaLpNativeGllPackedChoice *choice) {
    uintptr_t choice_idx;

    if (!nodes || !choice || !nodes->packed.data)
        return -1;
    if (choice < nodes->packed.data)
        return -1;
    choice_idx = (uintptr_t)(choice - nodes->packed.data);
    if (choice_idx >= nodes->packed.len)
        return -1;
    if (nodes->packed.wide_prod_idx)
        return nodes->packed.prod_idx32[choice_idx];
    if (!nodes->packed.prod_idx16 || nodes->packed.prod_idx16[choice_idx] == 0)
        return -1;
    return (int32_t)nodes->packed.prod_idx16[choice_idx] - 1;
}

static uint32_t gll_choice_pivot(const CettaLpNativeGllNodeVec *nodes,
                                 const CettaLpNativeGllPackedChoice *choice) {
    if (!nodes || !choice || choice->right_idx >= nodes->len)
        return CETTA_LP_NATIVE_NODE_NONE;
    return nodes->data[choice->right_idx].left;
}

static bool gll_choice_equals(const CettaLpNativeGllNodeVec *nodes,
                              const CettaLpNativeGllPackedChoice *choice,
                              uint32_t left_idx,
                              uint32_t right_idx,
                              uint32_t pivot,
                              int32_t prod_idx) {
    return choice->left_idx == left_idx &&
        choice->right_idx == right_idx &&
        gll_choice_pivot(nodes, choice) == pivot &&
        gll_choice_prod_idx(nodes, choice) == prod_idx;
}

static bool gll_node_push_packed_unique(CettaLpNativeGllNodeVec *nodes,
                                        uint32_t node_idx,
                                        uint32_t left_idx,
                                        uint32_t right_idx,
                                        uint32_t pivot,
                                        int32_t prod_idx) {
    CettaLpNativeGllNode *node;
    CettaLpNativeGllPackedChoice *choice;
    uint32_t choice_idx;
    uint32_t prev_idx = CETTA_LP_NATIVE_NODE_NONE;

    if (!nodes || node_idx >= nodes->len)
        return false;
    node = &nodes->data[node_idx];
    for (choice_idx = node->first_choice;
         choice_idx != CETTA_LP_NATIVE_NODE_NONE;
         choice_idx = nodes->packed.data[choice_idx].next_idx) {
        if (choice_idx >= nodes->packed.len)
            return false;
        if (gll_choice_equals(nodes, &nodes->packed.data[choice_idx],
                              left_idx, right_idx, pivot, prod_idx)) {
            return true;
        }
        prev_idx = choice_idx;
    }
    if (!gll_packed_store_ensure(&nodes->packed)) {
        return false;
    }
    choice_idx = nodes->packed.len;
    choice = &nodes->packed.data[choice_idx];
    choice->left_idx = left_idx;
    choice->right_idx = right_idx;
    choice->next_idx = CETTA_LP_NATIVE_NODE_NONE;
    if (!gll_packed_store_set_prod(&nodes->packed, choice_idx, prod_idx))
        return false;
    nodes->packed.len++;
    if (prev_idx == CETTA_LP_NATIVE_NODE_NONE) {
        node->first_choice = choice_idx;
    } else {
        nodes->packed.data[prev_idx].next_idx = choice_idx;
    }
    return true;
}

static void gll_nodevec_free(CettaLpNativeGllNodeVec *vec) {
    if (!vec)
        return;
    free(vec->packed.data);
    free(vec->packed.prod_idx16);
    free(vec->packed.prod_idx32);
    free(vec->data);
    free(vec->index.slots);
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
    free(vec->index.slots);
    memset(vec, 0, sizeof(*vec));
}

enum { CETTA_LP_NATIVE_GLL_REC_LINK_CHUNK_LEN = 65536u };
enum { CETTA_LP_NATIVE_GLL_REC_LINK_BITS = 24u };
enum { CETTA_LP_NATIVE_GLL_REC_LINK_MASK = 0xFFFFFFu };
enum { CETTA_LP_NATIVE_GLL_REC_LINK_NONE = 0xFFFFFFu };

static uint32_t gll_rec_link_chunk_count(uint32_t cap) {
    return (cap + CETTA_LP_NATIVE_GLL_REC_LINK_CHUNK_LEN - 1u) /
        CETTA_LP_NATIVE_GLL_REC_LINK_CHUNK_LEN;
}

static bool gll_rec_link_can_compact(uint32_t value, uint32_t next_idx) {
    if (value > CETTA_LP_NATIVE_GLL_REC_LINK_MASK)
        return false;
    if (next_idx == CETTA_LP_NATIVE_NODE_NONE)
        return true;
    return next_idx < CETTA_LP_NATIVE_GLL_REC_LINK_NONE;
}

static uint64_t gll_rec_link_pack_compact(uint32_t value, uint32_t next_idx) {
    uint32_t encoded_next = next_idx == CETTA_LP_NATIVE_NODE_NONE
        ? CETTA_LP_NATIVE_GLL_REC_LINK_NONE
        : next_idx;

    return (uint64_t)value |
        ((uint64_t)encoded_next << CETTA_LP_NATIVE_GLL_REC_LINK_BITS);
}

static void gll_rec_link_unpack_compact(uint64_t raw,
                                        uint32_t *value,
                                        uint32_t *next_idx) {
    uint32_t encoded_next;

    *value = (uint32_t)(raw & CETTA_LP_NATIVE_GLL_REC_LINK_MASK);
    encoded_next = (uint32_t)((raw >> CETTA_LP_NATIVE_GLL_REC_LINK_BITS) &
                              CETTA_LP_NATIVE_GLL_REC_LINK_MASK);
    *next_idx = encoded_next == CETTA_LP_NATIVE_GLL_REC_LINK_NONE
        ? CETTA_LP_NATIVE_NODE_NONE
        : encoded_next;
}

static uint64_t gll_rec_link_pack_wide(uint32_t value, uint32_t next_idx) {
    return (uint64_t)value | ((uint64_t)next_idx << 32);
}

static void gll_rec_link_unpack_wide(uint64_t raw,
                                     uint32_t *value,
                                     uint32_t *next_idx) {
    *value = (uint32_t)raw;
    *next_idx = (uint32_t)(raw >> 32);
}

static bool gll_rec_link_ensure_cap(CettaLpNativeGllRecLinkStore *store,
                                    uint32_t next_len) {
    uint32_t old_chunk_count;
    uint32_t new_chunk_count;
    uint32_t next_cap;

    if (store->cap >= next_len)
        return true;
    next_cap = store->cap == 0 ? 8u : store->cap;
    while (next_cap < next_len) {
        if (next_cap > UINT32_MAX / 2u)
            return false;
        next_cap *= 2u;
    }
    old_chunk_count = gll_rec_link_chunk_count(store->cap);
    new_chunk_count = gll_rec_link_chunk_count(next_cap);
    if (store->wide) {
        store->wide_chunks =
            cetta_realloc(store->wide_chunks,
                          sizeof(*store->wide_chunks) * new_chunk_count);
        memset(&store->wide_chunks[old_chunk_count], 0,
               sizeof(*store->wide_chunks) *
               (new_chunk_count - old_chunk_count));
    } else {
        store->lo_chunks =
            cetta_realloc(store->lo_chunks,
                          sizeof(*store->lo_chunks) * new_chunk_count);
        store->hi_chunks =
            cetta_realloc(store->hi_chunks,
                          sizeof(*store->hi_chunks) * new_chunk_count);
        memset(&store->lo_chunks[old_chunk_count], 0,
               sizeof(*store->lo_chunks) *
               (new_chunk_count - old_chunk_count));
        memset(&store->hi_chunks[old_chunk_count], 0,
               sizeof(*store->hi_chunks) *
               (new_chunk_count - old_chunk_count));
    }
    store->cap = next_cap;
    return true;
}

static bool gll_rec_link_alloc_compact_chunk(
    CettaLpNativeGllRecLinkStore *store,
    uint32_t chunk_idx) {
    if (!store->lo_chunks[chunk_idx]) {
        store->lo_chunks[chunk_idx] =
            cetta_malloc(sizeof(*store->lo_chunks[chunk_idx]) *
                         CETTA_LP_NATIVE_GLL_REC_LINK_CHUNK_LEN);
        memset(store->lo_chunks[chunk_idx], 0,
               sizeof(*store->lo_chunks[chunk_idx]) *
               CETTA_LP_NATIVE_GLL_REC_LINK_CHUNK_LEN);
    }
    if (!store->hi_chunks[chunk_idx]) {
        store->hi_chunks[chunk_idx] =
            cetta_malloc(sizeof(*store->hi_chunks[chunk_idx]) *
                         CETTA_LP_NATIVE_GLL_REC_LINK_CHUNK_LEN);
        memset(store->hi_chunks[chunk_idx], 0,
               sizeof(*store->hi_chunks[chunk_idx]) *
               CETTA_LP_NATIVE_GLL_REC_LINK_CHUNK_LEN);
    }
    return true;
}

static bool gll_rec_link_alloc_wide_chunk(
    CettaLpNativeGllRecLinkStore *store,
    uint32_t chunk_idx) {
    if (!store->wide_chunks[chunk_idx]) {
        store->wide_chunks[chunk_idx] =
            cetta_malloc(sizeof(*store->wide_chunks[chunk_idx]) *
                         CETTA_LP_NATIVE_GLL_REC_LINK_CHUNK_LEN);
        memset(store->wide_chunks[chunk_idx], 0,
               sizeof(*store->wide_chunks[chunk_idx]) *
               CETTA_LP_NATIVE_GLL_REC_LINK_CHUNK_LEN);
    }
    return true;
}

static bool gll_rec_link_promote(CettaLpNativeGllRecLinkStore *store) {
    uint32_t chunk_count;
    uint32_t chunk_idx;
    uint32_t i;
    uint64_t **wide_chunks;

    if (!store || store->wide)
        return true;
    chunk_count = gll_rec_link_chunk_count(store->cap);
    wide_chunks = cetta_malloc(sizeof(*wide_chunks) * chunk_count);
    memset(wide_chunks, 0, sizeof(*wide_chunks) * chunk_count);
    for (chunk_idx = 0; chunk_idx < chunk_count; chunk_idx++) {
        uint32_t base = chunk_idx * CETTA_LP_NATIVE_GLL_REC_LINK_CHUNK_LEN;
        uint32_t limit = store->len > base ? store->len - base : 0;
        if (limit > CETTA_LP_NATIVE_GLL_REC_LINK_CHUNK_LEN)
            limit = CETTA_LP_NATIVE_GLL_REC_LINK_CHUNK_LEN;
        if (limit == 0)
            break;
        if (!store->lo_chunks || !store->hi_chunks ||
            !store->lo_chunks[chunk_idx] || !store->hi_chunks[chunk_idx]) {
            continue;
        }
        wide_chunks[chunk_idx] =
            cetta_malloc(sizeof(*wide_chunks[chunk_idx]) *
                         CETTA_LP_NATIVE_GLL_REC_LINK_CHUNK_LEN);
        memset(wide_chunks[chunk_idx], 0,
               sizeof(*wide_chunks[chunk_idx]) *
               CETTA_LP_NATIVE_GLL_REC_LINK_CHUNK_LEN);
        for (i = 0; i < limit; i++) {
            uint32_t value;
            uint32_t next_idx;
            uint64_t raw = (uint64_t)store->lo_chunks[chunk_idx][i] |
                ((uint64_t)store->hi_chunks[chunk_idx][i] << 32);
            gll_rec_link_unpack_compact(raw, &value, &next_idx);
            wide_chunks[chunk_idx][i] =
                gll_rec_link_pack_wide(value, next_idx);
        }
        free(store->lo_chunks[chunk_idx]);
        free(store->hi_chunks[chunk_idx]);
    }
    free(store->lo_chunks);
    free(store->hi_chunks);
    store->lo_chunks = NULL;
    store->hi_chunks = NULL;
    store->wide_chunks = wide_chunks;
    store->wide = true;
    return true;
}

static bool gll_rec_link_push(CettaLpNativeGllRecLinkStore *store,
                              uint32_t value,
                              uint32_t next_idx,
                              uint32_t *out_idx) {
    uint32_t idx;
    uint32_t chunk_idx;
    uint32_t offset;

    if (!store || !out_idx)
        return false;
    if (!gll_rec_link_ensure_cap(store, store->len + 1u))
        return false;
    if (!store->wide && !gll_rec_link_can_compact(value, next_idx)) {
        if (!gll_rec_link_promote(store))
            return false;
    }
    idx = store->len;
    chunk_idx = idx / CETTA_LP_NATIVE_GLL_REC_LINK_CHUNK_LEN;
    offset = idx % CETTA_LP_NATIVE_GLL_REC_LINK_CHUNK_LEN;
    if (store->wide) {
        if (!gll_rec_link_alloc_wide_chunk(store, chunk_idx))
            return false;
        store->wide_chunks[chunk_idx][offset] =
            gll_rec_link_pack_wide(value, next_idx);
    } else {
        uint64_t raw;
        if (!gll_rec_link_alloc_compact_chunk(store, chunk_idx))
            return false;
        raw = gll_rec_link_pack_compact(value, next_idx);
        store->lo_chunks[chunk_idx][offset] = (uint32_t)raw;
        store->hi_chunks[chunk_idx][offset] = (uint16_t)(raw >> 32);
    }
    store->len++;
    *out_idx = idx;
    return true;
}

static bool gll_rec_link_get(const CettaLpNativeGllRecLinkStore *store,
                             uint32_t idx,
                             uint32_t *value,
                             uint32_t *next_idx) {
    uint32_t chunk_idx;
    uint32_t offset;

    if (!store || !value || !next_idx || idx >= store->len)
        return false;
    chunk_idx = idx / CETTA_LP_NATIVE_GLL_REC_LINK_CHUNK_LEN;
    offset = idx % CETTA_LP_NATIVE_GLL_REC_LINK_CHUNK_LEN;
    if (store->wide) {
        if (!store->wide_chunks || !store->wide_chunks[chunk_idx])
            return false;
        gll_rec_link_unpack_wide(store->wide_chunks[chunk_idx][offset],
                                 value, next_idx);
        return true;
    }
    if (!store->lo_chunks || !store->hi_chunks ||
        !store->lo_chunks[chunk_idx] || !store->hi_chunks[chunk_idx]) {
        return false;
    }
    gll_rec_link_unpack_compact(
        (uint64_t)store->lo_chunks[chunk_idx][offset] |
        ((uint64_t)store->hi_chunks[chunk_idx][offset] << 32),
        value, next_idx);
    return true;
}

static void gll_rec_link_free(CettaLpNativeGllRecLinkStore *store) {
    uint32_t chunk_count;
    uint32_t i;

    if (!store)
        return;
    chunk_count = gll_rec_link_chunk_count(store->cap);
    for (i = 0; i < chunk_count; i++) {
        free(store->wide_chunks ? store->wide_chunks[i] : NULL);
        free(store->lo_chunks ? store->lo_chunks[i] : NULL);
        free(store->hi_chunks ? store->hi_chunks[i] : NULL);
    }
    free(store->wide_chunks);
    free(store->lo_chunks);
    free(store->hi_chunks);
    memset(store, 0, sizeof(*store));
}

static void gll_rec_gssvec_free(CettaLpNativeGllRecGssNodeVec *vec) {
    if (!vec)
        return;
    free(vec->data);
    free(vec->index.slots);
    gll_rec_link_free(&vec->edges);
    gll_rec_link_free(&vec->popped);
    memset(vec, 0, sizeof(*vec));
}

static void gll_descvec_free(CettaLpNativeGllDescriptorVec *vec) {
    if (!vec)
        return;
    free(vec->data);
    free(vec->compact_lo);
    free(vec->compact_hi);
    free(vec->index.slots);
    memset(vec, 0, sizeof(*vec));
}

static bool u32vec_push_unique(CettaLpNativeU32Vec *vec, uint32_t value) {
    uint32_t i;

    for (i = 0; i < vec->len; i++) {
        if (vec->data[i] == value)
            return true;
    }
    return u32vec_push(vec, value);
}

/* Recognizer GSS events may repeat; the descriptor set is the exact deduplicator. */
static bool gll_rec_gss_add_popped(CettaLpNativeGllRecGssNodeVec *nodes,
                                   uint32_t gss_idx,
                                   uint32_t pos) {
    CettaLpNativeGllRecGssNode *node;
    uint32_t popped_idx;

    if (!nodes || gss_idx >= nodes->len)
        return false;
    node = &nodes->data[gss_idx];
    if (!gll_rec_link_push(&nodes->popped, pos, node->first_popped,
                           &popped_idx)) {
        return false;
    }
    node->first_popped = popped_idx;
    return true;
}

static bool u32vec_contains(const CettaLpNativeU32Vec *vec, uint32_t value) {
    uint32_t i;

    for (i = 0; i < vec->len; i++) {
        if (vec->data[i] == value)
            return true;
    }
    return false;
}

static uint8_t gll_bits_needed(uint32_t max_value) {
    uint8_t bits = 0;

    do {
        bits++;
        max_value >>= 1;
    } while (max_value != 0);
    return bits;
}

static uint64_t gll_bits_mask(uint8_t bits) {
    if (bits >= 64)
        return UINT64_MAX;
    return (UINT64_C(1) << bits) - UINT64_C(1);
}

static bool gll_rec_gss_packing_init(
    CettaLpNativeGllRecGssPacking *packing,
    uint32_t production_len,
    uint32_t max_rhs_len,
    uint32_t token_len) {
    uint32_t used;

    if (!packing || production_len == UINT32_MAX)
        return false;
    memset(packing, 0, sizeof(*packing));
    packing->prod_bits = gll_bits_needed(production_len);
    packing->dot_bits = gll_bits_needed(max_rhs_len);
    packing->pos_bits = gll_bits_needed(token_len);
    used = (uint32_t)packing->prod_bits +
        (uint32_t)packing->dot_bits +
        (uint32_t)packing->pos_bits;
    if (used > 64u)
        return false;
    packing->pos_mask = gll_bits_mask(packing->pos_bits);
    packing->dot_mask = gll_bits_mask(packing->dot_bits);
    packing->prod_mask = gll_bits_mask(packing->prod_bits);
    packing->dot_shift = packing->pos_bits;
    packing->prod_shift = (uint8_t)(packing->dot_shift + packing->dot_bits);
    return true;
}

static void gll_rec_gss_key_unpack(
    const CettaLpNativeGllRecGssPacking *packing,
    uint64_t key,
    bool *is_root,
    int32_t *prod_idx,
    uint32_t *dot,
    uint32_t *pos) {
    uint32_t prod_field;

    *pos = (uint32_t)(key & packing->pos_mask);
    *dot = (uint32_t)((key >> packing->dot_shift) & packing->dot_mask);
    prod_field = (uint32_t)((key >> packing->prod_shift) &
                            packing->prod_mask);
    *is_root = prod_field == 0;
    *prod_idx = prod_field == 0 ? -1 : (int32_t)(prod_field - 1u);
}

static uint32_t gll_max_rhs_len(CettaLpNativeSlrProduction *productions,
                                uint32_t production_len) {
    uint32_t max_rhs = 0;
    uint32_t i;

    for (i = 0; i < production_len; i++) {
        if (productions[i].rhs_len > max_rhs)
            max_rhs = productions[i].rhs_len;
    }
    return max_rhs;
}

static bool gll_rec_desc_packing_init(
    CettaLpNativeGllRecDescPacking *packing,
    uint32_t production_len,
    uint32_t max_rhs_len,
    uint32_t token_len) {
    uint32_t used;

    if (!packing || production_len == 0)
        return false;
    memset(packing, 0, sizeof(*packing));
    packing->prod_bits = gll_bits_needed(production_len - 1u);
    packing->dot_bits = gll_bits_needed(max_rhs_len + 1u);
    packing->pos_bits = gll_bits_needed(token_len);
    used = (uint32_t)packing->prod_bits +
        (uint32_t)packing->dot_bits +
        (uint32_t)packing->pos_bits;
    if (used >= 64u)
        return false;
    packing->gss_bits = (uint8_t)(64u - used);
    packing->pos_mask = gll_bits_mask(packing->pos_bits);
    packing->gss_mask = gll_bits_mask(packing->gss_bits);
    packing->dot_mask = gll_bits_mask(packing->dot_bits);
    packing->dot_shift = packing->pos_bits;
    packing->prod_shift = (uint8_t)(packing->dot_shift + packing->dot_bits);
    packing->gss_shift = (uint8_t)(packing->prod_shift + packing->prod_bits);
    return true;
}

static bool gll_rec_desc_pack(const CettaLpNativeGllRecDescPacking *packing,
                              int32_t prod_idx,
                              uint32_t dot,
                              uint32_t gss_idx,
                              uint32_t pos,
                              uint64_t *out) {
    uint64_t key;

    if (!packing || !out || prod_idx < 0)
        return false;
    if (((uint64_t)(uint32_t)prod_idx >> packing->prod_bits) != 0 ||
        ((uint64_t)dot >> packing->dot_bits) != 0 ||
        ((uint64_t)gss_idx >> packing->gss_bits) != 0 ||
        ((uint64_t)pos >> packing->pos_bits) != 0) {
        return false;
    }
    key = (uint64_t)pos;
    key |= ((uint64_t)dot << packing->dot_shift);
    key |= ((uint64_t)(uint32_t)prod_idx << packing->prod_shift);
    key |= ((uint64_t)gss_idx << packing->gss_shift);
    if (key == UINT64_MAX)
        return false;
    *out = key;
    return true;
}

static void gll_rec_desc_unpack(const CettaLpNativeGllRecDescPacking *packing,
                                uint64_t key,
                                int32_t *prod_idx,
                                uint32_t *dot,
                                uint32_t *gss_idx,
                                uint32_t *pos) {
    *pos = (uint32_t)(key & packing->pos_mask);
    *dot = (uint32_t)((key >> packing->dot_shift) & packing->dot_mask);
    *prod_idx = (int32_t)((key >> packing->prod_shift) &
                          gll_bits_mask(packing->prod_bits));
    *gss_idx = (uint32_t)((key >> packing->gss_shift) & packing->gss_mask);
}

static bool gll_span_packing_init(CettaLpNativeGllSpanPacking *packing,
                                  uint32_t production_len,
                                  uint32_t token_len) {
    uint32_t used;

    if (!packing || production_len == 0)
        return false;
    memset(packing, 0, sizeof(*packing));
    packing->prod_bits = gll_bits_needed(production_len - 1u);
    packing->pos_bits = gll_bits_needed(token_len);
    used = (uint32_t)packing->prod_bits + 2u * (uint32_t)packing->pos_bits;
    if (used >= 64u)
        return false;
    packing->prod_mask = gll_bits_mask(packing->prod_bits);
    packing->pos_mask = gll_bits_mask(packing->pos_bits);
    packing->origin_shift = packing->prod_bits;
    packing->end_shift = (uint8_t)(packing->origin_shift + packing->pos_bits);
    return true;
}

static bool gll_span_pack(const CettaLpNativeGllSpanPacking *packing,
                          int32_t prod_idx,
                          uint32_t origin,
                          uint32_t end,
                          uint64_t *out) {
    uint64_t key;

    if (!packing || !out || prod_idx < 0)
        return false;
    if (((uint64_t)(uint32_t)prod_idx >> packing->prod_bits) != 0 ||
        ((uint64_t)origin >> packing->pos_bits) != 0 ||
        ((uint64_t)end >> packing->pos_bits) != 0) {
        return false;
    }
    key = (uint64_t)(uint32_t)prod_idx;
    key |= ((uint64_t)origin << packing->origin_shift);
    key |= ((uint64_t)end << packing->end_shift);
    if (key == UINT64_MAX)
        return false;
    *out = key;
    return true;
}

static bool u64stack_push(CettaLpNativeU64Stack *stack, uint64_t value) {
    enum { BLOCK_LEN = 65536u };
    uint32_t block_idx;
    uint32_t offset;

    if (!stack)
        return false;
    if (stack->len == stack->block_len * BLOCK_LEN) {
        if (!grow_storage((void **)&stack->blocks, &stack->block_len,
                          &stack->block_cap, sizeof(*stack->blocks))) {
            return false;
        }
        stack->blocks[stack->block_len] =
            cetta_malloc(sizeof(*stack->blocks[stack->block_len]) * BLOCK_LEN);
        stack->block_len++;
    }
    block_idx = stack->len / BLOCK_LEN;
    offset = stack->len % BLOCK_LEN;
    stack->blocks[block_idx][offset] = value;
    stack->len++;
    return true;
}

static bool u64stack_pop(CettaLpNativeU64Stack *stack, uint64_t *out) {
    enum { BLOCK_LEN = 65536u };
    uint32_t block_idx;
    uint32_t offset;

    if (!stack || !out || stack->len == 0)
        return false;
    stack->len--;
    block_idx = stack->len / BLOCK_LEN;
    offset = stack->len % BLOCK_LEN;
    *out = stack->blocks[block_idx][offset];
    if (offset == 0) {
        free(stack->blocks[block_idx]);
        stack->blocks[block_idx] = NULL;
        stack->block_len--;
    }
    return true;
}

static void u64stack_free(CettaLpNativeU64Stack *stack) {
    uint32_t i;

    if (!stack)
        return;
    for (i = 0; i < stack->block_len; i++)
        free(stack->blocks[i]);
    free(stack->blocks);
    memset(stack, 0, sizeof(*stack));
}

static bool gll_rec_desc_capacity_for(uint32_t min_len,
                                      uint32_t old_cap,
                                      uint32_t *out) {
    uint64_t needed = ((uint64_t)min_len * 9u + 6u) / 7u;
    uint64_t geometric;

    if (!out)
        return false;
    if (needed < 16u)
        needed = 16u;
    if (old_cap > 0) {
        geometric = ((uint64_t)old_cap * 13u + 7u) / 8u;
        if (geometric > needed)
            needed = geometric;
    }
    if (needed > UINT32_MAX)
        return false;
    *out = (uint32_t)needed;
    return true;
}

enum { CETTA_LP_NATIVE_GLL_REC_DESC_CHUNK_LEN = 65536u };
enum { CETTA_LP_NATIVE_GLL_REC_DESC_COMPACT_BITS = 48u };

static uint32_t gll_rec_desc_chunk_count(uint32_t cap) {
    return (cap + CETTA_LP_NATIVE_GLL_REC_DESC_CHUNK_LEN - 1u) /
        CETTA_LP_NATIVE_GLL_REC_DESC_CHUNK_LEN;
}

static uint64_t gll_rec_desc_compact_max(void) {
    return (UINT64_C(1) << CETTA_LP_NATIVE_GLL_REC_DESC_COMPACT_BITS) - 1u;
}

static bool gll_rec_desc_alloc_compact_chunk(
    CettaLpNativeGllRecDescSet *set,
    uint32_t chunk_idx) {
    if (!set->slot_lo_chunks[chunk_idx]) {
        set->slot_lo_chunks[chunk_idx] =
            cetta_malloc(sizeof(*set->slot_lo_chunks[chunk_idx]) *
                         CETTA_LP_NATIVE_GLL_REC_DESC_CHUNK_LEN);
        memset(set->slot_lo_chunks[chunk_idx], 0,
               sizeof(*set->slot_lo_chunks[chunk_idx]) *
               CETTA_LP_NATIVE_GLL_REC_DESC_CHUNK_LEN);
    }
    if (!set->slot_hi_chunks[chunk_idx]) {
        set->slot_hi_chunks[chunk_idx] =
            cetta_malloc(sizeof(*set->slot_hi_chunks[chunk_idx]) *
                         CETTA_LP_NATIVE_GLL_REC_DESC_CHUNK_LEN);
        memset(set->slot_hi_chunks[chunk_idx], 0,
               sizeof(*set->slot_hi_chunks[chunk_idx]) *
               CETTA_LP_NATIVE_GLL_REC_DESC_CHUNK_LEN);
    }
    return true;
}

static bool gll_rec_desc_alloc_wide_chunk(CettaLpNativeGllRecDescSet *set,
                                          uint32_t chunk_idx) {
    if (!set->slot_chunks[chunk_idx]) {
        set->slot_chunks[chunk_idx] =
            cetta_malloc(sizeof(*set->slot_chunks[chunk_idx]) *
                         CETTA_LP_NATIVE_GLL_REC_DESC_CHUNK_LEN);
        memset(set->slot_chunks[chunk_idx], 0,
               sizeof(*set->slot_chunks[chunk_idx]) *
               CETTA_LP_NATIVE_GLL_REC_DESC_CHUNK_LEN);
    }
    return true;
}

static bool gll_rec_desc_promote_slots(CettaLpNativeGllRecDescSet *set);

static uint64_t gll_rec_desc_slot_get(CettaLpNativeGllRecDescSet *set,
                                      uint32_t slot) {
    uint32_t chunk_idx;
    uint32_t offset;

    if (!set || slot >= set->cap)
        return 0;
    chunk_idx = slot / CETTA_LP_NATIVE_GLL_REC_DESC_CHUNK_LEN;
    offset = slot % CETTA_LP_NATIVE_GLL_REC_DESC_CHUNK_LEN;
    if (set->wide_slots) {
        if (!set->slot_chunks || !set->slot_chunks[chunk_idx])
            return 0;
        return set->slot_chunks[chunk_idx][offset];
    }
    if (!set->slot_lo_chunks || !set->slot_hi_chunks ||
        !set->slot_lo_chunks[chunk_idx] || !set->slot_hi_chunks[chunk_idx]) {
        return 0;
    }
    return (uint64_t)set->slot_lo_chunks[chunk_idx][offset] |
        ((uint64_t)set->slot_hi_chunks[chunk_idx][offset] << 32);
}

static bool gll_rec_desc_slot_set(CettaLpNativeGllRecDescSet *set,
                                  uint32_t slot,
                                  uint64_t value) {
    uint32_t chunk_idx;
    uint32_t offset;

    if (!set || slot >= set->cap)
        return false;
    chunk_idx = slot / CETTA_LP_NATIVE_GLL_REC_DESC_CHUNK_LEN;
    offset = slot % CETTA_LP_NATIVE_GLL_REC_DESC_CHUNK_LEN;
    if (!set->wide_slots && value > gll_rec_desc_compact_max()) {
        if (!gll_rec_desc_promote_slots(set))
            return false;
    }
    if (set->wide_slots) {
        if (!gll_rec_desc_alloc_wide_chunk(set, chunk_idx))
            return false;
        set->slot_chunks[chunk_idx][offset] = value;
        return true;
    }
    if (!gll_rec_desc_alloc_compact_chunk(set, chunk_idx))
        return false;
    set->slot_lo_chunks[chunk_idx][offset] = (uint32_t)value;
    set->slot_hi_chunks[chunk_idx][offset] = (uint16_t)(value >> 32);
    return true;
}

static uint64_t gll_rec_desc_slot_value(uint64_t key) {
    return key + UINT64_C(1);
}

static uint64_t gll_rec_desc_key_value(uint64_t slot_value) {
    return slot_value - UINT64_C(1);
}

static bool gll_rec_desc_insert_existing(CettaLpNativeGllRecDescSet *set,
                                         uint64_t key) {
    uint32_t slot;

    if (set->cap == 0 ||
        (set->wide_slots && !set->slot_chunks) ||
        (!set->wide_slots && (!set->slot_lo_chunks || !set->slot_hi_chunks)))
        return false;
    slot = gll_hash_slot_mod(gll_hash_u64(UINT64_C(1469598103934665603), key),
                             set->cap);
    while (gll_rec_desc_slot_get(set, slot) != 0)
        slot = (slot + 1u == set->cap) ? 0 : slot + 1u;
    return gll_rec_desc_slot_set(set, slot, gll_rec_desc_slot_value(key));
}

static bool gll_rec_desc_promote_slots(CettaLpNativeGllRecDescSet *set) {
    uint64_t **wide_chunks;
    uint32_t chunk_count;
    uint32_t chunk_idx;
    uint32_t i;

    if (!set || set->wide_slots)
        return true;
    chunk_count = gll_rec_desc_chunk_count(set->cap);
    wide_chunks = cetta_malloc(sizeof(*wide_chunks) * chunk_count);
    memset(wide_chunks, 0, sizeof(*wide_chunks) * chunk_count);
    for (chunk_idx = 0; chunk_idx < chunk_count; chunk_idx++) {
        uint32_t base = chunk_idx * CETTA_LP_NATIVE_GLL_REC_DESC_CHUNK_LEN;
        uint32_t limit = set->cap - base;
        if (limit > CETTA_LP_NATIVE_GLL_REC_DESC_CHUNK_LEN)
            limit = CETTA_LP_NATIVE_GLL_REC_DESC_CHUNK_LEN;
        if (!set->slot_lo_chunks || !set->slot_hi_chunks ||
            !set->slot_lo_chunks[chunk_idx] || !set->slot_hi_chunks[chunk_idx]) {
            continue;
        }
        wide_chunks[chunk_idx] =
            cetta_malloc(sizeof(*wide_chunks[chunk_idx]) *
                         CETTA_LP_NATIVE_GLL_REC_DESC_CHUNK_LEN);
        memset(wide_chunks[chunk_idx], 0,
               sizeof(*wide_chunks[chunk_idx]) *
               CETTA_LP_NATIVE_GLL_REC_DESC_CHUNK_LEN);
        for (i = 0; i < limit; i++) {
            wide_chunks[chunk_idx][i] =
                (uint64_t)set->slot_lo_chunks[chunk_idx][i] |
                ((uint64_t)set->slot_hi_chunks[chunk_idx][i] << 32);
        }
        free(set->slot_lo_chunks[chunk_idx]);
        free(set->slot_hi_chunks[chunk_idx]);
    }
    free(set->slot_lo_chunks);
    free(set->slot_hi_chunks);
    set->slot_lo_chunks = NULL;
    set->slot_hi_chunks = NULL;
    set->slot_chunks = wide_chunks;
    set->wide_slots = true;
    return true;
}

static bool gll_rec_desc_rebuild(CettaLpNativeGllRecDescSet *set,
                                 uint32_t min_len) {
    CettaLpNativeGllRecDescSet old = *set;
    uint32_t old_cap = set->cap;
    uint32_t old_chunk_count = gll_rec_desc_chunk_count(old_cap);
    uint32_t cap;
    uint32_t chunk_count;
    uint32_t chunk_idx;
    uint32_t i;

    if (!gll_rec_desc_capacity_for(min_len, old_cap, &cap))
        return false;
    chunk_count = gll_rec_desc_chunk_count(cap);
    set->slot_chunks = NULL;
    set->slot_lo_chunks = NULL;
    set->slot_hi_chunks = NULL;
    if (old.wide_slots) {
        set->slot_chunks = cetta_malloc(sizeof(*set->slot_chunks) * chunk_count);
        memset(set->slot_chunks, 0, sizeof(*set->slot_chunks) * chunk_count);
    } else {
        set->slot_lo_chunks =
            cetta_malloc(sizeof(*set->slot_lo_chunks) * chunk_count);
        set->slot_hi_chunks =
            cetta_malloc(sizeof(*set->slot_hi_chunks) * chunk_count);
        memset(set->slot_lo_chunks, 0,
               sizeof(*set->slot_lo_chunks) * chunk_count);
        memset(set->slot_hi_chunks, 0,
               sizeof(*set->slot_hi_chunks) * chunk_count);
    }
    set->cap = cap;
    set->wide_slots = old.wide_slots;
    for (chunk_idx = 0; chunk_idx < old_chunk_count; chunk_idx++) {
        uint32_t base = chunk_idx * CETTA_LP_NATIVE_GLL_REC_DESC_CHUNK_LEN;
        uint32_t limit = old_cap - base;
        if (limit > CETTA_LP_NATIVE_GLL_REC_DESC_CHUNK_LEN)
            limit = CETTA_LP_NATIVE_GLL_REC_DESC_CHUNK_LEN;
        for (i = 0; i < limit; i++) {
            uint64_t old_slot = gll_rec_desc_slot_get(&old, base + i);
            if (old_slot != 0 &&
                !gll_rec_desc_insert_existing(
                    set, gll_rec_desc_key_value(old_slot))) {
                return false;
            }
        }
        if (old.wide_slots) {
            free(old.slot_chunks ? old.slot_chunks[chunk_idx] : NULL);
            if (old.slot_chunks)
                old.slot_chunks[chunk_idx] = NULL;
        } else {
            free(old.slot_lo_chunks ? old.slot_lo_chunks[chunk_idx] : NULL);
            free(old.slot_hi_chunks ? old.slot_hi_chunks[chunk_idx] : NULL);
            if (old.slot_lo_chunks)
                old.slot_lo_chunks[chunk_idx] = NULL;
            if (old.slot_hi_chunks)
                old.slot_hi_chunks[chunk_idx] = NULL;
        }
    }
    free(old.slot_chunks);
    free(old.slot_lo_chunks);
    free(old.slot_hi_chunks);
    return true;
}

static bool gll_rec_desc_set_insert(CettaLpNativeGllRecDescSet *seen,
                                    uint64_t key,
                                    bool *inserted) {
    uint32_t slot;

    if (!seen)
        return false;
    if (inserted)
        *inserted = false;
    if (seen->cap == 0 ||
        ((uint64_t)(seen->len + 1u) * 10u) >= ((uint64_t)seen->cap * 9u)) {
        if (!gll_rec_desc_rebuild(seen, seen->len + 1u))
            return false;
    }
    slot = gll_hash_slot_mod(gll_hash_u64(UINT64_C(1469598103934665603), key),
                             seen->cap);
    while (true) {
        uint64_t slot_value = gll_rec_desc_slot_get(seen, slot);
        if (slot_value == 0)
            break;
        if (gll_rec_desc_key_value(slot_value) == key)
            return true;
        slot = (slot + 1u == seen->cap) ? 0 : slot + 1u;
    }
    if (!gll_rec_desc_slot_set(seen, slot, gll_rec_desc_slot_value(key)))
        return false;
    seen->len++;
    if (inserted)
        *inserted = true;
    return true;
}

static bool gll_rec_desc_enqueue(CettaLpNativeGllRecDescSet *seen,
                                 CettaLpNativeU64Stack *work,
                                 uint64_t key) {
    bool inserted = false;

    if (!work)
        return false;
    if (!gll_rec_desc_set_insert(seen, key, &inserted))
        return false;
    if (!inserted)
        return true;
    return u64stack_push(work, key);
}

static void gll_rec_desc_set_free(CettaLpNativeGllRecDescSet *set) {
    uint32_t chunk_count;
    uint32_t i;

    if (!set)
        return;
    chunk_count = gll_rec_desc_chunk_count(set->cap);
    for (i = 0; i < chunk_count; i++) {
        free(set->slot_chunks ? set->slot_chunks[i] : NULL);
        free(set->slot_lo_chunks ? set->slot_lo_chunks[i] : NULL);
        free(set->slot_hi_chunks ? set->slot_hi_chunks[i] : NULL);
    }
    free(set->slot_chunks);
    free(set->slot_lo_chunks);
    free(set->slot_hi_chunks);
    memset(set, 0, sizeof(*set));
}

static bool gll_rec_desc_pack_enqueue(
    const CettaLpNativeGllRecDescPacking *packing,
    CettaLpNativeGllRecDescSet *seen,
    CettaLpNativeU64Stack *work,
    int32_t prod_idx,
    uint32_t dot,
    uint32_t gss_idx,
    uint32_t pos) {
    uint64_t key;

    if (!gll_rec_desc_pack(packing, prod_idx, dot, gss_idx, pos, &key))
        return false;
    return gll_rec_desc_enqueue(seen, work, key);
}

static int32_t gll_rec_gss_find_key(const CettaLpNativeGllRecGssNodeVec *nodes,
                                    uint64_t key) {
    uint32_t i;

    if (nodes->index.slots && nodes->index.cap > 0) {
        uint32_t slot = gll_hash_slot(gll_hash_u64(
            UINT64_C(1469598103934665603), key), nodes->index.cap);
        while (nodes->index.slots[slot] != 0) {
            uint32_t idx = nodes->index.slots[slot] - 1u;
            if (idx < nodes->len &&
                gll_rec_gss_key_equals(&nodes->data[idx], key)) {
                return (int32_t)idx;
            }
            slot = gll_index_next_slot(slot, nodes->index.cap);
        }
        return -1;
    }
    for (i = 0; i < nodes->len; i++) {
        if (gll_rec_gss_key_equals(&nodes->data[i], key)) {
            return (int32_t)i;
        }
    }
    return -1;
}

static int32_t gll_rec_gss_get(CettaLpNativeGllRecGssNodeVec *nodes,
                               bool is_root,
                               int32_t prod_idx,
                               uint32_t dot,
                               uint32_t pos) {
    int32_t found;
    uint64_t key;

    if (!gll_rec_gss_key_pack(&nodes->packing, is_root, prod_idx, dot, pos,
                              &key))
        return -1;
    if (!gll_rec_gss_index_ensure(nodes, nodes->len + 1u))
        return -1;
    found = gll_rec_gss_find_key(nodes, key);
    if (found >= 0)
        return found;
    if (!grow_storage((void **)&nodes->data, &nodes->len, &nodes->cap,
                      sizeof(*nodes->data))) {
        return -1;
    }
    memset(&nodes->data[nodes->len], 0, sizeof(nodes->data[nodes->len]));
    nodes->data[nodes->len].key = key;
    nodes->data[nodes->len].first_edge = CETTA_LP_NATIVE_NODE_NONE;
    nodes->data[nodes->len].first_popped = CETTA_LP_NATIVE_NODE_NONE;
    nodes->len++;
    gll_rec_gss_index_insert_existing(nodes, nodes->len - 1u);
    return (int32_t)(nodes->len - 1);
}

static bool gll_rec_gss_add_edge(CettaLpNativeGllRecGssNodeVec *nodes,
                                 uint32_t gss_idx,
                                 uint32_t parent_gss) {
    CettaLpNativeGllRecGssNode *node;
    uint32_t edge_idx;

    if (!nodes || gss_idx >= nodes->len)
        return false;
    node = &nodes->data[gss_idx];
    if (!gll_rec_link_push(&nodes->edges, parent_gss, node->first_edge,
                           &edge_idx)) {
        return false;
    }
    node->first_edge = edge_idx;
    return true;
}

static int32_t gll_node_find(const CettaLpNativeGllNodeVec *nodes,
                             CettaLpNativeGllNodeKind kind,
                             SymbolId symbol,
                             int32_t prod_idx,
                             uint32_t dot,
                             uint32_t left,
                             uint32_t right,
                             CettaLpNativeUtf8TerminalValueKind
                                 terminal_value_kind,
                             uint32_t terminal_value) {
    uint32_t i;

    if (nodes->index.slots && nodes->index.cap > 0) {
        uint32_t slot = gll_hash_slot(
            gll_node_key_hash(kind, symbol, prod_idx, dot, left, right,
                              terminal_value_kind, terminal_value),
            nodes->index.cap);
        while (nodes->index.slots[slot] != 0) {
            uint32_t idx = nodes->index.slots[slot] - 1u;
            if (idx < nodes->len &&
                gll_node_key_equals(&nodes->data[idx], kind, symbol, prod_idx,
                                    dot, left, right, terminal_value_kind,
                                    terminal_value)) {
                return (int32_t)idx;
            }
            slot = gll_index_next_slot(slot, nodes->index.cap);
        }
        return -1;
    }
    for (i = 0; i < nodes->len; i++) {
        const CettaLpNativeGllNode *cur = &nodes->data[i];
        if (gll_node_key_equals(cur, kind, symbol, prod_idx, dot, left, right,
                                terminal_value_kind, terminal_value)) {
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
                            uint32_t right,
                            CettaLpNativeUtf8TerminalValueKind
                                terminal_value_kind,
                            uint32_t terminal_value) {
    int32_t found;

    if (!gll_node_index_ensure(nodes, nodes->len + 1u))
        return -1;
    found = gll_node_find(nodes, kind, symbol, prod_idx, dot, left, right,
                          terminal_value_kind, terminal_value);
    if (found >= 0)
        return found;
    if (!grow_storage((void **)&nodes->data, &nodes->len, &nodes->cap,
                      sizeof(*nodes->data))) {
        return -1;
    }
    memset(&nodes->data[nodes->len], 0, sizeof(nodes->data[nodes->len]));
    if (!gll_node_set_key(&nodes->data[nodes->len], kind, symbol, prod_idx,
                          dot, terminal_value_kind, terminal_value)) {
        return -1;
    }
    nodes->data[nodes->len].left = left;
    nodes->data[nodes->len].right = right;
    nodes->data[nodes->len].first_choice = CETTA_LP_NATIVE_NODE_NONE;
    nodes->len++;
    gll_node_index_insert_existing(nodes, nodes->len - 1u);
    return (int32_t)(nodes->len - 1);
}

static int32_t gll_node_get_term(CettaLpNativeGllNodeVec *nodes,
                                 SymbolId symbol,
                                 uint32_t pos) {
    return gll_node_get(nodes, CETTA_LP_NATIVE_GLL_NODE_TERM,
                        symbol, -1, 0, pos, pos + 1,
                        CETTA_LP_NATIVE_UTF8_TERMINAL_VALUE_SCALAR, 0u);
}

static int32_t gll_node_get_term_value(
    CettaLpNativeGllNodeVec *nodes,
    SymbolId symbol,
    uint32_t left,
    uint32_t right,
    CettaLpNativeUtf8TerminalValueKind terminal_value_kind,
    uint32_t terminal_value) {
    return gll_node_get(nodes, CETTA_LP_NATIVE_GLL_NODE_TERM,
                        symbol, -1, 0, left, right,
                        terminal_value_kind, terminal_value);
}

static int32_t gll_node_get_eps(CettaLpNativeGllNodeVec *nodes,
                                uint32_t pos) {
    return gll_node_get(nodes, CETTA_LP_NATIVE_GLL_NODE_EPS,
                        0, -1, 0, pos, pos,
                        CETTA_LP_NATIVE_UTF8_TERMINAL_VALUE_SCALAR, 0u);
}

static int32_t gll_node_get_sym(CettaLpNativeGllNodeVec *nodes,
                                SymbolId symbol,
                                uint32_t left,
                                uint32_t right) {
    return gll_node_get(nodes, CETTA_LP_NATIVE_GLL_NODE_SYM,
                        symbol, -1, 0, left, right,
                        CETTA_LP_NATIVE_UTF8_TERMINAL_VALUE_SCALAR, 0u);
}

static int32_t gll_node_get_inter(CettaLpNativeGllNodeVec *nodes,
                                  int32_t prod_idx,
                                  uint32_t dot,
                                  uint32_t left,
                                  uint32_t right) {
    return gll_node_get(nodes, CETTA_LP_NATIVE_GLL_NODE_INTER,
                        0, prod_idx, dot, left, right,
                        CETTA_LP_NATIVE_UTF8_TERMINAL_VALUE_SCALAR, 0u);
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
    const CettaLpNativeGllNodeVec *nodes,
    const CettaLpNativeGllNode *node) {
    const CettaLpNativeGllPackedChoice *best;
    uint32_t choice_idx;

    if (!nodes || !node ||
        node->first_choice == CETTA_LP_NATIVE_NODE_NONE ||
        node->first_choice >= nodes->packed.len) {
        return NULL;
    }
    best = &nodes->packed.data[node->first_choice];
    for (choice_idx = best->next_idx;
         choice_idx != CETTA_LP_NATIVE_NODE_NONE;
         choice_idx = nodes->packed.data[choice_idx].next_idx) {
        const CettaLpNativeGllPackedChoice *cur;
        uint32_t cur_pivot;
        uint32_t best_pivot;
        int32_t cur_prod_idx;
        int32_t best_prod_idx;
        if (choice_idx >= nodes->packed.len)
            return NULL;
        cur = &nodes->packed.data[choice_idx];
        cur_pivot = gll_choice_pivot(nodes, cur);
        best_pivot = gll_choice_pivot(nodes, best);
        cur_prod_idx = gll_choice_prod_idx(nodes, cur);
        best_prod_idx = gll_choice_prod_idx(nodes, best);
        if (cur->left_idx < best->left_idx ||
            (cur->left_idx == best->left_idx &&
             (cur->right_idx < best->right_idx ||
              (cur->right_idx == best->right_idx &&
               (cur_pivot < best_pivot ||
                (cur_pivot == best_pivot &&
                 cur_prod_idx < best_prod_idx)))))) {
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
            !gll_node_push_packed_unique(nodes, (uint32_t)node_idx,
                                         left_idx, right_idx, pivot, prod_idx)) {
            slr_summary_set_error(error_buf, error_buf_size,
                                  "failed to record GLL SPPF symbol node");
            return -1;
        }
        return node_idx;
    }
    node_idx = gll_node_get_inter(nodes, prod_idx, dot, left, right);
    if (node_idx < 0 ||
        !gll_node_push_packed_unique(nodes, (uint32_t)node_idx,
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

    if (nodes->index.slots && nodes->index.cap > 0) {
        uint32_t slot = gll_hash_slot(
            gll_gss_key_hash(is_root, prod_idx, dot, pos), nodes->index.cap);
        while (nodes->index.slots[slot] != 0) {
            uint32_t idx = nodes->index.slots[slot] - 1u;
            if (idx < nodes->len &&
                gll_gss_key_equals(&nodes->data[idx], is_root, prod_idx, dot, pos)) {
                return (int32_t)idx;
            }
            slot = gll_index_next_slot(slot, nodes->index.cap);
        }
        return -1;
    }
    for (i = 0; i < nodes->len; i++) {
        const CettaLpNativeGllGssNode *cur = &nodes->data[i];
        if (gll_gss_key_equals(cur, is_root, prod_idx, dot, pos)) {
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
    int32_t found;

    if (!gll_gss_index_ensure(nodes, nodes->len + 1u))
        return -1;
    found = gll_gss_find(nodes, is_root, prod_idx, dot, pos);
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
    gll_gss_index_insert_existing(nodes, nodes->len - 1u);
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
    return gll_desc_key_equals(lhs, rhs->prod_idx, rhs->dot, rhs->gss_idx,
                               rhs->left_label, rhs->pos);
}

static int32_t gll_desc_find(const CettaLpNativeGllDescriptorVec *descs,
                             const CettaLpNativeGllDescriptor *desc) {
    uint32_t i;

    if (descs->index.slots && descs->index.cap > 0) {
        uint32_t slot = gll_hash_slot(
            gll_desc_key_hash(desc->prod_idx, desc->dot, desc->gss_idx,
                              desc->left_label, desc->pos),
            descs->index.cap);
        while (descs->index.slots[slot] != 0) {
            uint32_t idx = descs->index.slots[slot] - 1u;
            CettaLpNativeGllDescriptor cur;
            if (gll_descvec_get(descs, idx, &cur) &&
                gll_desc_equals(&cur, desc)) {
                return (int32_t)idx;
            }
            slot = gll_index_next_slot(slot, descs->index.cap);
        }
        return -1;
    }
    for (i = 0; i < descs->len; i++) {
        CettaLpNativeGllDescriptor cur;
        if (gll_descvec_get(descs, i, &cur) &&
            gll_desc_equals(&cur, desc)) {
            return (int32_t)i;
        }
    }
    return -1;
}

static bool gll_desc_enqueue(CettaLpNativeGllDescriptorVec *seen,
                             CettaLpNativeU32Vec *work,
                             int32_t prod_idx,
                             uint32_t dot,
                             uint32_t gss_idx,
                             uint32_t left_label,
                             uint32_t pos) {
    CettaLpNativeGllDescriptor desc;
    uint32_t desc_idx;

    desc.prod_idx = prod_idx;
    desc.dot = dot;
    desc.gss_idx = gss_idx;
    desc.left_label = left_label;
    desc.pos = pos;
    if (!gll_desc_index_ensure(seen, seen->len + 1u))
        return false;
    if (gll_desc_find(seen, &desc) >= 0)
        return true;
    if (!gll_descvec_append(seen, &desc, &desc_idx))
        return false;
    gll_desc_index_insert_existing(seen, desc_idx);
    if (!u32vec_push(work, desc_idx))
        return false;
    return true;
}

static int32_t gll_count_node(const CettaLpNativeGllNodeVec *nodes,
                              uint32_t node_idx,
                              uint8_t *memo_seen,
                              uint8_t *memo_value) {
    const CettaLpNativeGllNode *node;
    int32_t total = 0;
    uint32_t choice_idx;
    CettaLpNativeGllNodeKind kind;

    node = &nodes->data[node_idx];
    kind = gll_node_kind_value(node);
    if (kind == CETTA_LP_NATIVE_GLL_NODE_TERM ||
        kind == CETTA_LP_NATIVE_GLL_NODE_EPS) {
        return 1;
    }
    if (memo_seen[node_idx])
        return memo_value[node_idx];
    memo_seen[node_idx] = 1;
    memo_value[node_idx] = 0;
    for (choice_idx = node->first_choice;
         choice_idx != CETTA_LP_NATIVE_NODE_NONE;
         choice_idx = nodes->packed.data[choice_idx].next_idx) {
        const CettaLpNativeGllPackedChoice *choice;
        int32_t left_count = 1;
        int32_t right_count;
        if (choice_idx >= nodes->packed.len)
            return 0;
        choice = &nodes->packed.data[choice_idx];
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
        if (gll_node_kind_value(left) == CETTA_LP_NATIVE_GLL_NODE_INTER) {
            choice = gll_pick_choice(nodes, left);
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

static Atom *cetta_lp_native_gll_recognize_tokens(
    const CettaLpNativeGrammar *grammar,
    SymbolId start_nt,
    const CettaLpNativeInputTokenVec *tokens,
    Arena *arena,
    char *error_buf,
    size_t error_buf_size);

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
    CettaLpNativeGllNodeKind kind = gll_node_kind_value(node);
    int32_t choice_prod_idx;

    if (kind == CETTA_LP_NATIVE_GLL_NODE_TERM) {
        return make_tok_cert(arena, gll_node_symbol_value(node), node->left);
    }
    if (kind == CETTA_LP_NATIVE_GLL_NODE_EPS) {
        return make_eps_cert(arena);
    }

    choice = gll_pick_choice(nodes, node);
    if (!choice) {
        slr_summary_set_error(error_buf, error_buf_size,
                              "GLL cert extraction found node without packed choice");
        return NULL;
    }
    choice_prod_idx = gll_choice_prod_idx(nodes, choice);
    if (!gll_collect_spine(nodes, choice->left_idx, choice->right_idx, &kids)) {
        slr_summary_set_error(error_buf, error_buf_size,
                              "failed to flatten GLL cert spine");
        goto fail;
    }

    if (kind == CETTA_LP_NATIVE_GLL_NODE_SYM) {
        SymbolId lhs = 0;
        const CettaLpNativeSymbol *rhs = NULL;
        uint32_t rhs_len = 0;
        slr_get_prod(productions, production_len, start_nt,
                     choice_prod_idx, &lhs, &rhs, &rhs_len);
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
            if (gll_node_kind_value(child) != CETTA_LP_NATIVE_GLL_NODE_TERM ||
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
            result = make_node_cert(arena, gll_prod_label(grammar, choice_prod_idx), lhs,
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
            result = make_node_cert(arena, gll_prod_label(grammar, choice_prod_idx), lhs,
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
    CettaLpNativeU32Vec work = {0};
    uint32_t root_idx = CETTA_LP_NATIVE_NODE_NONE;
    uint32_t max_rhs_len = 0;
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
    max_rhs_len = gll_max_rhs_len(productions, production_len);
    gll_descvec_init_compact(&seen, production_len, max_rhs_len, tokens.len);
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
        uint32_t cur_idx = work.data[work.len - 1];
        CettaLpNativeGllDescriptor cur;
        SymbolId lhs = 0;
        const CettaLpNativeSymbol *rhs = NULL;
        uint32_t rhs_len = 0;

        work.len--;
        if (!gll_descvec_get(&seen, cur_idx, &cur)) {
            slr_summary_set_error(error_buf, error_buf_size,
                                  "bad GLL descriptor index");
            goto fail;
        }
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
                    !gll_node_push_packed_unique(&nodes, (uint32_t)sym_idx,
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
                                 start_nt, -1, 0, 0, tokens.len,
                                 CETTA_LP_NATIVE_UTF8_TERMINAL_VALUE_SCALAR,
                                 0u);
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
    gll_descvec_free(&seen);
    free(work.data);
    slr_productions_free(productions, production_len);
    return result;

fail:
    free(tokens.data);
    gll_nodevec_free(&nodes);
    gll_gssvec_free(&gss_nodes);
    gll_descvec_free(&seen);
    free(work.data);
    slr_productions_free(productions, production_len);
    return NULL;
}

Atom *cetta_lp_native_gll_recognize(const CettaLpNativeGrammar *grammar,
                                    SymbolId start_nt,
                                    Atom *token_list,
                                    Arena *arena,
                                    char *error_buf,
                                    size_t error_buf_size) {
    CettaLpNativeInputTokenVec tokens = {0};
    Atom *result;

    if (!input_tokens_from_list(token_list, &tokens, error_buf, error_buf_size))
        return NULL;
    result = cetta_lp_native_gll_recognize_tokens(
        grammar, start_nt, &tokens, arena, error_buf, error_buf_size);
    free(tokens.data);
    return result;
}

static uint32_t gll_total_packed_choices(const CettaLpNativeGllNodeVec *nodes) {
    return nodes ? nodes->packed.len : 0;
}

static uint32_t gll_kind_count(const CettaLpNativeGllNodeVec *nodes,
                               CettaLpNativeGllNodeKind kind) {
    uint32_t total = 0;
    uint32_t i;

    for (i = 0; i < nodes->len; i++) {
        if (gll_node_kind_value(&nodes->data[i]) == kind)
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

static Atom *gll_recognize_atom(Arena *arena,
                                const char *status,
                                uint32_t token_len,
                                uint32_t gss_len,
                                uint32_t descriptor_len) {
    Atom **elems = arena_alloc(arena, sizeof(Atom *) * 5);

    elems[0] = atom_symbol(arena, "GParseGLLRecognize");
    elems[1] = atom_symbol(arena, status);
    elems[2] = atom_int(arena, token_len);
    elems[3] = atom_int(arena, gss_len);
    elems[4] = atom_int(arena, descriptor_len);
    return atom_expr(arena, elems, 5);
}

static Atom *gll_span_summary_atom(Arena *arena,
                                   const char *status,
                                   uint32_t token_len,
                                   uint32_t span_len,
                                   uint32_t gss_len,
                                   uint32_t descriptor_len) {
    Atom **elems = arena_alloc(arena, sizeof(Atom *) * 6);

    elems[0] = atom_symbol(arena, "GParseGLLSpanSummary");
    elems[1] = atom_symbol(arena, status);
    elems[2] = atom_int(arena, token_len);
    elems[3] = atom_int(arena, span_len);
    elems[4] = atom_int(arena, gss_len);
    elems[5] = atom_int(arena, descriptor_len);
    return atom_expr(arena, elems, 6);
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
    uint32_t choice_idx;

    if (!nodes || idx == CETTA_LP_NATIVE_NODE_NONE || idx >= nodes->len)
        return;
    if (seen[idx])
        return;
    seen[idx] = 1;
    node = &nodes->data[idx];
    if (gll_node_kind_value(node) != CETTA_LP_NATIVE_GLL_NODE_SYM &&
        gll_node_kind_value(node) != CETTA_LP_NATIVE_GLL_NODE_INTER) {
        return;
    }
    for (choice_idx = node->first_choice;
         choice_idx != CETTA_LP_NATIVE_NODE_NONE;
         choice_idx = nodes->packed.data[choice_idx].next_idx) {
        const CettaLpNativeGllPackedChoice *choice;
        if (choice_idx >= nodes->packed.len)
            return;
        choice = &nodes->packed.data[choice_idx];
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
        if (gll_node_kind_value(node) == CETTA_LP_NATIVE_GLL_NODE_TERM) {
            kind = 0;
            symbol = gll_node_symbol_value(node);
        } else if (gll_node_kind_value(node) == CETTA_LP_NATIVE_GLL_NODE_EPS) {
            kind = 1;
        } else if (gll_node_kind_value(node) == CETTA_LP_NATIVE_GLL_NODE_SYM) {
            kind = 2;
            symbol = gll_node_symbol_value(node);
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
                             const CettaLpNativeGllNodeVec *nodes,
                             const CettaLpNativeGllPackedChoice *choice) {
    Atom **elems = arena_alloc(arena, sizeof(Atom *) * 5);

    elems[0] = atom_symbol(arena, "GChoice");
    elems[1] = gll_idx_atom(arena, choice->left_idx);
    elems[2] = gll_idx_atom(arena, choice->right_idx);
    elems[3] = atom_int(arena, gll_choice_pivot(nodes, choice));
    elems[4] = atom_int(arena, gll_choice_prod_idx(nodes, choice));
    return atom_expr(arena, elems, 5);
}

static Atom *gll_choices_list_atom(Arena *arena,
                                   const CettaLpNativeGllNodeVec *nodes,
                                   const CettaLpNativeGllNode *node) {
    Atom **items = NULL;
    uint32_t item_idx = 0;
    uint32_t count = 0;
    uint32_t choice_idx;

    for (choice_idx = node->first_choice;
         choice_idx != CETTA_LP_NATIVE_NODE_NONE;
         choice_idx = nodes->packed.data[choice_idx].next_idx) {
        if (choice_idx >= nodes->packed.len)
            break;
        count++;
    }
    if (count > 0)
        items = arena_alloc(arena, sizeof(Atom *) * count);
    for (choice_idx = node->first_choice;
         choice_idx != CETTA_LP_NATIVE_NODE_NONE;
         choice_idx = nodes->packed.data[choice_idx].next_idx) {
        if (choice_idx >= nodes->packed.len)
            break;
        items[item_idx++] = gll_choice_atom(arena, nodes,
                                            &nodes->packed.data[choice_idx]);
    }
    return make_cons_list(arena, items, item_idx);
}

static Atom *gll_node_data_atom(Arena *arena,
                                const CettaLpNativeGllNodeVec *nodes,
                                uint32_t idx) {
    const CettaLpNativeGllNode *node = &nodes->data[idx];
    Atom **elems;
    CettaLpNativeGllNodeKind kind = gll_node_kind_value(node);

    if (kind == CETTA_LP_NATIVE_GLL_NODE_TERM) {
        elems = arena_alloc(arena, sizeof(Atom *) * 5);
        elems[0] = atom_symbol(arena, "GNodeTerm");
        elems[1] = atom_int(arena, idx);
        elems[2] = atom_symbol_id(arena, gll_node_symbol_value(node));
        elems[3] = atom_int(arena, node->left);
        elems[4] = atom_int(arena, node->right);
        return atom_expr(arena, elems, 5);
    }
    if (kind == CETTA_LP_NATIVE_GLL_NODE_EPS) {
        elems = arena_alloc(arena, sizeof(Atom *) * 4);
        elems[0] = atom_symbol(arena, "GNodeEps");
        elems[1] = atom_int(arena, idx);
        elems[2] = atom_int(arena, node->left);
        elems[3] = atom_int(arena, node->right);
        return atom_expr(arena, elems, 4);
    }
    if (kind == CETTA_LP_NATIVE_GLL_NODE_SYM) {
        elems = arena_alloc(arena, sizeof(Atom *) * 6);
        elems[0] = atom_symbol(arena, "GNodeSym");
        elems[1] = atom_int(arena, idx);
        elems[2] = atom_symbol_id(arena, gll_node_symbol_value(node));
        elems[3] = atom_int(arena, node->left);
        elems[4] = atom_int(arena, node->right);
        elems[5] = gll_choices_list_atom(arena, nodes, node);
        return atom_expr(arena, elems, 6);
    }

    elems = arena_alloc(arena, sizeof(Atom *) * 7);
    elems[0] = atom_symbol(arena, "GNodeInter");
    elems[1] = atom_int(arena, idx);
    elems[2] = atom_int(arena, gll_node_prod_idx_value(node));
    elems[3] = atom_int(arena, gll_node_dot_value(node));
    elems[4] = atom_int(arena, node->left);
    elems[5] = atom_int(arena, node->right);
    elems[6] = gll_choices_list_atom(arena, nodes, node);
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

static Atom *cetta_lp_native_gll_recognize_tokens(
    const CettaLpNativeGrammar *grammar,
    SymbolId start_nt,
    const CettaLpNativeInputTokenVec *tokens,
    Arena *arena,
    char *error_buf,
    size_t error_buf_size) {
    CettaLpNativeSlrProduction *productions = NULL;
    uint32_t production_len = 0;
    CettaLpNativeGllRecGssNodeVec gss_nodes = {0};
    CettaLpNativeGllRecDescSet seen = {0};
    CettaLpNativeU64Stack work = {0};
    CettaLpNativeGllRecDescPacking packing;
    uint32_t max_rhs_len = 0;
    bool accepted = false;
    Atom *result = NULL;

    if (!grammar || !tokens || !arena) {
        slr_summary_set_error(error_buf, error_buf_size,
                              "bad GLL recognize args");
        return NULL;
    }
    if (!slr_build_productions(grammar, &productions, &production_len,
                               error_buf, error_buf_size) ||
        !slr_grammar_mentions_nonterminal(productions, production_len, start_nt)) {
        goto fail;
    }
    max_rhs_len = gll_max_rhs_len(productions, production_len);
    if (!gll_rec_desc_packing_init(&packing, production_len,
                                   max_rhs_len, tokens->len)) {
        slr_summary_set_error(error_buf, error_buf_size,
                              "GLL recognizer descriptor key is too wide");
        goto fail;
    }
    if (!gll_rec_gss_packing_init(&gss_nodes.packing, production_len,
                                  max_rhs_len, tokens->len)) {
        slr_summary_set_error(error_buf, error_buf_size,
                              "GLL recognizer GSS key is too wide");
        goto fail;
    }
    if (gll_rec_gss_get(&gss_nodes, true, -1, 0, 0) != 0) {
        slr_summary_set_error(error_buf, error_buf_size,
                              "failed to initialize GLL recognizer root");
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
            if (!gll_rec_desc_pack_enqueue(&packing, &seen, &work,
                                           (int32_t)prod_idx, 0, 0, 0)) {
                slr_summary_set_error(error_buf, error_buf_size,
                                      "failed to seed GLL recognizer descriptors");
                goto fail;
            }
        }
    }

    while (work.len > 0) {
        uint64_t cur_key;
        int32_t cur_prod_idx;
        uint32_t cur_dot;
        uint32_t cur_gss_idx;
        uint32_t cur_pos;
        SymbolId lhs = 0;
        const CettaLpNativeSymbol *rhs = NULL;
        uint32_t rhs_len = 0;

        if (!u64stack_pop(&work, &cur_key)) {
            slr_summary_set_error(error_buf, error_buf_size,
                                  "failed to pop GLL recognizer descriptor");
            goto fail;
        }
        gll_rec_desc_unpack(&packing, cur_key, &cur_prod_idx, &cur_dot,
                            &cur_gss_idx, &cur_pos);
        slr_get_prod(productions, production_len, start_nt,
                     cur_prod_idx, &lhs, &rhs, &rhs_len);
        if (cur_dot < rhs_len) {
            CettaLpNativeSymbol sym = rhs[cur_dot];
            if (sym.kind == CETTA_LP_NATIVE_SYMBOL_TM) {
                if (cur_pos >= tokens->len ||
                    tokens->data[cur_pos].term_kind != sym.name) {
                    continue;
                }
                if (!gll_rec_desc_pack_enqueue(&packing, &seen, &work,
                                               cur_prod_idx, cur_dot + 1,
                                               cur_gss_idx, cur_pos + 1)) {
                    slr_summary_set_error(error_buf, error_buf_size,
                                          "failed to advance GLL recognizer terminal");
                    goto fail;
                }
                continue;
            }
            {
                int32_t next_gss = gll_rec_gss_get(&gss_nodes, false,
                                                   cur_prod_idx, cur_dot + 1,
                                                   cur_pos);
                uint32_t prod_idx;
                uint32_t popped_idx;
                if (next_gss < 0 ||
                    !gll_rec_gss_add_edge(&gss_nodes, (uint32_t)next_gss,
                                          cur_gss_idx)) {
                    slr_summary_set_error(error_buf, error_buf_size,
                                          "failed to create GLL recognizer continuation");
                    goto fail;
                }
                for (popped_idx = gss_nodes.data[next_gss].first_popped;
                     popped_idx != CETTA_LP_NATIVE_NODE_NONE;
                     ) {
                    uint32_t done_pos;
                    uint32_t next_popped;
                    if (!gll_rec_link_get(&gss_nodes.popped, popped_idx,
                                          &done_pos, &next_popped)) {
                        slr_summary_set_error(error_buf, error_buf_size,
                                              "bad GLL recognizer popped index");
                        goto fail;
                    }
                    if (!gll_rec_desc_pack_enqueue(&packing, &seen, &work,
                                                   cur_prod_idx, cur_dot + 1,
                                                   cur_gss_idx, done_pos)) {
                        slr_summary_set_error(error_buf, error_buf_size,
                                              "failed to resume GLL recognizer continuation");
                        goto fail;
                    }
                    popped_idx = next_popped;
                }
                for (prod_idx = 0; prod_idx < production_len; prod_idx++) {
                    SymbolId child_lhs = 0;
                    const CettaLpNativeSymbol *child_rhs = NULL;
                    uint32_t child_rhs_len = 0;
                    slr_get_prod(productions, production_len, start_nt,
                                 (int32_t)prod_idx, &child_lhs, &child_rhs,
                                 &child_rhs_len);
                    (void)child_rhs;
                    (void)child_rhs_len;
                    if (child_lhs != sym.name)
                        continue;
                    if (!gll_rec_desc_pack_enqueue(&packing, &seen, &work,
                                                   (int32_t)prod_idx, 0,
                                                   (uint32_t)next_gss, cur_pos)) {
                        slr_summary_set_error(error_buf, error_buf_size,
                                              "failed to seed GLL recognizer child");
                        goto fail;
                    }
                }
                continue;
            }
        }
        {
            uint32_t edge_idx;
            bool resume_root;
            int32_t resume_prod_idx;
            uint32_t resume_dot;
            uint32_t resume_pos;
            if (cur_gss_idx == 0) {
                if (cur_pos == tokens->len)
                    accepted = true;
                continue;
            }
            if (!gll_rec_gss_add_popped(&gss_nodes, cur_gss_idx, cur_pos)) {
                slr_summary_set_error(error_buf, error_buf_size,
                                      "failed to record GLL recognizer completion");
                goto fail;
            }
            gll_rec_gss_key_unpack(&gss_nodes.packing,
                                   gss_nodes.data[cur_gss_idx].key,
                                   &resume_root, &resume_prod_idx,
                                   &resume_dot, &resume_pos);
            (void)resume_root;
            (void)resume_pos;
            for (edge_idx = gss_nodes.data[cur_gss_idx].first_edge;
                 edge_idx != CETTA_LP_NATIVE_NODE_NONE;
                 ) {
                uint32_t parent_gss;
                uint32_t next_edge;
                if (!gll_rec_link_get(&gss_nodes.edges, edge_idx,
                                      &parent_gss, &next_edge)) {
                    slr_summary_set_error(error_buf, error_buf_size,
                                          "bad GLL recognizer edge index");
                    goto fail;
                }
                if (!gll_rec_desc_pack_enqueue(&packing, &seen, &work,
                                               resume_prod_idx, resume_dot,
                                               parent_gss, cur_pos)) {
                    slr_summary_set_error(error_buf, error_buf_size,
                                          "failed to propagate GLL recognizer completion");
                    goto fail;
                }
                edge_idx = next_edge;
            }
        }
    }

    result = gll_recognize_atom(arena, accepted ? "Accepted" : "Rejected",
                                tokens->len, gss_nodes.len, seen.len);
    gll_rec_gssvec_free(&gss_nodes);
    gll_rec_desc_set_free(&seen);
    u64stack_free(&work);
    slr_productions_free(productions, production_len);
    return result;

fail:
    gll_rec_gssvec_free(&gss_nodes);
    gll_rec_desc_set_free(&seen);
    u64stack_free(&work);
    slr_productions_free(productions, production_len);
    return NULL;
}

static Atom *cetta_lp_native_gll_forest_result_tokens(
    const CettaLpNativeGrammar *grammar,
    SymbolId start_nt,
    const CettaLpNativeInputTokenVec *tokens,
    Arena *arena,
    char *error_buf,
    size_t error_buf_size,
    CettaLpNativeForestOutput output) {
    CettaLpNativeSlrProduction *productions = NULL;
    uint32_t production_len = 0;
    CettaLpNativeGllNodeVec nodes = {0};
    CettaLpNativeGllGssNodeVec gss_nodes = {0};
    CettaLpNativeGllDescriptorVec seen = {0};
    CettaLpNativeU32Vec work = {0};
    uint32_t root_count = 0;
    int32_t root_idx = -1;
    const char *status = "NoParse";
    uint32_t max_rhs_len = 0;
    Atom *result = NULL;

    if (!grammar || !tokens || !arena) {
        slr_summary_set_error(error_buf, error_buf_size,
                              "bad GLL forest-summary args");
        return NULL;
    }
    if (!slr_build_productions(grammar, &productions, &production_len,
                               error_buf, error_buf_size) ||
        !slr_grammar_mentions_nonterminal(productions, production_len, start_nt)) {
        goto fail;
    }
    max_rhs_len = gll_max_rhs_len(productions, production_len);
    gll_descvec_init_compact(&seen, production_len, max_rhs_len, tokens->len);
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
        uint32_t cur_idx = work.data[work.len - 1];
        CettaLpNativeGllDescriptor cur;
        SymbolId lhs = 0;
        const CettaLpNativeSymbol *rhs = NULL;
        uint32_t rhs_len = 0;

        work.len--;
        if (!gll_descvec_get(&seen, cur_idx, &cur)) {
            slr_summary_set_error(error_buf, error_buf_size,
                                  "bad GLL descriptor index");
            goto fail;
        }
        slr_get_prod(productions, production_len, start_nt,
                     cur.prod_idx, &lhs, &rhs, &rhs_len);
        if (cur.dot < rhs_len) {
            CettaLpNativeSymbol sym = rhs[cur.dot];
            if (sym.kind == CETTA_LP_NATIVE_SYMBOL_TM) {
                int32_t term_idx;
                int32_t parent_idx;
                if (cur.pos >= tokens->len || tokens->data[cur.pos].term_kind != sym.name)
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
                    !gll_node_push_packed_unique(&nodes, (uint32_t)sym_idx,
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
                                 start_nt, -1, 0, 0, tokens->len,
                                 CETTA_LP_NATIVE_UTF8_TERMINAL_VALUE_SCALAR,
                                 0u);
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
        result = gll_forest_data_atom(arena, status, tokens->len, root_count,
                                      root_idx, &nodes);
    } else if (output == CETTA_LP_NATIVE_FOREST_OUT_SIGNATURE) {
        result = gll_forest_signature_atom(arena, status, tokens->len,
                                           root_count, root_idx, &nodes);
        if (!result) {
            slr_summary_set_error(error_buf, error_buf_size,
                                  "failed to build GLL forest signature");
            goto fail;
        }
    } else if (output == CETTA_LP_NATIVE_FOREST_OUT_SIGNATURE_DIGEST) {
        result = gll_forest_signature_digest_atom(arena, status, tokens->len,
                                                  root_count, root_idx, &nodes);
        if (!result) {
            slr_summary_set_error(error_buf, error_buf_size,
                                  "failed to build GLL forest signature digest");
            goto fail;
        }
    } else {
        result = gll_forest_summary_atom(arena, status, tokens->len, &nodes,
                                         &gss_nodes, seen.len, root_count);
    }

    gll_nodevec_free(&nodes);
    gll_gssvec_free(&gss_nodes);
    gll_descvec_free(&seen);
    free(work.data);
    slr_productions_free(productions, production_len);
    return result;

fail:
    gll_nodevec_free(&nodes);
    gll_gssvec_free(&gss_nodes);
    gll_descvec_free(&seen);
    free(work.data);
    slr_productions_free(productions, production_len);
    return NULL;
}

static Atom *cetta_lp_native_gll_forest_result(const CettaLpNativeGrammar *grammar,
                                               SymbolId start_nt,
                                               Atom *token_list,
                                               Arena *arena,
                                               char *error_buf,
                                               size_t error_buf_size,
                                               CettaLpNativeForestOutput output) {
    CettaLpNativeInputTokenVec tokens = {0};
    Atom *result;

    if (!input_tokens_from_list(token_list, &tokens, error_buf, error_buf_size))
        return NULL;
    result = cetta_lp_native_gll_forest_result_tokens(
        grammar, start_nt, &tokens, arena, error_buf, error_buf_size, output);
    free(tokens.data);
    return result;
}

static Atom *cetta_lp_native_gll_forest_result_token_file(
    const CettaLpNativeGrammar *grammar,
    SymbolId start_nt,
    const char *token_filename,
    Arena *arena,
    char *error_buf,
    size_t error_buf_size,
    CettaLpNativeForestOutput output) {
    CettaLpNativeInputTokenVec tokens = {0};
    Atom *result;

    if (!input_tokens_from_file(arena, token_filename, &tokens,
                                error_buf, error_buf_size))
        return NULL;
    result = cetta_lp_native_gll_forest_result_tokens(
        grammar, start_nt, &tokens, arena, error_buf, error_buf_size, output);
    free(tokens.data);
    return result;
}

static Atom *cetta_lp_native_gll_span_summary_tokens(
    const CettaLpNativeGrammar *grammar,
    SymbolId start_nt,
    const CettaLpNativeInputTokenVec *tokens,
    Arena *arena,
    char *error_buf,
    size_t error_buf_size) {
    CettaLpNativeSlrProduction *productions = NULL;
    uint32_t production_len = 0;
    CettaLpNativeGllRecGssNodeVec gss_nodes = {0};
    CettaLpNativeGllRecDescSet seen = {0};
    CettaLpNativeGllRecDescSet spans = {0};
    CettaLpNativeU64Stack work = {0};
    CettaLpNativeGllRecDescPacking desc_packing;
    CettaLpNativeGllSpanPacking span_packing;
    uint32_t max_rhs_len = 0;
    bool accepted = false;
    Atom *result = NULL;

    if (!grammar || !tokens || !arena) {
        slr_summary_set_error(error_buf, error_buf_size,
                              "bad GLL span-summary args");
        return NULL;
    }
    if (!slr_build_productions(grammar, &productions, &production_len,
                               error_buf, error_buf_size) ||
        !slr_grammar_mentions_nonterminal(productions, production_len, start_nt)) {
        goto fail;
    }
    max_rhs_len = gll_max_rhs_len(productions, production_len);
    if (!gll_rec_desc_packing_init(&desc_packing, production_len,
                                   max_rhs_len, tokens->len)) {
        slr_summary_set_error(error_buf, error_buf_size,
                              "GLL span-summary descriptor key is too wide");
        goto fail;
    }
    if (!gll_span_packing_init(&span_packing, production_len, tokens->len)) {
        slr_summary_set_error(error_buf, error_buf_size,
                              "GLL span-summary relation key is too wide");
        goto fail;
    }
    if (!gll_rec_gss_packing_init(&gss_nodes.packing, production_len,
                                  max_rhs_len, tokens->len)) {
        slr_summary_set_error(error_buf, error_buf_size,
                              "GLL span-summary GSS key is too wide");
        goto fail;
    }
    if (gll_rec_gss_get(&gss_nodes, true, -1, 0, 0) != 0) {
        slr_summary_set_error(error_buf, error_buf_size,
                              "failed to initialize GLL span-summary root");
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
            if (!gll_rec_desc_pack_enqueue(&desc_packing, &seen, &work,
                                           (int32_t)prod_idx, 0, 0, 0)) {
                slr_summary_set_error(error_buf, error_buf_size,
                                      "failed to seed GLL span-summary descriptors");
                goto fail;
            }
        }
    }

    while (work.len > 0) {
        uint64_t cur_key;
        int32_t cur_prod_idx;
        uint32_t cur_dot;
        uint32_t cur_gss_idx;
        uint32_t cur_pos;
        SymbolId lhs = 0;
        const CettaLpNativeSymbol *rhs = NULL;
        uint32_t rhs_len = 0;

        if (!u64stack_pop(&work, &cur_key)) {
            slr_summary_set_error(error_buf, error_buf_size,
                                  "failed to pop GLL span-summary descriptor");
            goto fail;
        }
        gll_rec_desc_unpack(&desc_packing, cur_key, &cur_prod_idx, &cur_dot,
                            &cur_gss_idx, &cur_pos);
        slr_get_prod(productions, production_len, start_nt,
                     cur_prod_idx, &lhs, &rhs, &rhs_len);
        if (cur_dot < rhs_len) {
            CettaLpNativeSymbol sym = rhs[cur_dot];
            if (sym.kind == CETTA_LP_NATIVE_SYMBOL_TM) {
                if (cur_pos >= tokens->len ||
                    tokens->data[cur_pos].term_kind != sym.name) {
                    continue;
                }
                if (!gll_rec_desc_pack_enqueue(&desc_packing, &seen, &work,
                                               cur_prod_idx, cur_dot + 1,
                                               cur_gss_idx, cur_pos + 1)) {
                    slr_summary_set_error(error_buf, error_buf_size,
                                          "failed to advance GLL span-summary terminal");
                    goto fail;
                }
                continue;
            }
            {
                int32_t next_gss = gll_rec_gss_get(&gss_nodes, false,
                                                   cur_prod_idx, cur_dot + 1,
                                                   cur_pos);
                uint32_t prod_idx;
                uint32_t popped_idx;
                if (next_gss < 0 ||
                    !gll_rec_gss_add_edge(&gss_nodes, (uint32_t)next_gss,
                                          cur_gss_idx)) {
                    slr_summary_set_error(error_buf, error_buf_size,
                                          "failed to create GLL span-summary continuation");
                    goto fail;
                }
                for (popped_idx = gss_nodes.data[next_gss].first_popped;
                     popped_idx != CETTA_LP_NATIVE_NODE_NONE;
                     ) {
                    uint32_t done_pos;
                    uint32_t next_popped;
                    if (!gll_rec_link_get(&gss_nodes.popped, popped_idx,
                                          &done_pos, &next_popped)) {
                        slr_summary_set_error(error_buf, error_buf_size,
                                              "bad GLL span-summary popped index");
                        goto fail;
                    }
                    if (!gll_rec_desc_pack_enqueue(&desc_packing, &seen, &work,
                                                   cur_prod_idx, cur_dot + 1,
                                                   cur_gss_idx, done_pos)) {
                        slr_summary_set_error(error_buf, error_buf_size,
                                              "failed to resume GLL span-summary continuation");
                        goto fail;
                    }
                    popped_idx = next_popped;
                }
                for (prod_idx = 0; prod_idx < production_len; prod_idx++) {
                    SymbolId child_lhs = 0;
                    const CettaLpNativeSymbol *child_rhs = NULL;
                    uint32_t child_rhs_len = 0;
                    slr_get_prod(productions, production_len, start_nt,
                                 (int32_t)prod_idx, &child_lhs, &child_rhs,
                                 &child_rhs_len);
                    (void)child_rhs;
                    (void)child_rhs_len;
                    if (child_lhs != sym.name)
                        continue;
                    if (!gll_rec_desc_pack_enqueue(&desc_packing, &seen, &work,
                                                   (int32_t)prod_idx, 0,
                                                   (uint32_t)next_gss, cur_pos)) {
                        slr_summary_set_error(error_buf, error_buf_size,
                                              "failed to seed GLL span-summary child");
                        goto fail;
                    }
                }
                continue;
            }
        }
        {
            uint64_t span_key;
            uint32_t edge_idx;
            bool resume_root = false;
            int32_t resume_prod_idx = -1;
            uint32_t resume_dot = 0;
            uint32_t resume_pos = 0;
            uint32_t origin = 0;

            if (cur_gss_idx != 0) {
                gll_rec_gss_key_unpack(&gss_nodes.packing,
                                       gss_nodes.data[cur_gss_idx].key,
                                       &resume_root, &resume_prod_idx,
                                       &resume_dot, &resume_pos);
                (void)resume_root;
                origin = resume_pos;
            }
            if (!gll_span_pack(&span_packing, cur_prod_idx, origin, cur_pos,
                               &span_key) ||
                !gll_rec_desc_set_insert(&spans, span_key, NULL)) {
                slr_summary_set_error(error_buf, error_buf_size,
                                      "failed to record GLL completed span");
                goto fail;
            }
            if (cur_gss_idx == 0) {
                if (cur_pos == tokens->len)
                    accepted = true;
                continue;
            }
            if (!gll_rec_gss_add_popped(&gss_nodes, cur_gss_idx, cur_pos)) {
                slr_summary_set_error(error_buf, error_buf_size,
                                      "failed to record GLL span-summary completion");
                goto fail;
            }
            for (edge_idx = gss_nodes.data[cur_gss_idx].first_edge;
                 edge_idx != CETTA_LP_NATIVE_NODE_NONE;
                 ) {
                uint32_t parent_gss;
                uint32_t next_edge;
                if (!gll_rec_link_get(&gss_nodes.edges, edge_idx,
                                      &parent_gss, &next_edge)) {
                    slr_summary_set_error(error_buf, error_buf_size,
                                          "bad GLL span-summary edge index");
                    goto fail;
                }
                if (!gll_rec_desc_pack_enqueue(&desc_packing, &seen, &work,
                                               resume_prod_idx, resume_dot,
                                               parent_gss, cur_pos)) {
                    slr_summary_set_error(error_buf, error_buf_size,
                                          "failed to propagate GLL span-summary completion");
                    goto fail;
                }
                edge_idx = next_edge;
            }
        }
    }

    result = gll_span_summary_atom(arena, accepted ? "Accepted" : "Rejected",
                                   tokens->len, spans.len, gss_nodes.len,
                                   seen.len);
    gll_rec_gssvec_free(&gss_nodes);
    gll_rec_desc_set_free(&seen);
    gll_rec_desc_set_free(&spans);
    u64stack_free(&work);
    slr_productions_free(productions, production_len);
    return result;

fail:
    gll_rec_gssvec_free(&gss_nodes);
    gll_rec_desc_set_free(&seen);
    gll_rec_desc_set_free(&spans);
    u64stack_free(&work);
    slr_productions_free(productions, production_len);
    return NULL;
}

Atom *cetta_lp_native_gll_span_summary(const CettaLpNativeGrammar *grammar,
                                       SymbolId start_nt,
                                       Atom *token_list,
                                       Arena *arena,
                                       char *error_buf,
                                       size_t error_buf_size) {
    CettaLpNativeInputTokenVec tokens = {0};
    Atom *result;

    if (!input_tokens_from_list(token_list, &tokens, error_buf, error_buf_size))
        return NULL;
    result = cetta_lp_native_gll_span_summary_tokens(
        grammar, start_nt, &tokens, arena, error_buf, error_buf_size);
    free(tokens.data);
    return result;
}

Atom *cetta_lp_native_gll_recognize_token_file(
    const CettaLpNativeGrammar *grammar,
    SymbolId start_nt,
    const char *token_filename,
    Arena *arena,
    char *error_buf,
    size_t error_buf_size) {
    CettaLpNativeInputTokenVec tokens = {0};
    Atom *result;

    if (!input_tokens_from_file(arena, token_filename, &tokens,
                                error_buf, error_buf_size))
        return NULL;
    result = cetta_lp_native_gll_recognize_tokens(
        grammar, start_nt, &tokens, arena, error_buf, error_buf_size);
    free(tokens.data);
    return result;
}

Atom *cetta_lp_native_gll_span_summary_token_file(
    const CettaLpNativeGrammar *grammar,
    SymbolId start_nt,
    const char *token_filename,
    Arena *arena,
    char *error_buf,
    size_t error_buf_size) {
    CettaLpNativeInputTokenVec tokens = {0};
    Atom *result;

    if (!input_tokens_from_file(arena, token_filename, &tokens,
                                error_buf, error_buf_size))
        return NULL;
    result = cetta_lp_native_gll_span_summary_tokens(
        grammar, start_nt, &tokens, arena, error_buf, error_buf_size);
    free(tokens.data);
    return result;
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

Atom *cetta_lp_native_gll_forest_summary_token_file(
    const CettaLpNativeGrammar *grammar,
    SymbolId start_nt,
    const char *token_filename,
    Arena *arena,
    char *error_buf,
    size_t error_buf_size) {
    return cetta_lp_native_gll_forest_result_token_file(
        grammar, start_nt, token_filename, arena, error_buf, error_buf_size,
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

Atom *cetta_lp_native_gll_forest_signature_digest_token_file(
    const CettaLpNativeGrammar *grammar,
    SymbolId start_nt,
    const char *token_filename,
    Arena *arena,
    char *error_buf,
    size_t error_buf_size) {
    return cetta_lp_native_gll_forest_result_token_file(
        grammar, start_nt, token_filename, arena, error_buf, error_buf_size,
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

typedef struct {
    const uint8_t *bytes;
    size_t byte_len;
    size_t byte_pos;
    uint32_t *codepoints;
    uint32_t *byte_offsets;
    uint32_t scalar_len;
    uint32_t scalar_cap;
    bool complete;
    bool owns_arrays;
    uint32_t decoded_byte_len;
    uint32_t source_pass_count;
} CettaLpNativeUtf8Input;

static bool utf8_scalar_valid(uint32_t scalar) {
    return scalar <= UINT32_C(0x10ffff) &&
        !(scalar >= UINT32_C(0xd800) && scalar <= UINT32_C(0xdfff));
}

static bool utf8_decode_one(const uint8_t *bytes,
                                size_t len,
                                size_t pos,
                                uint32_t *scalar,
                                uint32_t *width) {
    uint8_t first;

    if (!bytes || !scalar || !width || pos >= len)
        return false;
    first = bytes[pos];
    if (first <= UINT8_C(0x7f)) {
        *scalar = first;
        *width = 1u;
        return true;
    }
    if (first >= UINT8_C(0xc2) && first <= UINT8_C(0xdf)) {
        uint8_t second;
        if (pos + 1u >= len)
            return false;
        second = bytes[pos + 1u];
        if ((second & UINT8_C(0xc0)) != UINT8_C(0x80))
            return false;
        *scalar = ((uint32_t)(first & UINT8_C(0x1f)) << 6) |
            (uint32_t)(second & UINT8_C(0x3f));
        *width = 2u;
        return true;
    }
    if (first >= UINT8_C(0xe0) && first <= UINT8_C(0xef)) {
        uint8_t second;
        uint8_t third;
        if (pos + 2u >= len)
            return false;
        second = bytes[pos + 1u];
        third = bytes[pos + 2u];
        if ((third & UINT8_C(0xc0)) != UINT8_C(0x80) ||
            (first == UINT8_C(0xe0) &&
             (second < UINT8_C(0xa0) || second > UINT8_C(0xbf))) ||
            (first == UINT8_C(0xed) &&
             (second < UINT8_C(0x80) || second > UINT8_C(0x9f))) ||
            ((first != UINT8_C(0xe0) && first != UINT8_C(0xed)) &&
             (second & UINT8_C(0xc0)) != UINT8_C(0x80))) {
            return false;
        }
        *scalar = ((uint32_t)(first & UINT8_C(0x0f)) << 12) |
            ((uint32_t)(second & UINT8_C(0x3f)) << 6) |
            (uint32_t)(third & UINT8_C(0x3f));
        *width = 3u;
        return utf8_scalar_valid(*scalar);
    }
    if (first >= UINT8_C(0xf0) && first <= UINT8_C(0xf4)) {
        uint8_t second;
        uint8_t third;
        uint8_t fourth;
        if (pos + 3u >= len)
            return false;
        second = bytes[pos + 1u];
        third = bytes[pos + 2u];
        fourth = bytes[pos + 3u];
        if ((third & UINT8_C(0xc0)) != UINT8_C(0x80) ||
            (fourth & UINT8_C(0xc0)) != UINT8_C(0x80) ||
            (first == UINT8_C(0xf0) &&
             (second < UINT8_C(0x90) || second > UINT8_C(0xbf))) ||
            (first == UINT8_C(0xf4) &&
             (second < UINT8_C(0x80) || second > UINT8_C(0x8f))) ||
            ((first != UINT8_C(0xf0) && first != UINT8_C(0xf4)) &&
             (second & UINT8_C(0xc0)) != UINT8_C(0x80))) {
            return false;
        }
        *scalar = ((uint32_t)(first & UINT8_C(0x07)) << 18) |
            ((uint32_t)(second & UINT8_C(0x3f)) << 12) |
            ((uint32_t)(third & UINT8_C(0x3f)) << 6) |
            (uint32_t)(fourth & UINT8_C(0x3f));
        *width = 4u;
        return utf8_scalar_valid(*scalar);
    }
    return false;
}

static void utf8_input_init(CettaLpNativeUtf8Input *input,
                                const uint8_t *bytes,
                                size_t byte_len) {
    memset(input, 0, sizeof(*input));
    input->bytes = bytes;
    input->byte_len = byte_len;
    input->owns_arrays = true;
    input->source_pass_count = 1u;
}

static void utf8_input_init_scalar_view(
    CettaLpNativeUtf8Input *input,
    const CettaLpNativeUtf8ScalarView *view) {
    memset(input, 0, sizeof(*input));
    input->byte_len = view->input_byte_len;
    input->byte_pos = view->input_byte_len;
    input->codepoints = (uint32_t *)view->codepoints;
    input->byte_offsets = (uint32_t *)view->byte_offsets;
    input->scalar_len = view->scalar_len;
    input->scalar_cap = view->scalar_len;
    input->complete = true;
    input->owns_arrays = false;
    input->decoded_byte_len = view->decoded_byte_len;
    input->source_pass_count = view->source_pass_count;
}

static void utf8_input_free(CettaLpNativeUtf8Input *input) {
    if (!input)
        return;
    if (input->owns_arrays) {
        free(input->codepoints);
        free(input->byte_offsets);
    }
    memset(input, 0, sizeof(*input));
}

static bool utf8_input_reserve(CettaLpNativeUtf8Input *input,
                                   uint32_t needed) {
    uint32_t cap;

    if (needed <= input->scalar_cap)
        return true;
    cap = input->scalar_cap ? input->scalar_cap : 16u;
    while (cap < needed) {
        if (cap > UINT32_MAX / 2u)
            return false;
        cap *= 2u;
    }
    input->codepoints = cetta_realloc(
        input->codepoints, sizeof(*input->codepoints) * cap);
    input->byte_offsets = cetta_realloc(
        input->byte_offsets, sizeof(*input->byte_offsets) * ((size_t)cap + 1u));
    input->scalar_cap = cap;
    if (input->scalar_len == 0u)
        input->byte_offsets[0] = 0u;
    return true;
}

static bool utf8_input_decode_next(CettaLpNativeUtf8Input *input,
                                       char *error_buf,
                                       size_t error_buf_size) {
    uint32_t scalar;
    uint32_t width;
    size_t start;

    if (input->byte_pos == input->byte_len) {
        input->complete = true;
        return true;
    }
    start = input->byte_pos;
    if (!utf8_decode_one(input->bytes, input->byte_len, start,
                             &scalar, &width)) {
        slr_summary_set_error(error_buf, error_buf_size,
                              "invalid UTF-8 at byte offset %zu", start);
        return false;
    }
    if (input->scalar_len == UINT32_MAX ||
        !utf8_input_reserve(input, input->scalar_len + 1u)) {
        slr_summary_set_error(error_buf, error_buf_size,
                              "UTF-8 scalar index is too large");
        return false;
    }
    input->codepoints[input->scalar_len] = scalar;
    input->byte_pos += width;
    input->decoded_byte_len = (uint32_t)input->byte_pos;
    input->scalar_len++;
    input->byte_offsets[input->scalar_len] = (uint32_t)input->byte_pos;
    if (input->byte_pos == input->byte_len)
        input->complete = true;
    return true;
}

static bool utf8_input_ensure(CettaLpNativeUtf8Input *input,
                                  uint32_t position,
                                  char *error_buf,
                                  size_t error_buf_size) {
    while (!input->complete && input->scalar_len <= position) {
        if (!utf8_input_decode_next(input, error_buf, error_buf_size))
            return false;
    }
    return true;
}

static bool utf8_input_finish(CettaLpNativeUtf8Input *input,
                                  char *error_buf,
                                  size_t error_buf_size) {
    while (!input->complete) {
        if (!utf8_input_decode_next(input, error_buf, error_buf_size))
            return false;
    }
    if (!input->byte_offsets) {
        if (!input->owns_arrays || !utf8_input_reserve(input, 1u))
            return false;
    }
    if (input->owns_arrays)
        input->byte_offsets[0] = 0u;
    return true;
}

void cetta_lp_native_utf8_scalar_buffer_init(
    CettaLpNativeUtf8ScalarBuffer *buffer) {
    if (!buffer)
        return;
    memset(buffer, 0, sizeof(*buffer));
}

void cetta_lp_native_utf8_scalar_buffer_free(
    CettaLpNativeUtf8ScalarBuffer *buffer) {
    if (!buffer)
        return;
    free(buffer->byte_offsets);
    free(buffer->codepoints);
    memset(buffer, 0, sizeof(*buffer));
}

bool cetta_lp_native_utf8_scalar_buffer_decode(
    CettaLpNativeUtf8ScalarBuffer *buffer,
    const uint8_t *input_bytes,
    size_t input_byte_len,
    char *error_buf,
    size_t error_buf_size) {
    CettaLpNativeUtf8Input input;
    CettaLpNativeUtf8ScalarBuffer result;
    bool ok = false;

    memset(&input, 0, sizeof(input));
    cetta_lp_native_utf8_scalar_buffer_init(&result);
    if (error_buf && error_buf_size > 0u)
        error_buf[0] = '\0';
    if (!buffer || (input_byte_len > 0u && !input_bytes) ||
        input_byte_len > UINT32_MAX) {
        slr_summary_set_error(
            error_buf, error_buf_size,
            "bad owned UTF-8 scalar decode input");
        goto done;
    }
    utf8_input_init(&input, input_bytes, input_byte_len);
    if (!utf8_input_finish(&input, error_buf, error_buf_size)) {
        if (!error_buf || error_buf_size == 0u || error_buf[0] == '\0') {
            slr_summary_set_error(
                error_buf, error_buf_size,
                "failed to decode owned UTF-8 scalar view");
        }
        goto done;
    }
    result.codepoints = input.codepoints;
    result.byte_offsets = input.byte_offsets;
    input.codepoints = NULL;
    input.byte_offsets = NULL;
    result.view = (CettaLpNativeUtf8ScalarView){
        .codepoints = result.codepoints,
        .byte_offsets = result.byte_offsets,
        .scalar_len = input.scalar_len,
        .input_byte_len = (uint32_t)input_byte_len,
        .decoded_byte_len = (uint32_t)input_byte_len,
        .source_pass_count = 1u,
    };
    if (!cetta_lp_native_utf8_scalar_view_validate(
            &result.view, error_buf, error_buf_size)) {
        goto done;
    }
    cetta_lp_native_utf8_scalar_buffer_free(buffer);
    *buffer = result;
    memset(&result, 0, sizeof(result));
    ok = true;

done:
    utf8_input_free(&input);
    cetta_lp_native_utf8_scalar_buffer_free(&result);
    return ok;
}

static const CettaLpNativeUtf8Terminal *utf8_terminal_find(
    const CettaLpNativeUtf8Terminal *terminals,
    uint32_t terminal_len,
    uint32_t terminal_id) {
    uint32_t index;

    if (terminal_id < terminal_len &&
        terminals[terminal_id].terminal_id == terminal_id) {
        return &terminals[terminal_id];
    }
    for (index = 0u; index < terminal_len; index++) {
        if (terminals[index].terminal_id == terminal_id)
            return &terminals[index];
    }
    return NULL;
}

static bool utf8_range_contains(
    const CettaLpNativeUnicodeRange *ranges,
    uint32_t range_len,
    uint32_t scalar) {
    uint32_t low = 0u;
    uint32_t high = range_len;

    while (low < high) {
        uint32_t middle = low + (high - low) / 2u;
        if (scalar < ranges[middle].low) {
            high = middle;
        } else if (scalar > ranges[middle].high) {
            low = middle + 1u;
        } else {
            return true;
        }
    }
    return false;
}

static bool utf8_terminal_match(
    CettaLpNativeUtf8Input *input,
    const CettaLpNativeUtf8Terminal *terminal,
    uint32_t position,
    bool *matched,
    uint32_t *right,
    uint32_t *scalar,
    bool *is_eof,
    char *error_buf,
    size_t error_buf_size) {
    uint32_t value = 0u;

    *matched = false;
    *right = position;
    *scalar = 0u;
    *is_eof = false;
    if (!utf8_input_ensure(input, position, error_buf, error_buf_size))
        return false;
    if (terminal->kind == CETTA_LP_NATIVE_UTF8_TERMINAL_EOF) {
        if (input->complete && position == input->scalar_len) {
            *matched = true;
            *is_eof = true;
        }
        return true;
    }
    if (position >= input->scalar_len)
        return true;
    value = input->codepoints[position];
    if (terminal->kind == CETTA_LP_NATIVE_UTF8_TERMINAL_ANY ||
        (terminal->kind == CETTA_LP_NATIVE_UTF8_TERMINAL_SCALAR &&
         terminal->scalar == value) ||
        (terminal->kind == CETTA_LP_NATIVE_UTF8_TERMINAL_RANGES &&
         utf8_range_contains(terminal->ranges,
                                 terminal->range_len, value))) {
        *matched = true;
        *right = position + 1u;
        *scalar = value;
    }
    return true;
}

static bool utf8_terminals_validate(
    const CettaLpNativeUtf8Terminal *terminals,
    uint32_t terminal_len,
    char *error_buf,
    size_t error_buf_size) {
    uint32_t index;
    uint32_t other;

    if (terminal_len > 0u && !terminals) {
        slr_summary_set_error(error_buf, error_buf_size,
                              "UTF-8 terminal table is absent");
        return false;
    }
    for (index = 0u; index < terminal_len; index++) {
        const CettaLpNativeUtf8Terminal *terminal = &terminals[index];
        if ((uint32_t)terminal->kind >
            (uint32_t)CETTA_LP_NATIVE_UTF8_TERMINAL_RANGES) {
            slr_summary_set_error(error_buf, error_buf_size,
                                  "unknown UTF-8 terminal kind");
            return false;
        }
        for (other = 0u; other < index; other++) {
            if (terminals[other].terminal_id == terminal->terminal_id) {
                slr_summary_set_error(error_buf, error_buf_size,
                                      "duplicate UTF-8 terminal ID");
                return false;
            }
        }
        if (terminal->kind == CETTA_LP_NATIVE_UTF8_TERMINAL_SCALAR &&
            !utf8_scalar_valid(terminal->scalar)) {
            slr_summary_set_error(error_buf, error_buf_size,
                                  "terminal scalar is not Unicode");
            return false;
        }
        if (terminal->kind == CETTA_LP_NATIVE_UTF8_TERMINAL_RANGES) {
            uint32_t range_index;
            if (!terminal->ranges || terminal->range_len == 0u) {
                slr_summary_set_error(error_buf, error_buf_size,
                                      "range terminal has no ranges");
                return false;
            }
            for (range_index = 0u; range_index < terminal->range_len;
                 range_index++) {
                const CettaLpNativeUnicodeRange *range =
                    &terminal->ranges[range_index];
                if (range->low > range->high ||
                    !utf8_scalar_valid(range->low) ||
                    !utf8_scalar_valid(range->high) ||
                    (range->low <= UINT32_C(0xdfff) &&
                     range->high >= UINT32_C(0xd800)) ||
                    (range_index > 0u &&
                     terminal->ranges[range_index - 1u].high >= range->low)) {
                    slr_summary_set_error(
                        error_buf, error_buf_size,
                        "terminal ranges are not strict Unicode intervals");
                    return false;
                }
            }
        } else if (terminal->range_len != 0u || terminal->ranges) {
            slr_summary_set_error(error_buf, error_buf_size,
                                  "non-range terminal carries ranges");
            return false;
        }
    }
    return true;
}

typedef enum {
    CETTA_LP_NATIVE_PARSE_INPUT_UTF8 = 0,
    CETTA_LP_NATIVE_PARSE_INPUT_LATTICE = 1
} CettaLpNativeParseInputKind;

typedef struct {
    CettaLpNativeParseInputKind kind;
    CettaLpNativeUtf8Input *utf8;
    const CettaLpNativeUtf8Terminal *terminals;
    uint32_t terminal_len;
    const CettaLpNativeUtf8Lattice *lattice;
} CettaLpNativeParseInput;

typedef struct {
    uint32_t right;
    CettaLpNativeUtf8TerminalValueKind value_kind;
    uint32_t value;
} CettaLpNativeInputMatch;

typedef struct {
    bool single_ready;
    CettaLpNativeInputMatch single;
    uint32_t cursor;
    uint32_t end;
} CettaLpNativeInputMatchIter;

static uint32_t utf8_scalar_width(uint32_t scalar) {
    if (scalar <= UINT32_C(0x7f))
        return 1u;
    if (scalar <= UINT32_C(0x7ff))
        return 2u;
    if (scalar <= UINT32_C(0xffff))
        return 3u;
    return 4u;
}

static bool utf8_decode_receipt_valid(uint32_t input_byte_len,
                                      uint32_t decoded_byte_len,
                                      uint32_t source_pass_count) {
    return (source_pass_count == 0u && decoded_byte_len == 0u) ||
        (source_pass_count == 1u &&
         decoded_byte_len == input_byte_len);
}

bool cetta_lp_native_utf8_scalar_view_validate(
    const CettaLpNativeUtf8ScalarView *view,
    char *error_buf,
    size_t error_buf_size) {
    uint32_t index;

    if (!view || (view->scalar_len > 0u && !view->codepoints) ||
        !view->byte_offsets ||
        !utf8_decode_receipt_valid(
            view->input_byte_len, view->decoded_byte_len,
            view->source_pass_count) ||
        view->byte_offsets[0] != 0u ||
        view->byte_offsets[view->scalar_len] != view->input_byte_len) {
        slr_summary_set_error(error_buf, error_buf_size,
                              "malformed UTF-8 scalar view header");
        return false;
    }
    for (index = 0u; index < view->scalar_len; index++) {
        uint32_t scalar = view->codepoints[index];
        uint32_t left = view->byte_offsets[index];
        uint32_t right = view->byte_offsets[index + 1u];
        if (!utf8_scalar_valid(scalar) || right < left ||
            right - left != utf8_scalar_width(scalar)) {
            slr_summary_set_error(
                error_buf, error_buf_size,
                "UTF-8 scalar view index is invalid");
            return false;
        }
    }
    return true;
}

static bool utf8_lattice_terminal_declared(
    const CettaLpNativeUtf8Lattice *lattice,
    uint32_t terminal_id) {
    uint32_t low = 0u;
    uint32_t high = lattice->terminal_len;

    while (low < high) {
        uint32_t middle = low + (high - low) / 2u;
        if (lattice->terminal_ids[middle] < terminal_id) {
            low = middle + 1u;
        } else {
            high = middle;
        }
    }
    return low < lattice->terminal_len &&
        lattice->terminal_ids[low] == terminal_id;
}

static int utf8_lattice_edge_order(
    const CettaLpNativeUtf8LatticeEdge *left,
    const CettaLpNativeUtf8LatticeEdge *right) {
#define CETTA_LP_LATTICE_COMPARE(field) \
    do { \
        if (left->field != right->field) \
            return left->field < right->field ? -1 : 1; \
    } while (0)
    CETTA_LP_LATTICE_COMPARE(terminal_id);
    CETTA_LP_LATTICE_COMPARE(scalar_right);
    CETTA_LP_LATTICE_COMPARE(value_kind);
    CETTA_LP_LATTICE_COMPARE(value);
    CETTA_LP_LATTICE_COMPARE(byte_left);
    CETTA_LP_LATTICE_COMPARE(byte_right);
#undef CETTA_LP_LATTICE_COMPARE
    return 0;
}

bool cetta_lp_native_utf8_lattice_validate(
    const CettaLpNativeUtf8Lattice *lattice,
    char *error_buf,
    size_t error_buf_size) {
    uint32_t position;
    uint32_t index;

    if (!lattice ||
        (lattice->terminal_len > 0u && !lattice->terminal_ids) ||
        (lattice->edge_len > 0u && !lattice->edges) ||
        (lattice->scalar_len > 0u && !lattice->codepoints) ||
        lattice->scalar_len > UINT32_MAX - 2u ||
        !lattice->byte_offsets || !lattice->start_offsets ||
        lattice->start_offset_len != lattice->scalar_len + 2u ||
        !utf8_decode_receipt_valid(
            lattice->input_byte_len, lattice->decoded_byte_len,
            lattice->source_pass_count)) {
        slr_summary_set_error(error_buf, error_buf_size,
                              "malformed UTF-8 token lattice header");
        return false;
    }
    if (lattice->byte_offsets[0] != 0u ||
        lattice->byte_offsets[lattice->scalar_len] !=
            lattice->input_byte_len) {
        slr_summary_set_error(error_buf, error_buf_size,
                              "UTF-8 token lattice byte extent is invalid");
        return false;
    }
    for (index = 0u; index < lattice->terminal_len; index++) {
        if (index > 0u &&
            lattice->terminal_ids[index - 1u] >=
                lattice->terminal_ids[index]) {
            slr_summary_set_error(
                error_buf, error_buf_size,
                "UTF-8 token lattice terminal IDs are not strictly ordered");
            return false;
        }
    }
    for (index = 0u; index < lattice->scalar_len; index++) {
        uint32_t scalar = lattice->codepoints[index];
        uint32_t left = lattice->byte_offsets[index];
        uint32_t right = lattice->byte_offsets[index + 1u];
        if (!utf8_scalar_valid(scalar) || right < left ||
            right - left != utf8_scalar_width(scalar)) {
            slr_summary_set_error(
                error_buf, error_buf_size,
                "UTF-8 token lattice scalar/byte index is invalid");
            return false;
        }
    }
    if (lattice->start_offsets[0] != 0u ||
        lattice->start_offsets[lattice->scalar_len + 1u] !=
            lattice->edge_len) {
        slr_summary_set_error(error_buf, error_buf_size,
                              "UTF-8 token lattice position index is invalid");
        return false;
    }
    for (position = 0u; position <= lattice->scalar_len; position++) {
        uint32_t begin = lattice->start_offsets[position];
        uint32_t end = lattice->start_offsets[position + 1u];
        if (begin > end || end > lattice->edge_len) {
            slr_summary_set_error(
                error_buf, error_buf_size,
                "UTF-8 token lattice position index is not monotone");
            return false;
        }
        for (index = begin; index < end; index++) {
            const CettaLpNativeUtf8LatticeEdge *edge =
                &lattice->edges[index];
            if (edge->scalar_left != position ||
                edge->scalar_right < position ||
                edge->scalar_right > lattice->scalar_len ||
                edge->byte_left != lattice->byte_offsets[position] ||
                edge->byte_right !=
                    lattice->byte_offsets[edge->scalar_right] ||
                !utf8_lattice_terminal_declared(
                    lattice, edge->terminal_id) ||
                (uint32_t)edge->value_kind >
                    (uint32_t)
                        CETTA_LP_NATIVE_UTF8_TERMINAL_VALUE_WITNESS) {
                slr_summary_set_error(
                    error_buf, error_buf_size,
                    "UTF-8 token lattice edge is invalid");
                return false;
            }
            if (edge->value_kind ==
                    CETTA_LP_NATIVE_UTF8_TERMINAL_VALUE_SCALAR &&
                (edge->scalar_right != position + 1u ||
                 position >= lattice->scalar_len ||
                 edge->value != lattice->codepoints[position])) {
                slr_summary_set_error(
                    error_buf, error_buf_size,
                    "UTF-8 scalar lattice edge is inconsistent");
                return false;
            }
            if (edge->value_kind ==
                    CETTA_LP_NATIVE_UTF8_TERMINAL_VALUE_EOF &&
                (position != lattice->scalar_len ||
                 edge->scalar_right != position || edge->value != 0u)) {
                slr_summary_set_error(
                    error_buf, error_buf_size,
                    "UTF-8 EOF lattice edge is inconsistent");
                return false;
            }
            if (index > begin &&
                utf8_lattice_edge_order(
                    &lattice->edges[index - 1u], edge) >= 0) {
                slr_summary_set_error(
                    error_buf, error_buf_size,
                    "UTF-8 token lattice edges are not strictly ordered");
                return false;
            }
        }
    }
    return true;
}

static bool native_parse_input_terminal_declared(
    const CettaLpNativeParseInput *input,
    uint32_t terminal_id) {
    if (input->kind == CETTA_LP_NATIVE_PARSE_INPUT_UTF8) {
        return utf8_terminal_find(
                   input->terminals, input->terminal_len, terminal_id) != NULL;
    }
    return utf8_lattice_terminal_declared(input->lattice, terminal_id);
}

static bool native_parse_input_match_begin(
    CettaLpNativeParseInput *input,
    uint32_t terminal_id,
    uint32_t position,
    CettaLpNativeInputMatchIter *iter,
    char *error_buf,
    size_t error_buf_size) {
    memset(iter, 0, sizeof(*iter));
    if (input->kind == CETTA_LP_NATIVE_PARSE_INPUT_UTF8) {
        const CettaLpNativeUtf8Terminal *terminal = utf8_terminal_find(
            input->terminals, input->terminal_len, terminal_id);
        bool matched;
        bool is_eof;
        uint32_t right;
        uint32_t scalar;
        if (!terminal || !utf8_terminal_match(
                input->utf8, terminal, position, &matched, &right,
                &scalar, &is_eof, error_buf, error_buf_size)) {
            return false;
        }
        if (matched) {
            iter->single_ready = true;
            iter->single.right = right;
            iter->single.value_kind = is_eof
                ? CETTA_LP_NATIVE_UTF8_TERMINAL_VALUE_EOF
                : CETTA_LP_NATIVE_UTF8_TERMINAL_VALUE_SCALAR;
            iter->single.value = is_eof ? 0u : scalar;
        }
        return true;
    }
    if (position <= input->lattice->scalar_len) {
        uint32_t group_end =
            input->lattice->start_offsets[position + 1u];
        uint32_t low = input->lattice->start_offsets[position];
        uint32_t high = group_end;
        while (low < high) {
            uint32_t middle = low + (high - low) / 2u;
            if (input->lattice->edges[middle].terminal_id < terminal_id) {
                low = middle + 1u;
            } else {
                high = middle;
            }
        }
        iter->cursor = low;
        high = group_end;
        while (low < high) {
            uint32_t middle = low + (high - low) / 2u;
            if (input->lattice->edges[middle].terminal_id <= terminal_id) {
                low = middle + 1u;
            } else {
                high = middle;
            }
        }
        iter->end = low;
        if (iter->cursor < iter->end &&
            input->lattice->edges[iter->cursor].terminal_id != terminal_id) {
            iter->cursor = iter->end;
        }
    }
    return true;
}

static bool native_parse_input_match_next(
    const CettaLpNativeParseInput *input,
    CettaLpNativeInputMatchIter *iter,
    CettaLpNativeInputMatch *match) {
    if (input->kind == CETTA_LP_NATIVE_PARSE_INPUT_UTF8) {
        if (!iter->single_ready)
            return false;
        *match = iter->single;
        iter->single_ready = false;
        return true;
    }
    if (iter->cursor >= iter->end)
        return false;
    match->right = input->lattice->edges[iter->cursor].scalar_right;
    match->value_kind = input->lattice->edges[iter->cursor].value_kind;
    match->value = input->lattice->edges[iter->cursor].value;
    iter->cursor++;
    return true;
}

static bool native_parse_input_at_end(
    CettaLpNativeParseInput *input,
    uint32_t position,
    bool *at_end,
    char *error_buf,
    size_t error_buf_size) {
    if (input->kind == CETTA_LP_NATIVE_PARSE_INPUT_UTF8) {
        if (!utf8_input_ensure(
                input->utf8, position, error_buf, error_buf_size)) {
            return false;
        }
        *at_end = input->utf8->complete &&
            position == input->utf8->scalar_len;
        return true;
    }
    *at_end = position == input->lattice->scalar_len;
    return true;
}

static bool native_parse_input_finish(
    CettaLpNativeParseInput *input,
    char *error_buf,
    size_t error_buf_size) {
    if (input->kind == CETTA_LP_NATIVE_PARSE_INPUT_UTF8)
        return utf8_input_finish(input->utf8, error_buf, error_buf_size);
    return true;
}

static bool gll_full_prediction_matches_terminals(
    CettaLpNativeParseInput *input,
    const CettaLpNativeIdVec *terminals,
    const CettaLpNativeBitset *selection,
    uint32_t position,
    bool *matched,
    char *error_buf,
    size_t error_buf_size) {
    uint32_t terminal_index;

    *matched = false;
    for (terminal_index = 0u;
         terminal_index < terminals->len; terminal_index++) {
        CettaLpNativeInputMatchIter iter;
        CettaLpNativeInputMatch match;

        if (!bitset_test(selection, terminal_index))
            continue;
        if (!native_parse_input_match_begin(
                input, terminals->data[terminal_index], position,
                &iter, error_buf, error_buf_size)) {
            return false;
        }
        if (native_parse_input_match_next(input, &iter, &match)) {
            *matched = true;
            return true;
        }
    }
    if (bitset_test(selection, terminals->len)) {
        bool at_end;
        if (!native_parse_input_at_end(
                input, position, &at_end, error_buf, error_buf_size)) {
            return false;
        }
        *matched = at_end;
    }
    return true;
}

static bool gll_full_prediction_production(
    const CettaLpNativeSlrProduction *productions,
    uint32_t production_len,
    uint32_t production_index,
    const CettaLpNativeIdVec *nonterminals,
    const CettaLpNativeIdVec *terminals,
    const bool *nullable,
    const CettaLpNativeBitset *first,
    const CettaLpNativeBitset *follow,
    CettaLpNativeParseInput *input,
    uint32_t position,
    bool *predicted,
    char *error_buf,
    size_t error_buf_size) {
    const CettaLpNativeSlrProduction *production;
    uint32_t item_index;
    int32_t lhs_index;

    *predicted = false;
    if (production_index >= production_len)
        return false;
    production = &productions[production_index];
    for (item_index = 0u; item_index < production->rhs_len; item_index++) {
        const CettaLpNativeSymbol *symbol = &production->rhs[item_index];
        bool matched;

        if (symbol->kind == CETTA_LP_NATIVE_SYMBOL_TM) {
            CettaLpNativeInputMatchIter iter;
            CettaLpNativeInputMatch match;
            if (!native_parse_input_match_begin(
                    input, symbol->name, position, &iter,
                    error_buf, error_buf_size)) {
                return false;
            }
            *predicted = native_parse_input_match_next(input, &iter, &match);
            return true;
        }
        {
            int32_t nonterminal_index =
                idvec_find(nonterminals, symbol->name);
            if (nonterminal_index < 0) {
                slr_summary_set_error(
                    error_buf, error_buf_size,
                    "GLL prediction nonterminal is absent");
                return false;
            }
            if (!gll_full_prediction_matches_terminals(
                    input, terminals, &first[nonterminal_index], position,
                    &matched, error_buf, error_buf_size)) {
                return false;
            }
            if (matched) {
                *predicted = true;
                return true;
            }
            if (!nullable[nonterminal_index])
                return true;
        }
    }
    lhs_index = idvec_find(nonterminals, production->lhs);
    if (lhs_index < 0) {
        slr_summary_set_error(
            error_buf, error_buf_size,
            "GLL prediction lhs is absent");
        return false;
    }
    return gll_full_prediction_matches_terminals(
        input, terminals, &follow[lhs_index], position, predicted,
        error_buf, error_buf_size);
}

static bool gll_full_prediction_collect_expected(
    const CettaLpNativeSlrProduction *productions,
    uint32_t production_len,
    uint32_t production_index,
    const CettaLpNativeIdVec *nonterminals,
    const CettaLpNativeIdVec *terminals,
    const bool *nullable,
    const CettaLpNativeBitset *first,
    const CettaLpNativeBitset *follow,
    CettaLpNativeU32Vec *expectations,
    char *error_buf,
    size_t error_buf_size) {
    const CettaLpNativeSlrProduction *production;
    uint32_t item_index;
    int32_t lhs_index;

    if (production_index >= production_len || !expectations)
        return false;
    production = &productions[production_index];
    for (item_index = 0u; item_index < production->rhs_len; item_index++) {
        const CettaLpNativeSymbol *symbol = &production->rhs[item_index];
        uint32_t terminal_index;

        if (symbol->kind == CETTA_LP_NATIVE_SYMBOL_TM) {
            return u32vec_push_unique(expectations, symbol->name);
        }
        {
            int32_t nonterminal_index =
                idvec_find(nonterminals, symbol->name);
            if (nonterminal_index < 0) {
                slr_summary_set_error(
                    error_buf, error_buf_size,
                    "GLL expected-set nonterminal is absent");
                return false;
            }
            for (terminal_index = 0u;
                 terminal_index < terminals->len; terminal_index++) {
                if (bitset_test(
                        &first[nonterminal_index], terminal_index) &&
                    !u32vec_push_unique(
                        expectations, terminals->data[terminal_index])) {
                    return false;
                }
            }
            if (!nullable[nonterminal_index])
                return true;
        }
    }
    lhs_index = idvec_find(nonterminals, production->lhs);
    if (lhs_index < 0) {
        slr_summary_set_error(
            error_buf, error_buf_size,
            "GLL expected-set lhs is absent");
        return false;
    }
    for (item_index = 0u; item_index < terminals->len; item_index++) {
        if (bitset_test(&follow[lhs_index], item_index) &&
            !u32vec_push_unique(
                expectations, terminals->data[item_index])) {
            return false;
        }
    }
    return true;
}

static uint32_t native_parse_input_scalar_len(
    const CettaLpNativeParseInput *input) {
    return input->kind == CETTA_LP_NATIVE_PARSE_INPUT_UTF8
        ? input->utf8->scalar_len : input->lattice->scalar_len;
}

static uint32_t native_parse_input_byte_len(
    const CettaLpNativeParseInput *input) {
    return input->kind == CETTA_LP_NATIVE_PARSE_INPUT_UTF8
        ? (uint32_t)input->utf8->byte_len : input->lattice->input_byte_len;
}

static const uint32_t *native_parse_input_codepoints(
    const CettaLpNativeParseInput *input) {
    return input->kind == CETTA_LP_NATIVE_PARSE_INPUT_UTF8
        ? input->utf8->codepoints : input->lattice->codepoints;
}

static const uint32_t *native_parse_input_byte_offsets(
    const CettaLpNativeParseInput *input) {
    return input->kind == CETTA_LP_NATIVE_PARSE_INPUT_UTF8
        ? input->utf8->byte_offsets : input->lattice->byte_offsets;
}

static uint32_t native_parse_input_decoded_byte_len(
    const CettaLpNativeParseInput *input) {
    return input->kind == CETTA_LP_NATIVE_PARSE_INPUT_UTF8
        ? input->utf8->decoded_byte_len : input->lattice->decoded_byte_len;
}

static uint32_t native_parse_input_source_pass_count(
    const CettaLpNativeParseInput *input) {
    return input->kind == CETTA_LP_NATIVE_PARSE_INPUT_UTF8
        ? input->utf8->source_pass_count : input->lattice->source_pass_count;
}

static bool gll_utf8_enqueue(CettaLpNativeGllDescriptorVec *seen,
                             CettaLpNativeU32Vec *work,
                             int32_t prod_idx,
                             uint32_t dot,
                             uint32_t gss_idx,
                             uint32_t left_label,
                             uint32_t pos,
                             uint32_t descriptor_limit,
                             bool *limit_hit) {
    uint32_t previous_len = seen->len;

    if (!gll_desc_enqueue(seen, work, prod_idx, dot, gss_idx,
                          left_label, pos)) {
        return false;
    }
    if (seen->len > previous_len && seen->len > descriptor_limit)
        *limit_hit = true;
    return true;
}

static int utf8_u32_compare(const void *lhs, const void *rhs) {
    uint32_t left = *(const uint32_t *)lhs;
    uint32_t right = *(const uint32_t *)rhs;
    return left < right ? -1 : left > right ? 1 : 0;
}

static bool native_forest_copy_input(CettaLpNativeUtf8Forest *forest,
                                     const CettaLpNativeParseInput *input,
                                     char *error_buf,
                                     size_t error_buf_size) {
    uint32_t scalar_len = native_parse_input_scalar_len(input);
    const uint32_t *codepoints = native_parse_input_codepoints(input);
    const uint32_t *byte_offsets = native_parse_input_byte_offsets(input);

    if (scalar_len > 0u) {
        forest->codepoints = cetta_malloc(
            sizeof(*forest->codepoints) * scalar_len);
        memcpy(forest->codepoints, codepoints,
               sizeof(*forest->codepoints) * scalar_len);
    }
    forest->byte_offsets = cetta_malloc(
        sizeof(*forest->byte_offsets) * ((size_t)scalar_len + 1u));
    if (!byte_offsets) {
        slr_summary_set_error(error_buf, error_buf_size,
                              "UTF-8 input lacks byte offsets");
        return false;
    }
    memcpy(forest->byte_offsets, byte_offsets,
           sizeof(*forest->byte_offsets) * ((size_t)scalar_len + 1u));
    forest->scalar_len = scalar_len;
    forest->input_byte_len = native_parse_input_byte_len(input);
    forest->decoded_byte_len = native_parse_input_decoded_byte_len(input);
    forest->source_pass_count = native_parse_input_source_pass_count(input);
    return true;
}

static bool gll_utf8_collect_roots(
    const CettaLpNativeGllNodeVec *nodes,
    const CettaLpNativeGllGssNodeVec *gss_nodes,
    uint32_t start_nt,
    uint32_t start_scalar,
    uint32_t input_scalar_len,
    CettaLpNativeU32Vec *roots) {
    uint32_t index;

    if (!gss_nodes || gss_nodes->len == 0u)
        return false;
    for (index = 0u; index < gss_nodes->data[0].popped.len; index++) {
        uint32_t node_idx = gss_nodes->data[0].popped.data[index];
        const CettaLpNativeGllNode *node;
        if (node_idx >= nodes->len)
            return false;
        node = &nodes->data[node_idx];
        if (gll_node_kind_value(node) == CETTA_LP_NATIVE_GLL_NODE_SYM &&
            gll_node_symbol_value(node) == start_nt &&
            node->left == start_scalar &&
            node->right <= input_scalar_len &&
            !u32vec_push_unique(roots, node_idx)) {
            return false;
        }
    }
    for (index = 1u; index < roots->len; index++) {
        uint32_t value = roots->data[index];
        uint32_t insert = index;
        while (insert > 0u &&
               nodes->data[roots->data[insert - 1u]].right >
                   nodes->data[value].right) {
            roots->data[insert] = roots->data[insert - 1u];
            insert--;
        }
        roots->data[insert] = value;
    }
    return true;
}

static bool utf8_forest_reachable_map(
    const CettaLpNativeGllNodeVec *nodes,
    const CettaLpNativeU32Vec *roots,
    uint8_t **out_reachable,
    uint32_t **out_map,
    uint32_t *out_len) {
    uint8_t *reachable = NULL;
    uint32_t *map = NULL;
    uint32_t *stack = NULL;
    uint32_t stack_len = 0u;
    uint32_t next = 0u;
    uint32_t index;

    *out_reachable = NULL;
    *out_map = NULL;
    *out_len = 0u;
    reachable = calloc(nodes->len ? nodes->len : 1u, 1u);
    map = malloc(sizeof(*map) * (nodes->len ? nodes->len : 1u));
    stack = malloc(sizeof(*stack) * (nodes->len ? nodes->len : 1u));
    if (!reachable || !map || !stack)
        goto fail;
    for (index = 0u; index < nodes->len; index++)
        map[index] = CETTA_LP_NATIVE_NODE_NONE;
    for (index = 0u; index < roots->len; index++) {
        uint32_t root = roots->data[index];
        if (root >= nodes->len)
            goto fail;
        if (!reachable[root]) {
            reachable[root] = 1u;
            stack[stack_len++] = root;
        }
    }
    while (stack_len > 0u) {
        const CettaLpNativeGllNode *node =
            &nodes->data[stack[--stack_len]];
        uint32_t choice_idx;
        for (choice_idx = node->first_choice;
             choice_idx != CETTA_LP_NATIVE_NODE_NONE;
             choice_idx = nodes->packed.data[choice_idx].next_idx) {
            const CettaLpNativeGllPackedChoice *choice;
            if (choice_idx >= nodes->packed.len)
                goto fail;
            choice = &nodes->packed.data[choice_idx];
            if (choice->left_idx != CETTA_LP_NATIVE_NODE_NONE) {
                if (choice->left_idx >= nodes->len)
                    goto fail;
                if (!reachable[choice->left_idx]) {
                    reachable[choice->left_idx] = 1u;
                    stack[stack_len++] = choice->left_idx;
                }
            }
            if (choice->right_idx >= nodes->len)
                goto fail;
            if (!reachable[choice->right_idx]) {
                reachable[choice->right_idx] = 1u;
                stack[stack_len++] = choice->right_idx;
            }
        }
    }
    for (index = 0u; index < nodes->len; index++) {
        if (reachable[index])
            map[index] = next++;
    }
    free(stack);
    *out_reachable = reachable;
    *out_map = map;
    *out_len = next;
    return true;

fail:
    free(reachable);
    free(map);
    free(stack);
    return false;
}

static bool native_forest_export(
    CettaLpNativeUtf8Forest *forest,
    const CettaLpNativeParseInput *input,
    const CettaLpNativeGllNodeVec *nodes,
    const CettaLpNativeU32Vec *internal_roots,
    char *error_buf,
    size_t error_buf_size) {
    uint8_t *reachable = NULL;
    uint32_t *map = NULL;
    uint32_t reachable_len = 0u;
    uint64_t choice_count = 0u;
    uint32_t node_idx;
    uint32_t choice_write = 0u;
    uint32_t scalar_len = native_parse_input_scalar_len(input);
    const uint32_t *codepoints = native_parse_input_codepoints(input);
    const uint32_t *byte_offsets = native_parse_input_byte_offsets(input);

    if (!utf8_forest_reachable_map(nodes, internal_roots, &reachable, &map,
                                &reachable_len)) {
        slr_summary_set_error(error_buf, error_buf_size,
                              "failed to trace reachable GLL forest");
        return false;
    }
    for (node_idx = 0u; node_idx < nodes->len; node_idx++) {
        uint32_t choice_idx;
        if (!reachable[node_idx])
            continue;
        for (choice_idx = nodes->data[node_idx].first_choice;
             choice_idx != CETTA_LP_NATIVE_NODE_NONE;
             choice_idx = nodes->packed.data[choice_idx].next_idx) {
            if (choice_idx >= nodes->packed.len || choice_count == UINT32_MAX)
                goto malformed;
            choice_count++;
        }
    }
    forest->nodes = calloc(reachable_len ? reachable_len : 1u,
                           sizeof(*forest->nodes));
    forest->choices = calloc(choice_count ? (size_t)choice_count : 1u,
                             sizeof(*forest->choices));
    forest->roots = malloc(sizeof(*forest->roots) *
                           (internal_roots->len ? internal_roots->len : 1u));
    if (!forest->nodes || !forest->choices || !forest->roots)
        goto malformed;
    forest->node_len = reachable_len;
    forest->choice_len = (uint32_t)choice_count;
    forest->root_len = internal_roots->len;

    for (node_idx = 0u; node_idx < nodes->len; node_idx++) {
        const CettaLpNativeGllNode *source;
        CettaLpNativeUtf8ForestNode *target;
        CettaLpNativeGllNodeKind kind;
        uint32_t choice_idx;
        uint32_t mapped;
        if (!reachable[node_idx])
            continue;
        source = &nodes->data[node_idx];
        mapped = map[node_idx];
        target = &forest->nodes[mapped];
        kind = gll_node_kind_value(source);
        target->kind = (CettaLpNativeUtf8ForestNodeKind)kind;
        target->symbol_id = CETTA_LP_NATIVE_UTF8_FOREST_NONE;
        target->production_index = CETTA_LP_NATIVE_UTF8_FOREST_NONE;
        target->scalar_left = source->left;
        target->scalar_right = source->right;
        if (source->left > scalar_len || source->right > scalar_len) {
            goto malformed;
        }
        target->byte_left = byte_offsets[source->left];
        target->byte_right = byte_offsets[source->right];
        target->choice_begin = choice_write;
        if (kind == CETTA_LP_NATIVE_GLL_NODE_TERM) {
            target->symbol_id = gll_node_symbol_value(source);
            target->terminal_value_kind =
                gll_node_terminal_value_kind(source);
            if (target->terminal_value_kind ==
                    CETTA_LP_NATIVE_UTF8_TERMINAL_VALUE_EOF) {
                target->terminal_is_eof = true;
                if (source->left != source->right ||
                    source->left != scalar_len)
                    goto malformed;
            } else if (target->terminal_value_kind ==
                       CETTA_LP_NATIVE_UTF8_TERMINAL_VALUE_SCALAR) {
                if (source->right != source->left + 1u ||
                    source->left >= scalar_len ||
                    gll_node_terminal_value(source) !=
                        codepoints[source->left]) {
                    goto malformed;
                }
                target->terminal_scalar =
                    gll_node_terminal_value(source);
            } else if (target->terminal_value_kind ==
                       CETTA_LP_NATIVE_UTF8_TERMINAL_VALUE_WITNESS) {
                target->terminal_witness_id =
                    gll_node_terminal_value(source);
            } else {
                goto malformed;
            }
        } else if (kind == CETTA_LP_NATIVE_GLL_NODE_SYM) {
            target->symbol_id = gll_node_symbol_value(source);
        } else if (kind == CETTA_LP_NATIVE_GLL_NODE_INTER) {
            int32_t production_index = gll_node_prod_idx_value(source);
            if (production_index < 0)
                goto malformed;
            target->production_index = (uint32_t)production_index;
            target->dot = gll_node_dot_value(source);
        }
        for (choice_idx = source->first_choice;
             choice_idx != CETTA_LP_NATIVE_NODE_NONE;
             choice_idx = nodes->packed.data[choice_idx].next_idx) {
            const CettaLpNativeGllPackedChoice *source_choice;
            CettaLpNativeUtf8ForestChoice *target_choice;
            int32_t production_index;
            uint32_t pivot;
            if (choice_idx >= nodes->packed.len ||
                choice_write >= forest->choice_len) {
                goto malformed;
            }
            source_choice = &nodes->packed.data[choice_idx];
            if (source_choice->right_idx >= nodes->len ||
                map[source_choice->right_idx] == CETTA_LP_NATIVE_NODE_NONE ||
                (source_choice->left_idx != CETTA_LP_NATIVE_NODE_NONE &&
                 (source_choice->left_idx >= nodes->len ||
                  map[source_choice->left_idx] == CETTA_LP_NATIVE_NODE_NONE))) {
                goto malformed;
            }
            target_choice = &forest->choices[choice_write++];
            target_choice->parent_node = mapped;
            target_choice->prefix_node =
                source_choice->left_idx == CETTA_LP_NATIVE_NODE_NONE
                ? CETTA_LP_NATIVE_UTF8_FOREST_NONE
                : map[source_choice->left_idx];
            target_choice->child_node = map[source_choice->right_idx];
            production_index = gll_choice_prod_idx(nodes, source_choice);
            if (production_index < 0 &&
                kind == CETTA_LP_NATIVE_GLL_NODE_INTER) {
                production_index = gll_node_prod_idx_value(source);
            }
            if (production_index < 0)
                goto malformed;
            target_choice->production_index = (uint32_t)production_index;
            pivot = nodes->data[source_choice->right_idx].left;
            if (pivot > scalar_len)
                goto malformed;
            target_choice->scalar_pivot = pivot;
            target_choice->byte_pivot = byte_offsets[pivot];
            target->choice_len++;
        }
    }
    if (choice_write != forest->choice_len)
        goto malformed;
    for (node_idx = 0u; node_idx < internal_roots->len; node_idx++) {
        uint32_t root = internal_roots->data[node_idx];
        if (root >= nodes->len || map[root] == CETTA_LP_NATIVE_NODE_NONE)
            goto malformed;
        forest->roots[node_idx] = map[root];
    }
    free(reachable);
    free(map);
    return true;

malformed:
    free(reachable);
    free(map);
    slr_summary_set_error(error_buf, error_buf_size,
                          "native GLL produced a malformed neutral forest");
    return false;
}

void cetta_lp_native_utf8_forest_init(
    CettaLpNativeUtf8Forest *forest) {
    if (!forest)
        return;
    memset(forest, 0, sizeof(*forest));
    forest->outcome = CETTA_LP_NATIVE_UTF8_FOREST_COMPLETED;
}

void cetta_lp_native_utf8_forest_free(
    CettaLpNativeUtf8Forest *forest) {
    if (!forest)
        return;
    free(forest->nodes);
    free(forest->choices);
    free(forest->roots);
    free(forest->expected_terminal_ids);
    free(forest->codepoints);
    free(forest->byte_offsets);
    memset(forest, 0, sizeof(*forest));
}

typedef struct {
    uint32_t token_idx;
    const CettaLpNativeUtf8LatticeEdge *edge;
    bool parser_eof;
} CettaLpNativeSlrLatticeCandidate;

typedef struct {
    CettaLpNativeSlrLatticeCandidate *data;
    uint32_t len;
} CettaLpNativeSlrLatticeCandidates;

static void slr_lattice_candidates_free(
    CettaLpNativeSlrLatticeCandidates *candidates) {
    if (!candidates)
        return;
    free(candidates->data);
    memset(candidates, 0, sizeof(*candidates));
}

static bool slr_lattice_candidates_build(
    const CettaLpNativeSlrPreparedImpl *prepared,
    const CettaLpNativeUtf8Lattice *lattice,
    uint32_t position,
    CettaLpNativeSlrLatticeCandidates *candidates,
    char *error_buf,
    size_t error_buf_size) {
    uint32_t begin;
    uint32_t end;
    uint32_t capacity;
    uint32_t edge_index;

    if (!prepared || !lattice || !candidates ||
        position > lattice->scalar_len) {
        slr_summary_set_error(error_buf, error_buf_size,
                              "bad SLR lattice candidate arguments");
        return false;
    }
    slr_lattice_candidates_free(candidates);
    begin = lattice->start_offsets[position];
    end = lattice->start_offsets[position + 1u];
    capacity = end - begin;
    if (position == lattice->scalar_len) {
        if (capacity == UINT32_MAX) {
            slr_summary_set_error(error_buf, error_buf_size,
                                  "SLR lattice candidate count overflow");
            return false;
        }
        capacity++;
    }
    if (capacity > 0u) {
        candidates->data = malloc(
            sizeof(*candidates->data) * (size_t)capacity);
        if (!candidates->data) {
            slr_summary_set_error(error_buf, error_buf_size,
                                  "failed to allocate SLR lattice candidates");
            return false;
        }
    }
    for (edge_index = begin; edge_index < end; edge_index++) {
        const CettaLpNativeUtf8LatticeEdge *edge =
            &lattice->edges[edge_index];
        int32_t terminal_index = idvec_find(
            &prepared->terminals, edge->terminal_id);
        if (terminal_index < 0)
            continue;
        candidates->data[candidates->len++] =
            (CettaLpNativeSlrLatticeCandidate){
                .token_idx = (uint32_t)terminal_index,
                .edge = edge,
                .parser_eof = false,
            };
    }
    if (position == lattice->scalar_len) {
        candidates->data[candidates->len++] =
            (CettaLpNativeSlrLatticeCandidate){
                .token_idx = prepared->terminals.len,
                .edge = NULL,
                .parser_eof = true,
            };
    }
    return true;
}

static bool slr_lattice_action_find(
    const CettaLpNativeSlrPreparedImpl *prepared,
    uint32_t state,
    uint32_t token_idx,
    const CettaLpNativeAction **out,
    char *error_buf,
    size_t error_buf_size) {
    uint32_t action_index;

    *out = NULL;
    for (action_index = 0u;
         action_index < prepared->actions.len; action_index++) {
        const CettaLpNativeAction *action =
            &prepared->actions.data[action_index];
        if (action->state != state || action->token_idx != token_idx)
            continue;
        if (*out && ((*out)->kind != action->kind ||
                     (*out)->value != action->value)) {
            slr_summary_set_error(error_buf, error_buf_size,
                                  "prepared SLR table contains a conflict");
            return false;
        }
        *out = action;
    }
    return true;
}

static bool slr_lattice_reduce(
    const CettaLpNativeSlrPreparedImpl *prepared,
    CettaLpNativeGllNodeVec *nodes,
    CettaLpNativeU32Vec *states,
    CettaLpNativeParseValueVec *values,
    uint32_t position,
    int32_t production_index,
    char *error_buf,
    size_t error_buf_size) {
    SymbolId lhs = 0u;
    const CettaLpNativeSymbol *rhs = NULL;
    uint32_t rhs_len = 0u;
    uint32_t value_base;
    uint32_t child_index;
    int32_t lhs_symbol_index;
    int32_t goto_edge_index;
    CettaLpNativeParseValue next_value;

    if (!prepared || !nodes || !states || !values ||
        states->len != values->len + 1u || production_index < 0 ||
        (uint32_t)production_index >= prepared->grammar_production_len) {
        slr_summary_set_error(error_buf, error_buf_size,
                              "invalid prepared SLR reduction");
        return false;
    }
    slr_get_prod(prepared->productions, prepared->production_len,
                 prepared->start_nt, production_index,
                 &lhs, &rhs, &rhs_len);
    if (rhs_len > values->len) {
        slr_summary_set_error(error_buf, error_buf_size,
                              "prepared SLR reduction underflows its stack");
        return false;
    }
    value_base = values->len - rhs_len;
    lhs_symbol_index = symbolvec_find(&prepared->symbols, false, lhs);
    goto_edge_index = lhs_symbol_index < 0 ? -1 : edgevec_find(
        &prepared->edges, states->data[value_base],
        (uint32_t)lhs_symbol_index);
    if (goto_edge_index < 0) {
        slr_summary_set_error(error_buf, error_buf_size,
                              "prepared SLR reduction lacks a goto");
        return false;
    }

    memset(&next_value, 0, sizeof(next_value));
    next_value.is_cert = true;
    if (rhs_len == 0u) {
        int32_t symbol_node = gll_node_get_sym(
            nodes, lhs, position, position);
        int32_t epsilon_node = gll_node_get_eps(nodes, position);
        if (symbol_node < 0 || epsilon_node < 0 ||
            !gll_node_push_packed_unique(
                nodes, (uint32_t)symbol_node,
                CETTA_LP_NATIVE_NODE_NONE, (uint32_t)epsilon_node,
                position, production_index)) {
            slr_summary_set_error(error_buf, error_buf_size,
                                  "failed to record prepared SLR epsilon");
            return false;
        }
        next_value.start = position;
        next_value.end = position;
        next_value.forest_idx = (uint32_t)symbol_node;
    } else {
        uint32_t parent = CETTA_LP_NATIVE_NODE_NONE;

        next_value.start = values->data[value_base].start;
        next_value.end = values->data[values->len - 1u].end;
        for (child_index = 0u; child_index < rhs_len; child_index++) {
            const CettaLpNativeParseValue *child =
                &values->data[value_base + child_index];
            const CettaLpNativeGllNode *child_node;
            int32_t next_parent;

            if (child->forest_idx >= nodes->len) {
                slr_summary_set_error(error_buf, error_buf_size,
                                      "prepared SLR forest child is invalid");
                return false;
            }
            child_node = &nodes->data[child->forest_idx];
            if ((rhs[child_index].kind == CETTA_LP_NATIVE_SYMBOL_TM &&
                 (child->is_cert ||
                  child->term_kind != rhs[child_index].name ||
                  gll_node_kind_value(child_node) !=
                      CETTA_LP_NATIVE_GLL_NODE_TERM)) ||
                (rhs[child_index].kind != CETTA_LP_NATIVE_SYMBOL_TM &&
                 (!child->is_cert ||
                  gll_node_kind_value(child_node) !=
                      CETTA_LP_NATIVE_GLL_NODE_SYM ||
                  gll_node_symbol_value(child_node) !=
                      rhs[child_index].name))) {
                slr_summary_set_error(error_buf, error_buf_size,
                                      "prepared SLR reduction child mismatch");
                return false;
            }
            next_parent = gll_sppf_get_node(
                nodes, prepared->productions, prepared->production_len,
                prepared->start_nt, production_index, child_index + 1u,
                parent, child->forest_idx, error_buf, error_buf_size);
            if (next_parent < 0)
                return false;
            parent = (uint32_t)next_parent;
        }
        next_value.forest_idx = parent;
    }

    states->len = value_base + 1u;
    values->len = value_base;
    if (!u32vec_push(
            states, prepared->edges.data[goto_edge_index].target) ||
        !parsevaluevec_push(values, &next_value)) {
        slr_summary_set_error(error_buf, error_buf_size,
                              "failed to advance prepared SLR reduction");
        return false;
    }
    return true;
}

bool cetta_lp_native_slr_prepared_parse_utf8_lattice_forest(
    const CettaLpNativeSlrPrepared *prepared_owner,
    const CettaLpNativeUtf8Lattice *lattice,
    uint32_t start_scalar,
    uint32_t work_limit,
    CettaLpNativeSlrLatticeOutcome *outcome,
    CettaLpNativeUtf8Forest *out,
    char *error_buf,
    size_t error_buf_size) {
    const CettaLpNativeSlrPreparedImpl *prepared =
        prepared_owner ? prepared_owner->implementation : NULL;
    CettaLpNativeParseInput input;
    CettaLpNativeSlrLatticeCandidates candidates = {0};
    CettaLpNativeU32Vec states = {0};
    CettaLpNativeParseValueVec values = {0};
    CettaLpNativeGllNodeVec nodes = {0};
    CettaLpNativeU32Vec roots = {0};
    CettaLpNativeUtf8Forest result;
    uint32_t position = start_scalar;
    uint32_t farthest = start_scalar;
    uint32_t peak_stack = 0u;
    uint32_t work_items = 0u;
    bool ok = false;

    cetta_lp_native_utf8_forest_init(&result);
    if (error_buf && error_buf_size > 0u)
        error_buf[0] = '\0';
    if (!prepared || !lattice || !outcome || !out || work_limit == 0u ||
        start_scalar > lattice->scalar_len ||
        !cetta_lp_native_utf8_lattice_validate(
            lattice, error_buf, error_buf_size)) {
        if (!error_buf || error_buf[0] == '\0') {
            slr_summary_set_error(
                error_buf, error_buf_size,
                "bad prepared SLR UTF-8 lattice arguments");
        }
        goto done;
    }
    input = (CettaLpNativeParseInput){
        .kind = CETTA_LP_NATIVE_PARSE_INPUT_LATTICE,
        .utf8 = NULL,
        .terminals = NULL,
        .terminal_len = 0u,
        .lattice = lattice,
    };
    {
        uint32_t terminal_index;
        for (terminal_index = 0u;
             terminal_index < prepared->terminals.len; terminal_index++) {
            if (!native_parse_input_terminal_declared(
                    &input, prepared->terminals.data[terminal_index])) {
                slr_summary_set_error(
                    error_buf, error_buf_size,
                    "grammar terminal %u is absent from parse input",
                    prepared->terminals.data[terminal_index]);
                goto done;
            }
        }
    }
    if (!u32vec_push(&states, 0u) ||
        !slr_lattice_candidates_build(
            prepared, lattice, position, &candidates,
            error_buf, error_buf_size)) {
        goto done;
    }
    peak_stack = states.len;

    for (;;) {
        const CettaLpNativeAction *common_action = NULL;
        uint32_t candidate_index;
        uint32_t candidate_write = 0u;
        uint32_t state;

        if (states.len == 0u || states.len != values.len + 1u) {
            slr_summary_set_error(error_buf, error_buf_size,
                                  "prepared SLR stack is malformed");
            goto done;
        }
        state = states.data[states.len - 1u];
        for (candidate_index = 0u;
             candidate_index < candidates.len; candidate_index++) {
            const CettaLpNativeAction *action = NULL;
            CettaLpNativeSlrLatticeCandidate candidate =
                candidates.data[candidate_index];

            if (!slr_lattice_action_find(
                    prepared, state, candidate.token_idx, &action,
                    error_buf, error_buf_size)) {
                goto done;
            }
            if (!action)
                continue;
            if (common_action &&
                (common_action->kind != action->kind ||
                 common_action->value != action->value)) {
                *outcome = CETTA_LP_NATIVE_SLR_LATTICE_NEEDS_GENERAL;
                goto finish;
            }
            common_action = action;
            candidates.data[candidate_write++] = candidate;
        }
        candidates.len = candidate_write;
        if (!common_action || candidates.len == 0u) {
            *outcome = CETTA_LP_NATIVE_SLR_LATTICE_NEEDS_GENERAL;
            goto finish;
        }
        if (work_items >= work_limit) {
            *outcome = CETTA_LP_NATIVE_SLR_LATTICE_RESOURCE_LIMIT;
            result.outcome = CETTA_LP_NATIVE_UTF8_FOREST_RESOURCE_LIMIT;
            goto finish;
        }
        work_items++;

        if (common_action->kind == 'r') {
            if (!slr_lattice_reduce(
                    prepared, &nodes, &states, &values, position,
                    common_action->value, error_buf, error_buf_size)) {
                goto done;
            }
            if (states.len > peak_stack)
                peak_stack = states.len;
            continue;
        }
        if (common_action->kind == 's') {
            const CettaLpNativeSlrLatticeCandidate *candidate;
            const CettaLpNativeUtf8LatticeEdge *edge;
            CettaLpNativeParseValue value;
            int32_t terminal_node;

            if (candidates.len != 1u || candidates.data[0].parser_eof ||
                !candidates.data[0].edge || common_action->value < 0) {
                *outcome = CETTA_LP_NATIVE_SLR_LATTICE_NEEDS_GENERAL;
                goto finish;
            }
            candidate = &candidates.data[0];
            edge = candidate->edge;
            terminal_node = gll_node_get_term_value(
                &nodes, edge->terminal_id, edge->scalar_left,
                edge->scalar_right, edge->value_kind, edge->value);
            if (terminal_node < 0) {
                slr_summary_set_error(error_buf, error_buf_size,
                                      "failed to allocate prepared SLR terminal");
                goto done;
            }
            memset(&value, 0, sizeof(value));
            value.is_cert = false;
            value.term_kind = edge->terminal_id;
            value.start = edge->scalar_left;
            value.end = edge->scalar_right;
            value.forest_idx = (uint32_t)terminal_node;
            if (!u32vec_push(&states, (uint32_t)common_action->value) ||
                !parsevaluevec_push(&values, &value)) {
                slr_summary_set_error(error_buf, error_buf_size,
                                      "failed to advance prepared SLR shift");
                goto done;
            }
            position = edge->scalar_right;
            if (position > farthest)
                farthest = position;
            if (states.len > peak_stack)
                peak_stack = states.len;
            if (!slr_lattice_candidates_build(
                    prepared, lattice, position, &candidates,
                    error_buf, error_buf_size)) {
                goto done;
            }
            continue;
        }
        if (common_action->kind == 'a') {
            const CettaLpNativeParseValue *root_value;
            const CettaLpNativeGllNode *root_node;

            if (candidates.len != 1u ||
                !candidates.data[0].parser_eof ||
                position != lattice->scalar_len || values.len == 0u) {
                *outcome = CETTA_LP_NATIVE_SLR_LATTICE_NEEDS_GENERAL;
                goto finish;
            }
            root_value = &values.data[values.len - 1u];
            if (!root_value->is_cert || root_value->forest_idx >= nodes.len) {
                slr_summary_set_error(error_buf, error_buf_size,
                                      "prepared SLR accept root is invalid");
                goto done;
            }
            root_node = &nodes.data[root_value->forest_idx];
            if (gll_node_kind_value(root_node) !=
                    CETTA_LP_NATIVE_GLL_NODE_SYM ||
                gll_node_symbol_value(root_node) != prepared->start_nt ||
                root_node->left != start_scalar ||
                root_node->right != lattice->scalar_len ||
                !u32vec_push(&roots, root_value->forest_idx) ||
                !native_parse_input_finish(
                    &input, error_buf, error_buf_size) ||
                !native_forest_copy_input(
                    &result, &input, error_buf, error_buf_size) ||
                !native_forest_export(
                    &result, &input, &nodes, &roots,
                    error_buf, error_buf_size)) {
                if (!error_buf || error_buf[0] == '\0') {
                    slr_summary_set_error(
                        error_buf, error_buf_size,
                        "prepared SLR accept root does not span the source");
                }
                goto done;
            }
            *outcome = CETTA_LP_NATIVE_SLR_LATTICE_ACCEPTED;
            goto finish;
        }
        slr_summary_set_error(error_buf, error_buf_size,
                              "prepared SLR table has an unknown action");
        goto done;
    }

finish:
    result.farthest_scalar = farthest;
    result.farthest_byte = lattice->byte_offsets[farthest];
    result.graph_node_len = nodes.len;
    result.stack_node_len = peak_stack;
    result.work_item_len = work_items;
    if (*outcome == CETTA_LP_NATIVE_SLR_LATTICE_RESOURCE_LIMIT &&
        !result.byte_offsets &&
        !native_forest_copy_input(
            &result, &input, error_buf, error_buf_size)) {
        goto done;
    }
    cetta_lp_native_utf8_forest_free(out);
    *out = result;
    memset(&result, 0, sizeof(result));
    ok = true;

done:
    cetta_lp_native_utf8_forest_free(&result);
    slr_lattice_candidates_free(&candidates);
    free(states.data);
    free(values.data);
    gll_nodevec_free(&nodes);
    free(roots.data);
    return ok;
}

typedef struct {
    CettaLpNativeSlrProduction *productions;
    uint32_t production_len;
    uint32_t start_nt;
    uint32_t max_rhs_len;
    CettaLpNativeIdVec nonterminals;
    CettaLpNativeIdVec terminals;
    bool *nullable;
    CettaLpNativeBitset *first;
    CettaLpNativeBitset *follow;
    bool full_prediction;
} CettaLpNativeGllPreparedImpl;

static void gll_prepared_impl_free(
    CettaLpNativeGllPreparedImpl *prepared) {
    uint32_t index;

    if (!prepared)
        return;
    if (prepared->first) {
        for (index = 0u; index < prepared->nonterminals.len; index++)
            bitset_free(&prepared->first[index]);
    }
    if (prepared->follow) {
        for (index = 0u; index < prepared->nonterminals.len; index++)
            bitset_free(&prepared->follow[index]);
    }
    free(prepared->nullable);
    free(prepared->first);
    free(prepared->follow);
    free(prepared->nonterminals.data);
    free(prepared->terminals.data);
    slr_productions_free(
        prepared->productions, prepared->production_len);
    memset(prepared, 0, sizeof(*prepared));
}

static bool gll_prepared_impl_build(
    CettaLpNativeGllPreparedImpl *prepared,
    const CettaLpNativeGrammar *grammar,
    uint32_t start_nt,
    bool full_prediction,
    char *error_buf,
    size_t error_buf_size) {
    bool ok = false;

    if (!prepared || !grammar) {
        slr_summary_set_error(
            error_buf, error_buf_size,
            "bad native GLL preparation arguments");
        return false;
    }
    memset(prepared, 0, sizeof(*prepared));
    prepared->start_nt = start_nt;
    prepared->full_prediction = full_prediction;
    if (!slr_build_productions(
            grammar, &prepared->productions, &prepared->production_len,
            error_buf, error_buf_size) ||
        !slr_grammar_mentions_nonterminal(
            prepared->productions, prepared->production_len, start_nt)) {
        if (!error_buf || error_buf[0] == '\0') {
            slr_summary_set_error(
                error_buf, error_buf_size,
                "start nonterminal not present in grammar");
        }
        goto done;
    }
    if (full_prediction &&
        (!slr_collect_symbol_sets(
             prepared->productions, prepared->production_len, start_nt,
             &prepared->nonterminals, &prepared->terminals,
             error_buf, error_buf_size) ||
         !slr_compute_nullable_first_follow(
             prepared->productions, prepared->production_len,
             &prepared->nonterminals, &prepared->terminals, start_nt,
             &prepared->nullable, &prepared->first, &prepared->follow,
             error_buf, error_buf_size))) {
        goto done;
    }
    prepared->max_rhs_len = gll_max_rhs_len(
        prepared->productions, prepared->production_len);
    ok = true;

done:
    if (!ok)
        gll_prepared_impl_free(prepared);
    return ok;
}

void cetta_lp_native_gll_prepared_init(
    CettaLpNativeGllPrepared *prepared) {
    if (!prepared)
        return;
    prepared->implementation = NULL;
}

void cetta_lp_native_gll_prepared_free(
    CettaLpNativeGllPrepared *prepared) {
    CettaLpNativeGllPreparedImpl *implementation;

    if (!prepared)
        return;
    implementation = prepared->implementation;
    if (implementation) {
        gll_prepared_impl_free(implementation);
        free(implementation);
    }
    prepared->implementation = NULL;
}

bool cetta_lp_native_gll_prepare(
    CettaLpNativeGllPrepared *prepared,
    const CettaLpNativeGrammar *grammar,
    uint32_t start_nt,
    char *error_buf,
    size_t error_buf_size) {
    CettaLpNativeGllPreparedImpl *implementation;

    if (error_buf && error_buf_size > 0u)
        error_buf[0] = '\0';
    if (!prepared || !grammar) {
        slr_summary_set_error(
            error_buf, error_buf_size,
            "bad native GLL preparation arguments");
        return false;
    }
    implementation = cetta_malloc(sizeof(*implementation));
    if (!gll_prepared_impl_build(
            implementation, grammar, start_nt, true,
            error_buf, error_buf_size)) {
        free(implementation);
        return false;
    }
    cetta_lp_native_gll_prepared_free(prepared);
    prepared->implementation = implementation;
    return true;
}

static bool native_gll_parse_input_forest(
    const CettaLpNativeGllPreparedImpl *prepared,
    CettaLpNativeParseInput *input,
    uint32_t start_scalar,
    uint32_t descriptor_limit,
    CettaLpNativeUtf8Forest *out,
    char *error_buf,
    size_t error_buf_size) {
    CettaLpNativeSlrProduction *productions =
        prepared ? prepared->productions : NULL;
    uint32_t production_len = prepared ? prepared->production_len : 0u;
    uint32_t start_nt = prepared ? prepared->start_nt : 0u;
    CettaLpNativeGllNodeVec nodes = {0};
    CettaLpNativeGllGssNodeVec gss_nodes = {0};
    CettaLpNativeGllDescriptorVec seen = {0};
    CettaLpNativeU32Vec work = {0};
    CettaLpNativeU32Vec roots = {0};
    CettaLpNativeU32Vec expectations = {0};
    CettaLpNativeIdVec nonterminals = prepared
        ? prepared->nonterminals : (CettaLpNativeIdVec){0};
    CettaLpNativeIdVec grammar_terminals = prepared
        ? prepared->terminals : (CettaLpNativeIdVec){0};
    bool *nullable = prepared ? prepared->nullable : NULL;
    CettaLpNativeBitset *first = prepared ? prepared->first : NULL;
    CettaLpNativeBitset *follow = prepared ? prepared->follow : NULL;
    CettaLpNativeUtf8Forest result;
    uint32_t farthest = start_scalar;
    uint32_t max_rhs_len = prepared ? prepared->max_rhs_len : 0u;
    uint32_t prod_idx;
    bool full_prediction = prepared && prepared->full_prediction;
    bool limit_hit = false;
    bool ok = false;

    cetta_lp_native_utf8_forest_init(&result);
    if (error_buf && error_buf_size > 0u)
        error_buf[0] = '\0';
    if (!prepared || !input || !out || descriptor_limit == 0u ||
        start_scalar > native_parse_input_scalar_len(input)) {
        slr_summary_set_error(error_buf, error_buf_size,
                              "bad native GLL parse-input arguments");
        goto done;
    }
    for (prod_idx = 0u; prod_idx < production_len; prod_idx++) {
        uint32_t item_idx;
        for (item_idx = 0u; item_idx < productions[prod_idx].rhs_len;
             item_idx++) {
            const CettaLpNativeSymbol *symbol =
                &productions[prod_idx].rhs[item_idx];
            if (symbol->kind == CETTA_LP_NATIVE_SYMBOL_TM &&
                !native_parse_input_terminal_declared(input, symbol->name)) {
                slr_summary_set_error(
                    error_buf, error_buf_size,
                    "grammar terminal %u is absent from parse input",
                    symbol->name);
                goto done;
            }
        }
    }
    gll_descvec_init_compact(&seen, production_len, max_rhs_len,
                             native_parse_input_byte_len(input));
    if (gll_gss_get(
            &gss_nodes, true, -1, 0u, start_scalar) != 0) {
        slr_summary_set_error(error_buf, error_buf_size,
                              "failed to initialize UTF-8 GLL root");
        goto done;
    }
    for (prod_idx = 0u; prod_idx < production_len; prod_idx++) {
        SymbolId lhs = 0;
        const CettaLpNativeSymbol *rhs = NULL;
        uint32_t rhs_len = 0u;
        slr_get_prod(productions, production_len, start_nt,
                     (int32_t)prod_idx, &lhs, &rhs, &rhs_len);
        (void)rhs;
        (void)rhs_len;
        if (lhs != start_nt)
            continue;
        if (full_prediction) {
            bool predicted;
            if (!gll_full_prediction_production(
                    productions, production_len, prod_idx,
                    &nonterminals, &grammar_terminals,
                    nullable, first, follow, input, start_scalar,
                    &predicted, error_buf, error_buf_size)) {
                goto done;
            }
            if (!predicted) {
                if (!gll_full_prediction_collect_expected(
                        productions, production_len, prod_idx,
                        &nonterminals, &grammar_terminals,
                        nullable, first, follow, &expectations,
                        error_buf, error_buf_size)) {
                    goto done;
                }
                continue;
            }
        }
        if (!gll_utf8_enqueue(&seen, &work, (int32_t)prod_idx, 0u, 0u,
                              CETTA_LP_NATIVE_NODE_NONE, start_scalar,
                              descriptor_limit, &limit_hit)) {
            slr_summary_set_error(error_buf, error_buf_size,
                                  "failed to seed UTF-8 GLL descriptors");
            goto done;
        }
        if (limit_hit)
            goto resource;
    }

    while (work.len > 0u) {
        uint32_t cur_idx = work.data[--work.len];
        CettaLpNativeGllDescriptor cur;
        SymbolId lhs = 0;
        const CettaLpNativeSymbol *rhs = NULL;
        uint32_t rhs_len = 0u;

        if (!gll_descvec_get(&seen, cur_idx, &cur)) {
            slr_summary_set_error(error_buf, error_buf_size,
                                  "bad UTF-8 GLL descriptor index");
            goto done;
        }
        if (cur.pos > farthest) {
            farthest = cur.pos;
            expectations.len = 0u;
        }
        slr_get_prod(productions, production_len, start_nt,
                     cur.prod_idx, &lhs, &rhs, &rhs_len);
        if (cur.dot < rhs_len) {
            CettaLpNativeSymbol symbol = rhs[cur.dot];
            if (symbol.kind == CETTA_LP_NATIVE_SYMBOL_TM) {
                CettaLpNativeInputMatchIter iter;
                CettaLpNativeInputMatch match;
                if (cur.pos == farthest &&
                    !u32vec_push_unique(&expectations, symbol.name)) {
                    goto done;
                }
                if (!native_parse_input_match_begin(
                        input, symbol.name, cur.pos, &iter,
                        error_buf, error_buf_size)) {
                    goto done;
                }
                while (native_parse_input_match_next(input, &iter, &match)) {
                    int32_t term_idx = gll_node_get_term_value(
                        &nodes, symbol.name, cur.pos, match.right,
                        match.value_kind, match.value);
                    int32_t parent_idx;
                    if (term_idx < 0) {
                        slr_summary_set_error(
                            error_buf, error_buf_size,
                            "failed to allocate GLL terminal match");
                        goto done;
                    }
                    parent_idx = gll_sppf_get_node(
                        &nodes, productions, production_len, start_nt,
                        cur.prod_idx, cur.dot + 1u, cur.left_label,
                        (uint32_t)term_idx, error_buf, error_buf_size);
                    if (parent_idx < 0 ||
                        !gll_utf8_enqueue(
                            &seen, &work, cur.prod_idx, cur.dot + 1u,
                            cur.gss_idx, (uint32_t)parent_idx, match.right,
                            descriptor_limit, &limit_hit)) {
                        slr_summary_set_error(
                            error_buf, error_buf_size,
                            "failed to advance GLL terminal match");
                        goto done;
                    }
                    if (limit_hit)
                        goto resource;
                }
                continue;
            }
            {
                int32_t next_gss = gll_gss_get(
                    &gss_nodes, false, cur.prod_idx, cur.dot + 1u, cur.pos);
                uint32_t child_prod_idx;
                uint32_t popped_idx;
                if (next_gss < 0 ||
                    !gll_gss_add_edge(&gss_nodes, (uint32_t)next_gss,
                                      cur.left_label, cur.gss_idx)) {
                    slr_summary_set_error(error_buf, error_buf_size,
                                          "failed to create UTF-8 GLL continuation");
                    goto done;
                }
                for (popped_idx = 0u;
                     popped_idx < gss_nodes.data[next_gss].popped.len;
                     popped_idx++) {
                    uint32_t child =
                        gss_nodes.data[next_gss].popped.data[popped_idx];
                    int32_t parent_idx = gll_sppf_get_node(
                        &nodes, productions, production_len, start_nt,
                        cur.prod_idx, cur.dot + 1u, cur.left_label, child,
                        error_buf, error_buf_size);
                    if (parent_idx < 0 ||
                        !gll_utf8_enqueue(
                            &seen, &work, cur.prod_idx, cur.dot + 1u,
                            cur.gss_idx, (uint32_t)parent_idx,
                            nodes.data[child].right, descriptor_limit,
                            &limit_hit)) {
                        slr_summary_set_error(
                            error_buf, error_buf_size,
                            "failed to resume UTF-8 GLL continuation");
                        goto done;
                    }
                    if (limit_hit)
                        goto resource;
                }
                for (child_prod_idx = 0u;
                     child_prod_idx < production_len; child_prod_idx++) {
                    SymbolId child_lhs = 0;
                    const CettaLpNativeSymbol *child_rhs = NULL;
                    uint32_t child_rhs_len = 0u;
                    slr_get_prod(productions, production_len, start_nt,
                                 (int32_t)child_prod_idx, &child_lhs,
                                 &child_rhs, &child_rhs_len);
                    (void)child_rhs;
                    (void)child_rhs_len;
                    if (child_lhs != symbol.name)
                        continue;
                    if (full_prediction) {
                        bool predicted;
                        if (!gll_full_prediction_production(
                                productions, production_len,
                                child_prod_idx,
                                &nonterminals, &grammar_terminals,
                                nullable, first, follow, input, cur.pos,
                                &predicted, error_buf, error_buf_size)) {
                            goto done;
                        }
                        if (!predicted) {
                            if (cur.pos == farthest &&
                                !gll_full_prediction_collect_expected(
                                    productions, production_len,
                                    child_prod_idx,
                                    &nonterminals, &grammar_terminals,
                                    nullable, first, follow,
                                    &expectations,
                                    error_buf, error_buf_size)) {
                                goto done;
                            }
                            continue;
                        }
                    }
                    if (!gll_utf8_enqueue(
                            &seen, &work, (int32_t)child_prod_idx, 0u,
                            (uint32_t)next_gss,
                            CETTA_LP_NATIVE_NODE_NONE, cur.pos,
                            descriptor_limit, &limit_hit)) {
                        slr_summary_set_error(error_buf, error_buf_size,
                                              "failed to seed UTF-8 GLL child");
                        goto done;
                    }
                    if (limit_hit)
                        goto resource;
                }
                continue;
            }
        }
        {
            uint32_t done_idx;
            uint32_t edge_idx;
            if (rhs_len == 0u) {
                int32_t symbol_idx =
                    gll_node_get_sym(&nodes, lhs, cur.pos, cur.pos);
                int32_t epsilon_idx = gll_node_get_eps(&nodes, cur.pos);
                if (symbol_idx < 0 || epsilon_idx < 0 ||
                    !gll_node_push_packed_unique(
                        &nodes, (uint32_t)symbol_idx,
                        CETTA_LP_NATIVE_NODE_NONE, (uint32_t)epsilon_idx,
                        cur.pos, cur.prod_idx)) {
                    slr_summary_set_error(error_buf, error_buf_size,
                                          "failed to record UTF-8 GLL epsilon");
                    goto done;
                }
                done_idx = (uint32_t)symbol_idx;
            } else {
                done_idx = cur.left_label;
            }
            if (cur.gss_idx == 0u) {
                if (!u32vec_push_unique(&gss_nodes.data[0].popped,
                                        done_idx)) {
                    goto done;
                }
                continue;
            }
            if (!u32vec_push_unique(
                    &gss_nodes.data[cur.gss_idx].popped, done_idx)) {
                goto done;
            }
            for (edge_idx = 0u;
                 edge_idx < gss_nodes.data[cur.gss_idx].edges.len;
                 edge_idx++) {
                CettaLpNativeGllGssEdge edge =
                    gss_nodes.data[cur.gss_idx].edges.data[edge_idx];
                const CettaLpNativeGllGssNode *gss =
                    &gss_nodes.data[cur.gss_idx];
                int32_t parent_idx = gll_sppf_get_node(
                    &nodes, productions, production_len, start_nt,
                    gss->prod_idx, gss->dot, edge.left_label, done_idx,
                    error_buf, error_buf_size);
                if (parent_idx < 0 ||
                    !gll_utf8_enqueue(
                        &seen, &work, gss->prod_idx, gss->dot,
                        edge.parent_gss, (uint32_t)parent_idx,
                        nodes.data[done_idx].right, descriptor_limit,
                        &limit_hit)) {
                    slr_summary_set_error(error_buf, error_buf_size,
                                          "failed to propagate UTF-8 GLL completion");
                    goto done;
                }
                if (limit_hit)
                    goto resource;
            }
        }
    }

    if (!native_parse_input_finish(input, error_buf, error_buf_size) ||
        !gll_utf8_collect_roots(&nodes, &gss_nodes, start_nt, start_scalar,
                                native_parse_input_scalar_len(input), &roots) ||
        !native_forest_copy_input(
            &result, input, error_buf, error_buf_size) ||
        !native_forest_export(
            &result, input, &nodes, &roots,
            error_buf, error_buf_size)) {
        goto done;
    }
    goto finish_result;

resource:
    if (!native_parse_input_finish(input, error_buf, error_buf_size) ||
        !native_forest_copy_input(
            &result, input, error_buf, error_buf_size)) {
        goto done;
    }
    result.outcome = CETTA_LP_NATIVE_UTF8_FOREST_RESOURCE_LIMIT;

finish_result:
    if (expectations.len > 1u) {
        qsort(expectations.data, expectations.len,
              sizeof(*expectations.data), utf8_u32_compare);
    }
    if (expectations.len > 0u) {
        result.expected_terminal_ids = cetta_malloc(
            sizeof(*result.expected_terminal_ids) * expectations.len);
        memcpy(result.expected_terminal_ids, expectations.data,
               sizeof(*result.expected_terminal_ids) * expectations.len);
    }
    result.expected_terminal_len = expectations.len;
    result.farthest_scalar = farthest;
    if (farthest > native_parse_input_scalar_len(input)) {
        slr_summary_set_error(error_buf, error_buf_size,
                              "UTF-8 GLL farthest position is invalid");
        goto done;
    }
    result.farthest_byte =
        native_parse_input_byte_offsets(input)[farthest];
    result.graph_node_len = gss_nodes.len;
    result.work_item_len = seen.len;
    cetta_lp_native_utf8_forest_free(out);
    *out = result;
    memset(&result, 0, sizeof(result));
    ok = true;

done:
    cetta_lp_native_utf8_forest_free(&result);
    free(roots.data);
    free(expectations.data);
    gll_nodevec_free(&nodes);
    gll_gssvec_free(&gss_nodes);
    gll_descvec_free(&seen);
    free(work.data);
    return ok;
}

static bool native_gll_parse_utf8_forest_policy(
    const CettaLpNativeGrammar *grammar,
    uint32_t start_nt,
    const CettaLpNativeUtf8Terminal *terminals,
    uint32_t terminal_len,
    const uint8_t *input_bytes,
    size_t input_byte_len,
    bool full_prediction,
    uint32_t descriptor_limit,
    CettaLpNativeUtf8Forest *out,
    char *error_buf,
    size_t error_buf_size) {
    CettaLpNativeGllPreparedImpl prepared;
    CettaLpNativeUtf8Input decoded;
    CettaLpNativeParseInput input;
    bool ok;

    if (error_buf && error_buf_size > 0u)
        error_buf[0] = '\0';
    if (!grammar || !out || descriptor_limit == 0u ||
        input_byte_len > UINT32_MAX ||
        (input_byte_len > 0u && !input_bytes)) {
        slr_summary_set_error(error_buf, error_buf_size,
                              "bad native GLL UTF-8 arguments");
        return false;
    }
    if (!utf8_terminals_validate(
            terminals, terminal_len, error_buf, error_buf_size)) {
        return false;
    }
    if (!gll_prepared_impl_build(
            &prepared, grammar, start_nt, full_prediction,
            error_buf, error_buf_size)) {
        return false;
    }
    utf8_input_init(&decoded, input_bytes, input_byte_len);
    input = (CettaLpNativeParseInput){
        .kind = CETTA_LP_NATIVE_PARSE_INPUT_UTF8,
        .utf8 = &decoded,
        .terminals = terminals,
        .terminal_len = terminal_len,
        .lattice = NULL,
    };
    ok = native_gll_parse_input_forest(
        &prepared, &input, 0u, descriptor_limit,
        out, error_buf, error_buf_size);
    utf8_input_free(&decoded);
    gll_prepared_impl_free(&prepared);
    return ok;
}

bool cetta_lp_native_gll_parse_utf8_forest(
    const CettaLpNativeGrammar *grammar,
    uint32_t start_nt,
    const CettaLpNativeUtf8Terminal *terminals,
    uint32_t terminal_len,
    const uint8_t *input_bytes,
    size_t input_byte_len,
    uint32_t descriptor_limit,
    CettaLpNativeUtf8Forest *out,
    char *error_buf,
    size_t error_buf_size) {
    return native_gll_parse_utf8_forest_policy(
        grammar, start_nt, terminals, terminal_len,
        input_bytes, input_byte_len, false, descriptor_limit,
        out, error_buf, error_buf_size);
}

bool cetta_lp_native_gll_parse_utf8_forest_complete(
    const CettaLpNativeGrammar *grammar,
    uint32_t start_nt,
    const CettaLpNativeUtf8Terminal *terminals,
    uint32_t terminal_len,
    const uint8_t *input_bytes,
    size_t input_byte_len,
    uint32_t descriptor_limit,
    CettaLpNativeUtf8Forest *out,
    char *error_buf,
    size_t error_buf_size) {
    return native_gll_parse_utf8_forest_policy(
        grammar, start_nt, terminals, terminal_len,
        input_bytes, input_byte_len, true, descriptor_limit,
        out, error_buf, error_buf_size);
}

bool cetta_lp_native_gll_prepared_parse_utf8_forest_complete(
    const CettaLpNativeGllPrepared *prepared,
    const CettaLpNativeUtf8Terminal *terminals,
    uint32_t terminal_len,
    const uint8_t *input_bytes,
    size_t input_byte_len,
    uint32_t descriptor_limit,
    CettaLpNativeUtf8Forest *out,
    char *error_buf,
    size_t error_buf_size) {
    const CettaLpNativeGllPreparedImpl *implementation =
        prepared ? prepared->implementation : NULL;
    CettaLpNativeUtf8Input decoded;
    CettaLpNativeParseInput input;
    bool ok;

    if (error_buf && error_buf_size > 0u)
        error_buf[0] = '\0';
    if (!implementation || !out || descriptor_limit == 0u ||
        input_byte_len > UINT32_MAX ||
        (input_byte_len > 0u && !input_bytes)) {
        slr_summary_set_error(
            error_buf, error_buf_size,
            "bad prepared native GLL UTF-8 arguments");
        return false;
    }
    if (!utf8_terminals_validate(
            terminals, terminal_len, error_buf, error_buf_size)) {
        return false;
    }
    utf8_input_init(&decoded, input_bytes, input_byte_len);
    input = (CettaLpNativeParseInput){
        .kind = CETTA_LP_NATIVE_PARSE_INPUT_UTF8,
        .utf8 = &decoded,
        .terminals = terminals,
        .terminal_len = terminal_len,
        .lattice = NULL,
    };
    ok = native_gll_parse_input_forest(
        implementation, &input, 0u, descriptor_limit,
        out, error_buf, error_buf_size);
    utf8_input_free(&decoded);
    return ok;
}

static bool native_gll_parse_utf8_scalar_view_forest_policy(
    const CettaLpNativeGrammar *grammar,
    uint32_t start_nt,
    const CettaLpNativeUtf8Terminal *terminals,
    uint32_t terminal_len,
    const CettaLpNativeUtf8ScalarView *view,
    bool full_prediction,
    uint32_t descriptor_limit,
    CettaLpNativeUtf8Forest *out,
    char *error_buf,
    size_t error_buf_size) {
    CettaLpNativeGllPreparedImpl prepared;
    CettaLpNativeUtf8Input decoded;
    CettaLpNativeParseInput input;
    bool ok;

    if (error_buf && error_buf_size > 0u)
        error_buf[0] = '\0';
    if (!grammar || !out || descriptor_limit == 0u ||
        !cetta_lp_native_utf8_scalar_view_validate(
            view, error_buf, error_buf_size)) {
        if (!error_buf || error_buf[0] == '\0') {
            slr_summary_set_error(
                error_buf, error_buf_size,
                "bad native GLL UTF-8 scalar view arguments");
        }
        return false;
    }
    if (!utf8_terminals_validate(
            terminals, terminal_len, error_buf, error_buf_size)) {
        return false;
    }
    if (!gll_prepared_impl_build(
            &prepared, grammar, start_nt, full_prediction,
            error_buf, error_buf_size)) {
        return false;
    }
    utf8_input_init_scalar_view(&decoded, view);
    input = (CettaLpNativeParseInput){
        .kind = CETTA_LP_NATIVE_PARSE_INPUT_UTF8,
        .utf8 = &decoded,
        .terminals = terminals,
        .terminal_len = terminal_len,
        .lattice = NULL,
    };
    ok = native_gll_parse_input_forest(
        &prepared, &input, 0u, descriptor_limit,
        out, error_buf, error_buf_size);
    utf8_input_free(&decoded);
    gll_prepared_impl_free(&prepared);
    return ok;
}

bool cetta_lp_native_gll_parse_utf8_scalar_view_forest(
    const CettaLpNativeGrammar *grammar,
    uint32_t start_nt,
    const CettaLpNativeUtf8Terminal *terminals,
    uint32_t terminal_len,
    const CettaLpNativeUtf8ScalarView *view,
    uint32_t descriptor_limit,
    CettaLpNativeUtf8Forest *out,
    char *error_buf,
    size_t error_buf_size) {
    return native_gll_parse_utf8_scalar_view_forest_policy(
        grammar, start_nt, terminals, terminal_len, view, false,
        descriptor_limit, out, error_buf, error_buf_size);
}

bool cetta_lp_native_gll_parse_utf8_scalar_view_forest_complete(
    const CettaLpNativeGrammar *grammar,
    uint32_t start_nt,
    const CettaLpNativeUtf8Terminal *terminals,
    uint32_t terminal_len,
    const CettaLpNativeUtf8ScalarView *view,
    uint32_t descriptor_limit,
    CettaLpNativeUtf8Forest *out,
    char *error_buf,
    size_t error_buf_size) {
    return native_gll_parse_utf8_scalar_view_forest_policy(
        grammar, start_nt, terminals, terminal_len, view, true,
        descriptor_limit, out, error_buf, error_buf_size);
}

bool cetta_lp_native_gll_prepared_parse_utf8_scalar_view_forest_complete(
    const CettaLpNativeGllPrepared *prepared,
    const CettaLpNativeUtf8Terminal *terminals,
    uint32_t terminal_len,
    const CettaLpNativeUtf8ScalarView *view,
    uint32_t descriptor_limit,
    CettaLpNativeUtf8Forest *out,
    char *error_buf,
    size_t error_buf_size) {
    const CettaLpNativeGllPreparedImpl *implementation =
        prepared ? prepared->implementation : NULL;
    CettaLpNativeUtf8Input decoded;
    CettaLpNativeParseInput input;
    bool ok;

    if (error_buf && error_buf_size > 0u)
        error_buf[0] = '\0';
    if (!implementation || !out || descriptor_limit == 0u ||
        !cetta_lp_native_utf8_scalar_view_validate(
            view, error_buf, error_buf_size)) {
        if (!error_buf || error_buf[0] == '\0') {
            slr_summary_set_error(
                error_buf, error_buf_size,
                "bad prepared native GLL UTF-8 scalar view arguments");
        }
        return false;
    }
    if (!utf8_terminals_validate(
            terminals, terminal_len, error_buf, error_buf_size)) {
        return false;
    }
    utf8_input_init_scalar_view(&decoded, view);
    input = (CettaLpNativeParseInput){
        .kind = CETTA_LP_NATIVE_PARSE_INPUT_UTF8,
        .utf8 = &decoded,
        .terminals = terminals,
        .terminal_len = terminal_len,
        .lattice = NULL,
    };
    ok = native_gll_parse_input_forest(
        implementation, &input, 0u, descriptor_limit,
        out, error_buf, error_buf_size);
    utf8_input_free(&decoded);
    return ok;
}

bool cetta_lp_native_gll_parse_utf8_lattice_forest(
    const CettaLpNativeGrammar *grammar,
    uint32_t start_nt,
    const CettaLpNativeUtf8Lattice *lattice,
    uint32_t descriptor_limit,
    CettaLpNativeUtf8Forest *out,
    char *error_buf,
    size_t error_buf_size) {
    return cetta_lp_native_gll_parse_utf8_lattice_forest_from(
        grammar, start_nt, lattice, 0u, descriptor_limit,
        out, error_buf, error_buf_size);
}

bool cetta_lp_native_gll_parse_utf8_lattice_forest_complete(
    const CettaLpNativeGrammar *grammar,
    uint32_t start_nt,
    const CettaLpNativeUtf8Lattice *lattice,
    uint32_t descriptor_limit,
    CettaLpNativeUtf8Forest *out,
    char *error_buf,
    size_t error_buf_size) {
    return cetta_lp_native_gll_parse_utf8_lattice_forest_from_complete(
        grammar, start_nt, lattice, 0u, descriptor_limit,
        out, error_buf, error_buf_size);
}

static bool native_gll_parse_utf8_lattice_forest_from_policy(
    const CettaLpNativeGrammar *grammar,
    uint32_t start_nt,
    const CettaLpNativeUtf8Lattice *lattice,
    uint32_t start_scalar,
    bool full_prediction,
    uint32_t descriptor_limit,
    CettaLpNativeUtf8Forest *out,
    char *error_buf,
    size_t error_buf_size) {
    CettaLpNativeGllPreparedImpl prepared;
    CettaLpNativeParseInput input;
    bool ok;

    if (error_buf && error_buf_size > 0u)
        error_buf[0] = '\0';
    if (!grammar || !out || descriptor_limit == 0u || !lattice ||
        start_scalar > lattice->scalar_len ||
        !cetta_lp_native_utf8_lattice_validate(
            lattice, error_buf, error_buf_size)) {
        if ((!error_buf || error_buf[0] == '\0')) {
            slr_summary_set_error(error_buf, error_buf_size,
                                  "bad native GLL UTF-8 lattice arguments");
        }
        return false;
    }
    if (!gll_prepared_impl_build(
            &prepared, grammar, start_nt, full_prediction,
            error_buf, error_buf_size)) {
        return false;
    }
    input = (CettaLpNativeParseInput){
        .kind = CETTA_LP_NATIVE_PARSE_INPUT_LATTICE,
        .utf8 = NULL,
        .terminals = NULL,
        .terminal_len = 0u,
        .lattice = lattice,
    };
    ok = native_gll_parse_input_forest(
        &prepared, &input, start_scalar, descriptor_limit,
        out, error_buf, error_buf_size);
    gll_prepared_impl_free(&prepared);
    return ok;
}

bool cetta_lp_native_gll_parse_utf8_lattice_forest_from(
    const CettaLpNativeGrammar *grammar,
    uint32_t start_nt,
    const CettaLpNativeUtf8Lattice *lattice,
    uint32_t start_scalar,
    uint32_t descriptor_limit,
    CettaLpNativeUtf8Forest *out,
    char *error_buf,
    size_t error_buf_size) {
    return native_gll_parse_utf8_lattice_forest_from_policy(
        grammar, start_nt, lattice, start_scalar, false,
        descriptor_limit, out, error_buf, error_buf_size);
}

bool cetta_lp_native_gll_parse_utf8_lattice_forest_from_complete(
    const CettaLpNativeGrammar *grammar,
    uint32_t start_nt,
    const CettaLpNativeUtf8Lattice *lattice,
    uint32_t start_scalar,
    uint32_t descriptor_limit,
    CettaLpNativeUtf8Forest *out,
    char *error_buf,
    size_t error_buf_size) {
    return native_gll_parse_utf8_lattice_forest_from_policy(
        grammar, start_nt, lattice, start_scalar, true,
        descriptor_limit, out, error_buf, error_buf_size);
}

bool cetta_lp_native_gll_prepared_parse_utf8_lattice_forest_from_complete(
    const CettaLpNativeGllPrepared *prepared,
    const CettaLpNativeUtf8Lattice *lattice,
    uint32_t start_scalar,
    uint32_t descriptor_limit,
    CettaLpNativeUtf8Forest *out,
    char *error_buf,
    size_t error_buf_size) {
    const CettaLpNativeGllPreparedImpl *implementation =
        prepared ? prepared->implementation : NULL;
    CettaLpNativeParseInput input;

    if (error_buf && error_buf_size > 0u)
        error_buf[0] = '\0';
    if (!implementation || !out || descriptor_limit == 0u || !lattice ||
        start_scalar > lattice->scalar_len ||
        !cetta_lp_native_utf8_lattice_validate(
            lattice, error_buf, error_buf_size)) {
        if (!error_buf || error_buf[0] == '\0') {
            slr_summary_set_error(
                error_buf, error_buf_size,
                "bad prepared native GLL UTF-8 lattice arguments");
        }
        return false;
    }
    input = (CettaLpNativeParseInput){
        .kind = CETTA_LP_NATIVE_PARSE_INPUT_LATTICE,
        .utf8 = NULL,
        .terminals = NULL,
        .terminal_len = 0u,
        .lattice = lattice,
    };
    return native_gll_parse_input_forest(
        implementation, &input, start_scalar, descriptor_limit,
        out, error_buf, error_buf_size);
}

typedef struct {
    CettaLpNativeSlrProduction *productions;
    uint32_t production_len;
    uint32_t grammar_production_len;
    CettaLpNativeIdVec nonterminals;
    CettaLpNativeIdVec terminals;
    bool *nullable;
    CettaLpNativeBitset *first;
    CettaLpNativeBitset *follow;
    CettaLpNativeStateVec states;
    CettaLpNativeSymbolVec symbols;
    CettaLpNativeEdgeVec edges;
    CettaLpNativeActionVec actions;
    uint32_t conflict_len;
} CettaLpNativeGlrUtf8Table;

static void glr_utf8_table_free(CettaLpNativeGlrUtf8Table *table) {
    uint32_t index;

    if (!table)
        return;
    if (table->first && table->follow) {
        for (index = 0u; index < table->nonterminals.len; index++) {
            bitset_free(&table->first[index]);
            bitset_free(&table->follow[index]);
        }
    }
    free(table->nullable);
    free(table->first);
    free(table->follow);
    free(table->nonterminals.data);
    free(table->terminals.data);
    free(table->symbols.data);
    free(table->edges.data);
    free(table->actions.data);
    statevec_free(&table->states);
    slr_productions_free(table->productions, table->production_len);
    memset(table, 0, sizeof(*table));
}

static bool glr_utf8_table_build(
    const CettaLpNativeGrammar *grammar,
    uint32_t start_nt,
    CettaLpNativeGlrUtf8Table *table,
    char *error_buf,
    size_t error_buf_size) {
    uint32_t state_index;

    memset(table, 0, sizeof(*table));
    if (!grammar) {
        slr_summary_set_error(
            error_buf, error_buf_size,
            "bad native GLR table preparation arguments");
        return false;
    }
    table->grammar_production_len = grammar->production_len;
    if (!slr_build_productions(
            grammar, &table->productions, &table->production_len,
            error_buf, error_buf_size) ||
        !slr_collect_symbol_sets(
            table->productions, table->production_len, start_nt,
            &table->nonterminals, &table->terminals,
            error_buf, error_buf_size)) {
        return false;
    }
    if (!slr_grammar_mentions_nonterminal(
            table->productions, table->production_len, start_nt)) {
        slr_summary_set_error(error_buf, error_buf_size,
                              "start nonterminal not present in grammar");
        return false;
    }
    if (!slr_compute_nullable_first_follow(
            table->productions, table->production_len,
            &table->nonterminals, &table->terminals, start_nt,
            &table->nullable, &table->first, &table->follow,
            error_buf, error_buf_size) ||
        !slr_build_states(
            table->productions, table->production_len, start_nt,
            &table->states, &table->symbols, &table->edges,
            error_buf, error_buf_size)) {
        return false;
    }

    for (state_index = 0u; state_index < table->states.len; state_index++) {
        uint32_t item_index;
        for (item_index = 0u;
             item_index < table->states.data[state_index].len;
             item_index++) {
            SymbolId lhs = 0u;
            const CettaLpNativeSymbol *rhs = NULL;
            uint32_t rhs_len = 0u;
            CettaLpNativeItem item =
                table->states.data[state_index].items[item_index];

            slr_get_prod(table->productions, table->production_len,
                         start_nt, item.prod_idx, &lhs, &rhs, &rhs_len);
            if (rhs && item.dot < rhs_len) {
                if (rhs[item.dot].kind == CETTA_LP_NATIVE_SYMBOL_TM) {
                    int32_t symbol_index = symbolvec_find(
                        &table->symbols, true, rhs[item.dot].name);
                    int32_t terminal_index = idvec_find(
                        &table->terminals, rhs[item.dot].name);
                    int32_t edge_index = -1;
                    if (symbol_index >= 0 && terminal_index >= 0) {
                        edge_index = edgevec_find(
                            &table->edges, state_index,
                            (uint32_t)symbol_index);
                    }
                    if (edge_index >= 0 && terminal_index >= 0 &&
                        !actionvec_push_unique(
                            &table->actions, state_index,
                            (uint32_t)terminal_index, 's',
                            (int32_t)table->edges.data[edge_index].target,
                            &table->conflict_len)) {
                        slr_summary_set_error(
                            error_buf, error_buf_size,
                            "failed to record UTF-8 GLR shift action");
                        return false;
                    }
                }
                continue;
            }
            if (item.prod_idx == -1) {
                if (!actionvec_push_unique(
                        &table->actions, state_index,
                        table->terminals.len, 'a', 0,
                        &table->conflict_len)) {
                    slr_summary_set_error(
                        error_buf, error_buf_size,
                        "failed to record UTF-8 GLR accept action");
                    return false;
                }
                continue;
            }
            {
                int32_t lhs_index = idvec_find(&table->nonterminals, lhs);
                uint32_t token_index;
                if (lhs_index < 0) {
                    slr_summary_set_error(
                        error_buf, error_buf_size,
                        "UTF-8 GLR reduce lhs missing from follow set");
                    return false;
                }
                for (token_index = 0u;
                     token_index <= table->terminals.len; token_index++) {
                    if (!bitset_test(
                            &table->follow[lhs_index], token_index)) {
                        continue;
                    }
                    if (!actionvec_push_unique(
                            &table->actions, state_index, token_index,
                            'r', item.prod_idx,
                            &table->conflict_len)) {
                        slr_summary_set_error(
                            error_buf, error_buf_size,
                            "failed to record UTF-8 GLR reduce action");
                        return false;
                    }
                }
            }
        }
    }
    return true;
}

typedef struct {
    uint32_t start_nt;
    CettaLpNativeGlrUtf8Table table;
} CettaLpNativeGlrPreparedImpl;

void cetta_lp_native_glr_prepared_init(
    CettaLpNativeGlrPrepared *prepared) {
    if (!prepared)
        return;
    prepared->implementation = NULL;
}

void cetta_lp_native_glr_prepared_free(
    CettaLpNativeGlrPrepared *prepared) {
    CettaLpNativeGlrPreparedImpl *implementation;

    if (!prepared)
        return;
    implementation = prepared->implementation;
    if (implementation) {
        glr_utf8_table_free(&implementation->table);
        free(implementation);
    }
    prepared->implementation = NULL;
}

bool cetta_lp_native_glr_prepare(
    CettaLpNativeGlrPrepared *prepared,
    const CettaLpNativeGrammar *grammar,
    uint32_t start_nt,
    char *error_buf,
    size_t error_buf_size) {
    CettaLpNativeGlrPreparedImpl *implementation;

    if (error_buf && error_buf_size > 0u)
        error_buf[0] = '\0';
    if (!prepared || !grammar) {
        slr_summary_set_error(
            error_buf, error_buf_size,
            "bad native GLR preparation arguments");
        return false;
    }
    implementation = cetta_malloc(sizeof(*implementation));
    memset(implementation, 0, sizeof(*implementation));
    implementation->start_nt = start_nt;
    if (!glr_utf8_table_build(
            grammar, start_nt, &implementation->table,
            error_buf, error_buf_size)) {
        glr_utf8_table_free(&implementation->table);
        free(implementation);
        return false;
    }
    cetta_lp_native_glr_prepared_free(prepared);
    prepared->implementation = implementation;
    return true;
}

static bool glr_utf8_collect_roots(
    const CettaLpNativeGllNodeVec *nodes,
    uint32_t start_nt,
    uint32_t start_scalar,
    uint32_t input_scalar_len,
    CettaLpNativeU32Vec *roots) {
    uint32_t index;

    for (index = 0u; index < nodes->len; index++) {
        const CettaLpNativeGllNode *node = &nodes->data[index];
        if (gll_node_kind_value(node) != CETTA_LP_NATIVE_GLL_NODE_SYM ||
            gll_node_symbol_value(node) != start_nt ||
            node->left != start_scalar || node->right > input_scalar_len) {
            continue;
        }
        if (!u32vec_push_unique(roots, index))
            return false;
    }
    for (index = 1u; index < roots->len; index++) {
        uint32_t value = roots->data[index];
        uint32_t insert = index;
        while (insert > 0u &&
               nodes->data[roots->data[insert - 1u]].right >
                   nodes->data[value].right) {
            roots->data[insert] = roots->data[insert - 1u];
            insert--;
        }
        roots->data[insert] = value;
    }
    return true;
}

static bool glr_parse_input_action_begin(
    CettaLpNativeParseInput *input,
    const CettaLpNativeGlrUtf8Table *table,
    const CettaLpNativeAction *action,
    uint32_t position,
    bool *applicable,
    uint32_t *terminal_id,
    CettaLpNativeInputMatchIter *iter,
    CettaLpNativeInputMatch *match,
    char *error_buf,
    size_t error_buf_size) {
    if (action->token_idx > table->terminals.len) {
        slr_summary_set_error(error_buf, error_buf_size,
                              "UTF-8 GLR action has an invalid lookahead");
        return false;
    }
    if (action->token_idx == table->terminals.len) {
        if (!native_parse_input_at_end(
                input, position, applicable,
                error_buf, error_buf_size)) {
            return false;
        }
        *terminal_id = UINT32_MAX;
        memset(iter, 0, sizeof(*iter));
        match->right = position;
        match->value_kind = CETTA_LP_NATIVE_UTF8_TERMINAL_VALUE_EOF;
        match->value = 0u;
        return true;
    }
    *terminal_id = table->terminals.data[action->token_idx];
    if (!native_parse_input_match_begin(
            input, *terminal_id, position, iter,
            error_buf, error_buf_size)) {
        return false;
    }
    *applicable = native_parse_input_match_next(input, iter, match);
    return true;
}

enum { CETTA_LP_NATIVE_GLR_STACK_NONE = UINT32_MAX };

typedef struct {
    uint32_t parent;
    uint32_t depth;
    uint32_t state;
    bool has_value;
    CettaLpNativeParseValue value;
} CettaLpNativeGlrForestStackNode;

typedef struct {
    CettaLpNativeGlrForestStackNode *data;
    uint32_t len;
    uint32_t cap;
    uint32_t *slots;
    uint32_t slot_cap;
} CettaLpNativeGlrForestStackPool;

typedef struct {
    uint32_t pos;
    uint32_t top;
    bool done;
} CettaLpNativeGlrForestConfig;

typedef struct {
    CettaLpNativeGlrForestConfig *data;
    uint32_t len;
    uint32_t cap;
    uint32_t *slots;
    uint32_t slot_cap;
} CettaLpNativeGlrForestConfigSet;

static bool glr_forest_value_equals(
    const CettaLpNativeParseValue *left,
    const CettaLpNativeParseValue *right) {
    return left->is_cert == right->is_cert &&
        left->term_kind == right->term_kind &&
        left->start == right->start && left->end == right->end &&
        left->forest_idx == right->forest_idx;
}

static uint64_t glr_forest_stack_hash(
    uint32_t parent,
    uint32_t state,
    bool has_value,
    const CettaLpNativeParseValue *value) {
    uint64_t hash = UINT64_C(1469598103934665603);

    hash = gll_hash_u32(hash, parent);
    hash = gll_hash_u32(hash, state);
    hash = gll_hash_u32(hash, has_value ? 1u : 0u);
    if (has_value) {
        hash = gll_hash_u32(hash, value->is_cert ? 1u : 0u);
        hash = gll_hash_u32(hash, value->term_kind);
        hash = gll_hash_u32(hash, value->start);
        hash = gll_hash_u32(hash, value->end);
        hash = gll_hash_u32(hash, value->forest_idx);
    }
    return hash;
}

static bool glr_forest_stack_node_equals(
    const CettaLpNativeGlrForestStackNode *node,
    uint32_t parent,
    uint32_t state,
    bool has_value,
    const CettaLpNativeParseValue *value) {
    return node->parent == parent && node->state == state &&
        node->has_value == has_value &&
        (!has_value || glr_forest_value_equals(&node->value, value));
}

static bool glr_forest_stack_rehash(
    CettaLpNativeGlrForestStackPool *pool,
    uint32_t min_len) {
    uint32_t next_cap;
    uint32_t *next_slots;
    uint32_t index;

    if (!gll_index_capacity_for(min_len, &next_cap))
        return false;
    if (next_cap == pool->slot_cap)
        return true;
    next_slots = calloc(next_cap, sizeof(*next_slots));
    if (!next_slots)
        return false;
    for (index = 0u; index < pool->len; index++) {
        const CettaLpNativeGlrForestStackNode *node = &pool->data[index];
        uint64_t hash = glr_forest_stack_hash(
            node->parent, node->state, node->has_value, &node->value);
        uint32_t slot = gll_hash_slot(hash, next_cap);
        while (next_slots[slot] != 0u)
            slot = gll_index_next_slot(slot, next_cap);
        next_slots[slot] = index + 1u;
    }
    free(pool->slots);
    pool->slots = next_slots;
    pool->slot_cap = next_cap;
    return true;
}

static bool glr_forest_stack_intern(
    CettaLpNativeGlrForestStackPool *pool,
    uint32_t parent,
    uint32_t state,
    bool has_value,
    const CettaLpNativeParseValue *value,
    uint32_t *out_index) {
    uint64_t hash;
    uint32_t slot;
    CettaLpNativeGlrForestStackNode *node;

    if (!pool || !out_index || has_value != (value != NULL) ||
        (parent == CETTA_LP_NATIVE_GLR_STACK_NONE && has_value) ||
        (parent != CETTA_LP_NATIVE_GLR_STACK_NONE &&
         parent >= pool->len)) {
        return false;
    }
    if (!glr_forest_stack_rehash(pool, pool->len + 1u))
        return false;
    hash = glr_forest_stack_hash(
        parent, state, has_value, has_value ? value : NULL);
    slot = gll_hash_slot(hash, pool->slot_cap);
    while (pool->slots[slot] != 0u) {
        uint32_t index = pool->slots[slot] - 1u;
        if (glr_forest_stack_node_equals(
                &pool->data[index], parent, state,
                has_value, value)) {
            *out_index = index;
            return true;
        }
        slot = gll_index_next_slot(slot, pool->slot_cap);
    }
    if (!grow_storage((void **)&pool->data, &pool->len, &pool->cap,
                      sizeof(*pool->data))) {
        return false;
    }
    node = &pool->data[pool->len];
    memset(node, 0, sizeof(*node));
    node->parent = parent;
    node->depth = parent == CETTA_LP_NATIVE_GLR_STACK_NONE
        ? 1u : pool->data[parent].depth + 1u;
    if (node->depth == 0u)
        return false;
    node->state = state;
    node->has_value = has_value;
    if (has_value)
        node->value = *value;
    pool->slots[slot] = pool->len + 1u;
    *out_index = pool->len++;
    return true;
}

static void glr_forest_stack_pool_free(
    CettaLpNativeGlrForestStackPool *pool) {
    if (!pool)
        return;
    free(pool->data);
    free(pool->slots);
    memset(pool, 0, sizeof(*pool));
}

static uint64_t glr_forest_config_hash(uint32_t pos, uint32_t top) {
    uint64_t hash = UINT64_C(1469598103934665603);
    hash = gll_hash_u32(hash, pos);
    return gll_hash_u32(hash, top);
}

static bool glr_forest_config_rehash(
    CettaLpNativeGlrForestConfigSet *configs,
    uint32_t min_len) {
    uint32_t next_cap;
    uint32_t *next_slots;
    uint32_t index;

    if (!gll_index_capacity_for(min_len, &next_cap))
        return false;
    if (next_cap == configs->slot_cap)
        return true;
    next_slots = calloc(next_cap, sizeof(*next_slots));
    if (!next_slots)
        return false;
    for (index = 0u; index < configs->len; index++) {
        uint64_t hash = glr_forest_config_hash(
            configs->data[index].pos, configs->data[index].top);
        uint32_t slot = gll_hash_slot(hash, next_cap);
        while (next_slots[slot] != 0u)
            slot = gll_index_next_slot(slot, next_cap);
        next_slots[slot] = index + 1u;
    }
    free(configs->slots);
    configs->slots = next_slots;
    configs->slot_cap = next_cap;
    return true;
}

static bool glr_forest_config_insert(
    CettaLpNativeGlrForestConfigSet *configs,
    uint32_t pos,
    uint32_t top,
    uint32_t *out_index,
    bool *inserted) {
    uint64_t hash;
    uint32_t slot;

    if (!configs || !out_index || !inserted)
        return false;
    if (!glr_forest_config_rehash(configs, configs->len + 1u))
        return false;
    hash = glr_forest_config_hash(pos, top);
    slot = gll_hash_slot(hash, configs->slot_cap);
    while (configs->slots[slot] != 0u) {
        uint32_t index = configs->slots[slot] - 1u;
        if (configs->data[index].pos == pos &&
            configs->data[index].top == top) {
            *out_index = index;
            *inserted = false;
            return true;
        }
        slot = gll_index_next_slot(slot, configs->slot_cap);
    }
    if (!grow_storage((void **)&configs->data, &configs->len,
                      &configs->cap, sizeof(*configs->data))) {
        return false;
    }
    configs->data[configs->len] = (CettaLpNativeGlrForestConfig){
        .pos = pos,
        .top = top,
        .done = false,
    };
    configs->slots[slot] = configs->len + 1u;
    *out_index = configs->len++;
    *inserted = true;
    return true;
}

static void glr_forest_config_set_free(
    CettaLpNativeGlrForestConfigSet *configs) {
    if (!configs)
        return;
    free(configs->data);
    free(configs->slots);
    memset(configs, 0, sizeof(*configs));
}

static bool glr_forest_stack_materialize_states(
    const CettaLpNativeGlrForestStackPool *pool,
    uint32_t top,
    CettaLpNativeU32Vec *states) {
    uint32_t cursor;

    if (!pool || !states || top >= pool->len)
        return false;
    memset(states, 0, sizeof(*states));
    states->len = pool->data[top].depth;
    states->cap = states->len;
    states->data = malloc(sizeof(*states->data) * states->len);
    if (!states->data)
        return false;
    cursor = states->len;
    while (top != CETTA_LP_NATIVE_GLR_STACK_NONE) {
        if (top >= pool->len || cursor == 0u) {
            free(states->data);
            memset(states, 0, sizeof(*states));
            return false;
        }
        states->data[--cursor] = pool->data[top].state;
        top = pool->data[top].parent;
    }
    if (cursor != 0u) {
        free(states->data);
        memset(states, 0, sizeof(*states));
        return false;
    }
    return true;
}

static bool glr_utf8_reduce_forest_stack(
    uint32_t start_nt,
    const CettaLpNativeGlrUtf8Table *table,
    CettaLpNativeGllNodeVec *nodes,
    CettaLpNativeGlrForestStackPool *stacks,
    uint32_t position,
    int32_t production_index,
    uint32_t top,
    uint32_t *next_top,
    bool *applied,
    char *error_buf,
    size_t error_buf_size) {
    SymbolId lhs = 0u;
    const CettaLpNativeSymbol *rhs = NULL;
    uint32_t rhs_len = 0u;
    uint32_t base;
    uint32_t *children = NULL;
    int32_t lhs_symbol_index;
    int32_t goto_edge_index;
    CettaLpNativeParseValue next_value;
    uint32_t child_index;
    bool ok = false;

    *applied = false;
    if (production_index < 0 ||
        (uint32_t)production_index >= table->grammar_production_len) {
        slr_summary_set_error(
            error_buf, error_buf_size,
            "UTF-8 GLR reduction is not a grammar production");
        return false;
    }
    if (!stacks || top >= stacks->len)
        return false;
    slr_get_prod(
        table->productions, table->production_len, start_nt,
        production_index, &lhs, &rhs, &rhs_len);
    if (rhs_len >= stacks->data[top].depth)
        return true;
    base = top;
    if (rhs_len > 0u) {
        children = malloc(sizeof(*children) * rhs_len);
        if (!children) {
            slr_summary_set_error(
                error_buf, error_buf_size,
                "failed to inspect UTF-8 GLR shared stack");
            return false;
        }
        for (child_index = rhs_len; child_index > 0u; child_index--) {
            if (base >= stacks->len || !stacks->data[base].has_value) {
                slr_summary_set_error(
                    error_buf, error_buf_size,
                    "UTF-8 GLR shared stack is malformed");
                goto done;
            }
            children[child_index - 1u] = base;
            base = stacks->data[base].parent;
        }
    }
    if (base >= stacks->len) {
        slr_summary_set_error(
            error_buf, error_buf_size,
            "UTF-8 GLR reduction base is invalid");
        goto done;
    }
    lhs_symbol_index = symbolvec_find(&table->symbols, false, lhs);
    if (lhs_symbol_index < 0)
        goto not_applied;
    goto_edge_index = edgevec_find(
        &table->edges, stacks->data[base].state,
        (uint32_t)lhs_symbol_index);
    if (goto_edge_index < 0)
        goto not_applied;

    memset(&next_value, 0, sizeof(next_value));
    next_value.is_cert = true;
    if (rhs_len == 0u) {
        int32_t symbol_node = gll_node_get_sym(
            nodes, lhs, position, position);
        int32_t epsilon_node = gll_node_get_eps(nodes, position);
        if (symbol_node < 0 || epsilon_node < 0 ||
            !gll_node_push_packed_unique(
                nodes, (uint32_t)symbol_node,
                CETTA_LP_NATIVE_NODE_NONE,
                (uint32_t)epsilon_node, position,
                production_index)) {
            slr_summary_set_error(
                error_buf, error_buf_size,
                "failed to record UTF-8 GLR epsilon");
            goto done;
        }
        next_value.start = position;
        next_value.end = position;
        next_value.forest_idx = (uint32_t)symbol_node;
    } else {
        uint32_t parent = CETTA_LP_NATIVE_NODE_NONE;

        next_value.start = stacks->data[children[0u]].value.start;
        next_value.end = stacks->data[
            children[rhs_len - 1u]].value.end;
        for (child_index = 0u; child_index < rhs_len; child_index++) {
            CettaLpNativeParseValue *child =
                &stacks->data[children[child_index]].value;
            int32_t next_parent;
            if (child->forest_idx == UINT32_MAX ||
                (rhs[child_index].kind == CETTA_LP_NATIVE_SYMBOL_TM &&
                 (child->is_cert ||
                  child->term_kind != rhs[child_index].name)) ||
                (rhs[child_index].kind != CETTA_LP_NATIVE_SYMBOL_TM &&
                 !child->is_cert)) {
                slr_summary_set_error(
                    error_buf, error_buf_size,
                    "UTF-8 GLR reduction child mismatch");
                goto done;
            }
            next_parent = gll_sppf_get_node(
                nodes, table->productions, table->production_len,
                start_nt, production_index, child_index + 1u,
                parent, child->forest_idx,
                error_buf, error_buf_size);
            if (next_parent < 0)
                goto done;
            parent = (uint32_t)next_parent;
        }
        next_value.forest_idx = parent;
    }
    if (!glr_forest_stack_intern(
            stacks, base, table->edges.data[goto_edge_index].target,
            true, &next_value, next_top)) {
        slr_summary_set_error(
            error_buf, error_buf_size,
            "failed to advance UTF-8 GLR shared stack");
        goto done;
    }
    *applied = true;
    ok = true;
    goto done;

not_applied:
    ok = true;
done:
    free(children);
    return ok;
}

static bool glr_utf8_complete_prefix_forests(
    uint32_t start_nt,
    const CettaLpNativeGlrUtf8Table *table,
    const CettaLpNativeGlrForestConfigSet *configs,
    CettaLpNativeGlrForestStackPool *stacks,
    CettaLpNativeGllNodeVec *nodes,
    uint32_t work_limit,
    uint32_t *work_items,
    bool *limit_hit,
    char *error_buf,
    size_t error_buf_size) {
    CettaLpNativeGlrForestConfigSet closure = {0};
    CettaLpNativeU32Vec pending = {0};
    uint32_t config_index;
    bool ok = false;

    for (config_index = 0u; config_index < configs->len; config_index++) {
        uint32_t closure_index;
        bool inserted;
        if (!glr_forest_config_insert(
                &closure, configs->data[config_index].pos,
                configs->data[config_index].top,
                &closure_index, &inserted)) {
            slr_summary_set_error(
                error_buf, error_buf_size,
                "failed to initialize UTF-8 GLR prefix closure");
            goto done;
        }
        if (inserted && !u32vec_push(&pending, closure_index)) {
            slr_summary_set_error(
                error_buf, error_buf_size,
                "failed to queue UTF-8 GLR prefix closure");
            goto done;
        }
    }

    while (pending.len > 0u) {
        uint32_t closure_index = pending.data[--pending.len];
        uint32_t top;
        uint32_t position;
        uint32_t state;
        uint32_t action_index;

        if (closure_index >= closure.len) {
            slr_summary_set_error(
                error_buf, error_buf_size,
                "UTF-8 GLR prefix closure branch is invalid");
            goto done;
        }
        top = closure.data[closure_index].top;
        position = closure.data[closure_index].pos;
        if (top >= stacks->len) {
            slr_summary_set_error(
                error_buf, error_buf_size,
                "UTF-8 GLR prefix closure stack is invalid");
            goto done;
        }
        state = stacks->data[top].state;
        for (action_index = 0u;
             action_index < table->actions.len; action_index++) {
            const CettaLpNativeAction *action =
                &table->actions.data[action_index];
            uint32_t next_top;
            bool applied;

            if (action->state != state ||
                action->token_idx != table->terminals.len)
                continue;
            if (action->kind != 'r' && action->kind != 'a') {
                slr_summary_set_error(
                    error_buf, error_buf_size,
                    "UTF-8 GLR prefix closure has an invalid EOF action");
                goto done;
            }
            if (*work_items >= work_limit) {
                *limit_hit = true;
                ok = true;
                goto done;
            }
            (*work_items)++;
            if (action->kind == 'a')
                continue;
            if (!glr_utf8_reduce_forest_stack(
                    start_nt, table, nodes, stacks, position,
                    action->value, top, &next_top, &applied,
                    error_buf, error_buf_size)) {
                goto done;
            }
            if (applied) {
                uint32_t next_index;
                bool inserted;
                if (!glr_forest_config_insert(
                        &closure, position, next_top,
                        &next_index, &inserted) ||
                    (inserted && !u32vec_push(&pending, next_index))) {
                    slr_summary_set_error(
                        error_buf, error_buf_size,
                        "failed to enqueue UTF-8 GLR prefix reduction");
                    goto done;
                }
            }
        }
    }
    ok = true;

done:
    free(pending.data);
    glr_forest_config_set_free(&closure);
    return ok;
}

static bool glr_utf8_diagnostic_expected(
    const CettaLpNativeGlrUtf8Table *table,
    uint32_t start_nt,
    const CettaLpNativeGlrForestConfigSet *configs,
    const CettaLpNativeGlrForestStackPool *stacks,
    uint32_t position,
    uint32_t token_index,
    uint32_t work_limit,
    uint32_t *work_items,
    bool *expected,
    bool *limit_hit,
    char *error_buf,
    size_t error_buf_size) {
    CettaLpNativeBranchVec closure = {0};
    CettaLpNativeU32Vec pending = {0};
    CettaLpNativeParseValueVec empty_values = {0};
    uint32_t config_index;
    bool ok = false;

    *expected = false;
    for (config_index = 0u; config_index < configs->len; config_index++) {
        const CettaLpNativeGlrForestConfig *config =
            &configs->data[config_index];
        CettaLpNativeU32Vec state_stack = {0};
        if (config->pos != position)
            continue;
        if (!glr_forest_stack_materialize_states(
                stacks, config->top, &state_stack)) {
            slr_summary_set_error(
                error_buf, error_buf_size,
                "failed to inspect UTF-8 GLR diagnostic stack");
            goto done;
        }
        if (branchvec_find(&closure, position, &state_stack) < 0 &&
            (!branchvec_push_copy(
                 &closure, position, &state_stack,
                 &empty_values, 1u, true) ||
             !u32vec_push(&pending, closure.len - 1u))) {
            free(state_stack.data);
            slr_summary_set_error(
                error_buf, error_buf_size,
                "failed to initialize UTF-8 GLR diagnostic closure");
            goto done;
        }
        free(state_stack.data);
    }

    while (pending.len > 0u) {
        uint32_t closure_index = pending.data[--pending.len];
        uint32_t state;
        uint32_t action_index;

        if (closure_index >= closure.len ||
            closure.data[closure_index].state_stack.len == 0u) {
            slr_summary_set_error(
                error_buf, error_buf_size,
                "UTF-8 GLR diagnostic stack is invalid");
            goto done;
        }
        state = closure.data[closure_index].state_stack.data[
            closure.data[closure_index].state_stack.len - 1u];
        for (action_index = 0u;
             action_index < table->actions.len; action_index++) {
            const CettaLpNativeAction *action =
                &table->actions.data[action_index];
            const CettaLpNativeBranch *current;
            CettaLpNativeU32Vec next_stack = {0};
            SymbolId lhs = 0u;
            const CettaLpNativeSymbol *rhs = NULL;
            uint32_t rhs_len = 0u;
            int32_t lhs_symbol_index;
            int32_t goto_edge_index;

            if (action->state != state ||
                action->token_idx != token_index) {
                continue;
            }
            if (action->kind == 's') {
                *expected = true;
                ok = true;
                goto done;
            }
            if (action->kind != 'r')
                continue;
            if (*work_items >= work_limit) {
                *limit_hit = true;
                ok = true;
                goto done;
            }
            (*work_items)++;
            if (action->value < 0 ||
                (uint32_t)action->value >= table->production_len) {
                slr_summary_set_error(
                    error_buf, error_buf_size,
                    "UTF-8 GLR diagnostic reduction is invalid");
                goto done;
            }
            slr_get_prod(
                table->productions, table->production_len, start_nt,
                action->value, &lhs, &rhs, &rhs_len);
            current = &closure.data[closure_index];
            if ((rhs_len > 0u && !rhs) ||
                rhs_len >= current->state_stack.len)
                continue;
            if (!u32vec_copy(&next_stack, &current->state_stack)) {
                slr_summary_set_error(
                    error_buf, error_buf_size,
                    "failed to copy UTF-8 GLR diagnostic stack");
                goto done;
            }
            next_stack.len -= rhs_len;
            lhs_symbol_index = symbolvec_find(&table->symbols, false, lhs);
            if (lhs_symbol_index < 0 || next_stack.len == 0u) {
                free(next_stack.data);
                continue;
            }
            goto_edge_index = edgevec_find(
                &table->edges,
                next_stack.data[next_stack.len - 1u],
                (uint32_t)lhs_symbol_index);
            if (goto_edge_index < 0 ||
                !u32vec_push(
                    &next_stack,
                    table->edges.data[goto_edge_index].target)) {
                free(next_stack.data);
                if (goto_edge_index < 0)
                    continue;
                slr_summary_set_error(
                    error_buf, error_buf_size,
                    "failed to advance UTF-8 GLR diagnostic reduction");
                goto done;
            }
            if (branchvec_find(&closure, position, &next_stack) < 0) {
                if (!branchvec_push_copy(
                        &closure, position, &next_stack,
                        &empty_values, 1u, true) ||
                    !u32vec_push(&pending, closure.len - 1u)) {
                    free(next_stack.data);
                    slr_summary_set_error(
                        error_buf, error_buf_size,
                        "failed to enqueue UTF-8 GLR diagnostic reduction");
                    goto done;
                }
            }
            free(next_stack.data);
        }
    }
    ok = true;

done:
    free(pending.data);
    branchvec_free(&closure);
    return ok;
}

static bool glr_utf8_supplement_expectations(
    const CettaLpNativeGlrUtf8Table *table,
    uint32_t start_nt,
    const CettaLpNativeGlrForestConfigSet *configs,
    const CettaLpNativeGlrForestStackPool *stacks,
    uint32_t position,
    uint32_t work_limit,
    uint32_t *work_items,
    CettaLpNativeU32Vec *expectations,
    bool *limit_hit,
    char *error_buf,
    size_t error_buf_size) {
    uint32_t token_index;

    for (token_index = 0u;
         token_index < table->terminals.len; token_index++) {
        uint32_t terminal_id = table->terminals.data[token_index];
        bool expected = false;

        if (u32vec_contains(expectations, terminal_id))
            continue;
        if (!glr_utf8_diagnostic_expected(
                table, start_nt, configs, stacks, position, token_index,
                work_limit, work_items, &expected, limit_hit,
                error_buf, error_buf_size)) {
            return false;
        }
        if (*limit_hit)
            return true;
        if (expected && !u32vec_push_unique(expectations, terminal_id)) {
            slr_summary_set_error(
                error_buf, error_buf_size,
                "failed to record UTF-8 GLR diagnostic expectation");
            return false;
        }
    }
    return true;
}

static bool native_glr_parse_input_forest_prepared(
    const CettaLpNativeGlrUtf8Table *prepared_table,
    uint32_t start_nt,
    CettaLpNativeParseInput *input,
    uint32_t start_scalar,
    uint32_t work_limit,
    CettaLpNativeUtf8Forest *out,
    char *error_buf,
    size_t error_buf_size) {
    CettaLpNativeGlrUtf8Table table;
    CettaLpNativeGlrForestStackPool stacks = {0};
    CettaLpNativeGlrForestConfigSet configs = {0};
    CettaLpNativeU32Vec work = {0};
    CettaLpNativeGllNodeVec nodes = {0};
    CettaLpNativeU32Vec roots = {0};
    CettaLpNativeU32Vec expectations = {0};
    CettaLpNativeUtf8Forest result;
    uint32_t farthest = start_scalar;
    uint32_t work_items = 0u;
    uint32_t index;
    bool accepted = false;
    bool limit_hit = false;
    bool ok = false;

    memset(&table, 0, sizeof(table));
    cetta_lp_native_utf8_forest_init(&result);
    if (error_buf && error_buf_size > 0u)
        error_buf[0] = '\0';
    if (!prepared_table || !input || !out || work_limit == 0u ||
        start_scalar > native_parse_input_scalar_len(input)) {
        slr_summary_set_error(error_buf, error_buf_size,
                              "bad native GLR parse-input arguments");
        goto done;
    }
    table = *prepared_table;
    for (index = 0u; index < table.terminals.len; index++) {
        if (!native_parse_input_terminal_declared(
                input, table.terminals.data[index])) {
            slr_summary_set_error(
                error_buf, error_buf_size,
                "grammar terminal %u is absent from parse input",
                table.terminals.data[index]);
            goto done;
        }
    }
    {
        uint32_t initial_top;
        uint32_t initial_config;
        bool inserted;
        if (!glr_forest_stack_intern(
                &stacks, CETTA_LP_NATIVE_GLR_STACK_NONE,
                0u, false, NULL, &initial_top) ||
            !glr_forest_config_insert(
                &configs, start_scalar, initial_top,
                &initial_config, &inserted) ||
            !inserted || !u32vec_push(&work, initial_config)) {
            slr_summary_set_error(error_buf, error_buf_size,
                                  "failed to initialize UTF-8 GLR queue");
            goto done;
        }
    }

    while (work.len > 0u) {
        uint32_t config_index = work.data[--work.len];
        CettaLpNativeGlrForestConfig *current;
        uint32_t top;
        uint32_t position;
        uint32_t state;
        uint32_t action_index;

        if (config_index >= configs.len) {
            slr_summary_set_error(error_buf, error_buf_size,
                                  "UTF-8 GLR queue index is invalid");
            goto done;
        }
        current = &configs.data[config_index];
        if (current->done)
            continue;
        current->done = true;
        position = current->pos;
        top = current->top;
        if (top >= stacks.len) {
            slr_summary_set_error(error_buf, error_buf_size,
                                  "UTF-8 GLR stack index is invalid");
            goto done;
        }
        state = stacks.data[top].state;
        if (position > farthest) {
            farthest = position;
            expectations.len = 0u;
        }
        if (position == farthest) {
            for (action_index = 0u;
                 action_index < table.actions.len; action_index++) {
                const CettaLpNativeAction *action =
                    &table.actions.data[action_index];
                if (action->state == state && action->kind == 's' &&
                    action->token_idx < table.terminals.len &&
                    !u32vec_push_unique(
                        &expectations,
                        table.terminals.data[action->token_idx])) {
                    goto done;
                }
            }
        }

        for (action_index = 0u;
             action_index < table.actions.len; action_index++) {
            const CettaLpNativeAction *action =
                &table.actions.data[action_index];
            CettaLpNativeInputMatchIter match_iter;
            CettaLpNativeInputMatch match;
            uint32_t terminal_id;
            bool applicable;

            current = &configs.data[config_index];
            if (action->state != state)
                continue;
            if (!glr_parse_input_action_begin(
                    input, &table, action, position, &applicable,
                    &terminal_id, &match_iter, &match,
                    error_buf, error_buf_size)) {
                goto done;
            }
            if (!applicable)
                continue;
            do {
                uint32_t next_top = top;
                uint32_t next_position = position;

                if (work_items >= work_limit) {
                    limit_hit = true;
                    goto resource;
                }
                work_items++;
                if (action->kind == 'a') {
                    accepted = true;
                    break;
                }
                if (action->kind == 's') {
                    CettaLpNativeParseValue value;
                    int32_t terminal_node;

                    if (terminal_id == UINT32_MAX) {
                        slr_summary_set_error(
                            error_buf, error_buf_size,
                            "failed to advance UTF-8 GLR shift branch");
                        goto done;
                    }
                    terminal_node = gll_node_get_term_value(
                        &nodes, terminal_id, position, match.right,
                        match.value_kind, match.value);
                    if (terminal_node < 0) {
                        slr_summary_set_error(
                            error_buf, error_buf_size,
                            "failed to allocate UTF-8 GLR terminal");
                        goto done;
                    }
                    memset(&value, 0, sizeof(value));
                    value.is_cert = false;
                    value.term_kind = terminal_id;
                    value.start = position;
                    value.end = match.right;
                    value.forest_idx = (uint32_t)terminal_node;
                    if (!glr_forest_stack_intern(
                            &stacks, top, (uint32_t)action->value,
                            true, &value, &next_top)) {
                        slr_summary_set_error(
                            error_buf, error_buf_size,
                            "failed to push UTF-8 GLR shared terminal");
                        goto done;
                    }
                    next_position = match.right;
                } else if (action->kind == 'r') {
                    bool applied;
                    if (!glr_utf8_reduce_forest_stack(
                            start_nt, &table, &nodes, &stacks,
                            position, action->value, top, &next_top,
                            &applied, error_buf, error_buf_size)) {
                        goto done;
                    }
                    if (!applied)
                        break;
                } else {
                    slr_summary_set_error(error_buf, error_buf_size,
                                          "unknown UTF-8 GLR action kind");
                    goto done;
                }

                {
                    uint32_t next_index;
                    bool inserted;
                    if (!glr_forest_config_insert(
                            &configs, next_position, next_top,
                            &next_index, &inserted) ||
                        (inserted && !u32vec_push(&work, next_index))) {
                        slr_summary_set_error(
                            error_buf, error_buf_size,
                            "failed to enqueue UTF-8 GLR branch");
                        goto done;
                    }
                }
            } while (action->kind == 's' &&
                     native_parse_input_match_next(
                         input, &match_iter, &match));
        }
    }

    if (!native_parse_input_finish(input, error_buf, error_buf_size) ||
        !glr_utf8_complete_prefix_forests(
            start_nt, &table, &configs, &stacks, &nodes,
            work_limit, &work_items, &limit_hit,
            error_buf, error_buf_size)) {
        goto done;
    }
    if (limit_hit)
        goto resource;
    if (!glr_utf8_collect_roots(
            &nodes, start_nt, start_scalar,
            native_parse_input_scalar_len(input), &roots)) {
        goto done;
    }
    if (!accepted &&
        !glr_utf8_supplement_expectations(
            &table, start_nt, &configs, &stacks, farthest, work_limit,
            &work_items, &expectations, &limit_hit,
            error_buf, error_buf_size)) {
        goto done;
    }
    if (limit_hit)
        goto resource;
    if (!native_forest_copy_input(
            &result, input, error_buf, error_buf_size) ||
        !native_forest_export(
            &result, input,
            &nodes, &roots, error_buf, error_buf_size)) {
        goto done;
    }
    goto finish_result;

resource:
    if (!limit_hit ||
        !native_parse_input_finish(input, error_buf, error_buf_size) ||
        !native_forest_copy_input(
            &result, input, error_buf, error_buf_size)) {
        goto done;
    }
    result.outcome = CETTA_LP_NATIVE_UTF8_FOREST_RESOURCE_LIMIT;

finish_result:
    if (expectations.len > 1u) {
        qsort(expectations.data, expectations.len,
              sizeof(*expectations.data), utf8_u32_compare);
    }
    if (expectations.len > 0u) {
        result.expected_terminal_ids = cetta_malloc(
            sizeof(*result.expected_terminal_ids) * expectations.len);
        memcpy(result.expected_terminal_ids, expectations.data,
               sizeof(*result.expected_terminal_ids) * expectations.len);
    }
    result.expected_terminal_len = expectations.len;
    result.farthest_scalar = farthest;
    if (farthest > native_parse_input_scalar_len(input)) {
        slr_summary_set_error(error_buf, error_buf_size,
                              "UTF-8 GLR farthest position is invalid");
        goto done;
    }
    result.farthest_byte =
        native_parse_input_byte_offsets(input)[farthest];
    result.graph_node_len = configs.len;
    result.stack_node_len = stacks.len;
    result.work_item_len = work_items;
    cetta_lp_native_utf8_forest_free(out);
    *out = result;
    memset(&result, 0, sizeof(result));
    ok = true;

done:
    cetta_lp_native_utf8_forest_free(&result);
    free(work.data);
    glr_forest_config_set_free(&configs);
    glr_forest_stack_pool_free(&stacks);
    gll_nodevec_free(&nodes);
    free(roots.data);
    free(expectations.data);
    return ok;
}

static bool native_glr_parse_input_forest(
    const CettaLpNativeGrammar *grammar,
    uint32_t start_nt,
    CettaLpNativeParseInput *input,
    uint32_t start_scalar,
    uint32_t work_limit,
    CettaLpNativeUtf8Forest *out,
    char *error_buf,
    size_t error_buf_size) {
    CettaLpNativeGlrUtf8Table table;
    bool ok = false;

    memset(&table, 0, sizeof(table));
    if (error_buf && error_buf_size > 0u)
        error_buf[0] = '\0';
    if (!grammar || !input || !out || work_limit == 0u ||
        start_scalar > native_parse_input_scalar_len(input)) {
        slr_summary_set_error(
            error_buf, error_buf_size,
            "bad native GLR parse-input arguments");
        goto done;
    }
    if (!glr_utf8_table_build(
            grammar, start_nt, &table, error_buf, error_buf_size)) {
        goto done;
    }
    ok = native_glr_parse_input_forest_prepared(
        &table, start_nt, input, start_scalar, work_limit,
        out, error_buf, error_buf_size);

done:
    glr_utf8_table_free(&table);
    return ok;
}

bool cetta_lp_native_glr_parse_utf8_forest(
    const CettaLpNativeGrammar *grammar,
    uint32_t start_nt,
    const CettaLpNativeUtf8Terminal *terminals,
    uint32_t terminal_len,
    const uint8_t *input_bytes,
    size_t input_byte_len,
    uint32_t work_limit,
    CettaLpNativeUtf8Forest *out,
    char *error_buf,
    size_t error_buf_size) {
    CettaLpNativeUtf8Input decoded;
    CettaLpNativeParseInput input;
    bool ok;

    if (error_buf && error_buf_size > 0u)
        error_buf[0] = '\0';
    if (!grammar || !out || work_limit == 0u ||
        input_byte_len > UINT32_MAX ||
        (input_byte_len > 0u && !input_bytes)) {
        slr_summary_set_error(error_buf, error_buf_size,
                              "bad native GLR UTF-8 arguments");
        return false;
    }
    if (!utf8_terminals_validate(
            terminals, terminal_len, error_buf, error_buf_size)) {
        return false;
    }
    utf8_input_init(&decoded, input_bytes, input_byte_len);
    input = (CettaLpNativeParseInput){
        .kind = CETTA_LP_NATIVE_PARSE_INPUT_UTF8,
        .utf8 = &decoded,
        .terminals = terminals,
        .terminal_len = terminal_len,
        .lattice = NULL,
    };
    ok = native_glr_parse_input_forest(
        grammar, start_nt, &input, 0u, work_limit,
        out, error_buf, error_buf_size);
    utf8_input_free(&decoded);
    return ok;
}

bool cetta_lp_native_glr_prepared_parse_utf8_forest(
    const CettaLpNativeGlrPrepared *prepared,
    const CettaLpNativeUtf8Terminal *terminals,
    uint32_t terminal_len,
    const uint8_t *input_bytes,
    size_t input_byte_len,
    uint32_t work_limit,
    CettaLpNativeUtf8Forest *out,
    char *error_buf,
    size_t error_buf_size) {
    const CettaLpNativeGlrPreparedImpl *implementation =
        prepared ? prepared->implementation : NULL;
    CettaLpNativeUtf8Input decoded;
    CettaLpNativeParseInput input;
    bool ok;

    if (error_buf && error_buf_size > 0u)
        error_buf[0] = '\0';
    if (!implementation || !out || work_limit == 0u ||
        input_byte_len > UINT32_MAX ||
        (input_byte_len > 0u && !input_bytes)) {
        slr_summary_set_error(
            error_buf, error_buf_size,
            "bad prepared native GLR UTF-8 arguments");
        return false;
    }
    if (!utf8_terminals_validate(
            terminals, terminal_len, error_buf, error_buf_size)) {
        return false;
    }
    utf8_input_init(&decoded, input_bytes, input_byte_len);
    input = (CettaLpNativeParseInput){
        .kind = CETTA_LP_NATIVE_PARSE_INPUT_UTF8,
        .utf8 = &decoded,
        .terminals = terminals,
        .terminal_len = terminal_len,
        .lattice = NULL,
    };
    ok = native_glr_parse_input_forest_prepared(
        &implementation->table, implementation->start_nt,
        &input, 0u, work_limit, out, error_buf, error_buf_size);
    utf8_input_free(&decoded);
    return ok;
}

bool cetta_lp_native_glr_parse_utf8_scalar_view_forest(
    const CettaLpNativeGrammar *grammar,
    uint32_t start_nt,
    const CettaLpNativeUtf8Terminal *terminals,
    uint32_t terminal_len,
    const CettaLpNativeUtf8ScalarView *view,
    uint32_t work_limit,
    CettaLpNativeUtf8Forest *out,
    char *error_buf,
    size_t error_buf_size) {
    CettaLpNativeUtf8Input decoded;
    CettaLpNativeParseInput input;
    bool ok;

    if (error_buf && error_buf_size > 0u)
        error_buf[0] = '\0';
    if (!grammar || !out || work_limit == 0u ||
        !cetta_lp_native_utf8_scalar_view_validate(
            view, error_buf, error_buf_size)) {
        if (!error_buf || error_buf[0] == '\0') {
            slr_summary_set_error(
                error_buf, error_buf_size,
                "bad native GLR UTF-8 scalar view arguments");
        }
        return false;
    }
    if (!utf8_terminals_validate(
            terminals, terminal_len, error_buf, error_buf_size)) {
        return false;
    }
    utf8_input_init_scalar_view(&decoded, view);
    input = (CettaLpNativeParseInput){
        .kind = CETTA_LP_NATIVE_PARSE_INPUT_UTF8,
        .utf8 = &decoded,
        .terminals = terminals,
        .terminal_len = terminal_len,
        .lattice = NULL,
    };
    ok = native_glr_parse_input_forest(
        grammar, start_nt, &input, 0u, work_limit,
        out, error_buf, error_buf_size);
    utf8_input_free(&decoded);
    return ok;
}

bool cetta_lp_native_glr_prepared_parse_utf8_scalar_view_forest(
    const CettaLpNativeGlrPrepared *prepared,
    const CettaLpNativeUtf8Terminal *terminals,
    uint32_t terminal_len,
    const CettaLpNativeUtf8ScalarView *view,
    uint32_t work_limit,
    CettaLpNativeUtf8Forest *out,
    char *error_buf,
    size_t error_buf_size) {
    const CettaLpNativeGlrPreparedImpl *implementation =
        prepared ? prepared->implementation : NULL;
    CettaLpNativeUtf8Input decoded;
    CettaLpNativeParseInput input;
    bool ok;

    if (error_buf && error_buf_size > 0u)
        error_buf[0] = '\0';
    if (!implementation || !out || work_limit == 0u ||
        !cetta_lp_native_utf8_scalar_view_validate(
            view, error_buf, error_buf_size)) {
        if (!error_buf || error_buf[0] == '\0') {
            slr_summary_set_error(
                error_buf, error_buf_size,
                "bad prepared native GLR UTF-8 scalar view arguments");
        }
        return false;
    }
    if (!utf8_terminals_validate(
            terminals, terminal_len, error_buf, error_buf_size)) {
        return false;
    }
    utf8_input_init_scalar_view(&decoded, view);
    input = (CettaLpNativeParseInput){
        .kind = CETTA_LP_NATIVE_PARSE_INPUT_UTF8,
        .utf8 = &decoded,
        .terminals = terminals,
        .terminal_len = terminal_len,
        .lattice = NULL,
    };
    ok = native_glr_parse_input_forest_prepared(
        &implementation->table, implementation->start_nt,
        &input, 0u, work_limit, out, error_buf, error_buf_size);
    utf8_input_free(&decoded);
    return ok;
}

bool cetta_lp_native_glr_parse_utf8_lattice_forest(
    const CettaLpNativeGrammar *grammar,
    uint32_t start_nt,
    const CettaLpNativeUtf8Lattice *lattice,
    uint32_t work_limit,
    CettaLpNativeUtf8Forest *out,
    char *error_buf,
    size_t error_buf_size) {
    return cetta_lp_native_glr_parse_utf8_lattice_forest_from(
        grammar, start_nt, lattice, 0u, work_limit,
        out, error_buf, error_buf_size);
}

bool cetta_lp_native_glr_parse_utf8_lattice_forest_from(
    const CettaLpNativeGrammar *grammar,
    uint32_t start_nt,
    const CettaLpNativeUtf8Lattice *lattice,
    uint32_t start_scalar,
    uint32_t work_limit,
    CettaLpNativeUtf8Forest *out,
    char *error_buf,
    size_t error_buf_size) {
    CettaLpNativeParseInput input;

    if (error_buf && error_buf_size > 0u)
        error_buf[0] = '\0';
    if (!grammar || !out || work_limit == 0u || !lattice ||
        start_scalar > lattice->scalar_len ||
        !cetta_lp_native_utf8_lattice_validate(
            lattice, error_buf, error_buf_size)) {
        if (!error_buf || error_buf[0] == '\0') {
            slr_summary_set_error(error_buf, error_buf_size,
                                  "bad native GLR UTF-8 lattice arguments");
        }
        return false;
    }
    input = (CettaLpNativeParseInput){
        .kind = CETTA_LP_NATIVE_PARSE_INPUT_LATTICE,
        .utf8 = NULL,
        .terminals = NULL,
        .terminal_len = 0u,
        .lattice = lattice,
    };
    return native_glr_parse_input_forest(
        grammar, start_nt, &input, start_scalar, work_limit,
        out, error_buf, error_buf_size);
}

bool cetta_lp_native_glr_prepared_parse_utf8_lattice_forest_from(
    const CettaLpNativeGlrPrepared *prepared,
    const CettaLpNativeUtf8Lattice *lattice,
    uint32_t start_scalar,
    uint32_t work_limit,
    CettaLpNativeUtf8Forest *out,
    char *error_buf,
    size_t error_buf_size) {
    const CettaLpNativeGlrPreparedImpl *implementation =
        prepared ? prepared->implementation : NULL;
    CettaLpNativeParseInput input;

    if (error_buf && error_buf_size > 0u)
        error_buf[0] = '\0';
    if (!implementation || !out || work_limit == 0u || !lattice ||
        start_scalar > lattice->scalar_len ||
        !cetta_lp_native_utf8_lattice_validate(
            lattice, error_buf, error_buf_size)) {
        if (!error_buf || error_buf[0] == '\0') {
            slr_summary_set_error(
                error_buf, error_buf_size,
                "bad prepared native GLR UTF-8 lattice arguments");
        }
        return false;
    }
    input = (CettaLpNativeParseInput){
        .kind = CETTA_LP_NATIVE_PARSE_INPUT_LATTICE,
        .utf8 = NULL,
        .terminals = NULL,
        .terminal_len = 0u,
        .lattice = lattice,
    };
    return native_glr_parse_input_forest_prepared(
        &implementation->table, implementation->start_nt,
        &input, start_scalar, work_limit,
        out, error_buf, error_buf_size);
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
        uint32_t cur_pos;
        CettaLpNativeU32Vec cur_state_stack;
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
        cur_pos = cur->pos;
        cur_state_stack = cur->state_stack;
        state = cur_state_stack.data[cur_state_stack.len - 1];
        if (cur_pos < tokens.len) {
            int32_t term_idx = idvec_find(&terminals, tokens.data[cur_pos].term_kind);
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
            uint32_t next_pos = cur_pos;

            if (action->state != state || action->token_idx != token_idx)
                continue;
            if (action->kind == 'a') {
                if (cur_pos == tokens.len) {
                    accept_count += delta;
                    if (accept_count > 2)
                        accept_count = 2;
                }
                continue;
            }
            if (action->kind == 's') {
                if (cur_pos >= tokens.len ||
                    !u32vec_copy(&next_stack, &cur_state_stack) ||
                    !u32vec_push(&next_stack, (uint32_t)action->value)) {
                    free(next_stack.data);
                    slr_summary_set_error(error_buf, error_buf_size,
                                          "failed to fork GLR shift branch");
                    goto fail;
                }
                next_pos = cur_pos + 1;
            } else if (action->kind == 'r') {
                int32_t prod_idx = action->value;
                SymbolId lhs = 0;
                const CettaLpNativeSymbol *rhs = NULL;
                uint32_t rhs_len = 0;
                int32_t lhs_sym_idx;
                int32_t goto_edge_idx;

                slr_get_prod(productions, production_len, start_nt,
                             prod_idx, &lhs, &rhs, &rhs_len);
                if (!u32vec_copy(&next_stack, &cur_state_stack)) {
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
        uint32_t cur_pos;
        CettaLpNativeU32Vec cur_state_stack;
        CettaLpNativeParseValueVec cur_value_stack;
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
        cur_pos = cur->pos;
        cur_state_stack = cur->state_stack;
        cur_value_stack = cur->value_stack;
        state = cur_state_stack.data[cur_state_stack.len - 1];
        if (cur_pos < tokens.len) {
            int32_t term_idx = idvec_find(&terminals, tokens.data[cur_pos].term_kind);
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
            uint32_t next_pos = cur_pos;

            if (action->state != state || action->token_idx != token_idx)
                continue;
            if (action->kind == 'a') {
                if (cur_pos == tokens.len &&
                    cur_value_stack.len == 1 &&
                    cur_value_stack.data[0].is_cert &&
                    cur_value_stack.data[0].cert &&
                    cur_value_stack.data[0].start == 0 &&
                    cur_value_stack.data[0].end == tokens.len) {
                    if (accept_count == 0)
                        accept_cert = cur_value_stack.data[0].cert;
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

                if (cur_pos >= tokens.len ||
                    !u32vec_copy(&next_stack, &cur_state_stack) ||
                    !parsevaluevec_copy(&next_values, &cur_value_stack) ||
                    !u32vec_push(&next_stack, (uint32_t)action->value)) {
                    free(next_stack.data);
                    free(next_values.data);
                    slr_summary_set_error(error_buf, error_buf_size,
                                          "failed to fork GLR shared shift branch");
                    goto fail;
                }
                value.is_cert = false;
                value.token_atom = tokens.data[cur_pos].token_atom;
                value.term_kind = tokens.data[cur_pos].term_kind;
                value.start = cur_pos;
                value.end = cur_pos + 1;
                value.forest_idx = UINT32_MAX;
                value.cert = NULL;
                if (!parsevaluevec_push(&next_values, &value)) {
                    free(next_stack.data);
                    free(next_values.data);
                    slr_summary_set_error(error_buf, error_buf_size,
                                          "failed to push GLR shared shift value");
                    goto fail;
                }
                next_pos = cur_pos + 1;
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
                if (!u32vec_copy(&next_stack, &cur_state_stack) ||
                    !parsevaluevec_copy(&next_values, &cur_value_stack)) {
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
                    next_value.start = cur_pos;
                    next_value.end = cur_pos;
                    next_value.forest_idx = UINT32_MAX;
                    next_value.cert = make_node_cert(
                        arena, grammar->productions[prod_idx].label, lhs,
                        cur_pos, cur_pos, kids, 1);
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
        uint32_t cur_pos;
        CettaLpNativeU32Vec cur_state_stack;
        CettaLpNativeParseValueVec cur_value_stack;
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
        cur_pos = cur->pos;
        cur_state_stack = cur->state_stack;
        cur_value_stack = cur->value_stack;
        state = cur_state_stack.data[cur_state_stack.len - 1];
        if (cur_pos < tokens.len) {
            int32_t term_idx = idvec_find(&terminals, tokens.data[cur_pos].term_kind);
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
            uint32_t next_pos = cur_pos;

            if (action->state != state || action->token_idx != token_idx)
                continue;
            if (action->kind == 'a') {
                if (cur_pos == tokens.len &&
                    cur_value_stack.len == 1 &&
                    cur_value_stack.data[0].forest_idx != UINT32_MAX) {
                    if (accept_count == 0)
                        accept_root = (int32_t)cur_value_stack.data[0].forest_idx;
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

                if (cur_pos >= tokens.len ||
                    !u32vec_copy(&next_stack, &cur_state_stack) ||
                    !parsevaluevec_copy(&next_values, &cur_value_stack) ||
                    !u32vec_push(&next_stack, (uint32_t)action->value)) {
                    free(next_stack.data);
                    free(next_values.data);
                    slr_summary_set_error(error_buf, error_buf_size,
                                          "failed to fork GLR forest shift branch");
                    goto fail;
                }
                term_node = gll_node_get_term(&nodes, tokens.data[cur_pos].term_kind,
                                              cur_pos);
                if (term_node < 0) {
                    free(next_stack.data);
                    free(next_values.data);
                    slr_summary_set_error(error_buf, error_buf_size,
                                          "failed to allocate GLR forest terminal");
                    goto fail;
                }
                value.is_cert = false;
                value.token_atom = tokens.data[cur_pos].token_atom;
                value.term_kind = tokens.data[cur_pos].term_kind;
                value.start = cur_pos;
                value.end = cur_pos + 1;
                value.forest_idx = (uint32_t)term_node;
                value.cert = NULL;
                if (!parsevaluevec_push(&next_values, &value)) {
                    free(next_stack.data);
                    free(next_values.data);
                    slr_summary_set_error(error_buf, error_buf_size,
                                          "failed to push GLR forest shift value");
                    goto fail;
                }
                next_pos = cur_pos + 1;
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
                if (!u32vec_copy(&next_stack, &cur_state_stack) ||
                    !parsevaluevec_copy(&next_values, &cur_value_stack)) {
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
                    int32_t sym_idx = gll_node_get_sym(&nodes, lhs, cur_pos, cur_pos);
                    int32_t eps_idx = gll_node_get_eps(&nodes, cur_pos);
                    Atom *eps = make_eps_cert(arena);
                    Atom *kids[1] = {eps};
                    if (sym_idx < 0 || eps_idx < 0 ||
                        !gll_node_push_packed_unique(&nodes, (uint32_t)sym_idx,
                                                     CETTA_LP_NATIVE_NODE_NONE,
                                                     (uint32_t)eps_idx,
                                                     cur_pos,
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
                    next_value.start = cur_pos;
                    next_value.end = cur_pos;
                    next_value.forest_idx = (uint32_t)sym_idx;
                    next_value.cert = make_node_cert(
                        arena, grammar->productions[prod_idx].label, lhs,
                        cur_pos, cur_pos, kids, 1);
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
