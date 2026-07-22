#include "semantic_mask_nfa_v1.h"

#include "finite_horn_ground_term_v1.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    Atom *label;
    char *canonical;
} PPSemanticMaskV1Wrapper;

typedef struct {
    PPSemanticMaskV1Wrapper *data;
    uint32_t len;
    uint32_t cap;
} PPSemanticMaskV1WrapperVec;

static void ppsemantic_mask_v1_set_error(
    char *buf,
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

static bool ppsemantic_mask_v1_expr_head(
    Atom *term,
    const char *head,
    CettaExprLen argument_len) {
    return term && term->kind == ATOM_EXPR &&
        term->expr.len == argument_len + 1u &&
        atom_is_symbol(term->expr.elems[0], head);
}

static bool ppsemantic_mask_v1_grow(
    void **data,
    uint32_t *cap,
    uint32_t needed,
    size_t item_size) {
    uint32_t next_cap;
    void *next;

    if (needed <= *cap)
        return true;
    next_cap = *cap ? *cap : 8u;
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

static char *ppsemantic_mask_v1_canonical(Atom *term) {
    uint8_t *bytes = NULL;
    size_t len = 0u;

    if (!fh_ground_term_v1_render(term, &bytes, &len, NULL, 0u))
        return NULL;
    return (char *)bytes;
}

static int ppsemantic_mask_v1_wrapper_compare(
    const void *left,
    const void *right) {
    const PPSemanticMaskV1Wrapper *lhs = left;
    const PPSemanticMaskV1Wrapper *rhs = right;
    return strcmp(lhs->canonical, rhs->canonical);
}

static int ppsemantic_mask_v1_root_link_compare(
    const void *left,
    const void *right) {
    const PPSemanticMaskV1RootLink *lhs = left;
    const PPSemanticMaskV1RootLink *rhs = right;
    int comparison = strcmp(
        lhs->definition_canonical, rhs->definition_canonical);

    if (comparison != 0)
        return comparison;
    return strcmp(lhs->label_canonical, rhs->label_canonical);
}

static void ppsemantic_mask_v1_wrappers_free(
    PPSemanticMaskV1WrapperVec *wrappers) {
    uint32_t index;
    for (index = 0u; index < wrappers->len; index++)
        free(wrappers->data[index].canonical);
    free(wrappers->data);
    memset(wrappers, 0, sizeof(*wrappers));
}

static bool ppsemantic_mask_v1_wrapper_push(
    PPSemanticMaskV1WrapperVec *wrappers,
    Atom *label,
    char *error_buf,
    size_t error_buf_size) {
    char *canonical;

    if (!label || label->kind != ATOM_SYMBOL) {
        ppsemantic_mask_v1_set_error(
            error_buf, error_buf_size,
            "semantic-mask wrapper label is not a symbol");
        return false;
    }
    canonical = ppsemantic_mask_v1_canonical(label);
    if (!canonical ||
        !ppsemantic_mask_v1_grow(
            (void **)&wrappers->data, &wrappers->cap,
            wrappers->len + 1u, sizeof(*wrappers->data))) {
        free(canonical);
        ppsemantic_mask_v1_set_error(
            error_buf, error_buf_size,
            "failed to allocate semantic-mask wrapper identity");
        return false;
    }
    wrappers->data[wrappers->len++] =
        (PPSemanticMaskV1Wrapper){label, canonical};
    return true;
}

static bool ppsemantic_mask_v1_wrappers_canonicalize(
    PPSemanticMaskV1WrapperVec *wrappers,
    char *error_buf,
    size_t error_buf_size) {
    uint32_t read;
    uint32_t write = 0u;

    if (wrappers->len > 1u) {
        qsort(wrappers->data, wrappers->len,
              sizeof(*wrappers->data),
              ppsemantic_mask_v1_wrapper_compare);
    }
    for (read = 0u; read < wrappers->len; read++) {
        if (write > 0u &&
            strcmp(wrappers->data[read].canonical,
                   wrappers->data[write - 1u].canonical) == 0) {
            if (!atom_eq(wrappers->data[read].label,
                         wrappers->data[write - 1u].label)) {
                ppsemantic_mask_v1_set_error(
                    error_buf, error_buf_size,
                    "canonical semantic-mask wrapper collision");
                return false;
            }
            free(wrappers->data[read].canonical);
            continue;
        }
        if (write != read)
            wrappers->data[write] = wrappers->data[read];
        write++;
    }
    wrappers->len = write;
    return true;
}

static int32_t ppsemantic_mask_v1_wrapper_find(
    const PPSemanticMaskV1WrapperVec *wrappers,
    Atom *label) {
    char *canonical = ppsemantic_mask_v1_canonical(label);
    uint32_t low = 0u;
    uint32_t high = wrappers->len;
    int32_t result = -1;

    if (!canonical)
        return -1;
    while (low < high) {
        uint32_t middle = low + (high - low) / 2u;
        int comparison = strcmp(canonical, wrappers->data[middle].canonical);
        if (comparison < 0) {
            high = middle;
        } else if (comparison > 0) {
            low = middle + 1u;
        } else {
            if (atom_eq(label, wrappers->data[middle].label))
                result = (int32_t)middle;
            break;
        }
    }
    free(canonical);
    return result;
}

static Atom *ppsemantic_mask_v1_expr(
    Arena *arena,
    const char *head,
    Atom *const *arguments,
    CettaExprLen argument_len) {
    Atom **items;
    Atom *result;
    CettaExprIndex index;

    if (argument_len > (CettaExprLen)(SIZE_MAX / sizeof(*items)) - 1u)
        return NULL;
    items = arena_alloc(
        arena, sizeof(*items) * (size_t)(argument_len + 1u));
    if (!items)
        return NULL;
    items[0] = atom_symbol(arena, head);
    for (index = 0u; index < argument_len; index++)
        items[index + 1u] = arguments[index];
    if (!items[0])
        return NULL;
    result = atom_expr(arena, items, argument_len + 1u);
    return result;
}

static bool ppsemantic_mask_v1_action_shape(
    Atom *action,
    PPSemanticMaskV1WrapperVec *wrappers,
    bool collect,
    uint32_t *out_action,
    char *error_buf,
    size_t error_buf_size) {
    int32_t wrapper;

    if (atom_is_symbol(action, "semantic-mask-drop")) {
        if (out_action)
            *out_action = 0u;
        return true;
    }
    if (atom_is_symbol(action, "semantic-mask-copy")) {
        if (out_action)
            *out_action = 1u;
        return true;
    }
    if (!ppsemantic_mask_v1_expr_head(
            action, "semantic-mask-wrapper", 1u)) {
        ppsemantic_mask_v1_set_error(
            error_buf, error_buf_size,
            "unknown semantic-mask consuming action");
        return false;
    }
    if (collect) {
        return ppsemantic_mask_v1_wrapper_push(
            wrappers, action->expr.elems[1],
            error_buf, error_buf_size);
    }
    wrapper = ppsemantic_mask_v1_wrapper_find(
        wrappers, action->expr.elems[1]);
    if (wrapper < 0 || (uint32_t)wrapper > UINT32_MAX - 2u) {
        ppsemantic_mask_v1_set_error(
            error_buf, error_buf_size,
            "semantic-mask wrapper identity disappeared");
        return false;
    }
    if (out_action)
        *out_action = (uint32_t)wrapper + 2u;
    return true;
}

static bool ppsemantic_mask_v1_edge_shape(
    Atom *answer,
    Atom **out_tag,
    Atom **out_edge,
    Atom **out_action,
    const char **out_erased_head,
    CettaExprLen *out_erased_argument_len,
    bool *out_consuming,
    char *error_buf,
    size_t error_buf_size) {
    Atom *edge;

    if (!ppsemantic_mask_v1_expr_head(
            answer, "compile-semantic-mask-nfa", 2u)) {
        goto malformed;
    }
    edge = answer->expr.elems[2];
    *out_tag = answer->expr.elems[1];
    *out_edge = edge;
    *out_action = NULL;
    *out_consuming = false;
    if (ppsemantic_mask_v1_expr_head(
            edge, "semantic-mask-nfa-start", 1u)) {
        *out_erased_head = "nfa-start";
        *out_erased_argument_len = 1u;
    } else if (ppsemantic_mask_v1_expr_head(
                   edge, "semantic-mask-nfa-accept", 2u)) {
        if (!atom_eq(*out_tag, edge->expr.elems[2]))
            goto malformed;
        *out_erased_head = "nfa-accept";
        *out_erased_argument_len = 2u;
    } else if (ppsemantic_mask_v1_expr_head(
                   edge, "semantic-mask-nfa-epsilon", 2u)) {
        *out_erased_head = "nfa-epsilon";
        *out_erased_argument_len = 2u;
    } else if (ppsemantic_mask_v1_expr_head(
                   edge, "semantic-mask-nfa-any", 3u)) {
        *out_erased_head = "nfa-any";
        *out_erased_argument_len = 2u;
        *out_action = edge->expr.elems[3];
        *out_consuming = true;
    } else if (ppsemantic_mask_v1_expr_head(
                   edge, "semantic-mask-nfa-char", 4u)) {
        *out_erased_head = "nfa-char";
        *out_erased_argument_len = 3u;
        *out_action = edge->expr.elems[4];
        *out_consuming = true;
    } else if (ppsemantic_mask_v1_expr_head(
                   edge, "semantic-mask-nfa-class", 4u)) {
        *out_erased_head = "nfa-class";
        *out_erased_argument_len = 3u;
        *out_action = edge->expr.elems[4];
        *out_consuming = true;
    } else {
        goto malformed;
    }
    return true;

malformed:
    ppsemantic_mask_v1_set_error(
        error_buf, error_buf_size,
        "malformed semantic-mask NFA answer");
    return false;
}

static bool ppsemantic_mask_v1_root_link_shape(
    Atom *answer,
    Atom **out_definition,
    Atom **out_label,
    char *error_buf,
    size_t error_buf_size) {
    Atom *definition;
    Atom *label;

    if (!ppsemantic_mask_v1_expr_head(
            answer, "compile-semantic-mask-root-link", 2u)) {
        ppsemantic_mask_v1_set_error(
            error_buf, error_buf_size,
            "malformed semantic-mask root link");
        return false;
    }
    definition = answer->expr.elems[1];
    label = answer->expr.elems[2];
    if (!definition || definition->kind != ATOM_SYMBOL ||
        !label || label->kind != ATOM_SYMBOL) {
        ppsemantic_mask_v1_set_error(
            error_buf, error_buf_size,
            "semantic-mask root link is not symbol-to-symbol");
        return false;
    }
    if (out_definition)
        *out_definition = definition;
    if (out_label)
        *out_label = label;
    return true;
}

static bool ppsemantic_mask_v1_is_root_link(Atom *answer) {
    return answer && answer->kind == ATOM_EXPR &&
        answer->expr.len > 0u &&
        atom_is_symbol(
            answer->expr.elems[0], "compile-semantic-mask-root-link");
}

static bool ppsemantic_mask_v1_root_links_bind(
    PPSemanticMaskNfaV1Plan *plan,
    char *error_buf,
    size_t error_buf_size) {
    uint32_t index;
    uint32_t *tag_links = NULL;
    bool ok = false;

    if (plan->root_link_len == 0u)
        return true;
    if (plan->root_link_len != plan->erased.nfa.tag_len) {
        ppsemantic_mask_v1_set_error(
            error_buf, error_buf_size,
            "semantic-mask root links are not bijective with NFA tags");
        return false;
    }
    qsort(
        plan->root_links, plan->root_link_len,
        sizeof(*plan->root_links), ppsemantic_mask_v1_root_link_compare);
    tag_links = calloc(
        plan->erased.nfa.tag_len ? plan->erased.nfa.tag_len : 1u,
        sizeof(*tag_links));
    if (!tag_links) {
        ppsemantic_mask_v1_set_error(
            error_buf, error_buf_size,
            "failed to allocate semantic-mask root-link index");
        return false;
    }
    for (index = 0u; index < plan->root_link_len; index++) {
        PPSemanticMaskV1RootLink *link = &plan->root_links[index];
        uint32_t tag;
        bool found = false;

        if (index > 0u &&
            strcmp(
                link->definition_canonical,
                plan->root_links[index - 1u].definition_canonical) == 0) {
            ppsemantic_mask_v1_set_error(
                error_buf, error_buf_size,
                "semantic-mask definition has multiple root links");
            goto done;
        }
        for (tag = 0u; tag < plan->erased.nfa.tag_len; tag++) {
            if (strcmp(
                    link->label_canonical,
                    plan->erased.tag_canonical[tag]) != 0) {
                continue;
            }
            if (!atom_eq(link->label, plan->erased.tags[tag])) {
                ppsemantic_mask_v1_set_error(
                    error_buf, error_buf_size,
                    "canonical semantic-mask root-label collision");
                goto done;
            }
            link->tag = tag;
            tag_links[tag]++;
            found = true;
            break;
        }
        if (!found) {
            ppsemantic_mask_v1_set_error(
                error_buf, error_buf_size,
                "semantic-mask root link names no NFA tag");
            goto done;
        }
    }
    for (index = 0u; index < plan->erased.nfa.tag_len; index++) {
        if (tag_links[index] != 1u) {
            ppsemantic_mask_v1_set_error(
                error_buf, error_buf_size,
                "semantic-mask NFA tag lacks a unique root link");
            goto done;
        }
    }
    ok = true;

done:
    free(tag_links);
    return ok;
}

void ppsemantic_mask_nfa_v1_plan_init(PPSemanticMaskNfaV1Plan *plan) {
    if (!plan)
        return;
    memset(plan, 0, sizeof(*plan));
    rsnfa_v1_plan_init(&plan->erased);
    arena_init(&plan->transformed_arena);
}

void ppsemantic_mask_nfa_v1_plan_free(PPSemanticMaskNfaV1Plan *plan) {
    uint32_t index;

    if (!plan)
        return;
    rsnfa_v1_plan_free(&plan->erased);
    for (index = 0u; index < plan->action_len; index++)
        free(plan->actions[index].wrapper_canonical);
    for (index = 0u; index < plan->root_link_len; index++) {
        free(plan->root_links[index].definition_canonical);
        free(plan->root_links[index].label_canonical);
    }
    free(plan->actions);
    free(plan->edge_actions);
    free(plan->root_links);
    arena_free(&plan->transformed_arena);
    memset(plan, 0, sizeof(*plan));
}

bool ppsemantic_mask_nfa_v1_plan_load(
    const PPABIV1Pack *class_pack,
    Atom *const *answer_terms,
    size_t answer_len,
    PPSemanticMaskNfaV1Plan *out,
    char *error_buf,
    size_t error_buf_size) {
    PPSemanticMaskNfaV1Plan plan;
    PPSemanticMaskV1WrapperVec wrappers = {0};
    Atom **erased_answers = NULL;
    uint32_t consuming_len = 0u;
    uint32_t edge_write = 0u;
    uint32_t nfa_answer_len = 0u;
    uint32_t nfa_answer_write = 0u;
    uint32_t root_link_len = 0u;
    uint32_t root_link_write = 0u;
    size_t index;
    bool ok = false;

    ppsemantic_mask_nfa_v1_plan_init(&plan);
    if (error_buf && error_buf_size > 0u)
        error_buf[0] = '\0';
    if (!class_pack || !answer_terms || answer_len == 0u ||
        answer_len > UINT32_MAX || !out) {
        ppsemantic_mask_v1_set_error(
            error_buf, error_buf_size,
            "bad semantic-mask NFA answer set");
        goto done;
    }
    for (index = 0u; index < answer_len; index++) {
        Atom *tag;
        Atom *edge;
        Atom *action;
        const char *erased_head;
        CettaExprLen erased_argument_len;
        bool consuming;
        if (!answer_terms[index]) {
            ppsemantic_mask_v1_set_error(
                error_buf, error_buf_size,
                "null semantic-mask artifact answer");
            goto done;
        }
        if (ppsemantic_mask_v1_is_root_link(answer_terms[index])) {
            if (!ppsemantic_mask_v1_root_link_shape(
                    answer_terms[index], NULL, NULL,
                    error_buf, error_buf_size) ||
                root_link_len == UINT32_MAX) {
                goto done;
            }
            root_link_len++;
            continue;
        }
        if (!ppsemantic_mask_v1_edge_shape(
                answer_terms[index], &tag, &edge, &action,
                &erased_head, &erased_argument_len, &consuming,
                error_buf, error_buf_size) ||
            nfa_answer_len == UINT32_MAX) {
            goto done;
        }
        nfa_answer_len++;
        (void)tag;
        (void)edge;
        (void)erased_head;
        (void)erased_argument_len;
        if (!consuming)
            continue;
        if (consuming_len == UINT32_MAX ||
            !ppsemantic_mask_v1_action_shape(
                action, &wrappers, true, NULL,
                error_buf, error_buf_size)) {
            goto done;
        }
        consuming_len++;
    }
    if (!ppsemantic_mask_v1_wrappers_canonicalize(
            &wrappers, error_buf, error_buf_size) ||
        wrappers.len > UINT32_MAX - 2u ||
        (size_t)(wrappers.len + 2u) >
            SIZE_MAX / sizeof(*plan.actions) ||
        (size_t)nfa_answer_len > SIZE_MAX / sizeof(*erased_answers) ||
        (size_t)root_link_len > SIZE_MAX / sizeof(*plan.root_links)) {
        goto done;
    }
    if (nfa_answer_len == 0u) {
        ppsemantic_mask_v1_set_error(
            error_buf, error_buf_size,
            "semantic-mask artifact lacks NFA answers");
        goto done;
    }
    plan.action_len = wrappers.len + 2u;
    plan.actions = calloc(plan.action_len, sizeof(*plan.actions));
    plan.edge_actions = malloc(
        sizeof(*plan.edge_actions) *
        (size_t)(consuming_len ? consuming_len : 1u));
    erased_answers = malloc(
        sizeof(*erased_answers) * (size_t)nfa_answer_len);
    plan.root_links = calloc(
        root_link_len ? root_link_len : 1u, sizeof(*plan.root_links));
    plan.root_link_len = root_link_len;
    if (!plan.actions || !plan.edge_actions || !erased_answers ||
        !plan.root_links) {
        ppsemantic_mask_v1_set_error(
            error_buf, error_buf_size,
            "failed to allocate semantic-mask NFA plan");
        goto done;
    }
    plan.actions[0].kind = PPSEMANTIC_MASK_V1_DROP;
    plan.actions[1].kind = PPSEMANTIC_MASK_V1_COPY;

    for (index = 0u; index < answer_len; index++) {
        Atom *tag;
        Atom *edge;
        Atom *action;
        Atom *erased_edge;
        Atom *erased_arguments[3];
        Atom *top_arguments[2];
        const char *erased_head;
        CettaExprLen erased_argument_len;
        CettaExprIndex argument_index;
        bool consuming;
        uint32_t action_id = UINT32_MAX;

        if (ppsemantic_mask_v1_is_root_link(answer_terms[index])) {
            PPSemanticMaskV1RootLink *link;
            Atom *definition;
            Atom *label;
            if (root_link_write >= root_link_len ||
                !ppsemantic_mask_v1_root_link_shape(
                    answer_terms[index], &definition, &label,
                    error_buf, error_buf_size)) {
                goto done;
            }
            link = &plan.root_links[root_link_write++];
            link->definition = definition;
            link->label = label;
            link->definition_canonical =
                ppsemantic_mask_v1_canonical(definition);
            link->label_canonical = ppsemantic_mask_v1_canonical(label);
            if (!link->definition_canonical || !link->label_canonical) {
                ppsemantic_mask_v1_set_error(
                    error_buf, error_buf_size,
                    "failed to retain semantic-mask root link");
                goto done;
            }
            continue;
        }

        if (!ppsemantic_mask_v1_edge_shape(
                answer_terms[index], &tag, &edge, &action,
                &erased_head, &erased_argument_len, &consuming,
                error_buf, error_buf_size)) {
            goto done;
        }
        for (argument_index = 0u;
             argument_index < erased_argument_len;
             argument_index++) {
            erased_arguments[argument_index] =
                edge->expr.elems[argument_index + 1u];
        }
        if (consuming) {
            if (!ppsemantic_mask_v1_action_shape(
                    action, &wrappers, false, &action_id,
                    error_buf, error_buf_size) ||
                edge_write >= consuming_len) {
                goto done;
            }
            plan.edge_actions[edge_write++] = action_id;
        }
        erased_edge = ppsemantic_mask_v1_expr(
            &plan.transformed_arena, erased_head,
            erased_arguments, erased_argument_len);
        top_arguments[0] = tag;
        top_arguments[1] = erased_edge;
        if (nfa_answer_write >= nfa_answer_len)
            goto done;
        erased_answers[nfa_answer_write++] = erased_edge
            ? ppsemantic_mask_v1_expr(
                  &plan.transformed_arena, "compile-span-nfa",
                  top_arguments, 2u)
            : NULL;
        if (!erased_answers[nfa_answer_write - 1u]) {
            ppsemantic_mask_v1_set_error(
                error_buf, error_buf_size,
                "failed to construct semantic-mask recognition erasure");
            goto done;
        }
    }
    if (edge_write != consuming_len ||
        nfa_answer_write != nfa_answer_len ||
        root_link_write != root_link_len ||
        !rsnfa_v1_plan_load(
            class_pack, erased_answers, nfa_answer_len,
            &plan.erased, error_buf, error_buf_size) ||
        plan.erased.nfa.edge_len < consuming_len ||
        !ppsemantic_mask_v1_root_links_bind(
            &plan, error_buf, error_buf_size)) {
        goto done;
    }
    {
        uint32_t source;
        uint32_t erased_edge;
        uint32_t *all_edge_actions = calloc(
            plan.erased.nfa.edge_len ? plan.erased.nfa.edge_len : 1u,
            sizeof(*all_edge_actions));
        if (!all_edge_actions) {
            ppsemantic_mask_v1_set_error(
                error_buf, error_buf_size,
                "failed to align semantic-mask NFA actions");
            goto done;
        }
        source = 0u;
        for (erased_edge = 0u;
             erased_edge < plan.erased.nfa.edge_len;
             erased_edge++) {
            if (plan.erased.nfa.edges[erased_edge].kind ==
                    RSDFA_V1_NFA_RANGES) {
                if (source >= consuming_len) {
                    ppsemantic_mask_v1_set_error(
                        error_buf, error_buf_size,
                        "semantic-mask NFA gained a consuming edge during erasure");
                    free(all_edge_actions);
                    goto done;
                }
                all_edge_actions[erased_edge] = plan.edge_actions[source++];
            } else {
                all_edge_actions[erased_edge] = UINT32_MAX;
            }
        }
        free(plan.edge_actions);
        plan.edge_actions = all_edge_actions;
        if (source != consuming_len) {
            ppsemantic_mask_v1_set_error(
                error_buf, error_buf_size,
                "semantic-mask NFA lost a consuming edge during erasure");
            goto done;
        }
    }
    for (index = 0u; index < wrappers.len; index++) {
        plan.actions[index + 2u] = (PPSemanticMaskV1Action){
            .kind = PPSEMANTIC_MASK_V1_WRAPPER,
            .wrapper_label = wrappers.data[index].label,
            .wrapper_canonical = wrappers.data[index].canonical,
        };
        wrappers.data[index].canonical = NULL;
    }
    ppsemantic_mask_nfa_v1_plan_free(out);
    *out = plan;
    memset(&plan, 0, sizeof(plan));
    ok = true;

done:
    free(erased_answers);
    ppsemantic_mask_v1_wrappers_free(&wrappers);
    ppsemantic_mask_nfa_v1_plan_free(&plan);
    return ok;
}

void ppsemantic_mask_dfa_v1_program_init(
    PPSemanticMaskDfaV1Program *program) {
    if (!program)
        return;
    memset(program, 0, sizeof(*program));
    rsdfa_v1_program_init(&program->dfa);
}

void ppsemantic_mask_dfa_v1_program_free(
    PPSemanticMaskDfaV1Program *program) {
    if (!program)
        return;
    rsdfa_v1_program_free(&program->dfa);
    free(program->transition_actions);
    memset(program, 0, sizeof(*program));
}

bool ppsemantic_mask_dfa_v1_program_validate(
    const PPSemanticMaskDfaV1Program *program,
    char *error_buf,
    size_t error_buf_size) {
    uint32_t index;

    if (error_buf && error_buf_size > 0u)
        error_buf[0] = '\0';
    if (!program || program->action_len == 0u ||
        program->transition_action_len != program->dfa.transition_len ||
        (program->transition_action_len > 0u &&
         !program->transition_actions) ||
        !rsdfa_v1_program_validate(
            &program->dfa, error_buf, error_buf_size)) {
        if (error_buf && error_buf_size > 0u && error_buf[0] == '\0') {
            ppsemantic_mask_v1_set_error(
                error_buf, error_buf_size,
                "bad semantic-mask DFA program");
        }
        return false;
    }
    for (index = 0u; index < program->transition_action_len; index++) {
        if (program->transition_actions[index] >= program->action_len) {
            ppsemantic_mask_v1_set_error(
                error_buf, error_buf_size,
                "semantic-mask DFA action is out of range");
            return false;
        }
    }
    return true;
}

bool ppsemantic_mask_dfa_v1_program_build(
    const PPSemanticMaskNfaV1Plan *mask,
    uint32_t state_limit,
    uint32_t transition_limit,
    PPSemanticMaskDfaV1Program *out,
    RSDFAV1BuildOutcome *outcome,
    char *error_buf,
    size_t error_buf_size) {
    PPSemanticMaskDfaV1Program result;
    RSDFAV1Plan dfa;
    RSDFAV1BuildOutcome build_outcome = RSDFA_V1_BUILD_STATE_LIMIT;
    bool ok = false;

    ppsemantic_mask_dfa_v1_program_init(&result);
    rsdfa_v1_plan_init(&dfa);
    if (error_buf && error_buf_size > 0u)
        error_buf[0] = '\0';
    if (outcome)
        *outcome = build_outcome;
    if (!mask || !mask->erased.nfa.state_len || !mask->edge_actions ||
        mask->action_len == 0u || !out || !outcome ||
        state_limit == 0u || transition_limit == 0u) {
        ppsemantic_mask_v1_set_error(
            error_buf, error_buf_size,
            "bad semantic-mask DFA build arguments");
        goto done;
    }
    if (!rsdfa_v1_plan_build_annotated(
            &mask->erased.nfa, mask->edge_actions, UINT32_MAX,
            state_limit, transition_limit, &dfa, &build_outcome,
            error_buf, error_buf_size)) {
        goto done;
    }
    *outcome = build_outcome;
    if (build_outcome != RSDFA_V1_BUILD_COMPLETED) {
        ok = true;
        goto done;
    }
    result.action_len = mask->action_len;
    if (!rsdfa_v1_plan_export_program(
            &dfa, &result.dfa, error_buf, error_buf_size) ||
        !rsdfa_v1_plan_export_transition_annotations(
            &dfa, &result.transition_actions,
            &result.transition_action_len,
            error_buf, error_buf_size) ||
        !ppsemantic_mask_dfa_v1_program_validate(
            &result, error_buf, error_buf_size)) {
        goto done;
    }
    ppsemantic_mask_dfa_v1_program_free(out);
    *out = result;
    memset(&result, 0, sizeof(result));
    ok = true;

done:
    rsdfa_v1_plan_free(&dfa);
    ppsemantic_mask_dfa_v1_program_free(&result);
    return ok;
}

static bool ppsemantic_mask_dfa_v1_state_accepts(
    const RSDFAV1Program *program,
    uint32_t state_index,
    uint32_t tag) {
    const RSDFAV1ProgramState *state;
    uint32_t low;
    uint32_t high;

    if (!program || state_index >= program->state_len ||
        tag >= program->tag_len) {
        return false;
    }
    state = &program->states[state_index];
    low = state->accept_begin;
    high = state->accept_begin + state->accept_len;
    while (low < high) {
        uint32_t middle = low + (high - low) / 2u;
        uint32_t value = program->accept_tags[middle];
        if (tag < value)
            high = middle;
        else if (tag > value)
            low = middle + 1u;
        else
            return true;
    }
    return false;
}

bool ppsemantic_mask_dfa_v1_run_exact_prevalidated(
    const PPSemanticMaskDfaV1Program *program,
    const CettaLpNativeUtf8ScalarView *view,
    uint32_t left,
    uint32_t right,
    uint32_t tag,
    uint64_t work_limit,
    uint32_t *out_actions,
    uint32_t action_capacity,
    PPSemanticMaskDfaV1RunResult *out,
    char *error_buf,
    size_t error_buf_size) {
    PPSemanticMaskDfaV1RunResult result = {
        .outcome = PPSEMANTIC_MASK_DFA_V1_REJECTED,
    };
    const RSDFAV1Program *dfa;
    uint32_t state;
    uint32_t cursor;

    if (error_buf && error_buf_size > 0u)
        error_buf[0] = '\0';
    if (out)
        memset(out, 0, sizeof(*out));
    if (!program || !view || !out || left > right ||
        right > view->scalar_len || tag >= program->dfa.tag_len ||
        action_capacity < right - left ||
        (right > left && !out_actions) ||
        program->transition_action_len != program->dfa.transition_len ||
        program->action_len == 0u) {
        ppsemantic_mask_v1_set_error(
            error_buf, error_buf_size,
            "bad semantic-mask DFA exact-run arguments");
        return false;
    }
    dfa = &program->dfa;
    state = dfa->start_state;
    for (cursor = left; cursor < right; cursor++) {
        const RSDFAV1ProgramState *source;
        uint32_t low;
        uint32_t high;
        uint32_t transition_index = UINT32_MAX;
        uint32_t scalar = view->codepoints[cursor];

        if (state >= dfa->state_len) {
            ppsemantic_mask_v1_set_error(
                error_buf, error_buf_size,
                "semantic-mask DFA escaped its states");
            return false;
        }
        source = &dfa->states[state];
        low = source->transition_begin;
        high = source->transition_begin + source->transition_len;
        while (low < high) {
            uint32_t middle;
            const RSDFAV1ProgramTransition *transition;
            if (result.work_item_len >= work_limit) {
                result.outcome = PPSEMANTIC_MASK_DFA_V1_WORK_LIMIT;
                *out = result;
                return true;
            }
            result.work_item_len++;
            middle = low + (high - low) / 2u;
            transition = &dfa->transitions[middle];
            if (scalar < transition->low)
                high = middle;
            else if (scalar > transition->high)
                low = middle + 1u;
            else {
                transition_index = middle;
                break;
            }
        }
        if (transition_index == UINT32_MAX) {
            *out = result;
            return true;
        }
        if (program->transition_actions[transition_index] >=
                program->action_len) {
            ppsemantic_mask_v1_set_error(
                error_buf, error_buf_size,
                "semantic-mask DFA selected an invalid action");
            return false;
        }
        out_actions[result.action_len++] =
            program->transition_actions[transition_index];
        state = dfa->transitions[transition_index].target;
    }
    if (ppsemantic_mask_dfa_v1_state_accepts(dfa, state, tag))
        result.outcome = PPSEMANTIC_MASK_DFA_V1_ACCEPTED;
    *out = result;
    return true;
}

typedef struct {
    uint32_t *data;
    uint32_t len;
    uint32_t cap;
} PPSemanticMaskTraceV1U32Vec;

typedef struct {
    PPSemanticMaskTraceV1Backstep *data;
    uint32_t len;
    uint32_t cap;
} PPSemanticMaskTraceV1BackstepVec;

static int ppsemantic_mask_trace_v1_u32_compare(
    const void *left,
    const void *right) {
    uint32_t lhs = *(const uint32_t *)left;
    uint32_t rhs = *(const uint32_t *)right;
    return lhs < rhs ? -1 : lhs > rhs ? 1 : 0;
}

static bool ppsemantic_mask_trace_v1_bit_test(
    const uint64_t *bits,
    uint32_t bit) {
    return (bits[bit / 64u] & (UINT64_C(1) << (bit % 64u))) != 0u;
}

static void ppsemantic_mask_trace_v1_bit_set(
    uint64_t *bits,
    uint32_t bit) {
    bits[bit / 64u] |= UINT64_C(1) << (bit % 64u);
}

static bool ppsemantic_mask_trace_v1_edge_matches(
    const RSDFAV1NfaEdge *edge,
    uint32_t scalar) {
    uint32_t index;

    for (index = 0u; index < edge->range_len; index++) {
        if (scalar >= edge->ranges[index].low &&
            scalar <= edge->ranges[index].high) {
            return true;
        }
    }
    return false;
}

static bool ppsemantic_mask_trace_v1_boundaries(
    const RSDFAV1Nfa *nfa,
    PPSemanticMaskTraceV1U32Vec *out,
    char *error_buf,
    size_t error_buf_size) {
    uint32_t edge_index;
    uint32_t write;

    for (edge_index = 0u; edge_index < nfa->edge_len; edge_index++) {
        const RSDFAV1NfaEdge *edge = &nfa->edges[edge_index];
        uint32_t range_index;

        if (edge->kind != RSDFA_V1_NFA_RANGES)
            continue;
        for (range_index = 0u; range_index < edge->range_len; range_index++) {
            const CettaLpNativeUnicodeRange *range =
                &edge->ranges[range_index];
            uint32_t needed = range->high < UINT32_C(0x10ffff)
                ? 2u : 1u;
            if (out->len > UINT32_MAX - needed ||
                !ppsemantic_mask_v1_grow(
                    (void **)&out->data, &out->cap,
                    out->len + needed, sizeof(*out->data))) {
                ppsemantic_mask_v1_set_error(
                    error_buf, error_buf_size,
                    "failed to allocate semantic-mask scalar boundaries");
                return false;
            }
            out->data[out->len++] = range->low;
            if (range->high < UINT32_C(0x10ffff))
                out->data[out->len++] = range->high + 1u;
        }
    }
    if (out->len > 1u) {
        qsort(out->data, out->len, sizeof(*out->data),
              ppsemantic_mask_trace_v1_u32_compare);
    }
    write = 0u;
    for (edge_index = 0u; edge_index < out->len; edge_index++) {
        if (write == 0u || out->data[edge_index] != out->data[write - 1u])
            out->data[write++] = out->data[edge_index];
    }
    out->len = write;
    return true;
}

static uint32_t ppsemantic_mask_trace_v1_split_count(
    const RSDFAV1ProgramTransition *transition,
    const PPSemanticMaskTraceV1U32Vec *boundaries) {
    uint32_t count = 1u;
    uint32_t index;

    for (index = 0u; index < boundaries->len; index++) {
        uint32_t boundary = boundaries->data[index];
        if (boundary > transition->low && boundary <= transition->high)
            count++;
    }
    return count;
}

static bool ppsemantic_mask_trace_v1_split_program(
    const RSDFAV1Program *source,
    const PPSemanticMaskTraceV1U32Vec *boundaries,
    uint32_t transition_limit,
    RSDFAV1Program *out,
    bool *limit_hit,
    char *error_buf,
    size_t error_buf_size) {
    RSDFAV1Program result;
    uint32_t state_index;
    uint32_t write = 0u;
    uint64_t transition_len = 0u;
    bool ok = false;

    rsdfa_v1_program_init(&result);
    *limit_hit = false;
    for (state_index = 0u; state_index < source->transition_len;
         state_index++) {
        transition_len += ppsemantic_mask_trace_v1_split_count(
            &source->transitions[state_index], boundaries);
        if (transition_len > transition_limit || transition_len > UINT32_MAX) {
            *limit_hit = true;
            return true;
        }
    }
    result.state_len = source->state_len;
    result.transition_len = (uint32_t)transition_len;
    result.accept_tag_len = source->accept_tag_len;
    result.eof_accept_tag_len = source->eof_accept_tag_len;
    result.start_state = source->start_state;
    result.tag_len = source->tag_len;
    if ((size_t)result.state_len > SIZE_MAX / sizeof(*result.states) ||
        (size_t)result.transition_len >
            SIZE_MAX / sizeof(*result.transitions) ||
        (size_t)result.accept_tag_len >
            SIZE_MAX / sizeof(*result.accept_tags) ||
        (size_t)result.eof_accept_tag_len >
            SIZE_MAX / sizeof(*result.eof_accept_tags)) {
        ppsemantic_mask_v1_set_error(
            error_buf, error_buf_size,
            "semantic-mask trace DFA allocation overflow");
        goto done;
    }
    result.states = calloc(result.state_len, sizeof(*result.states));
    result.transitions = calloc(
        result.transition_len ? result.transition_len : 1u,
        sizeof(*result.transitions));
    result.accept_tags = calloc(
        result.accept_tag_len ? result.accept_tag_len : 1u,
        sizeof(*result.accept_tags));
    result.eof_accept_tags = calloc(
        result.eof_accept_tag_len ? result.eof_accept_tag_len : 1u,
        sizeof(*result.eof_accept_tags));
    if (!result.states || !result.transitions || !result.accept_tags ||
        !result.eof_accept_tags) {
        ppsemantic_mask_v1_set_error(
            error_buf, error_buf_size,
            "failed to allocate semantic-mask trace DFA");
        goto done;
    }
    if (result.accept_tag_len > 0u) {
        memcpy(result.accept_tags, source->accept_tags,
               sizeof(*result.accept_tags) * result.accept_tag_len);
    }
    if (result.eof_accept_tag_len > 0u) {
        memcpy(result.eof_accept_tags, source->eof_accept_tags,
               sizeof(*result.eof_accept_tags) * result.eof_accept_tag_len);
    }
    for (state_index = 0u; state_index < source->state_len; state_index++) {
        const RSDFAV1ProgramState *source_state =
            &source->states[state_index];
        RSDFAV1ProgramState *target_state = &result.states[state_index];
        uint32_t transition_index;

        *target_state = *source_state;
        target_state->transition_begin = write;
        target_state->transition_len = 0u;
        for (transition_index = source_state->transition_begin;
             transition_index < source_state->transition_begin +
                 source_state->transition_len;
             transition_index++) {
            const RSDFAV1ProgramTransition *transition =
                &source->transitions[transition_index];
            uint32_t low = transition->low;
            uint32_t boundary_index;

            for (boundary_index = 0u;
                 boundary_index < boundaries->len;
                 boundary_index++) {
                uint32_t boundary = boundaries->data[boundary_index];
                if (boundary <= low || boundary > transition->high)
                    continue;
                result.transitions[write++] =
                    (RSDFAV1ProgramTransition){
                        .low = low,
                        .high = boundary - 1u,
                        .target = transition->target,
                    };
                target_state->transition_len++;
                low = boundary;
            }
            result.transitions[write++] =
                (RSDFAV1ProgramTransition){
                    .low = low,
                    .high = transition->high,
                    .target = transition->target,
                };
            target_state->transition_len++;
        }
    }
    if (write != result.transition_len ||
        !rsdfa_v1_program_validate(
            &result, error_buf, error_buf_size)) {
        goto done;
    }
    rsdfa_v1_program_free(out);
    *out = result;
    memset(&result, 0, sizeof(result));
    ok = true;

done:
    rsdfa_v1_program_free(&result);
    return ok;
}

static bool ppsemantic_mask_trace_v1_epsilon_closures(
    const RSDFAV1Nfa *nfa,
    uint32_t word_len,
    uint64_t **out,
    char *error_buf,
    size_t error_buf_size) {
    uint32_t *offsets = NULL;
    uint32_t *targets = NULL;
    uint32_t *queue = NULL;
    uint64_t *closures = NULL;
    uint32_t *write_offsets = NULL;
    uint32_t epsilon_len = 0u;
    uint32_t edge_index;
    uint32_t state_index;
    size_t closure_words;
    bool ok = false;

    *out = NULL;
    for (edge_index = 0u; edge_index < nfa->edge_len; edge_index++) {
        if (nfa->edges[edge_index].kind == RSDFA_V1_NFA_EOF) {
            ppsemantic_mask_v1_set_error(
                error_buf, error_buf_size,
                "semantic-mask trace v1 does not admit EOF edges");
            return false;
        }
        if (nfa->edges[edge_index].kind == RSDFA_V1_NFA_EPSILON)
            epsilon_len++;
    }
    if ((size_t)nfa->state_len + 1u >
            SIZE_MAX / sizeof(*offsets) ||
        (size_t)epsilon_len > SIZE_MAX / sizeof(*targets) ||
        (size_t)nfa->state_len > SIZE_MAX / sizeof(*queue) ||
        (size_t)nfa->state_len > SIZE_MAX / (size_t)word_len) {
        ppsemantic_mask_v1_set_error(
            error_buf, error_buf_size,
            "semantic-mask epsilon-closure size overflow");
        goto done;
    }
    closure_words = (size_t)nfa->state_len * (size_t)word_len;
    if (closure_words > SIZE_MAX / sizeof(*closures)) {
        ppsemantic_mask_v1_set_error(
            error_buf, error_buf_size,
            "semantic-mask epsilon-closure allocation overflow");
        goto done;
    }
    offsets = calloc((size_t)nfa->state_len + 1u, sizeof(*offsets));
    write_offsets = calloc(
        (size_t)nfa->state_len + 1u, sizeof(*write_offsets));
    targets = calloc(epsilon_len ? epsilon_len : 1u, sizeof(*targets));
    queue = malloc(sizeof(*queue) * (size_t)nfa->state_len);
    closures = calloc(closure_words, sizeof(*closures));
    if (!offsets || !write_offsets || !targets || !queue || !closures) {
        ppsemantic_mask_v1_set_error(
            error_buf, error_buf_size,
            "failed to allocate semantic-mask epsilon closures");
        goto done;
    }
    for (edge_index = 0u; edge_index < nfa->edge_len; edge_index++) {
        const RSDFAV1NfaEdge *edge = &nfa->edges[edge_index];
        if (edge->kind == RSDFA_V1_NFA_EPSILON)
            offsets[edge->from + 1u]++;
    }
    for (state_index = 1u; state_index <= nfa->state_len; state_index++)
        offsets[state_index] += offsets[state_index - 1u];
    memcpy(write_offsets, offsets,
           sizeof(*write_offsets) * ((size_t)nfa->state_len + 1u));
    for (edge_index = 0u; edge_index < nfa->edge_len; edge_index++) {
        const RSDFAV1NfaEdge *edge = &nfa->edges[edge_index];
        if (edge->kind == RSDFA_V1_NFA_EPSILON)
            targets[write_offsets[edge->from]++] = edge->to;
    }
    for (state_index = 0u; state_index < nfa->state_len; state_index++) {
        uint64_t *row = &closures[(size_t)state_index * word_len];
        uint32_t read = 0u;
        uint32_t queue_len = 1u;

        queue[0] = state_index;
        ppsemantic_mask_trace_v1_bit_set(row, state_index);
        while (read < queue_len) {
            uint32_t current = queue[read++];
            uint32_t target_index;
            for (target_index = offsets[current];
                 target_index < offsets[current + 1u];
                 target_index++) {
                uint32_t target = targets[target_index];
                if (!ppsemantic_mask_trace_v1_bit_test(row, target)) {
                    ppsemantic_mask_trace_v1_bit_set(row, target);
                    queue[queue_len++] = target;
                }
            }
        }
    }
    *out = closures;
    closures = NULL;
    ok = true;

done:
    free(write_offsets);
    free(offsets);
    free(targets);
    free(queue);
    free(closures);
    return ok;
}

void ppsemantic_mask_trace_dfa_v1_program_init(
    PPSemanticMaskTraceDfaV1Program *program) {
    if (!program)
        return;
    memset(program, 0, sizeof(*program));
    rsdfa_v1_program_init(&program->dfa);
}

void ppsemantic_mask_trace_dfa_v1_program_free(
    PPSemanticMaskTraceDfaV1Program *program) {
    if (!program)
        return;
    rsdfa_v1_program_free(&program->dfa);
    free(program->transition_actions);
    free(program->backstep_offsets);
    free(program->backsteps);
    free(program->accept_nfa_states);
    memset(program, 0, sizeof(*program));
}

bool ppsemantic_mask_trace_dfa_v1_program_validate(
    const PPSemanticMaskTraceDfaV1Program *program,
    char *error_buf,
    size_t error_buf_size) {
    uint32_t transition_index;
    uint32_t action_local_transition_len = 0u;
    size_t accept_len;

    if (error_buf && error_buf_size > 0u)
        error_buf[0] = '\0';
    if (!program || program->nfa_state_len == 0u ||
        program->action_len == 0u || !program->transition_actions ||
        program->transition_action_len != program->dfa.transition_len ||
        !program->backstep_offsets ||
        (program->backstep_len > 0u && !program->backsteps) ||
        !program->accept_nfa_states ||
        !rsdfa_v1_program_validate(
            &program->dfa, error_buf, error_buf_size)) {
        if (error_buf && error_buf_size > 0u && error_buf[0] == '\0') {
            ppsemantic_mask_v1_set_error(
                error_buf, error_buf_size,
                "bad semantic-mask trace program");
        }
        return false;
    }
    if ((size_t)program->dfa.state_len >
            SIZE_MAX / (size_t)program->dfa.tag_len) {
        ppsemantic_mask_v1_set_error(
            error_buf, error_buf_size,
            "semantic-mask trace acceptance table overflows");
        return false;
    }
    accept_len =
        (size_t)program->dfa.state_len * (size_t)program->dfa.tag_len;
    (void)accept_len;
    if (program->backstep_offsets[0] != 0u ||
        program->backstep_offsets[program->dfa.transition_len] !=
            program->backstep_len) {
        ppsemantic_mask_v1_set_error(
            error_buf, error_buf_size,
            "semantic-mask trace offsets disagree with their receipt");
        return false;
    }
    for (transition_index = 0u;
         transition_index < program->dfa.transition_len;
         transition_index++) {
        uint32_t begin = program->backstep_offsets[transition_index];
        uint32_t end = program->backstep_offsets[transition_index + 1u];
        uint32_t direct_action;
        uint32_t index;
        if (begin >= end || end > program->backstep_len) {
            ppsemantic_mask_v1_set_error(
                error_buf, error_buf_size,
                "semantic-mask trace transition lacks a reverse witness");
            return false;
        }
        for (index = begin; index < end; index++) {
            const PPSemanticMaskTraceV1Backstep *step =
                &program->backsteps[index];
            if (step->target_nfa_state >= program->nfa_state_len ||
                step->source_nfa_state >= program->nfa_state_len ||
                step->action >= program->action_len ||
                (index > begin &&
                 program->backsteps[index - 1u].target_nfa_state >=
                     step->target_nfa_state)) {
                ppsemantic_mask_v1_set_error(
                    error_buf, error_buf_size,
                    "semantic-mask trace reverse witness is malformed");
                return false;
            }
        }
        direct_action = program->backsteps[begin].action;
        for (index = begin + 1u; index < end; index++) {
            if (program->backsteps[index].action != direct_action) {
                direct_action = UINT32_MAX;
                break;
            }
        }
        if (program->transition_actions[transition_index] != direct_action) {
            ppsemantic_mask_v1_set_error(
                error_buf, error_buf_size,
                "semantic-mask local-action quotient is malformed");
            return false;
        }
        if (direct_action != UINT32_MAX)
            action_local_transition_len++;
    }
    if (action_local_transition_len !=
            program->action_local_transition_len) {
        ppsemantic_mask_v1_set_error(
            error_buf, error_buf_size,
            "semantic-mask local-action receipt changed");
        return false;
    }
    for (transition_index = 0u; transition_index < accept_len;
         transition_index++) {
        if (program->accept_nfa_states[transition_index] != UINT32_MAX &&
            program->accept_nfa_states[transition_index] >=
                program->nfa_state_len) {
            ppsemantic_mask_v1_set_error(
                error_buf, error_buf_size,
                "semantic-mask trace acceptance witness is malformed");
            return false;
        }
    }
    return true;
}

static bool ppsemantic_mask_trace_v1_build_backsteps(
    const PPSemanticMaskNfaV1Plan *mask,
    const RSDFAV1SubsetTable *subsets,
    const uint64_t *closures,
    uint64_t backstep_limit,
    PPSemanticMaskTraceDfaV1Program *program,
    char *error_buf,
    size_t error_buf_size) {
    const RSDFAV1Nfa *nfa = &mask->erased.nfa;
    PPSemanticMaskTraceV1BackstepVec steps = {0};
    uint32_t *sources = NULL;
    uint32_t *actions = NULL;
    uint32_t state_index;
    uint32_t transition_index = 0u;
    bool ok = false;

    program->backstep_offsets = calloc(
        (size_t)program->dfa.transition_len + 1u,
        sizeof(*program->backstep_offsets));
    program->transition_actions = malloc(
        sizeof(*program->transition_actions) *
        (size_t)program->dfa.transition_len);
    program->transition_action_len = program->dfa.transition_len;
    sources = malloc(sizeof(*sources) * (size_t)nfa->state_len);
    actions = malloc(sizeof(*actions) * (size_t)nfa->state_len);
    if (!program->backstep_offsets || !program->transition_actions ||
        !sources || !actions) {
        ppsemantic_mask_v1_set_error(
            error_buf, error_buf_size,
            "failed to allocate semantic-mask reverse witnesses");
        goto done;
    }
    for (state_index = 0u; state_index < program->dfa.state_len;
         state_index++) {
        const RSDFAV1ProgramState *dfa_state =
            &program->dfa.states[state_index];
        const uint64_t *source_subset =
            &subsets->words[(size_t)state_index * subsets->word_len];
        uint32_t state_transition;

        for (state_transition = dfa_state->transition_begin;
             state_transition < dfa_state->transition_begin +
                 dfa_state->transition_len;
             state_transition++) {
            const RSDFAV1ProgramTransition *transition =
                &program->dfa.transitions[state_transition];
            const uint64_t *target_subset =
                &subsets->words[
                    (size_t)transition->target * subsets->word_len];
            uint32_t edge_index;
            uint32_t target_state;

            if (state_transition != transition_index) {
                ppsemantic_mask_v1_set_error(
                    error_buf, error_buf_size,
                    "semantic-mask trace transition order changed");
                goto done;
            }
            program->backstep_offsets[transition_index] = steps.len;
            for (target_state = 0u; target_state < nfa->state_len;
                 target_state++) {
                sources[target_state] = UINT32_MAX;
                actions[target_state] = UINT32_MAX;
            }
            for (edge_index = 0u; edge_index < nfa->edge_len;
                 edge_index++) {
                const RSDFAV1NfaEdge *edge = &nfa->edges[edge_index];
                const uint64_t *closure;
                uint32_t word_index;

                if (edge->kind != RSDFA_V1_NFA_RANGES ||
                    !ppsemantic_mask_trace_v1_bit_test(
                        source_subset, edge->from) ||
                    !ppsemantic_mask_trace_v1_edge_matches(
                        edge, transition->low)) {
                    continue;
                }
                if (ppsemantic_mask_trace_v1_edge_matches(
                        edge, transition->low) !=
                    ppsemantic_mask_trace_v1_edge_matches(
                        edge, transition->high)) {
                    ppsemantic_mask_v1_set_error(
                        error_buf, error_buf_size,
                        "semantic-mask trace transition crossed an NFA boundary");
                    goto done;
                }
                closure = &closures[(size_t)edge->to * subsets->word_len];
                for (word_index = 0u; word_index < subsets->word_len;
                     word_index++) {
                    uint64_t bits = closure[word_index] &
                        target_subset[word_index];
                    while (bits != 0u) {
                        uint32_t bit = (uint32_t)__builtin_ctzll(bits);
                        uint32_t target = word_index * 64u + bit;
                        bits &= bits - 1u;
                        if (target >= nfa->state_len ||
                            mask->edge_actions[edge_index] == UINT32_MAX ||
                            mask->edge_actions[edge_index] >=
                                mask->action_len) {
                            ppsemantic_mask_v1_set_error(
                                error_buf, error_buf_size,
                                "semantic-mask trace edge action is malformed");
                            goto done;
                        }
                        if (sources[target] == UINT32_MAX) {
                            sources[target] = edge->from;
                            actions[target] =
                                mask->edge_actions[edge_index];
                        }
                    }
                }
            }
            for (target_state = 0u; target_state < nfa->state_len;
                 target_state++) {
                if (!ppsemantic_mask_trace_v1_bit_test(
                        target_subset, target_state)) {
                    continue;
                }
                if (sources[target_state] == UINT32_MAX) {
                    ppsemantic_mask_v1_set_error(
                        error_buf, error_buf_size,
                        "semantic-mask DFA target lacks an NFA predecessor");
                    goto done;
                }
                if ((uint64_t)steps.len >= backstep_limit ||
                    steps.len == UINT32_MAX ||
                    !ppsemantic_mask_v1_grow(
                        (void **)&steps.data, &steps.cap,
                        steps.len + 1u, sizeof(*steps.data))) {
                    ppsemantic_mask_v1_set_error(
                        error_buf, error_buf_size,
                        "semantic-mask reverse-witness limit reached");
                    goto done;
                }
                steps.data[steps.len++] =
                    (PPSemanticMaskTraceV1Backstep){
                        .target_nfa_state = target_state,
                        .source_nfa_state = sources[target_state],
                        .action = actions[target_state],
                    };
            }
            if (steps.len == program->backstep_offsets[transition_index]) {
                ppsemantic_mask_v1_set_error(
                    error_buf, error_buf_size,
                    "semantic-mask transition has no action witness");
                goto done;
            }
            {
                uint32_t begin =
                    program->backstep_offsets[transition_index];
                uint32_t direct_action = steps.data[begin].action;
                uint32_t step_index;
                for (step_index = begin + 1u;
                     step_index < steps.len; step_index++) {
                    if (steps.data[step_index].action != direct_action) {
                        direct_action = UINT32_MAX;
                        break;
                    }
                }
                program->transition_actions[transition_index] =
                    direct_action;
                if (direct_action != UINT32_MAX)
                    program->action_local_transition_len++;
            }
            transition_index++;
        }
    }
    if (transition_index != program->dfa.transition_len) {
        ppsemantic_mask_v1_set_error(
            error_buf, error_buf_size,
            "semantic-mask reverse-witness count changed");
        goto done;
    }
    program->backstep_offsets[transition_index] = steps.len;
    program->backsteps = steps.data;
    program->backstep_len = steps.len;
    memset(&steps, 0, sizeof(steps));
    ok = true;

done:
    free(sources);
    free(actions);
    free(steps.data);
    return ok;
}

static bool ppsemantic_mask_trace_v1_build_accepts(
    const PPSemanticMaskNfaV1Plan *mask,
    const RSDFAV1SubsetTable *subsets,
    PPSemanticMaskTraceDfaV1Program *program,
    char *error_buf,
    size_t error_buf_size) {
    const RSDFAV1Nfa *nfa = &mask->erased.nfa;
    size_t table_len;
    uint32_t state_index;

    if ((size_t)program->dfa.state_len >
            SIZE_MAX / (size_t)program->dfa.tag_len) {
        ppsemantic_mask_v1_set_error(
            error_buf, error_buf_size,
            "semantic-mask trace acceptance size overflow");
        return false;
    }
    table_len =
        (size_t)program->dfa.state_len * (size_t)program->dfa.tag_len;
    if (table_len > SIZE_MAX / sizeof(*program->accept_nfa_states)) {
        ppsemantic_mask_v1_set_error(
            error_buf, error_buf_size,
            "semantic-mask trace acceptance allocation overflow");
        return false;
    }
    program->accept_nfa_states = malloc(
        sizeof(*program->accept_nfa_states) * table_len);
    if (!program->accept_nfa_states) {
        ppsemantic_mask_v1_set_error(
            error_buf, error_buf_size,
            "failed to allocate semantic-mask acceptance witnesses");
        return false;
    }
    for (state_index = 0u; state_index < table_len; state_index++)
        program->accept_nfa_states[state_index] = UINT32_MAX;
    for (state_index = 0u; state_index < program->dfa.state_len;
         state_index++) {
        const uint64_t *subset =
            &subsets->words[(size_t)state_index * subsets->word_len];
        uint32_t accept_index;
        uint32_t tag;

        for (accept_index = 0u; accept_index < nfa->accept_len;
             accept_index++) {
            const RSDFAV1NfaAccept *accept = &nfa->accepts[accept_index];
            size_t cell =
                (size_t)state_index * program->dfa.tag_len + accept->tag;
            if (ppsemantic_mask_trace_v1_bit_test(subset, accept->state) &&
                program->accept_nfa_states[cell] == UINT32_MAX) {
                program->accept_nfa_states[cell] = accept->state;
            }
        }
        for (tag = 0u; tag < program->dfa.tag_len; tag++) {
            bool dfa_accepts = ppsemantic_mask_dfa_v1_state_accepts(
                &program->dfa, state_index, tag);
            bool witness_accepts =
                program->accept_nfa_states[
                    (size_t)state_index * program->dfa.tag_len + tag] !=
                UINT32_MAX;
            if (dfa_accepts != witness_accepts) {
                ppsemantic_mask_v1_set_error(
                    error_buf, error_buf_size,
                    "semantic-mask acceptance witness changed its language");
                return false;
            }
        }
    }
    return true;
}

bool ppsemantic_mask_trace_dfa_v1_program_build(
    const PPSemanticMaskNfaV1Plan *mask,
    uint32_t state_limit,
    uint32_t transition_limit,
    uint32_t functionality_pair_limit,
    uint64_t functionality_work_limit,
    PPSemanticMaskTraceDfaV1Program *out,
    RSDFAV1BuildOutcome *outcome,
    char *error_buf,
    size_t error_buf_size) {
    PPSemanticMaskTraceDfaV1Program result;
    RSDFAV1Plan dfa_plan;
    RSDFAV1Program base_program;
    RSDFAV1SubsetTable subsets;
    PPSemanticMaskTraceV1U32Vec boundaries = {0};
    uint64_t *closures = NULL;
    RSDFAV1BuildOutcome build_outcome = RSDFA_V1_BUILD_STATE_LIMIT;
    uint32_t tag;
    bool split_limit_hit = false;
    bool ok = false;

    ppsemantic_mask_trace_dfa_v1_program_init(&result);
    rsdfa_v1_plan_init(&dfa_plan);
    rsdfa_v1_program_init(&base_program);
    rsdfa_v1_subset_table_init(&subsets);
    if (error_buf && error_buf_size > 0u)
        error_buf[0] = '\0';
    if (outcome)
        *outcome = build_outcome;
    if (!mask || !out || !outcome || !mask->edge_actions ||
        mask->action_len == 0u || mask->erased.nfa.state_len == 0u ||
        mask->erased.nfa.tag_len == 0u || state_limit == 0u ||
        transition_limit == 0u || functionality_pair_limit == 0u ||
        functionality_work_limit == 0u) {
        ppsemantic_mask_v1_set_error(
            error_buf, error_buf_size,
            "bad semantic-mask trace build arguments");
        goto done;
    }
    for (tag = 0u; tag < mask->erased.nfa.tag_len; tag++) {
        RSDFAV1FunctionalityResult functionality;
        rsdfa_v1_functionality_result_init(&functionality);
        if (!rsdfa_v1_annotated_nfa_functionality(
                &mask->erased.nfa, mask->edge_actions, UINT32_MAX,
                tag, functionality_pair_limit,
                functionality_work_limit,
                &functionality, error_buf, error_buf_size)) {
            rsdfa_v1_functionality_result_free(&functionality);
            goto done;
        }
        if (functionality.outcome != RSDFA_V1_FUNCTIONALITY_COMPLETED ||
            !functionality.functional ||
            UINT32_MAX - result.functionality_pair_len <
                functionality.explored_pair_len ||
            UINT64_MAX - result.functionality_work_item_len <
                functionality.work_item_len) {
            if (error_buf && error_buf_size > 0u && error_buf[0] == '\0') {
                ppsemantic_mask_v1_set_error(
                    error_buf, error_buf_size,
                    functionality.outcome ==
                            RSDFA_V1_FUNCTIONALITY_COMPLETED
                        ? "semantic-mask action relation is not functional"
                        : "semantic-mask functionality limit reached");
            }
            rsdfa_v1_functionality_result_free(&functionality);
            goto done;
        }
        result.functionality_pair_len +=
            functionality.explored_pair_len;
        result.functionality_work_item_len +=
            functionality.work_item_len;
        rsdfa_v1_functionality_result_free(&functionality);
    }
    if (!rsdfa_v1_plan_build(
            &mask->erased.nfa, state_limit, transition_limit,
            &dfa_plan, &build_outcome, error_buf, error_buf_size)) {
        goto done;
    }
    *outcome = build_outcome;
    if (build_outcome != RSDFA_V1_BUILD_COMPLETED) {
        ok = true;
        goto done;
    }
    if (!rsdfa_v1_plan_export_program(
            &dfa_plan, &base_program, error_buf, error_buf_size) ||
        !rsdfa_v1_plan_export_subsets(
            &dfa_plan, &subsets, error_buf, error_buf_size) ||
        subsets.state_len != base_program.state_len ||
        subsets.word_len !=
            (mask->erased.nfa.state_len + 63u) / 64u ||
        !ppsemantic_mask_trace_v1_boundaries(
            &mask->erased.nfa, &boundaries,
            error_buf, error_buf_size) ||
        !ppsemantic_mask_trace_v1_split_program(
            &base_program, &boundaries, transition_limit,
            &result.dfa, &split_limit_hit,
            error_buf, error_buf_size)) {
        if (error_buf && error_buf_size > 0u && error_buf[0] == '\0') {
            ppsemantic_mask_v1_set_error(
                error_buf, error_buf_size,
                "semantic-mask trace DFA witness changed shape");
        }
        goto done;
    }
    if (split_limit_hit) {
        *outcome = RSDFA_V1_BUILD_TRANSITION_LIMIT;
        ok = true;
        goto done;
    }
    result.nfa_state_len = mask->erased.nfa.state_len;
    result.action_len = mask->action_len;
    if (!ppsemantic_mask_trace_v1_epsilon_closures(
            &mask->erased.nfa, subsets.word_len, &closures,
            error_buf, error_buf_size) ||
        !ppsemantic_mask_trace_v1_build_backsteps(
            mask, &subsets, closures, functionality_work_limit,
            &result, error_buf, error_buf_size) ||
        !ppsemantic_mask_trace_v1_build_accepts(
            mask, &subsets, &result, error_buf, error_buf_size) ||
        !ppsemantic_mask_trace_dfa_v1_program_validate(
            &result, error_buf, error_buf_size)) {
        goto done;
    }
    ppsemantic_mask_trace_dfa_v1_program_free(out);
    *out = result;
    memset(&result, 0, sizeof(result));
    ok = true;

done:
    free(closures);
    free(boundaries.data);
    rsdfa_v1_subset_table_free(&subsets);
    rsdfa_v1_program_free(&base_program);
    rsdfa_v1_plan_free(&dfa_plan);
    ppsemantic_mask_trace_dfa_v1_program_free(&result);
    return ok;
}

bool ppsemantic_mask_trace_dfa_v1_run_exact_prevalidated(
    const PPSemanticMaskTraceDfaV1Program *program,
    const CettaLpNativeUtf8ScalarView *view,
    uint32_t left,
    uint32_t right,
    uint32_t tag,
    uint64_t work_limit,
    uint32_t *out_actions,
    uint32_t action_capacity,
    PPSemanticMaskDfaV1RunResult *out,
    char *error_buf,
    size_t error_buf_size) {
    PPSemanticMaskDfaV1RunResult result = {
        .outcome = PPSEMANTIC_MASK_DFA_V1_REJECTED,
    };
    const RSDFAV1Program *dfa;
    uint32_t state;
    uint32_t cursor;
    uint32_t selected_nfa_state;
    bool action_local = true;

    if (error_buf && error_buf_size > 0u)
        error_buf[0] = '\0';
    if (out)
        memset(out, 0, sizeof(*out));
    if (!program || !view || !out || left > right ||
        right > view->scalar_len || tag >= program->dfa.tag_len ||
        work_limit == 0u || action_capacity < right - left ||
        (right > left && !out_actions) || !program->transition_actions ||
        program->transition_action_len != program->dfa.transition_len ||
        !program->backstep_offsets ||
        !program->accept_nfa_states || program->action_len == 0u) {
        ppsemantic_mask_v1_set_error(
            error_buf, error_buf_size,
            "bad semantic-mask trace exact-run arguments");
        return false;
    }
    dfa = &program->dfa;
    state = dfa->start_state;
    for (cursor = left; cursor < right; cursor++) {
        const RSDFAV1ProgramState *source;
        uint32_t low;
        uint32_t high;
        uint32_t transition_index = UINT32_MAX;
        uint32_t scalar = view->codepoints[cursor];

        if (state >= dfa->state_len) {
            ppsemantic_mask_v1_set_error(
                error_buf, error_buf_size,
                "semantic-mask trace escaped its DFA states");
            return false;
        }
        source = &dfa->states[state];
        low = source->transition_begin;
        high = source->transition_begin + source->transition_len;
        while (low < high) {
            uint32_t middle;
            const RSDFAV1ProgramTransition *transition;
            if (result.work_item_len >= work_limit) {
                result.outcome = PPSEMANTIC_MASK_DFA_V1_WORK_LIMIT;
                *out = result;
                return true;
            }
            result.work_item_len++;
            middle = low + (high - low) / 2u;
            transition = &dfa->transitions[middle];
            if (scalar < transition->low)
                high = middle;
            else if (scalar > transition->high)
                low = middle + 1u;
            else {
                transition_index = middle;
                break;
            }
        }
        if (transition_index == UINT32_MAX) {
            *out = result;
            return true;
        }
        out_actions[cursor - left] = transition_index;
        if (program->transition_actions[transition_index] == UINT32_MAX)
            action_local = false;
        state = dfa->transitions[transition_index].target;
    }
    selected_nfa_state = program->accept_nfa_states[
        (size_t)state * dfa->tag_len + tag];
    if (selected_nfa_state == UINT32_MAX) {
        *out = result;
        return true;
    }
    if (action_local) {
        for (cursor = left; cursor < right; cursor++) {
            uint32_t transition_index = out_actions[cursor - left];
            uint32_t action;
            if (result.work_item_len >= work_limit) {
                result.outcome = PPSEMANTIC_MASK_DFA_V1_WORK_LIMIT;
                *out = result;
                return true;
            }
            result.work_item_len++;
            if (transition_index >= program->transition_action_len ||
                (action = program->transition_actions[transition_index]) ==
                    UINT32_MAX ||
                action >= program->action_len) {
                ppsemantic_mask_v1_set_error(
                    error_buf, error_buf_size,
                    "semantic-mask local action escaped its certificate");
                return false;
            }
            out_actions[cursor - left] = action;
        }
        result.outcome = PPSEMANTIC_MASK_DFA_V1_ACCEPTED;
        result.action_len = right - left;
        *out = result;
        return true;
    }
    for (cursor = right; cursor > left;) {
        uint32_t transition_index;
        uint32_t low;
        uint32_t high;
        uint32_t found = UINT32_MAX;
        const PPSemanticMaskTraceV1Backstep *step;

        cursor--;
        transition_index = out_actions[cursor - left];
        if (transition_index >= dfa->transition_len) {
            ppsemantic_mask_v1_set_error(
                error_buf, error_buf_size,
                "semantic-mask trace retained an invalid transition");
            return false;
        }
        low = program->backstep_offsets[transition_index];
        high = program->backstep_offsets[transition_index + 1u];
        while (low < high) {
            uint32_t middle;
            if (result.work_item_len >= work_limit) {
                result.outcome = PPSEMANTIC_MASK_DFA_V1_WORK_LIMIT;
                *out = result;
                return true;
            }
            result.work_item_len++;
            middle = low + (high - low) / 2u;
            if (selected_nfa_state <
                    program->backsteps[middle].target_nfa_state) {
                high = middle;
            } else if (selected_nfa_state >
                       program->backsteps[middle].target_nfa_state) {
                low = middle + 1u;
            } else {
                found = middle;
                break;
            }
        }
        if (found == UINT32_MAX) {
            ppsemantic_mask_v1_set_error(
                error_buf, error_buf_size,
                "semantic-mask trace cannot reconstruct an accepting path");
            return false;
        }
        step = &program->backsteps[found];
        if (step->action >= program->action_len) {
            ppsemantic_mask_v1_set_error(
                error_buf, error_buf_size,
                "semantic-mask trace selected an invalid action");
            return false;
        }
        out_actions[cursor - left] = step->action;
        selected_nfa_state = step->source_nfa_state;
    }
    result.outcome = PPSEMANTIC_MASK_DFA_V1_ACCEPTED;
    result.action_len = right - left;
    *out = result;
    return true;
}
