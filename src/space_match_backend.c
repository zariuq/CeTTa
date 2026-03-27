#include "space.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#define IMPORTED_COREF_LIMIT 144

static int cmp_uint32(const void *a, const void *b) {
    uint32_t va = *(const uint32_t *)a, vb = *(const uint32_t *)b;
    return (va > vb) - (va < vb);
}

static const char *atom_head_sym(Atom *a) {
    if (a->kind == ATOM_SYMBOL) return a->name;
    if (a->kind == ATOM_EXPR && a->expr.len > 0 &&
        a->expr.elems[0]->kind == ATOM_SYMBOL)
        return a->expr.elems[0]->name;
    return NULL;
}

static bool subst_match_same(const SubstMatch *a, const SubstMatch *b) {
    return a->atom_idx == b->atom_idx &&
           a->epoch == b->epoch &&
           a->exact == b->exact &&
           bindings_eq((Bindings *)&a->bindings, (Bindings *)&b->bindings);
}

static void subst_matchset_push(SubstMatchSet *out, uint32_t atom_idx,
                                uint32_t epoch, const Bindings *bindings,
                                bool exact) {
    if (out->len >= out->cap) {
        out->cap = out->cap ? out->cap * 2 : 8;
        out->items = cetta_realloc(out->items, sizeof(SubstMatch) * out->cap);
    }
    out->items[out->len].atom_idx = atom_idx;
    out->items[out->len].epoch = epoch;
    if (!bindings_clone(&out->items[out->len].bindings, bindings))
        return;
    out->items[out->len].exact = exact;
    out->len++;
}

static void subst_match_move(SubstMatch *dst, SubstMatch *src) {
    dst->atom_idx = src->atom_idx;
    dst->epoch = src->epoch;
    bindings_move(&dst->bindings, &src->bindings);
    dst->exact = src->exact;
}

static void native_rebuild_match_trie(Space *s) {
    SpaceMatchNativeState *st = &s->match_backend.native;
    disc_node_free(st->match_trie);
    st->match_trie = disc_node_new();
    for (uint32_t i = 0; i < s->len; i++)
        disc_insert(st->match_trie, s->atoms[i], i);
    st->match_trie_dirty = false;
}

static void native_ensure_match_trie(Space *s) {
    SpaceMatchNativeState *st = &s->match_backend.native;
    if (!st->match_trie) {
        st->match_trie = disc_node_new();
        for (uint32_t i = 0; i < s->len; i++)
            disc_insert(st->match_trie, s->atoms[i], i);
        st->match_trie_dirty = false;
    } else if (st->match_trie_dirty) {
        native_rebuild_match_trie(s);
    }
}

static void native_ensure_stree(Space *s) {
    SpaceMatchNativeState *st = &s->match_backend.native;
    if (!st->stree) {
        st->stree = cetta_malloc(sizeof(SubstTree));
        stree_init(st->stree);
        for (uint32_t i = 0; i < s->len; i++)
            stree_insert(st->stree, s->atoms[i], i);
        st->stree_dirty = false;
    } else if (st->stree_dirty) {
        stree_free(st->stree);
        stree_init(st->stree);
        for (uint32_t i = 0; i < s->len; i++)
            stree_insert(st->stree, s->atoms[i], i);
        st->stree_dirty = false;
    }
}

static void native_free(Space *s) {
    SpaceMatchNativeState *st = &s->match_backend.native;
    disc_node_free(st->match_trie);
    st->match_trie = NULL;
    st->match_trie_dirty = false;
    if (st->stree) {
        stree_free(st->stree);
        free(st->stree);
        st->stree = NULL;
    }
    st->stree_dirty = false;
}

static void native_note_add(Space *s, Atom *atom, uint32_t atom_idx) {
    SpaceMatchNativeState *st = &s->match_backend.native;
    if (st->match_trie)
        disc_insert(st->match_trie, atom, atom_idx);
    if (st->stree)
        stree_insert(st->stree, atom, atom_idx);
}

static void native_note_remove(Space *s) {
    SpaceMatchNativeState *st = &s->match_backend.native;
    (void)s;
    st->match_trie_dirty = true;
    st->stree_dirty = true;
}

static uint32_t native_candidates(Space *s, Atom *pattern, uint32_t **out) {
    SpaceMatchNativeState *st = &s->match_backend.native;
    if (s->len <= MATCH_TRIE_THRESHOLD) {
        *out = cetta_malloc(sizeof(uint32_t) * (s->len ? s->len : 1));
        for (uint32_t i = 0; i < s->len; i++) (*out)[i] = i;
        return s->len;
    }
    native_ensure_match_trie(s);
    uint32_t ncand = 0, ccand = 0;
    disc_lookup(st->match_trie, pattern, out, &ncand, &ccand);
    if (ncand > 1) {
        qsort(*out, ncand, sizeof(uint32_t), cmp_uint32);
        uint32_t w = 1;
        for (uint32_t r = 1; r < ncand; r++)
            if ((*out)[r] != (*out)[r - 1]) (*out)[w++] = (*out)[r];
        ncand = w;
    }
    return ncand;
}

