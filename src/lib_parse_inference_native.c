#include "lib_parse_inference_native.h"

#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    Atom **data;
    uint32_t len;
    uint32_t cap;
} AtomVec;

typedef struct {
    Atom *atom;
    bool expanded;
} InferenceVisit;

typedef struct {
    InferenceVisit *data;
    uint32_t len;
    uint32_t cap;
} InferenceVisitVec;

typedef enum {
    INFERENCE_CERT_EPSILON = 0,
    INFERENCE_CERT_TOKEN = 1,
    INFERENCE_CERT_DERIVATION = 2
} InferenceCertKind;

typedef struct {
    Atom *certificate;
    InferenceCertKind kind;
    uint32_t left;
    uint32_t right;
    uint32_t node_id;
    SymbolId sort;
    Atom *tree;
    Atom *token;
} InferenceCertResult;

typedef struct {
    InferenceCertResult *data;
    uint32_t len;
    uint32_t cap;
} InferenceCertResultVec;

typedef struct {
    const char *head;
    uint32_t arity;
} InferenceDecl;

typedef struct {
    InferenceDecl *data;
    uint32_t len;
    uint32_t cap;
    uint32_t *slots;
    uint32_t slot_cap;
} InferenceDeclVec;

typedef struct {
    Arena *arena;
    const CettaLpNativeGrammar *grammar;
    bool compact;
    const char *source_text;
    Atom *source_pattern;
    Atom **tokens;
    Atom **token_patterns;
    Atom **boundary_patterns;
    const char **boundary_labels;
    uint32_t token_len;
} InferenceBuild;

static bool inference_set_error(char *buf, size_t size, const char *fmt, ...) {
    va_list ap;

    if (buf && size > 0) {
        va_start(ap, fmt);
        vsnprintf(buf, size, fmt, ap);
        va_end(ap);
    }
    return false;
}

static char *inference_format(Arena *arena, const char *fmt, ...) {
    va_list ap;
    va_list copy;
    int len;
    char *out;

    va_start(ap, fmt);
    va_copy(copy, ap);
    len = vsnprintf(NULL, 0, fmt, copy);
    va_end(copy);
    if (len < 0) {
        va_end(ap);
        return NULL;
    }
    out = arena_alloc(arena, (size_t)len + 1u);
    vsnprintf(out, (size_t)len + 1u, fmt, ap);
    va_end(ap);
    return out;
}

static Atom *inference_expr(Arena *arena, uint32_t len, ...) {
    Atom **elems;
    va_list ap;
    uint32_t i;

    elems = arena_alloc(arena, sizeof(*elems) * len);
    va_start(ap, len);
    for (i = 0; i < len; i++)
        elems[i] = va_arg(ap, Atom *);
    va_end(ap);
    return atom_expr(arena, elems, len);
}

static Atom *inference_lnil(Arena *arena) {
    return atom_symbol(arena, "LNil");
}

static Atom *inference_llist(Arena *arena, Atom **items, uint32_t len) {
    Atom *out = inference_lnil(arena);
    uint32_t i;

    for (i = len; i > 0; i--) {
        out = inference_expr(arena, 3, atom_symbol(arena, "LCons"),
                             items[i - 1u], out);
    }
    return out;
}

#define INFERENCE_DAG_BLOCK_SIZE 128u

static Atom *inference_llist_blocks(Arena *arena,
                                    Atom **items,
                                    uint32_t len) {
    uint32_t block_count =
        (len + INFERENCE_DAG_BLOCK_SIZE - 1u) / INFERENCE_DAG_BLOCK_SIZE;
    Atom **blocks = block_count
        ? arena_alloc(arena, sizeof(*blocks) * block_count)
        : NULL;
    uint32_t block;

    for (block = 0; block < block_count; block++) {
        uint32_t start = block * INFERENCE_DAG_BLOCK_SIZE;
        uint32_t remaining = len - start;
        uint32_t block_len = remaining < INFERENCE_DAG_BLOCK_SIZE
            ? remaining
            : INFERENCE_DAG_BLOCK_SIZE;
        blocks[block] = inference_llist(arena, items + start, block_len);
    }
    return inference_llist(arena, blocks, block_count);
}

static bool atom_vec_push(AtomVec *vec, Atom *atom) {
    if (vec->len == vec->cap) {
        uint32_t next_cap = vec->cap ? vec->cap * 2u : 16u;
        if (next_cap < vec->cap)
            return false;
        vec->data = cetta_realloc(vec->data, sizeof(*vec->data) * next_cap);
        vec->cap = next_cap;
    }
    vec->data[vec->len++] = atom;
    return true;
}

static bool inference_visit_push(InferenceVisitVec *vec,
                                 Atom *atom,
                                 bool expanded) {
    if (vec->len == vec->cap) {
        uint32_t next_cap = vec->cap ? vec->cap * 2u : 32u;
        if (next_cap < vec->cap)
            return false;
        vec->data = cetta_realloc(vec->data, sizeof(*vec->data) * next_cap);
        vec->cap = next_cap;
    }
    vec->data[vec->len].atom = atom;
    vec->data[vec->len].expanded = expanded;
    vec->len++;
    return true;
}

static bool inference_cert_result_push(InferenceCertResultVec *vec,
                                       InferenceCertResult result) {
    if (vec->len == vec->cap) {
        uint32_t next_cap = vec->cap ? vec->cap * 2u : 32u;
        if (next_cap < vec->cap)
            return false;
        vec->data = cetta_realloc(vec->data, sizeof(*vec->data) * next_cap);
        vec->cap = next_cap;
    }
    vec->data[vec->len++] = result;
    return true;
}

static InferenceCertResult *inference_cert_result_find(
    InferenceCertResultVec *vec,
    Atom *certificate) {
    uint32_t i;

    for (i = vec->len; i > 0; i--) {
        if (vec->data[i - 1u].certificate == certificate)
            return &vec->data[i - 1u];
    }
    return NULL;
}

