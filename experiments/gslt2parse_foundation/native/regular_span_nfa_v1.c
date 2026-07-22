#include "regular_span_nfa_v1.h"

#include "finite_horn_ground_term_v1.h"
#include "parser_pack_native_v1.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    Atom *atom;
    char *canonical;
} RSNFAV1CanonicalAtom;

typedef struct {
    RSNFAV1CanonicalAtom *data;
    uint32_t len;
    uint32_t cap;
} RSNFAV1CanonicalVec;

static void rsnfa_v1_set_error(char *buf,
                               size_t size,
                               const char *format,
                               ...) {
    va_list arguments;

    if (!buf || size == 0u)
        return;
    va_start(arguments, format);
    (void)vsnprintf(buf, size, format, arguments);
    va_end(arguments);
}

static bool rsnfa_v1_expr_head(Atom *term,
                               const char *head,
                               CettaExprLen argument_len) {
    return term && term->kind == ATOM_EXPR &&
           term->expr.len == argument_len + 1u &&
           atom_is_symbol(term->expr.elems[0], head);
}

static bool rsnfa_v1_grow(void **data,
                          uint32_t *cap,
                          uint32_t needed,
                          size_t item_size) {
    uint32_t next_cap;
    void *next;

    if (needed <= *cap)
        return true;
    next_cap = *cap ? *cap : 16u;
    while (next_cap < needed) {
        if (next_cap > UINT32_MAX / 2u)
            return false;
        next_cap *= 2u;
    }
    if ((size_t)next_cap > SIZE_MAX / item_size)
        return false;
    next = realloc(*data, (size_t)next_cap * item_size);
    if (!next)
        return false;
    *data = next;
    *cap = next_cap;
    return true;
}

static char *rsnfa_v1_canonical(Atom *term) {
    uint8_t *bytes = NULL;
    size_t len = 0u;

    if (!fh_ground_term_v1_render(term, &bytes, &len, NULL, 0u))
        return NULL;
    return (char *)bytes;
}

static bool rsnfa_v1_canonical_push(RSNFAV1CanonicalVec *vec,
                                    Atom *atom) {
    char *canonical;

    if (!rsnfa_v1_grow(
            (void **)&vec->data, &vec->cap,
            vec->len + 1u, sizeof(*vec->data))) {
        return false;
    }
    canonical = rsnfa_v1_canonical(atom);
    if (!canonical)
        return false;
    vec->data[vec->len++] = (RSNFAV1CanonicalAtom){
        .atom = atom,
        .canonical = canonical,
    };
    return true;
}

static int rsnfa_v1_canonical_compare(const void *left,
                                      const void *right) {
    const RSNFAV1CanonicalAtom *lhs = left;
    const RSNFAV1CanonicalAtom *rhs = right;
    return strcmp(lhs->canonical, rhs->canonical);
}

static bool rsnfa_v1_canonicalize(RSNFAV1CanonicalVec *vec,
                                  char *error_buf,
                                  size_t error_buf_size) {
    uint32_t read;
    uint32_t write = 0u;

    if (vec->len > 1u) {
        qsort(vec->data, vec->len,
              sizeof(*vec->data), rsnfa_v1_canonical_compare);
    }
    for (read = 0u; read < vec->len; read++) {
        if (write > 0u &&
            strcmp(vec->data[read].canonical,
                   vec->data[write - 1u].canonical) == 0) {
            if (!atom_eq(vec->data[read].atom,
                         vec->data[write - 1u].atom)) {
                rsnfa_v1_set_error(error_buf, error_buf_size,
                                   "canonical regular-span term collision");
                return false;
            }
            free(vec->data[read].canonical);
            continue;
        }
        if (write != read)
            vec->data[write] = vec->data[read];
        write++;
    }
    vec->len = write;
    return true;
}

static void rsnfa_v1_canonical_vec_free(RSNFAV1CanonicalVec *vec) {
    uint32_t index;
    for (index = 0u; index < vec->len; index++)
        free(vec->data[index].canonical);
    free(vec->data);
    memset(vec, 0, sizeof(*vec));
}