static void native_query(Space *s, Arena *a, Atom *query, SubstMatchSet *out) {
    SpaceMatchNativeState *st = &s->match_backend.native;
    smset_init(out);
    if (s->len == 0) return;
    if (s->len <= MATCH_TRIE_THRESHOLD) {
        for (uint32_t i = 0; i < s->len; i++) {
            uint32_t epoch = fresh_var_suffix();
            Bindings b;
            bindings_init(&b);
            if (match_atoms_epoch(query, s->atoms[i], &b, a, epoch) &&
                !bindings_has_loop(&b)) {
                subst_matchset_push(out, i, epoch, &b, false);
            }
            bindings_free(&b);
        }
        return;
    }
    native_ensure_stree(s);

    const char *head = NULL;
    if (query->kind == ATOM_SYMBOL) head = query->name;
    else if (query->kind == ATOM_EXPR && query->expr.len > 0 &&
             query->expr.elems[0]->kind == ATOM_SYMBOL)
        head = query->expr.elems[0]->name;

    if (head) {
        stree_query_bucket(&st->stree->buckets[stree_head_hash(head)],
                           a, query, s->atoms, out);
    } else {
        for (uint32_t i = 0; i < STREE_BUCKETS; i++)
            stree_query_bucket(&st->stree->buckets[i], a, query, s->atoms, out);
    }
    stree_query_bucket(&st->stree->wildcard, a, query, s->atoms, out);

    if (out->len > 1) {
        for (uint32_t i = 1; i < out->len; i++) {
            SubstMatch tmp;
            subst_match_move(&tmp, &out->items[i]);
            uint32_t j = i;
            while (j > 0 &&
                   (out->items[j - 1].atom_idx > tmp.atom_idx ||
                    (out->items[j - 1].atom_idx == tmp.atom_idx &&
                     out->items[j - 1].epoch > tmp.epoch))) {
                subst_match_move(&out->items[j], &out->items[j - 1]);
                j--;
            }
            subst_match_move(&out->items[j], &tmp);
        }
        uint32_t w = 1;
        for (uint32_t r = 1; r < out->len; r++) {
            if (!subst_match_same(&out->items[r], &out->items[r - 1])) {
                if (w != r)
                    subst_match_move(&out->items[w], &out->items[r]);
                w++;
            } else {
                bindings_free(&out->items[r].bindings);
            }
        }
        out->len = w;
    }
}

typedef struct {
    ImportedFlatToken *items;
    uint32_t len, cap;
} ImportedFlatBuilder;

typedef struct {
    VarId var_id;
    const char *name;
    uint32_t idx;
    uint32_t span;
    Atom *origin;
} ImportedCorefRef;

typedef struct {
    ImportedCorefRef query[IMPORTED_COREF_LIMIT];
    uint32_t nquery;
    ImportedCorefRef indexed[IMPORTED_COREF_LIMIT];
    uint32_t nindexed;
    ImportedCorefRef indexed_value[IMPORTED_COREF_LIMIT];
    uint32_t nindexed_value;
} ImportedCorefState;

typedef enum {
    IMPORTED_COREF_FAIL = 0,
    IMPORTED_COREF_EXACT = 1,
    IMPORTED_COREF_NEEDS_FALLBACK = 2,
} ImportedCorefVerdict;

static void imported_bucket_free(ImportedFlatBucket *bucket) {
    for (uint32_t i = 0; i < bucket->len; i++)
        free(bucket->entries[i].tokens);
    free(bucket->entries);
    bucket->entries = NULL;
    bucket->len = 0;
    bucket->cap = 0;
}

static void imported_state_free(PathmapImportedState *st) {
    for (uint32_t i = 0; i < STREE_BUCKETS; i++)
        imported_bucket_free(&st->buckets[i]);
    imported_bucket_free(&st->wildcard);
    st->built = false;
    st->dirty = false;
}

static void imported_builder_push(ImportedFlatBuilder *b, ImportedFlatToken tok) {
    if (b->len >= b->cap) {
        b->cap = b->cap ? b->cap * 2 : 16;
        b->items = cetta_realloc(b->items, sizeof(ImportedFlatToken) * b->cap);
    }
    b->items[b->len++] = tok;
}

static void imported_flatten_atom(ImportedFlatBuilder *b, Atom *atom) {
    uint32_t start = b->len;
    ImportedFlatToken tok;
    tok.origin = atom;
    tok.span = 1;
    switch (atom->kind) {
    case ATOM_SYMBOL:
        tok.kind = IMPORTED_FLAT_SYMBOL;
        tok.name = atom->name;
        imported_builder_push(b, tok);
        return;
    case ATOM_VAR:
        tok.kind = IMPORTED_FLAT_VAR;
        tok.name = atom->name;
        tok.var_id = atom->var_id;
        imported_builder_push(b, tok);
        return;
    case ATOM_GROUNDED:
        if (atom->ground.gkind == GV_INT) {
            tok.kind = IMPORTED_FLAT_INT;
            tok.ival = atom->ground.ival;
        } else {
            tok.kind = IMPORTED_FLAT_GROUNDED_OTHER;
        }
        imported_builder_push(b, tok);
        return;
    case ATOM_EXPR:
        tok.kind = IMPORTED_FLAT_EXPR;
        tok.arity = atom->expr.len;
        imported_builder_push(b, tok);
        for (uint32_t i = 0; i < atom->expr.len; i++)
            imported_flatten_atom(b, atom->expr.elems[i]);
        b->items[start].span = b->len - start;
        return;
    }
}

static bool imported_token_equal(const ImportedFlatToken *lhs,
                                 const ImportedFlatToken *rhs) {
    if (lhs->kind != rhs->kind) return false;
    switch (lhs->kind) {
    case IMPORTED_FLAT_SYMBOL:
        return strcmp(lhs->name, rhs->name) == 0;
    case IMPORTED_FLAT_VAR:
        return lhs->var_id == rhs->var_id;
    case IMPORTED_FLAT_EXPR:
        return lhs->arity == rhs->arity;
    case IMPORTED_FLAT_INT:
        return lhs->ival == rhs->ival;
    case IMPORTED_FLAT_GROUNDED_OTHER:
        return atom_eq(lhs->origin, rhs->origin);
    }
    return false;
}