static bool inference_collect_cons(Atom *list,
                                   AtomVec *items,
                                   char *error_buf,
                                   size_t error_buf_size) {
    Atom *cursor = list;

    memset(items, 0, sizeof(*items));
    while (!atom_is_symbol(cursor, "Nil")) {
        if (!cursor || cursor->kind != ATOM_EXPR || cursor->expr.len != 3 ||
            !atom_is_symbol(cursor->expr.elems[0], "Cons")) {
            free(items->data);
            memset(items, 0, sizeof(*items));
            return inference_set_error(error_buf, error_buf_size,
                                       "certificate children must be a proper Cons/Nil list");
        }
        if (!atom_vec_push(items, cursor->expr.elems[1])) {
            free(items->data);
            memset(items, 0, sizeof(*items));
            return inference_set_error(error_buf, error_buf_size,
                                       "certificate has too many children");
        }
        cursor = cursor->expr.elems[2];
    }
    return true;
}

static bool inference_atom_u32(Atom *atom, uint32_t *out) {
    if (!atom || !out || atom->kind != ATOM_GROUNDED ||
        atom->ground.gkind != GV_INT || atom->ground.ival < 0 ||
        (uint64_t)atom->ground.ival > UINT32_MAX) {
        return false;
    }
    *out = (uint32_t)atom->ground.ival;
    return true;
}

static bool inference_atom_symbol_id(Atom *atom, SymbolId *out) {
    if (!atom || !out)
        return false;
    if (atom->kind == ATOM_SYMBOL) {
        *out = atom->sym_id;
        return true;
    }
    if (atom->kind == ATOM_GROUNDED && atom->ground.gkind == GV_STRING) {
        *out = symbol_intern_cstr(g_symbols, atom->ground.sval);
        return true;
    }
    return false;
}

static bool inference_form_is(Atom *atom, const char *head, uint32_t len) {
    return atom && atom->kind == ATOM_EXPR && atom->expr.len == len &&
           atom_is_symbol(atom->expr.elems[0], head);
}

static bool inference_postorder(Atom *root,
                                AtomVec *order,
                                char *error_buf,
                                size_t error_buf_size) {
    InferenceVisitVec stack;

    memset(order, 0, sizeof(*order));
    memset(&stack, 0, sizeof(stack));
    if (!inference_visit_push(&stack, root, false))
        goto too_large;

    while (stack.len > 0) {
        InferenceVisit visit = stack.data[--stack.len];
        if (inference_form_is(visit.atom, "NodeC", 6) && !visit.expanded) {
            AtomVec kids;
            uint32_t i;

            if (!inference_collect_cons(visit.atom->expr.elems[5], &kids,
                                        error_buf, error_buf_size)) {
                free(stack.data);
                free(order->data);
                memset(order, 0, sizeof(*order));
                return false;
            }
            if (!inference_visit_push(&stack, visit.atom, true)) {
                free(kids.data);
                goto too_large;
            }
            for (i = kids.len; i > 0; i--) {
                if (!inference_visit_push(&stack, kids.data[i - 1u], false)) {
                    free(kids.data);
                    goto too_large;
                }
            }
            free(kids.data);
        } else if (!atom_vec_push(order, visit.atom)) {
            goto too_large;
        }
    }
    free(stack.data);
    return true;

too_large:
    free(stack.data);
    free(order->data);
    memset(order, 0, sizeof(*order));
    return inference_set_error(error_buf, error_buf_size,
                               "certificate is too large");
}

static bool inference_collect_tokens(Atom *list,
                                     AtomVec *tokens,
                                     char *error_buf,
                                     size_t error_buf_size) {
    Atom *cursor = list;
    CettaExprIndex i;

    memset(tokens, 0, sizeof(*tokens));
    if (cursor && cursor->kind == ATOM_EXPR &&
        (cursor->expr.len == 0 ||
         !atom_is_symbol(cursor->expr.elems[0], "Cons"))) {
        for (i = 0; i < cursor->expr.len; i++) {
            if (!atom_vec_push(tokens, cursor->expr.elems[i])) {
                free(tokens->data);
                memset(tokens, 0, sizeof(*tokens));
                return inference_set_error(error_buf, error_buf_size,
                                           "token ledger is too long");
            }
        }
        return true;
    }
    while (!atom_is_symbol(cursor, "Nil")) {
        if (!cursor || cursor->kind != ATOM_EXPR || cursor->expr.len != 3 ||
            !atom_is_symbol(cursor->expr.elems[0], "Cons")) {
            free(tokens->data);
            memset(tokens, 0, sizeof(*tokens));
            return inference_set_error(error_buf, error_buf_size,
                                       "token ledger must be a proper Cons/Nil list");
        }
        if (!atom_vec_push(tokens, cursor->expr.elems[1])) {
            free(tokens->data);
            memset(tokens, 0, sizeof(*tokens));
            return inference_set_error(error_buf, error_buf_size,
                                       "token ledger is too long");
        }
        cursor = cursor->expr.elems[2];
    }
    return true;
}

static bool inference_source_text(Atom *source, const char **out) {
    if (!source || !out)
        return false;
    if (source->kind == ATOM_GROUNDED && source->ground.gkind == GV_STRING) {
        *out = source->ground.sval;
        return source->ground.sval[0] != '\0';
    }
    if (source->kind == ATOM_SYMBOL) {
        *out = atom_name_cstr(source);
        return (*out)[0] != '\0';
    }
    return false;
}

static uint64_t inference_decl_hash(const char *text, uint32_t arity) {
    const unsigned char *p = (const unsigned char *)text;
    uint64_t hash = UINT64_C(1469598103934665603);

    while (*p) {
        hash ^= *p++;
        hash *= UINT64_C(1099511628211);
    }
    hash ^= arity;
    hash *= UINT64_C(1099511628211);
    return hash;
}

static void inference_decl_rehash(InferenceDeclVec *vec, uint32_t slot_cap) {
    uint32_t *slots = cetta_malloc(sizeof(*slots) * slot_cap);
    uint32_t i;

    memset(slots, 0, sizeof(*slots) * slot_cap);
    for (i = 0; i < vec->len; i++) {
        uint32_t pos = (uint32_t)inference_decl_hash(vec->data[i].head,
                                                     vec->data[i].arity) &
                       (slot_cap - 1u);
        while (slots[pos] != 0)
            pos = (pos + 1u) & (slot_cap - 1u);
        slots[pos] = i + 1u;
    }
    free(vec->slots);
    vec->slots = slots;
    vec->slot_cap = slot_cap;
}