static int32_t rsnfa_v1_canonical_find(const RSNFAV1CanonicalVec *vec,
                                       Atom *atom) {
    char *canonical = rsnfa_v1_canonical(atom);
    uint32_t low = 0u;
    uint32_t high = vec->len;
    int32_t result = -1;

    if (!canonical)
        return -1;
    while (low < high) {
        uint32_t middle = low + (high - low) / 2u;
        int comparison = strcmp(canonical, vec->data[middle].canonical);
        if (comparison < 0) {
            high = middle;
        } else if (comparison > 0) {
            low = middle + 1u;
        } else {
            if (atom_eq(atom, vec->data[middle].atom))
                result = (int32_t)middle;
            break;
        }
    }
    free(canonical);
    return result;
}

static bool rsnfa_v1_codepoint(Atom *term, uint32_t *codepoint) {
    int64_t value;

    if (!rsnfa_v1_expr_head(term, "cp", 1u) ||
        !term->expr.elems[1] ||
        term->expr.elems[1]->kind != ATOM_GROUNDED ||
        term->expr.elems[1]->ground.gkind != GV_INT) {
        return false;
    }
    value = term->expr.elems[1]->ground.ival;
    if (value < 0 || value > INT64_C(0x10ffff) ||
        (value >= INT64_C(0xd800) && value <= INT64_C(0xdfff))) {
        return false;
    }
    *codepoint = (uint32_t)value;
    return true;
}

static bool rsnfa_v1_collect_term_shape(
    Atom *answer,
    RSNFAV1CanonicalVec *states,
    RSNFAV1CanonicalVec *tags,
    uint32_t *start_len,
    uint32_t *edge_len,
    uint32_t *accept_len,
    char *error_buf,
    size_t error_buf_size) {
    Atom *tag;
    Atom *edge;

    if (!rsnfa_v1_expr_head(answer, "compile-span-nfa", 2u)) {
        rsnfa_v1_set_error(error_buf, error_buf_size,
                           "expected compile-span-nfa answer");
        return false;
    }
    tag = answer->expr.elems[1];
    edge = answer->expr.elems[2];
    if (!rsnfa_v1_canonical_push(tags, tag))
        goto allocation_error;
    if (rsnfa_v1_expr_head(edge, "nfa-start", 1u)) {
        if (!rsnfa_v1_canonical_push(states, edge->expr.elems[1]))
            goto allocation_error;
        (*start_len)++;
        return true;
    }
    if (rsnfa_v1_expr_head(edge, "nfa-accept", 2u)) {
        if (!atom_eq(tag, edge->expr.elems[2])) {
            rsnfa_v1_set_error(error_buf, error_buf_size,
                               "NFA accept tag differs from answer tag");
            return false;
        }
        if (!rsnfa_v1_canonical_push(states, edge->expr.elems[1]))
            goto allocation_error;
        (*accept_len)++;
        return true;
    }
    if (rsnfa_v1_expr_head(edge, "nfa-epsilon", 2u) ||
        rsnfa_v1_expr_head(edge, "nfa-any", 2u) ||
        rsnfa_v1_expr_head(edge, "nfa-eof", 2u)) {
        if (!rsnfa_v1_canonical_push(states, edge->expr.elems[1]) ||
            !rsnfa_v1_canonical_push(states, edge->expr.elems[2])) {
            goto allocation_error;
        }
        (*edge_len)++;
        return true;
    }
    if (rsnfa_v1_expr_head(edge, "nfa-char", 3u) ||
        rsnfa_v1_expr_head(edge, "nfa-class", 3u)) {
        if (!rsnfa_v1_canonical_push(states, edge->expr.elems[1]) ||
            !rsnfa_v1_canonical_push(states, edge->expr.elems[3])) {
            goto allocation_error;
        }
        (*edge_len)++;
        return true;
    }
    rsnfa_v1_set_error(error_buf, error_buf_size,
                       "unknown compile-span-nfa edge");
    return false;

allocation_error:
    rsnfa_v1_set_error(error_buf, error_buf_size,
                       "failed to allocate canonical NFA identities");
    return false;
}