static bool imported_flat_equal(const ImportedFlatToken *lhs, uint32_t li,
                                const ImportedFlatToken *rhs, uint32_t ri) {
    if (lhs[li].span != rhs[ri].span) return false;
    for (uint32_t off = 0; off < lhs[li].span; off++) {
        if (!imported_token_equal(&lhs[li + off], &rhs[ri + off]))
            return false;
    }
    return true;
}

static ImportedCorefRef *imported_find_query_ref(ImportedCorefState *refs,
                                                 VarId var_id) {
    for (uint32_t i = 0; i < refs->nquery; i++)
        if (refs->query[i].var_id == var_id)
            return &refs->query[i];
    return NULL;
}

static ImportedCorefRef *imported_find_indexed_ref(ImportedCorefState *refs,
                                                   VarId var_id) {
    for (uint32_t i = 0; i < refs->nindexed; i++)
        if (refs->indexed[i].var_id == var_id)
            return &refs->indexed[i];
    return NULL;
}

static ImportedCorefRef *imported_find_indexed_value(ImportedCorefState *refs,
                                                     VarId var_id) {
    for (uint32_t i = 0; i < refs->nindexed_value; i++)
        if (refs->indexed_value[i].var_id == var_id)
            return &refs->indexed_value[i];
    return NULL;
}

static ImportedCorefVerdict imported_add_query_ref(ImportedCorefState *refs,
                                                   VarId var_id,
                                                   const char *name,
                                                   uint32_t idx,
                                                   uint32_t span,
                                                   Atom *origin) {
    if (refs->nquery >= IMPORTED_COREF_LIMIT)
        return IMPORTED_COREF_NEEDS_FALLBACK;
    refs->query[refs->nquery++] = (ImportedCorefRef){
        .var_id = var_id, .name = name, .idx = idx, .span = span, .origin = origin,
    };
    return IMPORTED_COREF_EXACT;
}

static ImportedCorefVerdict imported_add_indexed_ref(ImportedCorefState *refs,
                                                     VarId var_id,
                                                     const char *name,
                                                     uint32_t idx,
                                                     uint32_t span,
                                                     Atom *origin) {
    if (refs->nindexed >= IMPORTED_COREF_LIMIT)
        return IMPORTED_COREF_NEEDS_FALLBACK;
    refs->indexed[refs->nindexed++] = (ImportedCorefRef){
        .var_id = var_id, .name = name, .idx = idx, .span = span, .origin = origin,
    };
    return IMPORTED_COREF_EXACT;
}

static ImportedCorefVerdict imported_add_indexed_value(ImportedCorefState *refs,
                                                       VarId var_id,
                                                       const char *name,
                                                       uint32_t idx,
                                                       uint32_t span,
                                                       Atom *origin) {
    ImportedCorefRef *existing = imported_find_indexed_value(refs, var_id);
    if (existing) {
        existing->idx = idx;
        existing->span = span;
        existing->origin = origin;
        return IMPORTED_COREF_EXACT;
    }
    if (refs->nindexed_value >= IMPORTED_COREF_LIMIT)
        return IMPORTED_COREF_NEEDS_FALLBACK;
    refs->indexed_value[refs->nindexed_value++] = (ImportedCorefRef){
        .var_id = var_id, .name = name, .idx = idx, .span = span, .origin = origin,
    };
    return IMPORTED_COREF_EXACT;
}

static bool imported_subtree_contains_var(const ImportedFlatToken *tokens,
                                          uint32_t idx,
                                          VarId var_id) {
    uint32_t end = idx + tokens[idx].span;
    for (uint32_t i = idx; i < end; i++) {
        if (tokens[i].kind == IMPORTED_FLAT_VAR &&
            tokens[i].var_id == var_id)
            return true;
    }
    return false;
}

static ImportedCorefVerdict imported_bind_indexed_value(ImportedCorefState *refs,
                                                        const ImportedFlatToken *tokens,
                                                        uint32_t var_idx,
                                                        uint32_t value_idx) {
    const ImportedFlatToken *vt = &tokens[var_idx];
    const ImportedFlatToken *val = &tokens[value_idx];
    ImportedCorefRef *existing = imported_find_indexed_value(refs, vt->var_id);
    if (existing) {
        if (imported_flat_equal(tokens, existing->idx, tokens, value_idx))
            return IMPORTED_COREF_EXACT;
        return IMPORTED_COREF_NEEDS_FALLBACK;
    }
    if (val->kind == IMPORTED_FLAT_VAR) {
        ImportedCorefRef *other = imported_find_indexed_value(refs, val->var_id);
        if (other)
            return imported_bind_indexed_value(refs, tokens, var_idx, other->idx);
    }
    if (imported_subtree_contains_var(tokens, value_idx, vt->var_id))
        return IMPORTED_COREF_NEEDS_FALLBACK;
    return imported_add_indexed_value(refs, vt->var_id, vt->name,
                                      value_idx, val->span, val->origin);
}