static bool inference_decl_add(InferenceDeclVec *vec,
                               const char *head,
                               uint32_t arity) {
    uint32_t pos;

    if (vec->slot_cap == 0)
        inference_decl_rehash(vec, 32u);
    if ((uint64_t)(vec->len + 1u) * 10u >=
        (uint64_t)vec->slot_cap * 7u) {
        if (vec->slot_cap > UINT32_MAX / 2u)
            return false;
        inference_decl_rehash(vec, vec->slot_cap * 2u);
    }

    pos = (uint32_t)inference_decl_hash(head, arity) &
          (vec->slot_cap - 1u);
    while (vec->slots[pos] != 0) {
        InferenceDecl *existing = &vec->data[vec->slots[pos] - 1u];
        if (existing->arity == arity && strcmp(existing->head, head) == 0)
            return true;
        pos = (pos + 1u) & (vec->slot_cap - 1u);
    }

    if (vec->len == vec->cap) {
        uint32_t next_cap = vec->cap ? vec->cap * 2u : 32u;
        if (next_cap < vec->cap)
            return false;
        vec->data = cetta_realloc(vec->data, sizeof(*vec->data) * next_cap);
        vec->cap = next_cap;
    }
    vec->data[vec->len].head = head;
    vec->data[vec->len].arity = arity;
    vec->slots[pos] = vec->len + 1u;
    vec->len++;
    return true;
}

static const char *inference_literal_label(Arena *arena,
                                           const char *kind,
                                           const char *payload) {
    return inference_format(arena, "__lib_parse.literal.%s.%s", kind, payload);
}

static const char *inference_rule_id(Arena *arena,
                                     const char *kind,
                                     const char *payload) {
    return inference_format(arena, "__lib_parse.rule.%s.%zu.%s",
                            kind, strlen(payload), payload);
}

static const char *inference_symbol_text(Arena *arena, SymbolId id) {
    return atom_name_cstr(atom_symbol_id(arena, id));
}

static Atom *inference_papp(Arena *arena, const char *head, Atom *args) {
    return inference_expr(arena, 3, atom_symbol(arena, "PApp"),
                          atom_string(arena, head), args);
}

static Atom *inference_papp0(Arena *arena, const char *head) {
    return inference_papp(arena, head, inference_lnil(arena));
}

static Atom *inference_fvar(Arena *arena, const char *name) {
    return inference_expr(arena, 2, atom_symbol(arena, "FVar"),
                          atom_string(arena, name));
}

static Atom *inference_boundary_var(Arena *arena, uint32_t index) {
    return inference_fvar(arena, inference_format(arena, "boundary%u", index));
}

static Atom *inference_tree_var(Arena *arena, uint32_t index) {
    return inference_fvar(arena, inference_format(arena, "tree%u", index));
}

static Atom *inference_formal(Arena *arena, const char *name) {
    return inference_expr(arena, 3, atom_symbol(arena, "Formal"),
                          atom_string(arena, name), atom_int(arena, 0));
}

static Atom *inference_token_pattern(Arena *arena, Atom *token) {
    const char *text = atom_to_parseable_string(arena, token);
    return inference_papp0(arena,
                           inference_literal_label(arena, "token", text));
}

static Atom *inference_sort_pattern(Arena *arena, SymbolId sort) {
    return inference_papp0(
        arena, inference_literal_label(arena, "sort",
                                       inference_symbol_text(arena, sort)));
}

static Atom *inference_boundary_judgment(Arena *arena,
                                         Atom *source,
                                         Atom *boundary) {
    Atom *args[2] = {source, boundary};
    return inference_papp(arena, "__lib_parse.Boundary",
                          inference_llist(arena, args, 2));
}

static Atom *inference_token_span_judgment(Arena *arena,
                                           Atom *source,
                                           Atom *left,
                                           Atom *right,
                                           Atom *token) {
    Atom *args[4] = {source, left, right, token};
    return inference_papp(arena, "__lib_parse.TokenSpan",
                          inference_llist(arena, args, 4));
}

static Atom *inference_derives_judgment(Arena *arena,
                                        Atom *source,
                                        Atom *sort,
                                        Atom *left,
                                        Atom *right,
                                        Atom *tree) {
    Atom *args[5] = {source, sort, left, right, tree};
    return inference_papp(arena, "__lib_parse.GrammarDerives",
                          inference_llist(arena, args, 5));
}

static Atom *inference_derives_node_judgment(Arena *arena,
                                             Atom *source,
                                             Atom *sort,
                                             Atom *left,
                                             Atom *right) {
    Atom *args[4] = {source, sort, left, right};
    return inference_papp(arena, "__lib_parse.GrammarDerivesNode",
                          inference_llist(arena, args, 4));
}

static Atom *inference_build_derives_judgment(InferenceBuild *build,
                                              Atom *source,
                                              Atom *sort,
                                              Atom *left,
                                              Atom *right,
                                              Atom *tree) {
    if (build->compact)
        return inference_derives_node_judgment(
            build->arena, source, sort, left, right);
    return inference_derives_judgment(
        build->arena, source, sort, left, right, tree);
}

static Atom *inference_ground_rule(Arena *arena,
                                    const char *rule_id,
                                    Atom *conclusion) {
    return inference_expr(arena, 5, atom_symbol(arena, "GRule"),
                          atom_string(arena, rule_id), inference_lnil(arena),
                          inference_lnil(arena), conclusion);
}

static bool inference_add_token_decl(InferenceDeclVec *decls,
                                     Arena *arena,
                                     Atom *token) {
    const char *text = atom_to_parseable_string(arena, token);
    return inference_decl_add(
        decls, inference_literal_label(arena, "token", text), 0);
}

static bool inference_add_sort_decl(InferenceDeclVec *decls,
                                    Arena *arena,
                                    SymbolId sort) {
    return inference_decl_add(
        decls,
        inference_literal_label(arena, "sort",
                                inference_symbol_text(arena, sort)),
        0);
}

static uint32_t inference_hole_count(const CettaLpNativeProduction *prod) {
    uint32_t count = 0;
    uint32_t i;

    for (i = 0; i < prod->rhs_len; i++) {
        if (prod->rhs[i].kind != CETTA_LP_NATIVE_SYMBOL_TM)
            count++;
    }
    return count;
}