void rsnfa_v1_plan_init(RSNFAV1Plan *plan) {
    if (plan)
        memset(plan, 0, sizeof(*plan));
}

void rsnfa_v1_plan_free(RSNFAV1Plan *plan) {
    uint32_t index;

    if (!plan)
        return;
    for (index = 0u; index < plan->nfa.edge_len; index++)
        free(plan->owned_ranges ? plan->owned_ranges[index] : NULL);
    for (index = 0u; index < plan->nfa.tag_len; index++)
        free(plan->tag_canonical ? plan->tag_canonical[index] : NULL);
    for (index = 0u; index < plan->nfa.state_len; index++)
        free(plan->state_canonical ? plan->state_canonical[index] : NULL);
    free(plan->tags);
    free(plan->tag_canonical);
    free(plan->states);
    free(plan->state_canonical);
    free(plan->start_states);
    free(plan->edges);
    free(plan->owned_ranges);
    free(plan->accepts);
    memset(plan, 0, sizeof(*plan));
}

static bool rsnfa_v1_copy_identities(
    RSNFAV1CanonicalVec *source,
    Atom ***out_atoms,
    char ***out_canonical) {
    Atom **atoms;
    char **canonical;
    uint32_t index;

    atoms = calloc(source->len ? source->len : 1u, sizeof(*atoms));
    canonical = calloc(source->len ? source->len : 1u,
                       sizeof(*canonical));
    if (!atoms || !canonical) {
        free(atoms);
        free(canonical);
        return false;
    }
    for (index = 0u; index < source->len; index++) {
        atoms[index] = source->data[index].atom;
        canonical[index] = source->data[index].canonical;
        source->data[index].canonical = NULL;
    }
    *out_atoms = atoms;
    *out_canonical = canonical;
    return true;
}