static bool imported_materialize_bindings(const ImportedCorefState *refs,
                                          const ImportedFlatToken *qtokens,
                                          const ImportedFlatToken *ctokens,
                                          uint32_t epoch, Arena *a,
                                          Bindings *out) {
    bindings_init(out);
    for (uint32_t i = 0; i < refs->nquery; i++) {
        Atom *val = rename_vars(a, ctokens[refs->query[i].idx].origin, epoch);
        if (!bindings_add_id(out, refs->query[i].var_id, refs->query[i].name, val)) {
            bindings_free(out);
            return false;
        }
    }
    for (uint32_t i = 0; i < refs->nindexed; i++) {
        if (!bindings_add_id(out,
                             var_epoch_id(refs->indexed[i].var_id, epoch),
                             refs->indexed[i].name,
                             qtokens[refs->indexed[i].idx].origin)) {
            bindings_free(out);
            return false;
        }
    }
    for (uint32_t i = 0; i < refs->nindexed_value; i++) {
        Atom *val = rename_vars(a, ctokens[refs->indexed_value[i].idx].origin, epoch);
        if (!bindings_add_id(out,
                             var_epoch_id(refs->indexed_value[i].var_id, epoch),
                             refs->indexed_value[i].name, val)) {
            bindings_free(out);
            return false;
        }
    }
    return true;
}

static void imported_bucket_add_entry(ImportedFlatBucket *bucket, Atom *atom,
                                      uint32_t atom_idx, uint32_t epoch) {
    ImportedFlatBuilder b = {0};
    imported_flatten_atom(&b, atom);
    if (bucket->len >= bucket->cap) {
        bucket->cap = bucket->cap ? bucket->cap * 2 : 8;
        bucket->entries = cetta_realloc(bucket->entries,
                                        sizeof(ImportedFlatEntry) * bucket->cap);
    }
    bucket->entries[bucket->len].atom_idx = atom_idx;
    bucket->entries[bucket->len].epoch = epoch;
    bucket->entries[bucket->len].tokens = b.items;
    bucket->entries[bucket->len].len = b.len;
    bucket->len++;
}

static ImportedFlatBucket *imported_bucket_for_atom(PathmapImportedState *st, Atom *atom) {
    const char *head = atom_head_sym(atom);
    return head ? &st->buckets[stree_head_hash(head)] : &st->wildcard;
}

static void imported_rebuild(Space *s) {
    PathmapImportedState *st = &s->match_backend.imported;
    imported_state_free(st);
    for (uint32_t i = 0; i < s->len; i++) {
        ImportedFlatBucket *bucket = imported_bucket_for_atom(st, s->atoms[i]);
        imported_bucket_add_entry(bucket, s->atoms[i], i, stree_next_epoch());
    }
    st->built = true;
    st->dirty = false;
}

static void imported_ensure_built(Space *s) {
    PathmapImportedState *st = &s->match_backend.imported;
    if (!st->built || st->dirty)
        imported_rebuild(s);
}

static ImportedCorefVerdict imported_match_subtree_coref(const ImportedFlatToken *q,
                                                         uint32_t qi,
                                                         const ImportedFlatToken *c,
                                                         uint32_t ci,
                                                         ImportedCorefState *refs,
                                                         uint32_t *qnext,
                                                         uint32_t *cnext,
                                                         int depth) {
    if (depth <= 0) return false;
    const ImportedFlatToken *qt = &q[qi];
    const ImportedFlatToken *ct = &c[ci];

    if (qt->kind == IMPORTED_FLAT_VAR) {
        ImportedCorefRef *existing = imported_find_query_ref(refs, qt->var_id);
        if (existing) {
            if (!imported_flat_equal(c, existing->idx, c, ci)) {
                if (c[existing->idx].kind == IMPORTED_FLAT_VAR) {
                    ImportedCorefVerdict bind =
                        imported_bind_indexed_value(refs, c, existing->idx, ci);
                    if (bind != IMPORTED_COREF_EXACT)
                        return bind;
                } else if (ct->kind == IMPORTED_FLAT_VAR) {
                    ImportedCorefVerdict bind =
                        imported_bind_indexed_value(refs, c, ci, existing->idx);
                    if (bind != IMPORTED_COREF_EXACT)
                        return bind;
                } else {
                    return IMPORTED_COREF_NEEDS_FALLBACK;
                }
            }
        } else {
            ImportedCorefVerdict add =
                imported_add_query_ref(refs, qt->var_id, qt->name,
                                       ci, ct->span, ct->origin);
            if (add != IMPORTED_COREF_EXACT)
                return add;
        }
        *qnext = qi + qt->span;
        *cnext = ci + ct->span;
        return IMPORTED_COREF_EXACT;
    }

    if (ct->kind == IMPORTED_FLAT_VAR) {
        ImportedCorefRef *value = imported_find_indexed_value(refs, ct->var_id);
        if (value) {
            if (qt->kind == IMPORTED_FLAT_VAR) {
                ImportedCorefRef *existing_q = imported_find_query_ref(refs, qt->var_id);
                if (existing_q) {
                    if (!imported_flat_equal(c, existing_q->idx, c, value->idx))
                        return IMPORTED_COREF_NEEDS_FALLBACK;
                } else {
                    ImportedCorefVerdict add =
                        imported_add_query_ref(refs, qt->var_id, qt->name, value->idx,
                                               c[value->idx].span,
                                               c[value->idx].origin);
                    if (add != IMPORTED_COREF_EXACT)
                        return add;
                }
                *qnext = qi + qt->span;
                *cnext = ci + ct->span;
                return IMPORTED_COREF_EXACT;
            }
            return IMPORTED_COREF_NEEDS_FALLBACK;
        }
        ImportedCorefRef *existing = imported_find_indexed_ref(refs, ct->var_id);
        if (existing) {
            if (!imported_flat_equal(q, existing->idx, q, qi))
                return IMPORTED_COREF_NEEDS_FALLBACK;
        } else {
            ImportedCorefVerdict add =
                imported_add_indexed_ref(refs, ct->var_id, ct->name,
                                         qi, qt->span, qt->origin);
            if (add != IMPORTED_COREF_EXACT)
                return add;
        }
        *qnext = qi + qt->span;
        *cnext = ci + ct->span;
        return IMPORTED_COREF_EXACT;
    }

    if (qt->kind != ct->kind) return IMPORTED_COREF_FAIL;

    switch (qt->kind) {
    case IMPORTED_FLAT_SYMBOL:
        if (strcmp(qt->name, ct->name) != 0) return IMPORTED_COREF_FAIL;
        *qnext = qi + 1;
        *cnext = ci + 1;
        return IMPORTED_COREF_EXACT;
    case IMPORTED_FLAT_INT:
        if (qt->ival != ct->ival) return IMPORTED_COREF_FAIL;
        *qnext = qi + 1;
        *cnext = ci + 1;
        return IMPORTED_COREF_EXACT;
    case IMPORTED_FLAT_GROUNDED_OTHER:
        if (!atom_eq(qt->origin, ct->origin)) return IMPORTED_COREF_FAIL;
        *qnext = qi + 1;
        *cnext = ci + 1;
        return IMPORTED_COREF_EXACT;
    case IMPORTED_FLAT_EXPR: {
        if (qt->arity != ct->arity) return IMPORTED_COREF_FAIL;
        uint32_t qcur = qi + 1;
        uint32_t ccur = ci + 1;
        for (uint32_t i = 0; i < qt->arity; i++) {
            ImportedCorefVerdict child =
                imported_match_subtree_coref(q, qcur, c, ccur, refs,
                                             &qcur, &ccur, depth - 1);
            if (child != IMPORTED_COREF_EXACT)
                return child;
        }
        *qnext = qcur;
        *cnext = ccur;
        return IMPORTED_COREF_EXACT;
    }
    case IMPORTED_FLAT_VAR:
        return IMPORTED_COREF_FAIL;
    }
    return IMPORTED_COREF_FAIL;
}