static bool inference_build_decls(InferenceBuild *build,
                                  InferenceDeclVec *decls,
                                  char *error_buf,
                                  size_t error_buf_size) {
    Arena *arena = build->arena;
    uint32_t i;

    memset(decls, 0, sizeof(*decls));
    if (!inference_decl_add(
            decls,
            inference_literal_label(arena, "source", build->source_text), 0) ||
        !inference_decl_add(decls, "__lib_parse.leaf", 1)) {
        return inference_set_error(error_buf, error_buf_size,
                                   "constructor table is too large");
    }
    for (i = 0; i < build->token_len; i++) {
        if (!inference_add_token_decl(decls, arena, build->tokens[i]))
            return inference_set_error(error_buf, error_buf_size,
                                       "constructor table is too large");
    }
    for (i = 0; i <= build->token_len; i++) {
        if (!inference_decl_add(decls, build->boundary_labels[i], 0))
            return inference_set_error(error_buf, error_buf_size,
                                       "constructor table is too large");
    }

    for (i = 0; i < build->grammar->entry_len; i++) {
        CettaLpNativeEntry entry = build->grammar->entries[i];
        if (entry.kind == CETTA_LP_NATIVE_ENTRY_PRODUCTION) {
            const CettaLpNativeProduction *prod =
                &build->grammar->productions[entry.index];
            uint32_t j;
            if (!inference_decl_add(decls,
                                    inference_symbol_text(arena, prod->label),
                                    inference_hole_count(prod)) ||
                !inference_add_sort_decl(decls, arena, prod->lhs)) {
                return inference_set_error(error_buf, error_buf_size,
                                           "constructor table is too large");
            }
            for (j = 0; j < prod->rhs_len; j++) {
                const CettaLpNativeSymbol *symbol = &prod->rhs[j];
                if (symbol->kind == CETTA_LP_NATIVE_SYMBOL_TM) {
                    if (!inference_add_token_decl(
                            decls, arena, atom_symbol_id(arena, symbol->name)))
                        return inference_set_error(
                            error_buf, error_buf_size,
                            "constructor table is too large");
                } else {
                    if (!inference_add_sort_decl(decls, arena, symbol->name))
                        return inference_set_error(
                            error_buf, error_buf_size,
                            "constructor table is too large");
                    if (symbol->kind == CETTA_LP_NATIVE_SYMBOL_HLB &&
                        !inference_add_sort_decl(decls, arena, symbol->scope))
                        return inference_set_error(
                            error_buf, error_buf_size,
                            "constructor table is too large");
                }
            }
        } else if (entry.kind == CETTA_LP_NATIVE_ENTRY_VAR) {
            const CettaLpNativeVarDecl *decl =
                &build->grammar->vars[entry.index];
            if (!inference_add_sort_decl(decls, arena, decl->nt) ||
                !inference_add_token_decl(
                    decls, arena, atom_symbol_id(arena, decl->atom))) {
                return inference_set_error(error_buf, error_buf_size,
                                           "constructor table is too large");
            }
        } else {
            const CettaLpNativeLexDecl *decl =
                &build->grammar->lexes[entry.index];
            if (!inference_add_sort_decl(decls, arena, decl->nt))
                return inference_set_error(error_buf, error_buf_size,
                                           "constructor table is too large");
        }
    }
    return true;
}

static Atom *inference_decls_atom(InferenceBuild *build,
                                  const InferenceDeclVec *decls) {
    Atom **items;
    Atom *out;
    uint32_t i;

    items = arena_alloc(build->arena, sizeof(*items) * decls->len);
    for (i = 0; i < decls->len; i++) {
        items[i] = inference_expr(
            build->arena, 3, atom_symbol(build->arena, "CDecl"),
            atom_string(build->arena, decls->data[i].head),
            atom_int(build->arena, decls->data[i].arity));
    }
    out = inference_llist(build->arena, items, decls->len);
    return out;
}

static Atom *inference_judgments_atom(Arena *arena, bool compact) {
    Atom *items[3];

    items[0] = inference_expr(arena, 3, atom_symbol(arena, "JDecl"),
                              atom_string(arena, "__lib_parse.Boundary"),
                              atom_int(arena, 2));
    items[1] = inference_expr(arena, 3, atom_symbol(arena, "JDecl"),
                              atom_string(arena, "__lib_parse.TokenSpan"),
                              atom_int(arena, 4));
    items[2] = inference_expr(
        arena, 3, atom_symbol(arena, "JDecl"),
        atom_string(arena, compact
            ? "__lib_parse.GrammarDerivesNode"
            : "__lib_parse.GrammarDerives"),
        atom_int(arena, compact ? 4 : 5));
    return inference_llist(arena, items, 3);
}

static bool inference_leaf_sort(InferenceBuild *build,
                                Atom *token,
                                SymbolId *sort_out) {
    Atom *lex_class;
    uint32_t i;

    if (token && token->kind == ATOM_EXPR && token->expr.len == 3 &&
        atom_is_symbol(token->expr.elems[0], "Lex")) {
        lex_class = token->expr.elems[1];
    } else {
        lex_class = atom_symbol(build->arena, "NF");
    }

    for (i = 0; i < build->grammar->entry_len; i++) {
        CettaLpNativeEntry entry = build->grammar->entries[i];
        if (entry.kind == CETTA_LP_NATIVE_ENTRY_VAR) {
            const CettaLpNativeVarDecl *decl =
                &build->grammar->vars[entry.index];
            if (atom_eq(token, atom_symbol_id(build->arena, decl->atom))) {
                *sort_out = decl->nt;
                return true;
            }
        } else if (entry.kind == CETTA_LP_NATIVE_ENTRY_LEX) {
            const CettaLpNativeLexDecl *decl =
                &build->grammar->lexes[entry.index];
            if (atom_eq(lex_class,
                        atom_symbol_id(build->arena, decl->klass))) {
                *sort_out = decl->nt;
                return true;
            }
        }
    }
    return false;
}

static Atom *inference_boundary_rule(InferenceBuild *build, uint32_t index) {
    const char *payload =
        inference_format(build->arena, "%s:%u", build->source_text, index);
    Atom *conclusion = inference_boundary_judgment(
        build->arena, build->source_pattern, build->boundary_patterns[index]);
    return inference_ground_rule(
        build->arena,
        inference_rule_id(build->arena, "boundary", payload), conclusion);
}