static bool rsnfa_v1_fill_term(
    const PPABIV1Pack *class_pack,
    Atom *answer,
    const RSNFAV1CanonicalVec *states,
    const RSNFAV1CanonicalVec *tags,
    RSNFAV1Plan *plan,
    uint32_t *start_index,
    uint32_t *edge_index,
    uint32_t *accept_index,
    uint32_t *tag_start_counts,
    uint32_t *tag_accept_counts,
    char *error_buf,
    size_t error_buf_size) {
    Atom *tag = answer->expr.elems[1];
    Atom *edge = answer->expr.elems[2];
    int32_t tag_id = rsnfa_v1_canonical_find(tags, tag);
    int32_t from;
    int32_t to;

    if (tag_id < 0)
        goto identity_error;
    if (rsnfa_v1_expr_head(edge, "nfa-start", 1u)) {
        from = rsnfa_v1_canonical_find(states, edge->expr.elems[1]);
        if (from < 0)
            goto identity_error;
        plan->start_states[(*start_index)++] = (uint32_t)from;
        tag_start_counts[tag_id]++;
        return true;
    }
    if (rsnfa_v1_expr_head(edge, "nfa-accept", 2u)) {
        from = rsnfa_v1_canonical_find(states, edge->expr.elems[1]);
        if (from < 0)
            goto identity_error;
        plan->accepts[(*accept_index)++] = (RSDFAV1NfaAccept){
            .state = (uint32_t)from,
            .tag = (uint32_t)tag_id,
        };
        tag_accept_counts[tag_id]++;
        return true;
    }

    from = rsnfa_v1_canonical_find(states, edge->expr.elems[1]);
    to = rsnfa_v1_canonical_find(
        states,
        edge->expr.elems[edge->expr.len - 1u]);
    if (from < 0 || to < 0)
        goto identity_error;
    plan->edges[*edge_index].from = (uint32_t)from;
    plan->edges[*edge_index].to = (uint32_t)to;
    if (rsnfa_v1_expr_head(edge, "nfa-epsilon", 2u)) {
        plan->edges[*edge_index].kind = RSDFA_V1_NFA_EPSILON;
    } else if (rsnfa_v1_expr_head(edge, "nfa-eof", 2u)) {
        plan->edges[*edge_index].kind = RSDFA_V1_NFA_EOF;
    } else if (rsnfa_v1_expr_head(edge, "nfa-any", 2u)) {
        CettaLpNativeUnicodeRange *ranges =
            malloc(2u * sizeof(*ranges));
        if (!ranges)
            goto allocation_error;
        ranges[0] = (CettaLpNativeUnicodeRange){0u, 0xd7ffu};
        ranges[1] =
            (CettaLpNativeUnicodeRange){0xe000u, 0x10ffffu};
        plan->edges[*edge_index].kind = RSDFA_V1_NFA_RANGES;
        plan->edges[*edge_index].ranges = ranges;
        plan->edges[*edge_index].range_len = 2u;
        plan->owned_ranges[*edge_index] = ranges;
    } else if (rsnfa_v1_expr_head(edge, "nfa-char", 3u)) {
        CettaLpNativeUnicodeRange *ranges = malloc(sizeof(*ranges));
        uint32_t codepoint;
        if (!ranges)
            goto allocation_error;
        if (!rsnfa_v1_codepoint(edge->expr.elems[2], &codepoint)) {
            free(ranges);
            rsnfa_v1_set_error(error_buf, error_buf_size,
                               "NFA character is not a Unicode scalar");
            return false;
        }
        ranges[0] = (CettaLpNativeUnicodeRange){codepoint, codepoint};
        plan->edges[*edge_index].kind = RSDFA_V1_NFA_RANGES;
        plan->edges[*edge_index].ranges = ranges;
        plan->edges[*edge_index].range_len = 1u;
        plan->owned_ranges[*edge_index] = ranges;
    } else if (rsnfa_v1_expr_head(edge, "nfa-class", 3u)) {
        CettaLpNativeUnicodeRange *ranges = NULL;
        uint32_t range_len = 0u;
        if (!ppnative_v1_class_ranges_build(
                class_pack, edge->expr.elems[2],
                &ranges, &range_len,
                error_buf, error_buf_size)) {
            return false;
        }
        plan->edges[*edge_index].kind = RSDFA_V1_NFA_RANGES;
        plan->edges[*edge_index].ranges = ranges;
        plan->edges[*edge_index].range_len = range_len;
        plan->owned_ranges[*edge_index] = ranges;
    } else {
        rsnfa_v1_set_error(error_buf, error_buf_size,
                           "unknown NFA edge during materialization");
        return false;
    }
    (*edge_index)++;
    return true;

identity_error:
    rsnfa_v1_set_error(error_buf, error_buf_size,
                       "NFA identity disappeared during canonicalization");
    return false;
allocation_error:
    rsnfa_v1_set_error(error_buf, error_buf_size,
                       "failed to allocate NFA range");
    return false;
}