static bool imported_match_subtree_legacy(const ImportedFlatToken *q, uint32_t qi,
                                          const ImportedFlatToken *c, uint32_t ci,
                                          Bindings *b, Arena *a, uint32_t epoch,
                                          uint32_t *qnext, uint32_t *cnext, int depth) {
    if (depth <= 0) return false;
    const ImportedFlatToken *qt = &q[qi];
    const ImportedFlatToken *ct = &c[ci];

    if (qt->kind == IMPORTED_FLAT_VAR) {
        Atom *existing = bindings_lookup_id(b, qt->var_id);
        if (existing) {
            if (!match_atoms_epoch(existing, ct->origin, b, a, epoch))
                return false;
        } else {
            if (!bindings_add_id(b, qt->var_id, qt->name,
                                 rename_vars(a, ct->origin, epoch)))
                return false;
        }
        *qnext = qi + qt->span;
        *cnext = ci + ct->span;
        return true;
    }

    if (ct->kind == IMPORTED_FLAT_VAR) {
        VarId tagged_id = var_epoch_id(ct->var_id, epoch);
        Atom *existing = bindings_lookup_id(b, tagged_id);
        if (existing) {
            if (!match_atoms(qt->origin, existing, b))
                return false;
        } else {
            if (!bindings_add_id(b, tagged_id, ct->name, qt->origin))
                return false;
        }
        *qnext = qi + qt->span;
        *cnext = ci + ct->span;
        return true;
    }

    if (qt->kind != ct->kind) return false;

    switch (qt->kind) {
    case IMPORTED_FLAT_SYMBOL:
        if (strcmp(qt->name, ct->name) != 0) return false;
        *qnext = qi + 1;
        *cnext = ci + 1;
        return true;
    case IMPORTED_FLAT_INT:
        if (qt->ival != ct->ival) return false;
        *qnext = qi + 1;
        *cnext = ci + 1;
        return true;
    case IMPORTED_FLAT_GROUNDED_OTHER:
        if (!atom_eq(qt->origin, ct->origin)) return false;
        *qnext = qi + 1;
        *cnext = ci + 1;
        return true;
    case IMPORTED_FLAT_EXPR: {
        if (qt->arity != ct->arity) return false;
        uint32_t qcur = qi + 1;
        uint32_t ccur = ci + 1;
        for (uint32_t i = 0; i < qt->arity; i++) {
            if (!imported_match_subtree_legacy(q, qcur, c, ccur, b, a, epoch,
                                               &qcur, &ccur, depth - 1))
                return false;
        }
        *qnext = qcur;
        *cnext = ccur;
        return true;
    }
    case IMPORTED_FLAT_VAR:
        return false;
    }
    return false;
}

static void imported_collect_bucket(const ImportedFlatBucket *bucket,
                                    const ImportedFlatToken *qtokens, uint32_t qlen,
                                    Arena *a, SubstMatchSet *out) {
    for (uint32_t i = 0; i < bucket->len; i++) {
        const ImportedFlatEntry *entry = &bucket->entries[i];
        if (entry->len == 0 || qlen == 0) continue;
        uint32_t qnext = 0, cnext = 0;
        ImportedCorefState refs = {0};
        ImportedCorefVerdict verdict =
            imported_match_subtree_coref(qtokens, 0, entry->tokens, 0, &refs,
                                         &qnext, &cnext, 64);
        if (verdict == IMPORTED_COREF_EXACT &&
            qnext == qlen && cnext == entry->len) {
            Bindings b;
            if (imported_materialize_bindings(&refs, qtokens, entry->tokens,
                                              entry->epoch, a, &b) &&
                !bindings_has_loop(&b)) {
                subst_matchset_push(out, entry->atom_idx, entry->epoch, &b, true);
                bindings_free(&b);
                continue;
            }
            bindings_free(&b);
            verdict = IMPORTED_COREF_NEEDS_FALLBACK;
        }
        if (verdict == IMPORTED_COREF_NEEDS_FALLBACK) {
            Bindings b;
            bindings_init(&b);
            qnext = 0;
            cnext = 0;
            if (imported_match_subtree_legacy(qtokens, 0, entry->tokens, 0, &b, a,
                                              entry->epoch, &qnext, &cnext, 64) &&
                qnext == qlen && cnext == entry->len &&
                !bindings_has_loop(&b)) {
                subst_matchset_push(out, entry->atom_idx, entry->epoch, &b, true);
            }
            bindings_free(&b);
        }
    }
}