static Atom *inference_token_rule(InferenceBuild *build, uint32_t index) {
    const char *payload =
        inference_format(build->arena, "%s:%u", build->source_text, index);
    Atom *conclusion = inference_token_span_judgment(
        build->arena, build->source_pattern, build->boundary_patterns[index],
        build->boundary_patterns[index + 1u], build->token_patterns[index]);
    return inference_ground_rule(
        build->arena,
        inference_rule_id(build->arena, "token", payload), conclusion);
}

static Atom *inference_leaf_rule(InferenceBuild *build,
                                 uint32_t index,
                                 SymbolId sort) {
    const char *payload =
        inference_format(build->arena, "%s:%u", build->source_text, index);
    Atom *tree_args[1] = {build->token_patterns[index]};
    Atom *tree = inference_papp(
        build->arena, "__lib_parse.leaf",
        inference_llist(build->arena, tree_args, 1));
    Atom *conclusion = inference_build_derives_judgment(
        build, build->source_pattern,
        inference_sort_pattern(build->arena, sort),
        build->boundary_patterns[index],
        build->boundary_patterns[index + 1u], tree);
    return inference_ground_rule(
        build->arena,
        inference_rule_id(build->arena, "leaf", payload), conclusion);
}

static Atom *inference_production_rule(
    InferenceBuild *build,
    const CettaLpNativeProduction *prod) {
    Arena *arena = build->arena;
    uint32_t holes = inference_hole_count(prod);
    uint32_t formal_len = 1u + prod->rhs_len + 1u +
        (build->compact ? 0u : holes);
    uint32_t premise_len = prod->rhs_len == 0 ? 1u : prod->rhs_len;
    Atom **formals = arena_alloc(arena, sizeof(*formals) * formal_len);
    Atom **premises = arena_alloc(arena, sizeof(*premises) * premise_len);
    Atom **children = holes ? arena_alloc(arena, sizeof(*children) * holes) : NULL;
    Atom *source_var = inference_fvar(arena, "source");
    uint32_t formal_index = 0;
    uint32_t tree_index = 0;
    uint32_t i;

    formals[formal_index++] = inference_formal(arena, "source");
    for (i = 0; i <= prod->rhs_len; i++) {
        formals[formal_index++] = inference_formal(
            arena, inference_format(arena, "boundary%u", i));
    }
    if (!build->compact) {
        for (i = 0; i < holes; i++) {
            formals[formal_index++] = inference_formal(
                arena, inference_format(arena, "tree%u", i));
        }
    }

    if (prod->rhs_len == 0) {
        premises[0] = inference_boundary_judgment(
            arena, source_var, inference_boundary_var(arena, 0));
    } else {
        for (i = 0; i < prod->rhs_len; i++) {
            const CettaLpNativeSymbol *symbol = &prod->rhs[i];
            Atom *left = inference_boundary_var(arena, i);
            Atom *right = inference_boundary_var(arena, i + 1u);
            if (symbol->kind == CETTA_LP_NATIVE_SYMBOL_TM) {
                premises[i] = inference_token_span_judgment(
                    arena, source_var, left, right,
                    inference_token_pattern(
                        arena, atom_symbol_id(arena, symbol->name)));
            } else {
                Atom *tree_var = build->compact
                    ? NULL
                    : inference_tree_var(arena, tree_index);
                premises[i] = inference_build_derives_judgment(
                    build, source_var,
                    inference_sort_pattern(arena, symbol->name), left, right,
                    tree_var);
                if (!build->compact)
                    children[tree_index] = tree_var;
                tree_index++;
            }
        }
    }

    {
        const char *label = inference_symbol_text(arena, prod->label);
        Atom *tree = build->compact
            ? NULL
            : inference_papp(arena, label,
                             inference_llist(arena, children, holes));
        Atom *conclusion = inference_build_derives_judgment(
            build, source_var, inference_sort_pattern(arena, prod->lhs),
            inference_boundary_var(arena, 0),
            inference_boundary_var(arena, prod->rhs_len), tree);
        return inference_expr(
            arena, 5, atom_symbol(arena, "GRule"),
            atom_string(arena,
                        inference_rule_id(arena, "production", label)),
            inference_llist(arena, formals, formal_len),
            inference_llist(arena, premises, premise_len), conclusion);
    }
}

static bool inference_build_rules(InferenceBuild *build,
                                  AtomVec *rules,
                                  char *error_buf,
                                  size_t error_buf_size) {
    uint32_t i;

    memset(rules, 0, sizeof(*rules));
    for (i = 0; i <= build->token_len; i++) {
        if (!atom_vec_push(rules, inference_boundary_rule(build, i)))
            goto too_large;
    }
    for (i = 0; i < build->token_len; i++) {
        if (!atom_vec_push(rules, inference_token_rule(build, i)))
            goto too_large;
    }
    for (i = 0; i < build->token_len; i++) {
        SymbolId sort;
        if (inference_leaf_sort(build, build->tokens[i], &sort) &&
            !atom_vec_push(rules, inference_leaf_rule(build, i, sort)))
            goto too_large;
    }
    for (i = 0; i < build->grammar->entry_len; i++) {
        CettaLpNativeEntry entry = build->grammar->entries[i];
        if (entry.kind == CETTA_LP_NATIVE_ENTRY_PRODUCTION &&
            !atom_vec_push(
                rules,
                inference_production_rule(
                    build, &build->grammar->productions[entry.index])))
            goto too_large;
    }
    return true;

too_large:
    free(rules->data);
    memset(rules, 0, sizeof(*rules));
    return inference_set_error(error_buf, error_buf_size,
                               "rule table is too large");
}