bool rsnfa_v1_plan_load(const PPABIV1Pack *class_pack,
                        Atom *const *answer_terms,
                        size_t answer_len,
                        RSNFAV1Plan *out,
                        char *error_buf,
                        size_t error_buf_size) {
    RSNFAV1CanonicalVec states = {0};
    RSNFAV1CanonicalVec tags = {0};
    RSNFAV1Plan plan;
    uint32_t start_len = 0u;
    uint32_t edge_len = 0u;
    uint32_t accept_len = 0u;
    uint32_t start_index = 0u;
    uint32_t edge_index = 0u;
    uint32_t accept_index = 0u;
    uint32_t *tag_start_counts = NULL;
    uint32_t *tag_accept_counts = NULL;
    size_t index;
    bool ok = false;

    rsnfa_v1_plan_init(&plan);
    if (!class_pack || !out || !answer_terms || answer_len == 0u ||
        answer_len > UINT32_MAX) {
        rsnfa_v1_set_error(error_buf, error_buf_size,
                           "bad regular-span NFA answer set");
        return false;
    }
    for (index = 0u; index < answer_len; index++) {
        size_t previous;
        if (!answer_terms[index]) {
            rsnfa_v1_set_error(error_buf, error_buf_size,
                               "null regular-span NFA answer");
            goto done;
        }
        for (previous = 0u; previous < index; previous++) {
            if (atom_eq(answer_terms[index], answer_terms[previous])) {
                rsnfa_v1_set_error(error_buf, error_buf_size,
                                   "duplicate regular-span NFA answer");
                goto done;
            }
        }
        if (!rsnfa_v1_collect_term_shape(
                answer_terms[index], &states, &tags,
                &start_len, &edge_len, &accept_len,
                error_buf, error_buf_size)) {
            goto done;
        }
    }
    if (!rsnfa_v1_canonicalize(
            &states, error_buf, error_buf_size) ||
        !rsnfa_v1_canonicalize(
            &tags, error_buf, error_buf_size)) {
        goto done;
    }
    if (states.len == 0u || tags.len == 0u ||
        start_len == 0u || accept_len == 0u) {
        rsnfa_v1_set_error(error_buf, error_buf_size,
                           "regular-span NFA lacks states, tags, or endpoints");
        goto done;
    }
    plan.start_states = calloc(start_len, sizeof(*plan.start_states));
    plan.edges = calloc(edge_len ? edge_len : 1u, sizeof(*plan.edges));
    plan.owned_ranges = calloc(
        edge_len ? edge_len : 1u, sizeof(*plan.owned_ranges));
    plan.accepts = calloc(accept_len, sizeof(*plan.accepts));
    tag_start_counts = calloc(tags.len, sizeof(*tag_start_counts));
    tag_accept_counts = calloc(tags.len, sizeof(*tag_accept_counts));
    if (!plan.start_states || !plan.edges || !plan.owned_ranges ||
        !plan.accepts || !tag_start_counts || !tag_accept_counts) {
        rsnfa_v1_set_error(error_buf, error_buf_size,
                           "failed to allocate regular-span NFA plan");
        goto done;
    }
    plan.nfa.state_len = states.len;
    plan.nfa.start_states = plan.start_states;
    plan.nfa.start_len = start_len;
    plan.nfa.edges = plan.edges;
    plan.nfa.edge_len = edge_len;
    plan.nfa.accepts = plan.accepts;
    plan.nfa.accept_len = accept_len;
    plan.nfa.tag_len = tags.len;
    for (index = 0u; index < answer_len; index++) {
        if (!rsnfa_v1_fill_term(
                class_pack, answer_terms[index], &states, &tags,
                &plan, &start_index, &edge_index, &accept_index,
                tag_start_counts, tag_accept_counts,
                error_buf, error_buf_size)) {
            goto done;
        }
    }
    if (start_index != start_len || edge_index != edge_len ||
        accept_index != accept_len) {
        rsnfa_v1_set_error(error_buf, error_buf_size,
                           "regular-span NFA record counts changed");
        goto done;
    }
    for (index = 0u; index < tags.len; index++) {
        if (tag_start_counts[index] == 0u ||
            tag_accept_counts[index] == 0u) {
            rsnfa_v1_set_error(error_buf, error_buf_size,
                               "regular-span tag lacks start or accept");
            goto done;
        }
    }
    if (!rsnfa_v1_copy_identities(
            &states, &plan.states, &plan.state_canonical) ||
        !rsnfa_v1_copy_identities(
            &tags, &plan.tags, &plan.tag_canonical)) {
        rsnfa_v1_set_error(error_buf, error_buf_size,
                           "failed to retain regular-span identities");
        goto done;
    }
    rsnfa_v1_plan_free(out);
    *out = plan;
    memset(&plan, 0, sizeof(plan));
    ok = true;

done:
    free(tag_start_counts);
    free(tag_accept_counts);
    rsnfa_v1_plan_free(&plan);
    rsnfa_v1_canonical_vec_free(&states);
    rsnfa_v1_canonical_vec_free(&tags);
    return ok;
}