static uint32_t imported_candidates(Space *s, Atom *pattern, uint32_t **out) {
    PathmapImportedState *st = &s->match_backend.imported;
    imported_ensure_built(s);
    *out = NULL;
    uint32_t len = 0, cap = 0;
    const char *head = atom_head_sym(pattern);
    if (head) {
        ImportedFlatBucket *bucket = &st->buckets[stree_head_hash(head)];
        for (uint32_t i = 0; i < bucket->len; i++) {
            if (len >= cap) {
                cap = cap ? cap * 2 : 8;
                *out = cetta_realloc(*out, sizeof(uint32_t) * cap);
            }
            (*out)[len++] = bucket->entries[i].atom_idx;
        }
    } else {
        for (uint32_t bi = 0; bi < STREE_BUCKETS; bi++) {
            ImportedFlatBucket *bucket = &st->buckets[bi];
            for (uint32_t i = 0; i < bucket->len; i++) {
                if (len >= cap) {
                    cap = cap ? cap * 2 : 8;
                    *out = cetta_realloc(*out, sizeof(uint32_t) * cap);
                }
                (*out)[len++] = bucket->entries[i].atom_idx;
            }
        }
    }
    for (uint32_t i = 0; i < st->wildcard.len; i++) {
        if (len >= cap) {
            cap = cap ? cap * 2 : 8;
            *out = cetta_realloc(*out, sizeof(uint32_t) * cap);
        }
        (*out)[len++] = st->wildcard.entries[i].atom_idx;
    }
    if (len > 1)
        qsort(*out, len, sizeof(uint32_t), cmp_uint32);
    return len;
}

static void imported_query(Space *s, Arena *a, Atom *query, SubstMatchSet *out) {
    PathmapImportedState *st = &s->match_backend.imported;
    ImportedFlatBuilder q = {0};
    smset_init(out);
    if (s->len == 0) return;
    imported_ensure_built(s);
    imported_flatten_atom(&q, query);
    const char *head = atom_head_sym(query);
    if (head) {
        imported_collect_bucket(&st->buckets[stree_head_hash(head)],
                                q.items, q.len, a, out);
    } else {
        for (uint32_t bi = 0; bi < STREE_BUCKETS; bi++)
            imported_collect_bucket(&st->buckets[bi], q.items, q.len, a, out);
    }
    imported_collect_bucket(&st->wildcard, q.items, q.len, a, out);
    free(q.items);

    if (out->len > 1) {
        for (uint32_t i = 1; i < out->len; i++) {
            SubstMatch tmp;
            subst_match_move(&tmp, &out->items[i]);
            uint32_t j = i;
            while (j > 0 &&
                   (out->items[j - 1].atom_idx > tmp.atom_idx ||
                    (out->items[j - 1].atom_idx == tmp.atom_idx &&
                     out->items[j - 1].epoch > tmp.epoch))) {
                subst_match_move(&out->items[j], &out->items[j - 1]);
                j--;
            }
            subst_match_move(&out->items[j], &tmp);
        }
        uint32_t w = 1;
        for (uint32_t r = 1; r < out->len; r++) {
            if (!subst_match_same(&out->items[r], &out->items[r - 1])) {
                if (w != r)
                    subst_match_move(&out->items[w], &out->items[r]);
                w++;
            } else {
                bindings_free(&out->items[r].bindings);
            }
        }
        out->len = w;
    }
}

static void imported_note_add(Space *s, Atom *atom, uint32_t atom_idx) {
    PathmapImportedState *st = &s->match_backend.imported;
    if (!st->built || st->dirty) return;
    imported_bucket_add_entry(imported_bucket_for_atom(st, atom), atom, atom_idx,
                              stree_next_epoch());
}

static void imported_note_remove(Space *s) {
    PathmapImportedState *st = &s->match_backend.imported;
    (void)s;
    if (st->built)
        st->dirty = true;
}

static void imported_free_backend(Space *s) {
    imported_state_free(&s->match_backend.imported);
}

static void native_candidate_exact_query(Space *s, Arena *a, Atom *query,
                                         SubstMatchSet *out) {
    smset_init(out);
    if (s->len == 0) return;
    uint32_t *candidates = NULL;
    uint32_t ncand = native_candidates(s, query, &candidates);
    for (uint32_t ci = 0; ci < ncand; ci++) {
        uint32_t idx = candidates[ci];
        if (idx >= s->len) continue;
        uint32_t epoch = fresh_var_suffix();
        Bindings b;
        bindings_init(&b);
        if (match_atoms_epoch(query, s->atoms[idx], &b, a, epoch) &&
            !bindings_has_loop(&b)) {
            subst_matchset_push(out, idx, epoch, &b, false);
        }
        bindings_free(&b);
    }
    free(candidates);
}

typedef struct {
    Atom *pattern;
    uint32_t idx;
    uint32_t estimate;
} ImportedConjStep;