static Atom *inference_presentation(
    const CettaLpNativeGrammar *grammar,
    Atom *source,
    Atom *token_list,
    bool compact,
    Arena *arena,
    char *error_buf,
    size_t error_buf_size) {
    InferenceBuild build;
    InferenceDeclVec decls;
    AtomVec tokens;
    AtomVec rules;
    Atom *constructors;
    Atom *judgments;
    Atom *rule_list;
    Atom *result = NULL;
    uint32_t i;

    memset(&build, 0, sizeof(build));
    memset(&decls, 0, sizeof(decls));
    memset(&tokens, 0, sizeof(tokens));
    memset(&rules, 0, sizeof(rules));
    if (!grammar || !arena ||
        !inference_source_text(source, &build.source_text)) {
        inference_set_error(error_buf, error_buf_size,
                            "inference presentation expects a nonempty source name");
        return NULL;
    }
    if (!inference_collect_tokens(token_list, &tokens,
                                  error_buf, error_buf_size))
        return NULL;

    build.arena = arena;
    build.grammar = grammar;
    build.compact = compact;
    build.tokens = tokens.data;
    build.token_len = tokens.len;
    build.source_pattern = inference_papp0(
        arena, inference_literal_label(arena, "source", build.source_text));
    build.token_patterns = tokens.len
        ? arena_alloc(arena, sizeof(*build.token_patterns) * tokens.len)
        : NULL;
    build.boundary_patterns = arena_alloc(
        arena, sizeof(*build.boundary_patterns) * ((size_t)tokens.len + 1u));
    build.boundary_labels = arena_alloc(
        arena, sizeof(*build.boundary_labels) * ((size_t)tokens.len + 1u));

    for (i = 0; i < tokens.len; i++)
        build.token_patterns[i] = inference_token_pattern(arena, tokens.data[i]);
    for (i = 0; i <= tokens.len; i++) {
        build.boundary_labels[i] = inference_literal_label(
            arena, "boundary", inference_format(arena, "%u", i));
        build.boundary_patterns[i] =
            inference_papp0(arena, build.boundary_labels[i]);
    }

    if (!inference_build_decls(&build, &decls,
                               error_buf, error_buf_size) ||
        !inference_build_rules(&build, &rules,
                               error_buf, error_buf_size)) {
        goto cleanup;
    }
    constructors = inference_decls_atom(&build, &decls);
    judgments = inference_judgments_atom(arena, compact);
    rule_list = inference_llist(arena, rules.data, rules.len);
    result = inference_expr(arena, 4, atom_symbol(arena, "GPresentation"),
                            constructors, judgments, rule_list);

cleanup:
    free(tokens.data);
    free(rules.data);
    free(decls.data);
    free(decls.slots);
    return result;
}

Atom *cetta_lp_native_inference_presentation(
    const CettaLpNativeGrammar *grammar,
    Atom *source,
    Atom *token_list,
    Arena *arena,
    char *error_buf,
    size_t error_buf_size) {
    return inference_presentation(
        grammar, source, token_list, false, arena,
        error_buf, error_buf_size);
}

Atom *cetta_lp_native_inference_dag_presentation(
    const CettaLpNativeGrammar *grammar,
    Atom *source,
    Atom *token_list,
    Arena *arena,
    char *error_buf,
    size_t error_buf_size) {
    return inference_presentation(
        grammar, source, token_list, true, arena,
        error_buf, error_buf_size);
}

static const CettaLpNativeProduction *inference_find_production(
    const CettaLpNativeGrammar *grammar,
    SymbolId label) {
    uint32_t i;

    for (i = 0; i < grammar->production_len; i++) {
        if (grammar->productions[i].label == label)
            return &grammar->productions[i];
    }
    return NULL;
}

static Atom *inference_drlist(Arena *arena,
                              const uint32_t *ids,
                              uint32_t len) {
    Atom *out = atom_symbol(arena, "DrNil");
    uint32_t i;

    for (i = len; i > 0; i--) {
        out = inference_expr(arena, 3, atom_symbol(arena, "DrCons"),
                             atom_int(arena, ids[i - 1u]), out);
    }
    return out;
}

static bool inference_add_dag_node(InferenceBuild *build,
                                   AtomVec *nodes,
                                   const char *rule_id,
                                   Atom *arguments,
                                   const uint32_t *child_ids,
                                   uint32_t child_len,
                                   uint32_t *node_id_out) {
    Atom *instance;
    Atom *node;
    uint32_t node_id = nodes->len;

    instance = inference_expr(
        build->arena, 3, atom_symbol(build->arena, "GRuleInst"),
        atom_string(build->arena, rule_id), arguments);
    node = inference_expr(
        build->arena, 4, atom_symbol(build->arena, "GDNode"),
        atom_int(build->arena, node_id), instance,
        inference_drlist(build->arena, child_ids, child_len));
    if (!atom_vec_push(nodes, node))
        return false;
    *node_id_out = node_id;
    return true;
}

static bool inference_add_ground_dag_node(InferenceBuild *build,
                                          AtomVec *nodes,
                                          const char *kind,
                                          uint32_t index,
                                          uint32_t *node_id_out) {
    const char *payload =
        inference_format(build->arena, "%s:%u", build->source_text, index);
    return inference_add_dag_node(
        build, nodes, inference_rule_id(build->arena, kind, payload),
        inference_lnil(build->arena), NULL, 0, node_id_out);
}

static bool inference_prepare_build(InferenceBuild *build,
                                    const CettaLpNativeGrammar *grammar,
                                    Atom *source,
                                    Atom *token_list,
                                    Arena *arena,
                                    AtomVec *tokens,
                                    char *error_buf,
                                    size_t error_buf_size) {
    uint32_t i;

    memset(build, 0, sizeof(*build));
    memset(tokens, 0, sizeof(*tokens));
    if (!grammar || !arena ||
        !inference_source_text(source, &build->source_text)) {
        return inference_set_error(
            error_buf, error_buf_size,
            "inference DAG expects a nonempty source name");
    }
    if (!inference_collect_tokens(token_list, tokens,
                                  error_buf, error_buf_size))
        return false;

    build->arena = arena;
    build->grammar = grammar;
    build->tokens = tokens->data;
    build->token_len = tokens->len;
    build->source_pattern = inference_papp0(
        arena, inference_literal_label(arena, "source", build->source_text));
    build->token_patterns = tokens->len
        ? arena_alloc(arena, sizeof(*build->token_patterns) * tokens->len)
        : NULL;
    build->boundary_patterns = arena_alloc(
        arena, sizeof(*build->boundary_patterns) *
                   ((size_t)tokens->len + 1u));
    build->boundary_labels = arena_alloc(
        arena, sizeof(*build->boundary_labels) *
                   ((size_t)tokens->len + 1u));
    for (i = 0; i < tokens->len; i++)
        build->token_patterns[i] =
            inference_token_pattern(arena, tokens->data[i]);
    for (i = 0; i <= tokens->len; i++) {
        build->boundary_labels[i] = inference_literal_label(
            arena, "boundary", inference_format(arena, "%u", i));
        build->boundary_patterns[i] =
            inference_papp0(arena, build->boundary_labels[i]);
    }
    return true;
}

static bool inference_lower_token_cert(InferenceBuild *build,
                                       AtomVec *nodes,
                                       Atom *certificate,
                                       InferenceCertResult *out,
                                       char *error_buf,
                                       size_t error_buf_size) {
    uint32_t index;

    if (!inference_atom_u32(certificate->expr.elems[2], &index) ||
        index >= build->token_len ||
        !atom_eq(certificate->expr.elems[1], build->tokens[index])) {
        return inference_set_error(error_buf, error_buf_size,
                                   "token certificate disagrees with source ledger");
    }
    memset(out, 0, sizeof(*out));
    out->certificate = certificate;
    out->kind = INFERENCE_CERT_TOKEN;
    out->left = index;
    out->right = index + 1u;
    out->token = build->tokens[index];
    if (!inference_add_ground_dag_node(build, nodes, "token", index,
                                       &out->node_id)) {
        return inference_set_error(error_buf, error_buf_size,
                                   "proof DAG is too large");
    }
    return true;
}

static bool inference_lower_leaf_cert(InferenceBuild *build,
                                      AtomVec *nodes,
                                      Atom *certificate,
                                      InferenceCertResult *out,
                                      char *error_buf,
                                      size_t error_buf_size) {
    Atom *tree_args[1];
    uint32_t left;
    uint32_t right;
    SymbolId sort;

    if (!inference_atom_u32(certificate->expr.elems[2], &left) ||
        !inference_atom_u32(certificate->expr.elems[3], &right) ||
        left >= build->token_len || right != left + 1u ||
        !atom_eq(certificate->expr.elems[1], build->tokens[left]) ||
        !inference_leaf_sort(build, build->tokens[left], &sort)) {
        return inference_set_error(error_buf, error_buf_size,
                                   "lexical certificate disagrees with grammar or source");
    }
    memset(out, 0, sizeof(*out));
    out->certificate = certificate;
    out->kind = INFERENCE_CERT_DERIVATION;
    out->left = left;
    out->right = right;
    out->sort = sort;
    if (!build->compact) {
        tree_args[0] = build->token_patterns[left];
        out->tree = inference_papp(
            build->arena, "__lib_parse.leaf",
            inference_llist(build->arena, tree_args, 1));
    }
    if (!inference_add_ground_dag_node(build, nodes, "leaf", left,
                                       &out->node_id)) {
        return inference_set_error(error_buf, error_buf_size,
                                   "proof DAG is too large");
    }
    return true;
}

static bool inference_lower_node_cert(InferenceBuild *build,
                                      AtomVec *nodes,
                                      InferenceCertResultVec *results,
                                      Atom *certificate,
                                      InferenceCertResult *out,
                                      char *error_buf,
                                      size_t error_buf_size) {
    const CettaLpNativeProduction *production;
    AtomVec kids;
    SymbolId label;
    SymbolId claimed_sort;
    uint32_t left;
    uint32_t right;
    uint32_t position;
    uint32_t holes;
    uint32_t premise_len;
    Atom **boundaries;
    Atom **trees;
    uint32_t *child_ids;
    Atom **arguments;
    uint32_t argument_len;
    uint32_t tree_index = 0;
    uint32_t i;
    bool ok = false;

    memset(&kids, 0, sizeof(kids));
    if (!inference_atom_symbol_id(certificate->expr.elems[1], &label) ||
        !inference_atom_symbol_id(certificate->expr.elems[2], &claimed_sort) ||
        !inference_atom_u32(certificate->expr.elems[3], &left) ||
        !inference_atom_u32(certificate->expr.elems[4], &right) ||
        right < left || right > build->token_len) {
        return inference_set_error(error_buf, error_buf_size,
                                   "malformed production certificate");
    }
    production = inference_find_production(build->grammar, label);
    if (!production || production->lhs != claimed_sort) {
        return inference_set_error(error_buf, error_buf_size,
                                   "production certificate names the wrong rule or sort");
    }
    if (!inference_collect_cons(certificate->expr.elems[5], &kids,
                                error_buf, error_buf_size))
        return false;

    holes = inference_hole_count(production);
    premise_len = production->rhs_len == 0 ? 1u : production->rhs_len;
    boundaries = arena_alloc(
        build->arena, sizeof(*boundaries) *
                          ((size_t)production->rhs_len + 1u));
    trees = !build->compact && holes
        ? arena_alloc(build->arena, sizeof(*trees) * holes)
        : NULL;
    child_ids = cetta_malloc(sizeof(*child_ids) * premise_len);
    position = left;
    boundaries[0] = build->boundary_patterns[left];

    if (production->rhs_len == 0) {
        if (kids.len != 1 || !inference_form_is(kids.data[0], "EpsC", 1) ||
            left != right ||
            !inference_add_ground_dag_node(build, nodes, "boundary", left,
                                           &child_ids[0])) {
            inference_set_error(error_buf, error_buf_size,
                                "empty production has an invalid witness");
            goto cleanup;
        }
    } else {
        if (kids.len != production->rhs_len) {
            inference_set_error(error_buf, error_buf_size,
                                "production child count disagrees with grammar");
            goto cleanup;
        }
        for (i = 0; i < production->rhs_len; i++) {
            const CettaLpNativeSymbol *symbol = &production->rhs[i];
            InferenceCertResult *child =
                inference_cert_result_find(results, kids.data[i]);
            if (!child || child->left != position) {
                inference_set_error(error_buf, error_buf_size,
                                    "production child spans are not contiguous");
                goto cleanup;
            }
            if (symbol->kind == CETTA_LP_NATIVE_SYMBOL_TM) {
                Atom *expected = atom_symbol_id(build->arena, symbol->name);
                if (child->kind != INFERENCE_CERT_TOKEN ||
                    !atom_eq(child->token, expected)) {
                    inference_set_error(error_buf, error_buf_size,
                                        "terminal child disagrees with grammar");
                    goto cleanup;
                }
            } else {
                if (child->kind != INFERENCE_CERT_DERIVATION ||
                    child->sort != symbol->name) {
                    inference_set_error(error_buf, error_buf_size,
                                        "nonterminal child disagrees with grammar");
                    goto cleanup;
                }
                if (!build->compact)
                    trees[tree_index] = child->tree;
                tree_index++;
            }
            child_ids[i] = child->node_id;
            position = child->right;
            boundaries[i + 1u] = build->boundary_patterns[position];
        }
        if (position != right) {
            inference_set_error(error_buf, error_buf_size,
                                "production span disagrees with its children");
            goto cleanup;
        }
    }

    argument_len = 1u + production->rhs_len + 1u +
        (build->compact ? 0u : holes);
    arguments = arena_alloc(build->arena, sizeof(*arguments) * argument_len);
    arguments[0] = build->source_pattern;
    for (i = 0; i <= production->rhs_len; i++)
        arguments[1u + i] = boundaries[i];
    if (!build->compact) {
        for (i = 0; i < holes; i++)
            arguments[1u + production->rhs_len + 1u + i] = trees[i];
    }

    memset(out, 0, sizeof(*out));
    out->certificate = certificate;
    out->kind = INFERENCE_CERT_DERIVATION;
    out->left = left;
    out->right = right;
    out->sort = production->lhs;
    if (!build->compact) {
        out->tree = inference_papp(
            build->arena,
            inference_symbol_text(build->arena, production->label),
            inference_llist(build->arena, trees, holes));
    }
    if (!inference_add_dag_node(
            build, nodes,
            inference_rule_id(
                build->arena, "production",
                inference_symbol_text(build->arena, production->label)),
            inference_llist(build->arena, arguments, argument_len),
            child_ids, premise_len, &out->node_id)) {
        inference_set_error(error_buf, error_buf_size,
                            "proof DAG is too large");
        goto cleanup;
    }
    ok = true;

cleanup:
    free(kids.data);
    free(child_ids);
    return ok;
}