static uint32_t imported_pattern_estimate(Space *s, Atom *pattern) {
    PathmapImportedState *st = &s->match_backend.imported;
    imported_ensure_built(s);
    const char *head = atom_head_sym(pattern);
    if (head)
        return st->buckets[stree_head_hash(head)].len + st->wildcard.len;
    uint32_t total = st->wildcard.len;
    for (uint32_t bi = 0; bi < STREE_BUCKETS; bi++)
        total += st->buckets[bi].len;
    return total;
}

static int imported_cmp_conj_step(const void *lhs, const void *rhs) {
    const ImportedConjStep *a = lhs;
    const ImportedConjStep *b = rhs;
    if (a->estimate != b->estimate)
        return (a->estimate > b->estimate) - (a->estimate < b->estimate);
    return (a->idx > b->idx) - (a->idx < b->idx);
}

static void space_query_conjunction_default(Space *s, Arena *a,
                                            Atom **patterns, uint32_t npatterns,
                                            const Bindings *seed, BindingSet *out) {
    binding_set_init(out);
    if (npatterns == 0) {
        if (seed) binding_set_push(out, seed);
        return;
    }

    BindingSet cur;
    binding_set_init(&cur);
    if (seed) {
        binding_set_push(&cur, seed);
    } else {
        Bindings empty;
        bindings_init(&empty);
        binding_set_push(&cur, &empty);
    }

    for (uint32_t pi = 0; pi < npatterns; pi++) {
        BindingSet next;
        binding_set_init(&next);
        for (uint32_t bi = 0; bi < cur.len; bi++) {
            Atom *grounded = bindings_apply(&cur.items[bi], a, patterns[pi]);
            SubstMatchSet smr;
            smset_init(&smr);
            space_subst_query(s, a, grounded, &smr);
            for (uint32_t mi = 0; mi < smr.len; mi++) {
                Bindings merged;
                if (space_subst_match_with_seed(s, grounded, &smr.items[mi],
                                                &cur.items[bi], a, &merged)) {
                    binding_set_push(&next, &merged);
                    bindings_free(&merged);
                }
            }
            smset_free(&smr);
        }
        binding_set_free(&cur);
        cur = next;
        if (cur.len == 0) break;
    }

    *out = cur;
}

static void imported_query_conjunction(Space *s, Arena *a, Atom **patterns,
                                       uint32_t npatterns, const Bindings *seed,
                                       BindingSet *out) {
    if (npatterns == 0) {
        binding_set_init(out);
        if (seed) binding_set_push(out, seed);
        return;
    }

    ImportedConjStep order[32];
    if (npatterns > 32) {
        space_query_conjunction_default(s, a, patterns, npatterns, seed, out);
        return;
    }
    for (uint32_t i = 0; i < npatterns; i++) {
        order[i].pattern = patterns[i];
        order[i].idx = i;
        order[i].estimate = imported_pattern_estimate(s, patterns[i]);
    }
    qsort(order, npatterns, sizeof(ImportedConjStep), imported_cmp_conj_step);

    BindingSet cur;
    binding_set_init(&cur);
    if (seed) {
        binding_set_push(&cur, seed);
    } else {
        Bindings empty;
        bindings_init(&empty);
        binding_set_push(&cur, &empty);
    }

    for (uint32_t pi = 0; pi < npatterns; pi++) {
        BindingSet next;
        binding_set_init(&next);
        for (uint32_t bi = 0; bi < cur.len; bi++) {
            Atom *grounded = bindings_apply(&cur.items[bi], a, order[pi].pattern);
            SubstMatchSet smr;
            smset_init(&smr);
            space_subst_query(s, a, grounded, &smr);
            for (uint32_t mi = 0; mi < smr.len; mi++) {
                Bindings merged;
                if (space_subst_match_with_seed(s, grounded, &smr.items[mi],
                                                &cur.items[bi], a, &merged)) {
                    binding_set_push(&next, &merged);
                    bindings_free(&merged);
                }
            }
            smset_free(&smr);
        }
        binding_set_free(&cur);
        cur = next;
        if (cur.len == 0) break;
    }
    *out = cur;
}

static const SpaceMatchBackendOps NATIVE_BACKEND_OPS = {
    .name = "native-subst-tree",
    .supports_direct_bindings = true,
    .free = native_free,
    .note_add = native_note_add,
    .note_remove = native_note_remove,
    .candidates = native_candidates,
    .query = native_query,
    .query_conjunction = NULL,
};

static const SpaceMatchBackendOps NATIVE_CANDIDATE_EXACT_BACKEND_OPS = {
    .name = "native-candidate-exact",
    .supports_direct_bindings = true,
    .free = native_free,
    .note_add = native_note_add,
    .note_remove = native_note_remove,
    .candidates = native_candidates,
    .query = native_candidate_exact_query,
    .query_conjunction = NULL,
};

static const SpaceMatchBackendOps IMPORTED_BACKEND_OPS = {
    .name = "pathmap-imported",
    .supports_direct_bindings = true,
    .free = imported_free_backend,
    .note_add = imported_note_add,
    .note_remove = imported_note_remove,
    .candidates = imported_candidates,
    .query = imported_query,
    .query_conjunction = imported_query_conjunction,
};

void space_match_backend_init(Space *s) {
    s->match_backend.kind = SPACE_MATCH_BACKEND_NATIVE;
    s->match_backend.ops = &NATIVE_BACKEND_OPS;
    s->match_backend.native.match_trie = NULL;
    s->match_backend.native.match_trie_dirty = false;
    s->match_backend.native.stree = NULL;
    s->match_backend.native.stree_dirty = false;
    memset(&s->match_backend.imported, 0, sizeof(s->match_backend.imported));
}

void space_match_backend_free(Space *s) {
    if (s->match_backend.ops && s->match_backend.ops->free)
        s->match_backend.ops->free(s);
}