static Atom *inference_dag(
    const CettaLpNativeGrammar *grammar,
    Atom *source,
    SymbolId start_nt,
    Atom *token_list,
    Atom *certificate,
    bool compact,
    Arena *arena,
    char *error_buf,
    size_t error_buf_size) {
    InferenceBuild build;
    AtomVec tokens;
    AtomVec order;
    AtomVec nodes;
    InferenceCertResultVec results;
    InferenceCertResult *root;
    Atom *goal;
    Atom *node_list;
    Atom *result = NULL;
    uint32_t i;

    memset(&tokens, 0, sizeof(tokens));
    memset(&order, 0, sizeof(order));
    memset(&nodes, 0, sizeof(nodes));
    memset(&results, 0, sizeof(results));
    if (!inference_prepare_build(&build, grammar, source, token_list, arena,
                                 &tokens, error_buf, error_buf_size))
        goto cleanup;
    build.compact = compact;
    if (!inference_postorder(certificate, &order,
                             error_buf, error_buf_size))
        goto cleanup;

    for (i = 0; i < order.len; i++) {
        Atom *entry = order.data[i];
        InferenceCertResult lowered;
        bool ok;

        memset(&lowered, 0, sizeof(lowered));
        if (inference_form_is(entry, "EpsC", 1)) {
            lowered.certificate = entry;
            lowered.kind = INFERENCE_CERT_EPSILON;
            ok = true;
        } else if (inference_form_is(entry, "TokC", 3)) {
            ok = inference_lower_token_cert(
                &build, &nodes, entry, &lowered,
                error_buf, error_buf_size);
        } else if (inference_form_is(entry, "LeafC", 4)) {
            ok = inference_lower_leaf_cert(
                &build, &nodes, entry, &lowered,
                error_buf, error_buf_size);
        } else if (inference_form_is(entry, "NodeC", 6)) {
            ok = inference_lower_node_cert(
                &build, &nodes, &results, entry, &lowered,
                error_buf, error_buf_size);
        } else {
            ok = inference_set_error(error_buf, error_buf_size,
                                     "unsupported certificate node");
        }
        if (!ok || !inference_cert_result_push(&results, lowered)) {
            if (ok)
                inference_set_error(error_buf, error_buf_size,
                                    "certificate result table is too large");
            goto cleanup;
        }
    }

    root = inference_cert_result_find(&results, certificate);
    if (!root || root->kind != INFERENCE_CERT_DERIVATION ||
        root->left != 0 || root->right != build.token_len ||
        root->sort != start_nt || nodes.len == 0) {
        inference_set_error(error_buf, error_buf_size,
                            "certificate root disagrees with requested judgment");
        goto cleanup;
    }
    goal = inference_build_derives_judgment(
        &build, build.source_pattern, inference_sort_pattern(arena, start_nt),
        build.boundary_patterns[0], build.boundary_patterns[build.token_len],
        root->tree);
    node_list = compact
        ? inference_llist_blocks(arena, nodes.data, nodes.len)
        : inference_llist(arena, nodes.data, nodes.len);
    result = inference_expr(
        arena, 5,
        atom_symbol(arena,
                    compact ? "LPInferenceDagBlocks" : "LPInferenceDag"),
        goal, atom_int(arena, root->node_id), node_list,
        atom_int(arena, nodes.len));

cleanup:
    free(tokens.data);
    free(order.data);
    free(nodes.data);
    free(results.data);
    return result;
}

Atom *cetta_lp_native_inference_dag(
    const CettaLpNativeGrammar *grammar,
    Atom *source,
    SymbolId start_nt,
    Atom *token_list,
    Atom *certificate,
    Arena *arena,
    char *error_buf,
    size_t error_buf_size) {
    return inference_dag(
        grammar, source, start_nt, token_list, certificate, false,
        arena, error_buf, error_buf_size);
}

Atom *cetta_lp_native_inference_dag_proof(
    const CettaLpNativeGrammar *grammar,
    Atom *source,
    SymbolId start_nt,
    Atom *token_list,
    Atom *certificate,
    Arena *arena,
    char *error_buf,
    size_t error_buf_size) {
    return inference_dag(
        grammar, source, start_nt, token_list, certificate, true,
        arena, error_buf, error_buf_size);
}