bool space_match_backend_try_set(Space *s, SpaceMatchBackendKind kind) {
    const SpaceMatchBackendOps *ops = NULL;
    switch (kind) {
    case SPACE_MATCH_BACKEND_NATIVE:
        ops = &NATIVE_BACKEND_OPS;
        break;
    case SPACE_MATCH_BACKEND_NATIVE_CANDIDATE_EXACT:
        ops = &NATIVE_CANDIDATE_EXACT_BACKEND_OPS;
        break;
    case SPACE_MATCH_BACKEND_PATHMAP_IMPORTED:
        ops = &IMPORTED_BACKEND_OPS;
        break;
    default:
        return false;
    }
    space_match_backend_free(s);
    space_match_backend_init(s);
    s->match_backend.kind = kind;
    s->match_backend.ops = ops;
    return true;
}

void space_match_backend_note_add(Space *s, Atom *atom, uint32_t atom_idx) {
    if (s->match_backend.ops && s->match_backend.ops->note_add)
        s->match_backend.ops->note_add(s, atom, atom_idx);
}

void space_match_backend_note_remove(Space *s) {
    if (s->match_backend.ops && s->match_backend.ops->note_remove)
        s->match_backend.ops->note_remove(s);
}

uint32_t space_match_backend_candidates(Space *s, Atom *pattern, uint32_t **out) {
    if (!s->match_backend.ops || !s->match_backend.ops->candidates) {
        *out = NULL;
        return 0;
    }
    return s->match_backend.ops->candidates(s, pattern, out);
}

void space_match_backend_query(Space *s, Arena *a, Atom *query, SubstMatchSet *out) {
    if (!s->match_backend.ops || !s->match_backend.ops->query) {
        smset_init(out);
        return;
    }
    s->match_backend.ops->query(s, a, query, out);
}

const char *space_match_backend_name(const Space *s) {
    if (!s->match_backend.ops || !s->match_backend.ops->name)
        return "unconfigured";
    return s->match_backend.ops->name;
}

bool space_match_backend_supports_direct_bindings(const Space *s) {
    return s->match_backend.ops && s->match_backend.ops->supports_direct_bindings;
}

const char *space_match_backend_kind_name(SpaceMatchBackendKind kind) {
    switch (kind) {
    case SPACE_MATCH_BACKEND_NATIVE:
        return "native-subst-tree";
    case SPACE_MATCH_BACKEND_NATIVE_CANDIDATE_EXACT:
        return "native-candidate-exact";
    case SPACE_MATCH_BACKEND_PATHMAP_IMPORTED:
        return "pathmap-imported";
    default:
        return "unknown";
    }
}

bool space_match_backend_kind_from_name(const char *name, SpaceMatchBackendKind *out) {
    if (strcmp(name, "native-subst-tree") == 0) {
        *out = SPACE_MATCH_BACKEND_NATIVE;
        return true;
    }
    if (strcmp(name, "native-candidate-exact") == 0) {
        *out = SPACE_MATCH_BACKEND_NATIVE_CANDIDATE_EXACT;
        return true;
    }
    if (strcmp(name, "pathmap-imported") == 0) {
        *out = SPACE_MATCH_BACKEND_PATHMAP_IMPORTED;
        return true;
    }
    return false;
}

void space_match_backend_print_inventory(FILE *out) {
    fprintf(out, "space match backends:\n");
    fprintf(out, "  native-subst-tree      direct bindings via substitution tree fast-path\n");
    fprintf(out, "  native-candidate-exact exact matcher over trie-pruned candidates\n");
    fprintf(out, "  pathmap-imported       flat/path imported matcher with direct bindings + coref fast-path\n");
}

uint32_t space_match_candidates(Space *s, Atom *pattern, uint32_t **out) {
    return space_match_backend_candidates(s, pattern, out);
}

void space_subst_query(Space *s, Arena *a, Atom *query, SubstMatchSet *out) {
    space_match_backend_query(s, a, query, out);
}

bool space_subst_match_with_seed(Space *space, Atom *pattern, const SubstMatch *sm,
                                 const Bindings *seed, Arena *a, Bindings *out) {
    if (sm->atom_idx >= space->len) return false;
    Bindings merged;
    if (!bindings_clone(&merged, seed)) return false;
    if (!bindings_try_merge(&merged, &sm->bindings)) {
        bindings_free(&merged);
        return false;
    }
    if (sm->exact) {
        if (bindings_has_loop(&merged)) {
            bindings_free(&merged);
            return false;
        }
        bindings_move(out, &merged);
        return true;
    }
    if (match_atoms_epoch(pattern, space->atoms[sm->atom_idx], &merged, a, sm->epoch) &&
        !bindings_has_loop(&merged)) {
        bindings_move(out, &merged);
        return true;
    }
    bindings_free(&merged);

    {
        uint32_t suffix = fresh_var_suffix();
        Atom *renamed = rename_vars(a, space->atoms[sm->atom_idx], suffix);
        Bindings exact;
        if (!bindings_clone(&exact, seed))
            return false;
        if (match_atoms(pattern, renamed, &exact) && !bindings_has_loop(&exact)) {
            bindings_move(out, &exact);
            return true;
        }
        bindings_free(&exact);
    }
    return false;
}

void space_query_conjunction(Space *s, Arena *a, Atom **patterns, uint32_t npatterns,
                             const Bindings *seed, BindingSet *out) {
    if (s->match_backend.ops && s->match_backend.ops->query_conjunction) {
        s->match_backend.ops->query_conjunction(s, a, patterns, npatterns, seed, out);
        return;
    }
    space_query_conjunction_default(s, a, patterns, npatterns, seed, out);
}
