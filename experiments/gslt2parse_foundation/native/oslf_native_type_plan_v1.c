#include "oslf_native_type_plan_v1.h"

#include "finite_horn_answer_stream_v1.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    FHAnswerStreamV1 answers;
} PPOSLFNativeTypePlanStorageV1;

typedef struct {
    PPOSLFNativeTypePlanV1 *plan;
    uint32_t term_cap;
    uint32_t term_edge_cap;
    uint32_t body_root_cap;
    bool has_variable;
    uint32_t maximum_variable;
    char *error_buf;
    size_t error_buf_size;
} PPOSLFNativeProgramBuilderV1;

typedef struct {
    PPOSLFNativeTermKindV1 kind;
    const char *text;
    int64_t integer;
    uint32_t step;
} PPOSLFNativeHeadIndexItemV1;

typedef struct {
    PPOSLFNativeTermKindV1 kind;
    const char *text;
    int64_t integer;
    uint32_t arity;
} PPOSLFNativeOpenHeadItemV1;

#define PPOSLF_NATIVE_TYPE_MAX_TERM_DEPTH_V1 4096u

static void pposlf_native_type_plan_v1_set_error(
    char *buf, size_t size, const char *format, ...) {
    va_list arguments;

    if (!buf || size == 0u)
        return;
    va_start(arguments, format);
    (void)vsnprintf(buf, size, format, arguments);
    va_end(arguments);
}

static bool pposlf_native_type_plan_v1_expr_head(
    const Atom *atom, const char *head, CettaExprLen arity) {
    return atom && atom->kind == ATOM_EXPR &&
           atom->expr.len == arity + 1u &&
           atom_is_symbol(atom->expr.elems[0], head);
}

static const char *pposlf_native_type_plan_v1_symbol(
    const Atom *atom) {
    const char *name;

    if (!atom || atom->kind != ATOM_SYMBOL)
        return NULL;
    name = atom_name_cstr((Atom *)atom);
    return name && name[0] != '\0' ? name : NULL;
}

static const char *pposlf_native_type_plan_v1_quoted_symbol(
    const Atom *atom) {
    if (!pposlf_native_type_plan_v1_expr_head(atom, "q-sym", 1u))
        return NULL;
    return pposlf_native_type_plan_v1_symbol(atom->expr.elems[1]);
}

static const char *pposlf_native_type_plan_v1_owner(
    const Atom *atom) {
    const char *owner = pposlf_native_type_plan_v1_symbol(atom);
    return owner ? owner : pposlf_native_type_plan_v1_quoted_symbol(atom);
}

static bool pposlf_native_type_plan_v1_qnat(
    const Atom *atom, uint32_t *value_out) {
    const Atom *cursor = atom;
    uint32_t value = 0u;

    if (!value_out)
        return false;
    while (pposlf_native_type_plan_v1_expr_head(
               cursor, "q-succ", 1u)) {
        if (value == UINT32_MAX)
            return false;
        value++;
        cursor = cursor->expr.elems[1];
    }
    if (!atom_is_symbol((Atom *)cursor, "q-zero"))
        return false;
    *value_out = value;
    return true;
}

static bool pposlf_native_type_plan_v1_quint32(
    const Atom *atom, uint32_t *value_out) {
    const Atom *value;

    if (!value_out ||
        !pposlf_native_type_plan_v1_expr_head(atom, "q-int", 1u))
        return false;
    value = atom->expr.elems[1];
    if (!value || value->kind != ATOM_GROUNDED ||
        value->ground.gkind != GV_INT || value->ground.ival < 0 ||
        (uint64_t)value->ground.ival > UINT32_MAX)
        return false;
    *value_out = (uint32_t)value->ground.ival;
    return true;
}

static bool pposlf_native_type_plan_v1_valid_fact(
    const Atom *fact) {
    uint32_t ignored_arity;

    if (pposlf_native_type_plan_v1_expr_head(
            fact, "oslf-carrier-v1", 1u)) {
        const char *carrier =
            pposlf_native_type_plan_v1_symbol(fact->expr.elems[1]);
        return carrier && strcmp(carrier, "finite-horn-goal-state-v1") == 0;
    }
    if (pposlf_native_type_plan_v1_expr_head(
            fact, "oslf-transition-semantics-v1", 1u)) {
        const char *semantics =
            pposlf_native_type_plan_v1_symbol(fact->expr.elems[1]);
        return semantics &&
               strcmp(semantics,
                      "finite-horn-backward-goal-reduction-v1") == 0;
    }
    if (pposlf_native_type_plan_v1_expr_head(
            fact, "oslf-galois-v1", 2u) ||
        pposlf_native_type_plan_v1_expr_head(
            fact, "oslf-modal-v1", 2u))
        return pposlf_native_type_plan_v1_symbol(fact->expr.elems[1]) &&
               pposlf_native_type_plan_v1_symbol(fact->expr.elems[2]);
    if (pposlf_native_type_plan_v1_expr_head(
            fact, "oslf-head-signature-v1", 2u) ||
        pposlf_native_type_plan_v1_expr_head(
            fact, "oslf-native-former-v1", 2u))
        return pposlf_native_type_plan_v1_symbol(fact->expr.elems[1]) &&
               pposlf_native_type_plan_v1_qnat(
                   fact->expr.elems[2], &ignored_arity);
    if (pposlf_native_type_plan_v1_expr_head(
            fact, "oslf-step-schema-v1", 4u))
        return pposlf_native_type_plan_v1_owner(fact->expr.elems[1]) &&
               pposlf_native_type_plan_v1_symbol(fact->expr.elems[2]);
    if (pposlf_native_type_plan_v1_expr_head(
            fact, "oslf-external-relation-v1", 3u))
        return pposlf_native_type_plan_v1_owner(fact->expr.elems[1]) &&
               pposlf_native_type_plan_v1_quoted_symbol(
                   fact->expr.elems[2]) &&
               pposlf_native_type_plan_v1_quint32(
                   fact->expr.elems[3], &ignored_arity);
    return false;
}

static int pposlf_native_type_plan_v1_signature_compare(
    const void *left_raw, const void *right_raw) {
    const PPOSLFNativeHeadSignatureV1 *left = left_raw;
    const PPOSLFNativeHeadSignatureV1 *right = right_raw;
    return strcmp(left->constructor, right->constructor);
}

static int pposlf_native_type_plan_v1_step_compare(
    const void *left_raw, const void *right_raw) {
    const PPOSLFNativeStepSchemaV1 *left = left_raw;
    const PPOSLFNativeStepSchemaV1 *right = right_raw;
    int owner_comparison = strcmp(left->owner, right->owner);

    return owner_comparison != 0
               ? owner_comparison
               : strcmp(left->rule, right->rule);
}

static int pposlf_native_type_plan_v1_external_compare(
    const void *left_raw, const void *right_raw) {
    const PPOSLFNativeExternalRelationV1 *left = left_raw;
    const PPOSLFNativeExternalRelationV1 *right = right_raw;
    int relation_comparison = strcmp(left->relation, right->relation);

    if (relation_comparison != 0)
        return relation_comparison;
    if (left->arity < right->arity)
        return -1;
    if (left->arity > right->arity)
        return 1;
    return strcmp(left->owner, right->owner);
}

static bool pposlf_native_type_plan_v1_head_key_uses_text(
    PPOSLFNativeTermKindV1 kind) {
    return kind == PPOSLF_NATIVE_TERM_SYMBOL_V1 ||
           kind == PPOSLF_NATIVE_TERM_INTEGER_BIG_V1 ||
           kind == PPOSLF_NATIVE_TERM_STRING_V1 ||
           kind == PPOSLF_NATIVE_TERM_APPLICATION_V1;
}

static int pposlf_native_type_plan_v1_head_key_compare(
    PPOSLFNativeTermKindV1 left_kind,
    const char *left_text,
    int64_t left_integer,
    PPOSLFNativeTermKindV1 right_kind,
    const char *right_text,
    int64_t right_integer) {
    if (left_kind != right_kind)
        return left_kind < right_kind ? -1 : 1;
    if (pposlf_native_type_plan_v1_head_key_uses_text(left_kind))
        return strcmp(left_text, right_text);
    if (left_kind == PPOSLF_NATIVE_TERM_INTEGER_I64_V1) {
        if (left_integer < right_integer)
            return -1;
        if (left_integer > right_integer)
            return 1;
    }
    return 0;
}

static int pposlf_native_type_plan_v1_head_index_compare(
    const void *left_raw, const void *right_raw) {
    const PPOSLFNativeHeadIndexItemV1 *left = left_raw;
    const PPOSLFNativeHeadIndexItemV1 *right = right_raw;
    int key_comparison = pposlf_native_type_plan_v1_head_key_compare(
        left->kind, left->text, left->integer,
        right->kind, right->text, right->integer);

    if (key_comparison != 0)
        return key_comparison;
    if (left->step < right->step)
        return -1;
    return left->step > right->step ? 1 : 0;
}

static int pposlf_native_type_plan_v1_open_head_compare(
    const void *left_raw, const void *right_raw) {
    const PPOSLFNativeOpenHeadItemV1 *left = left_raw;
    const PPOSLFNativeOpenHeadItemV1 *right = right_raw;
    int comparison = pposlf_native_type_plan_v1_head_key_compare(
        left->kind, left->text, left->integer,
        right->kind, right->text, right->integer);

    if (comparison != 0)
        return comparison;
    if (left->arity < right->arity)
        return -1;
    return left->arity > right->arity ? 1 : 0;
}

static bool pposlf_native_type_plan_v1_build_head_index(
    PPOSLFNativeTypePlanV1 *plan,
    char *error_buf,
    size_t error_buf_size) {
    PPOSLFNativeHeadIndexItemV1 *items = NULL;
    uint32_t group_len = 0u;

    if (plan->step_schema_len == 0u)
        return true;
    items = malloc((size_t)plan->step_schema_len * sizeof(*items));
    plan->head_groups = calloc(
        plan->step_schema_len, sizeof(*plan->head_groups));
    plan->head_step_indices = malloc(
        (size_t)plan->step_schema_len * sizeof(*plan->head_step_indices));
    if (!items || !plan->head_groups || !plan->head_step_indices) {
        pposlf_native_type_plan_v1_set_error(
            error_buf, error_buf_size,
            "cannot allocate OSLF step head index");
        free(items);
        return false;
    }
    for (uint32_t step = 0u; step < plan->step_schema_len; step++) {
        const PPOSLFNativeTermV1 *head =
            &plan->terms[plan->step_schemas[step].head];
        items[step] = (PPOSLFNativeHeadIndexItemV1){
            .kind = head->kind,
            .text = head->text,
            .integer = head->integer,
            .step = step,
        };
    }
    qsort(items, plan->step_schema_len, sizeof(*items),
          pposlf_native_type_plan_v1_head_index_compare);
    for (uint32_t item = 0u; item < plan->step_schema_len; item++) {
        PPOSLFNativeHeadGroupV1 *group;
        if (item == 0u ||
            pposlf_native_type_plan_v1_head_key_compare(
                items[item - 1u].kind, items[item - 1u].text,
                items[item - 1u].integer, items[item].kind,
                items[item].text, items[item].integer) != 0) {
            group = &plan->head_groups[group_len++];
            *group = (PPOSLFNativeHeadGroupV1){
                .kind = items[item].kind,
                .text = items[item].text,
                .integer = items[item].integer,
                .step_begin = item,
            };
        }
        group = &plan->head_groups[group_len - 1u];
        group->step_len++;
        plan->head_step_indices[item] = items[item].step;
    }
    plan->head_group_len = group_len;
    plan->head_step_index_len = plan->step_schema_len;
    free(items);
    return true;
}

static bool pposlf_native_type_plan_v1_build_open_heads(
    PPOSLFNativeTypePlanV1 *plan,
    char *error_buf,
    size_t error_buf_size) {
    PPOSLFNativeOpenHeadItemV1 *items = NULL;
    uint32_t item_len = 0u;
    uint32_t variable_group;
    bool has_variable_definition;

    if (plan->body_root_len == 0u)
        return true;
    items = malloc((size_t)plan->body_root_len * sizeof(*items));
    plan->open_heads = calloc(
        plan->body_root_len, sizeof(*plan->open_heads));
    if (!items || !plan->open_heads) {
        pposlf_native_type_plan_v1_set_error(
            error_buf, error_buf_size,
            "cannot allocate the OSLF open-relation inventory");
        free(items);
        return false;
    }
    has_variable_definition = pposlf_native_type_plan_v1_head_group(
        plan, PPOSLF_NATIVE_TERM_VARIABLE_V1,
        NULL, 0, &variable_group);
    for (uint32_t index = 0u; index < plan->body_root_len; index++) {
        const PPOSLFNativeTermV1 *head =
            &plan->terms[plan->body_roots[index]];
        uint32_t defined_group;

        if (has_variable_definition ||
            pposlf_native_type_plan_v1_head_group(
                plan, head->kind, head->text, head->integer,
                &defined_group))
            continue;
        items[item_len++] = (PPOSLFNativeOpenHeadItemV1){
            .kind = head->kind,
            .text = head->text,
            .integer = head->integer,
            .arity = head->kind == PPOSLF_NATIVE_TERM_APPLICATION_V1
                         ? head->edge_len
                         : 0u,
        };
    }
    qsort(items, item_len, sizeof(*items),
          pposlf_native_type_plan_v1_open_head_compare);
    for (uint32_t item = 0u; item < item_len; item++) {
        PPOSLFNativeOpenHeadV1 *open;

        if (item == 0u ||
            pposlf_native_type_plan_v1_open_head_compare(
                &items[item - 1u], &items[item]) != 0) {
            open = &plan->open_heads[plan->open_head_len++];
            *open = (PPOSLFNativeOpenHeadV1){
                .kind = items[item].kind,
                .text = items[item].text,
                .integer = items[item].integer,
                .arity = items[item].arity,
            };
        }
        plan->open_heads[plan->open_head_len - 1u].body_occurrences++;
    }
    free(items);
    return true;
}

static bool pposlf_native_type_plan_v1_validate_external_relations(
    PPOSLFNativeTypePlanV1 *plan,
    char *error_buf,
    size_t error_buf_size) {
    for (uint32_t index = 0u;
         index < plan->external_relation_len; index++) {
        PPOSLFNativeExternalRelationV1 *external =
            &plan->external_relations[index];
        uint32_t signature_arity;

        if (!pposlf_native_type_plan_v1_head_arity(
                plan, external->relation, &signature_arity) ||
            signature_arity != external->arity) {
            pposlf_native_type_plan_v1_set_error(
                error_buf, error_buf_size,
                "authored extensional interface violates its relation signature");
            return false;
        }
        for (uint32_t body = 0u; body < plan->body_root_len; body++) {
            const PPOSLFNativeTermV1 *term =
                &plan->terms[plan->body_roots[body]];

            if (term->kind == PPOSLF_NATIVE_TERM_APPLICATION_V1 &&
                term->edge_len == external->arity &&
                strcmp(term->text, external->relation) == 0)
                external->body_occurrences++;
        }
        if (external->body_occurrences == 0u) {
            pposlf_native_type_plan_v1_set_error(
                error_buf, error_buf_size,
                "authored extensional interface is not consumed by the program");
            return false;
        }
    }
    for (uint32_t index = 0u; index < plan->open_head_len; index++) {
        const PPOSLFNativeOpenHeadV1 *open = &plan->open_heads[index];
        uint32_t external;

        if (open->kind != PPOSLF_NATIVE_TERM_APPLICATION_V1 ||
            !pposlf_native_type_plan_v1_external_relation(
                plan, open->text, open->arity, &external)) {
            pposlf_native_type_plan_v1_set_error(
                error_buf, error_buf_size,
                "open relation %s/%u lacks an authored extensional interface",
                open->text ? open->text : "<non-symbol>", open->arity);
            return false;
        }
    }
    return true;
}

static bool pposlf_native_type_plan_v1_reserve(
    void **items,
    uint32_t *capacity,
    uint32_t needed,
    size_t item_size) {
    uint32_t next_cap;
    void *next;

    if (needed <= *capacity)
        return true;
    next_cap = *capacity ? *capacity : 64u;
    while (next_cap < needed) {
        if (next_cap > UINT32_MAX / 2u) {
            next_cap = needed;
            break;
        }
        next_cap *= 2u;
    }
    if ((size_t)next_cap > SIZE_MAX / item_size)
        return false;
    next = realloc(*items, (size_t)next_cap * item_size);
    if (!next)
        return false;
    *items = next;
    *capacity = next_cap;
    return true;
}

static bool pposlf_native_type_plan_v1_q_list_length(
    const Atom *list,
    uint32_t *length_out) {
    const Atom *cursor = list;
    uint32_t length = 0u;

    if (!length_out)
        return false;
    while (pposlf_native_type_plan_v1_expr_head(
               cursor, "q-cons", 2u)) {
        if (length == UINT32_MAX)
            return false;
        length++;
        cursor = cursor->expr.elems[2];
    }
    if (!atom_is_symbol((Atom *)cursor, "q-nil"))
        return false;
    *length_out = length;
    return true;
}

static bool pposlf_native_type_plan_v1_compile_term(
    PPOSLFNativeProgramBuilderV1 *builder,
    const Atom *quoted,
    uint32_t depth,
    uint32_t *term_out) {
    PPOSLFNativeTypePlanV1 *plan = builder->plan;
    PPOSLFNativeTermV1 term;
    uint32_t term_index;
    const char *text;

    if (depth > PPOSLF_NATIVE_TYPE_MAX_TERM_DEPTH_V1) {
        pposlf_native_type_plan_v1_set_error(
            builder->error_buf, builder->error_buf_size,
            "OSLF step term exceeds the admitted depth");
        return false;
    }
    if (plan->term_len == UINT32_MAX ||
        !pposlf_native_type_plan_v1_reserve(
            (void **)&plan->terms, &builder->term_cap,
            plan->term_len + 1u, sizeof(*plan->terms))) {
        pposlf_native_type_plan_v1_set_error(
            builder->error_buf, builder->error_buf_size,
            "cannot allocate OSLF step terms");
        return false;
    }
    memset(&term, 0, sizeof(term));
    term_index = plan->term_len++;

    if (pposlf_native_type_plan_v1_expr_head(quoted, "q-sym", 1u)) {
        text = pposlf_native_type_plan_v1_symbol(quoted->expr.elems[1]);
        if (!text)
            goto malformed;
        term.kind = PPOSLF_NATIVE_TERM_SYMBOL_V1;
        term.text = text;
    } else if (pposlf_native_type_plan_v1_expr_head(
                   quoted, "q-int", 1u)) {
        const Atom *value = quoted->expr.elems[1];
        if (value && value->kind == ATOM_GROUNDED &&
            value->ground.gkind == GV_INT) {
            term.kind = PPOSLF_NATIVE_TERM_INTEGER_I64_V1;
            term.integer = value->ground.ival;
        } else if (value && value->kind == ATOM_GROUNDED &&
                   value->ground.gkind == GV_BIGINT &&
                   atom_bigint_cstr(value)) {
            term.kind = PPOSLF_NATIVE_TERM_INTEGER_BIG_V1;
            term.text = atom_bigint_cstr(value);
        } else {
            goto malformed;
        }
    } else if (pposlf_native_type_plan_v1_expr_head(
                   quoted, "q-str", 1u)) {
        const Atom *value = quoted->expr.elems[1];
        if (!value || value->kind != ATOM_GROUNDED ||
            value->ground.gkind != GV_STRING || !value->ground.sval)
            goto malformed;
        term.kind = PPOSLF_NATIVE_TERM_STRING_V1;
        term.text = value->ground.sval;
    } else if (pposlf_native_type_plan_v1_expr_head(
                   quoted, "q-var", 1u)) {
        uint32_t variable;
        if (!pposlf_native_type_plan_v1_qnat(
                quoted->expr.elems[1], &variable) ||
            variable == UINT32_MAX)
            goto malformed;
        term.kind = PPOSLF_NATIVE_TERM_VARIABLE_V1;
        term.variable = variable;
        if (!builder->has_variable ||
            variable > builder->maximum_variable)
            builder->maximum_variable = variable;
        builder->has_variable = true;
    } else if (pposlf_native_type_plan_v1_expr_head(
                   quoted, "q-app", 2u)) {
        const Atom *cursor = quoted->expr.elems[2];
        uint32_t child_len;
        uint32_t expected_arity;
        uint32_t child_begin;

        text = pposlf_native_type_plan_v1_quoted_symbol(
            quoted->expr.elems[1]);
        if (!text ||
            !pposlf_native_type_plan_v1_q_list_length(
                cursor, &child_len))
            goto malformed;
        if (!pposlf_native_type_plan_v1_head_arity(
                plan, text, &expected_arity)) {
            pposlf_native_type_plan_v1_set_error(
                builder->error_buf, builder->error_buf_size,
                "OSLF step application uses an undeclared constructor");
            return false;
        }
        if (expected_arity != child_len) {
            pposlf_native_type_plan_v1_set_error(
                builder->error_buf, builder->error_buf_size,
                "OSLF step application violates its head signature");
            return false;
        }
        if (child_len > UINT32_MAX - plan->term_edge_len ||
            !pposlf_native_type_plan_v1_reserve(
                (void **)&plan->term_edges, &builder->term_edge_cap,
                plan->term_edge_len + child_len,
                sizeof(*plan->term_edges))) {
            pposlf_native_type_plan_v1_set_error(
                builder->error_buf, builder->error_buf_size,
                "cannot allocate OSLF term edges");
            return false;
        }
        child_begin = plan->term_edge_len;
        plan->term_edge_len += child_len;
        term.kind = PPOSLF_NATIVE_TERM_APPLICATION_V1;
        term.text = text;
        term.edge_begin = child_begin;
        term.edge_len = child_len;
        for (uint32_t child = 0u; child < child_len; child++) {
            uint32_t child_term;
            if (!pposlf_native_type_plan_v1_compile_term(
                    builder, cursor->expr.elems[1], depth + 1u,
                    &child_term))
                return false;
            plan->term_edges[child_begin + child] = child_term;
            cursor = cursor->expr.elems[2];
        }
    } else {
        goto malformed;
    }

    plan->terms[term_index] = term;
    *term_out = term_index;
    return true;

malformed:
    pposlf_native_type_plan_v1_set_error(
        builder->error_buf, builder->error_buf_size,
        "OSLF step schema contains a malformed quoted term");
    return false;
}

static bool pposlf_native_type_plan_v1_compile_body(
    PPOSLFNativeProgramBuilderV1 *builder,
    const Atom *body,
    uint32_t *begin_out,
    uint32_t *length_out) {
    PPOSLFNativeTypePlanV1 *plan = builder->plan;
    const Atom *cursor = body;
    uint32_t body_len;
    uint32_t body_begin;

    if (!pposlf_native_type_plan_v1_q_list_length(body, &body_len)) {
        pposlf_native_type_plan_v1_set_error(
            builder->error_buf, builder->error_buf_size,
            "OSLF step schema has an improper body list");
        return false;
    }
    if (body_len > UINT32_MAX - plan->body_root_len ||
        !pposlf_native_type_plan_v1_reserve(
            (void **)&plan->body_roots, &builder->body_root_cap,
            plan->body_root_len + body_len,
            sizeof(*plan->body_roots))) {
        pposlf_native_type_plan_v1_set_error(
            builder->error_buf, builder->error_buf_size,
            "cannot allocate OSLF step body roots");
        return false;
    }
    body_begin = plan->body_root_len;
    plan->body_root_len += body_len;
    for (uint32_t item = 0u; item < body_len; item++) {
        uint32_t root;
        if (!pposlf_native_type_plan_v1_compile_term(
                builder, cursor->expr.elems[1], 0u, &root))
            return false;
        plan->body_roots[body_begin + item] = root;
        cursor = cursor->expr.elems[2];
    }
    *begin_out = body_begin;
    *length_out = body_len;
    return true;
}

void pposlf_native_type_plan_v1_init(PPOSLFNativeTypePlanV1 *plan) {
    if (plan)
        memset(plan, 0, sizeof(*plan));
}

void pposlf_native_type_plan_v1_free(PPOSLFNativeTypePlanV1 *plan) {
    PPOSLFNativeTypePlanStorageV1 *storage;

    if (!plan)
        return;
    storage = plan->storage;
    if (storage) {
        fh_answer_stream_v1_free(&storage->answers);
        free(storage);
    }
    free(plan->head_signatures);
    free(plan->terms);
    free(plan->term_edges);
    free(plan->body_roots);
    free(plan->step_schemas);
    free(plan->head_groups);
    free(plan->head_step_indices);
    free(plan->external_relations);
    free(plan->open_heads);
    memset(plan, 0, sizeof(*plan));
}

bool pposlf_native_type_plan_v1_load(
    PPOSLFNativeTypePlanV1 *plan,
    const char *answer_path,
    char *error_buf,
    size_t error_buf_size) {
    PPOSLFNativeTypePlanV1 result;
    PPOSLFNativeTypePlanStorageV1 *storage = NULL;
    size_t index;
    uint32_t signature_len = 0u;
    uint32_t step_len = 0u;
    uint32_t external_len = 0u;
    uint32_t carrier_len = 0u;
    uint32_t transition_semantics_len = 0u;
    uint32_t write = 0u;
    PPOSLFNativeProgramBuilderV1 builder;
    bool ok = false;

    pposlf_native_type_plan_v1_init(&result);
    if (error_buf && error_buf_size > 0u)
        error_buf[0] = '\0';
    if (!plan || !answer_path) {
        pposlf_native_type_plan_v1_set_error(
            error_buf, error_buf_size,
            "invalid OSLF native-type plan request");
        return false;
    }
    storage = calloc(1u, sizeof(*storage));
    if (!storage) {
        pposlf_native_type_plan_v1_set_error(
            error_buf, error_buf_size,
            "cannot allocate OSLF native-type plan storage");
        return false;
    }
    fh_answer_stream_v1_init(&storage->answers);
    if (!fh_answer_stream_v1_read(
            &storage->answers, answer_path,
            error_buf, error_buf_size))
        goto done;
    if (storage->answers.len > UINT32_MAX) {
        pposlf_native_type_plan_v1_set_error(
            error_buf, error_buf_size,
            "OSLF native-type plan is too large");
        goto done;
    }
    for (index = 0u; index < storage->answers.len; index++) {
        const Atom *answer = storage->answers.terms[index];
        const Atom *fact;

        if (!pposlf_native_type_plan_v1_expr_head(
                answer, "compile-oslf-native-type-v1", 1u)) {
            pposlf_native_type_plan_v1_set_error(
                error_buf, error_buf_size,
                "OSLF native-type answer has an unknown wrapper");
            goto done;
        }
        fact = answer->expr.elems[1];
        if (!pposlf_native_type_plan_v1_valid_fact(fact)) {
            pposlf_native_type_plan_v1_set_error(
                error_buf, error_buf_size,
                "OSLF native-type answer has an unknown or malformed fact");
            goto done;
        }
        if (pposlf_native_type_plan_v1_expr_head(
                fact, "oslf-carrier-v1", 1u)) {
            carrier_len++;
        } else if (pposlf_native_type_plan_v1_expr_head(
                       fact, "oslf-transition-semantics-v1", 1u)) {
            transition_semantics_len++;
        } else if (pposlf_native_type_plan_v1_expr_head(
                fact, "oslf-head-signature-v1", 2u)) {
            if (signature_len == UINT32_MAX) {
                pposlf_native_type_plan_v1_set_error(
                    error_buf, error_buf_size,
                    "OSLF head-signature count overflows");
                goto done;
            }
            signature_len++;
        } else if (pposlf_native_type_plan_v1_expr_head(
                       fact, "oslf-step-schema-v1", 4u)) {
            if (step_len == UINT32_MAX) {
                pposlf_native_type_plan_v1_set_error(
                    error_buf, error_buf_size,
                    "OSLF step-schema count overflows");
                goto done;
            }
            step_len++;
        } else if (pposlf_native_type_plan_v1_expr_head(
                       fact, "oslf-external-relation-v1", 3u)) {
            if (external_len == UINT32_MAX) {
                pposlf_native_type_plan_v1_set_error(
                    error_buf, error_buf_size,
                    "OSLF extensional-interface count overflows");
                goto done;
            }
            external_len++;
        }
    }
    if (carrier_len != 1u || transition_semantics_len != 1u) {
        pposlf_native_type_plan_v1_set_error(
            error_buf, error_buf_size,
            "OSLF native-type plan must declare exactly one admitted "
            "finite-Horn carrier and transition semantics");
        goto done;
    }
    result.head_signatures = calloc(
        signature_len ? signature_len : 1u,
        sizeof(*result.head_signatures));
    if (!result.head_signatures) {
        pposlf_native_type_plan_v1_set_error(
            error_buf, error_buf_size,
            "cannot allocate OSLF head signatures");
        goto done;
    }
    result.step_schemas = calloc(
        step_len ? step_len : 1u, sizeof(*result.step_schemas));
    if (!result.step_schemas) {
        pposlf_native_type_plan_v1_set_error(
            error_buf, error_buf_size,
            "cannot allocate OSLF step schemas");
        goto done;
    }
    result.external_relations = calloc(
        external_len ? external_len : 1u,
        sizeof(*result.external_relations));
    if (!result.external_relations) {
        pposlf_native_type_plan_v1_set_error(
            error_buf, error_buf_size,
            "cannot allocate OSLF extensional interfaces");
        goto done;
    }
    for (index = 0u; index < storage->answers.len; index++) {
        const Atom *fact = storage->answers.terms[index]->expr.elems[1];
        PPOSLFNativeHeadSignatureV1 *signature;

        if (!pposlf_native_type_plan_v1_expr_head(
                fact, "oslf-head-signature-v1", 2u))
            continue;
        signature = &result.head_signatures[write++];
        signature->constructor =
            pposlf_native_type_plan_v1_symbol(fact->expr.elems[1]);
        if (!signature->constructor ||
            !pposlf_native_type_plan_v1_qnat(
                fact->expr.elems[2], &signature->arity)) {
            pposlf_native_type_plan_v1_set_error(
                error_buf, error_buf_size,
                "OSLF head signature is malformed");
            goto done;
        }
    }
    qsort(result.head_signatures, signature_len,
          sizeof(*result.head_signatures),
          pposlf_native_type_plan_v1_signature_compare);
    for (write = 1u; write < signature_len; write++) {
        if (strcmp(result.head_signatures[write - 1u].constructor,
                   result.head_signatures[write].constructor) == 0) {
            pposlf_native_type_plan_v1_set_error(
                error_buf, error_buf_size,
                "OSLF head signature is duplicated");
            goto done;
        }
    }
    result.head_signature_len = signature_len;
    write = 0u;
    for (index = 0u; index < storage->answers.len; index++) {
        const Atom *fact = storage->answers.terms[index]->expr.elems[1];
        PPOSLFNativeExternalRelationV1 *external;

        if (!pposlf_native_type_plan_v1_expr_head(
                fact, "oslf-external-relation-v1", 3u))
            continue;
        external = &result.external_relations[write++];
        external->owner = pposlf_native_type_plan_v1_owner(
            fact->expr.elems[1]);
        external->relation = pposlf_native_type_plan_v1_quoted_symbol(
            fact->expr.elems[2]);
        if (!external->owner || !external->relation ||
            !pposlf_native_type_plan_v1_quint32(
                fact->expr.elems[3], &external->arity)) {
            pposlf_native_type_plan_v1_set_error(
                error_buf, error_buf_size,
                "OSLF extensional interface is malformed");
            goto done;
        }
    }
    qsort(result.external_relations, external_len,
          sizeof(*result.external_relations),
          pposlf_native_type_plan_v1_external_compare);
    for (write = 1u; write < external_len; write++) {
        if (strcmp(result.external_relations[write - 1u].relation,
                   result.external_relations[write].relation) == 0 &&
            result.external_relations[write - 1u].arity ==
                result.external_relations[write].arity) {
            pposlf_native_type_plan_v1_set_error(
                error_buf, error_buf_size,
                "OSLF extensional interface is duplicated");
            goto done;
        }
    }
    result.external_relation_len = external_len;
    memset(&builder, 0, sizeof(builder));
    builder.plan = &result;
    builder.error_buf = error_buf;
    builder.error_buf_size = error_buf_size;
    write = 0u;
    for (index = 0u; index < storage->answers.len; index++) {
        const Atom *fact = storage->answers.terms[index]->expr.elems[1];
        PPOSLFNativeStepSchemaV1 *step;

        if (!pposlf_native_type_plan_v1_expr_head(
                fact, "oslf-step-schema-v1", 4u))
            continue;
        step = &result.step_schemas[write++];
        step->owner = pposlf_native_type_plan_v1_owner(
            fact->expr.elems[1]);
        step->rule = pposlf_native_type_plan_v1_symbol(
            fact->expr.elems[2]);
        builder.has_variable = false;
        builder.maximum_variable = 0u;
        if (!step->owner || !step->rule ||
            !pposlf_native_type_plan_v1_compile_term(
                &builder, fact->expr.elems[3], 0u, &step->head) ||
            !pposlf_native_type_plan_v1_compile_body(
                &builder, fact->expr.elems[4],
                &step->body_begin, &step->body_len))
            goto done;
        step->variable_count = builder.has_variable
                                   ? builder.maximum_variable + 1u
                                   : 0u;
    }
    qsort(result.step_schemas, step_len,
          sizeof(*result.step_schemas),
          pposlf_native_type_plan_v1_step_compare);
    for (write = 1u; write < step_len; write++) {
        if (strcmp(result.step_schemas[write - 1u].owner,
                   result.step_schemas[write].owner) == 0 &&
            strcmp(result.step_schemas[write - 1u].rule,
                   result.step_schemas[write].rule) == 0) {
            pposlf_native_type_plan_v1_set_error(
                error_buf, error_buf_size,
                "OSLF step-schema identity is duplicated");
            goto done;
        }
    }
    result.step_schema_len = step_len;
    if (!pposlf_native_type_plan_v1_build_head_index(
            &result, error_buf, error_buf_size))
        goto done;
    if (!pposlf_native_type_plan_v1_build_open_heads(
            &result, error_buf, error_buf_size))
        goto done;
    if (!pposlf_native_type_plan_v1_validate_external_relations(
            &result, error_buf, error_buf_size))
        goto done;
    memcpy(result.semantic_digest, storage->answers.digest,
           sizeof(result.semantic_digest));
    result.storage = storage;
    storage = NULL;
    pposlf_native_type_plan_v1_free(plan);
    *plan = result;
    memset(&result, 0, sizeof(result));
    ok = true;

done:
    if (storage) {
        fh_answer_stream_v1_free(&storage->answers);
        free(storage);
    }
    pposlf_native_type_plan_v1_free(&result);
    return ok;
}

bool pposlf_native_type_plan_v1_head_arity(
    const PPOSLFNativeTypePlanV1 *plan,
    const char *constructor,
    uint32_t *arity_out) {
    uint32_t low = 0u;
    uint32_t high;

    if (!plan || !constructor || !arity_out ||
        (plan->head_signature_len > 0u && !plan->head_signatures))
        return false;
    high = plan->head_signature_len;
    while (low < high) {
        uint32_t middle = low + (high - low) / 2u;
        int comparison = strcmp(
            plan->head_signatures[middle].constructor, constructor);
        if (comparison < 0)
            low = middle + 1u;
        else
            high = middle;
    }
    if (low >= plan->head_signature_len ||
        strcmp(plan->head_signatures[low].constructor, constructor) != 0)
        return false;
    *arity_out = plan->head_signatures[low].arity;
    return true;
}

bool pposlf_native_type_plan_v1_step_range(
    const PPOSLFNativeTypePlanV1 *plan,
    const char *owner,
    const char *rule,
    uint32_t *begin_out,
    uint32_t *end_out) {
    uint32_t low = 0u;
    uint32_t high;

    if (!plan || !owner || !rule || !begin_out || !end_out ||
        (plan->step_schema_len > 0u && !plan->step_schemas))
        return false;
    high = plan->step_schema_len;
    while (low < high) {
        uint32_t middle = low + (high - low) / 2u;
        PPOSLFNativeStepSchemaV1 key = {
            .owner = owner,
            .rule = rule,
        };
        int comparison = pposlf_native_type_plan_v1_step_compare(
            &plan->step_schemas[middle], &key);
        if (comparison < 0)
            low = middle + 1u;
        else
            high = middle;
    }
    if (low >= plan->step_schema_len ||
        strcmp(plan->step_schemas[low].owner, owner) != 0 ||
        strcmp(plan->step_schemas[low].rule, rule) != 0)
        return false;
    *begin_out = low;
    *end_out = low + 1u;
    return true;
}

static bool pposlf_native_type_plan_v1_count_variables(
    const PPOSLFNativeTypePlanV1 *plan,
    uint32_t root,
    uint32_t variable_count,
    uint32_t *counts) {
    const PPOSLFNativeTermV1 *term;

    if (!plan || !counts || root >= plan->term_len)
        return false;
    term = &plan->terms[root];
    if (term->kind == PPOSLF_NATIVE_TERM_VARIABLE_V1) {
        if (term->variable >= variable_count ||
            counts[term->variable] == UINT32_MAX)
            return false;
        counts[term->variable]++;
        return true;
    }
    if (term->kind != PPOSLF_NATIVE_TERM_APPLICATION_V1)
        return true;
    if (term->edge_begin > plan->term_edge_len ||
        term->edge_len > plan->term_edge_len - term->edge_begin)
        return false;
    for (uint32_t child = 0u; child < term->edge_len; child++) {
        if (!pposlf_native_type_plan_v1_count_variables(
                plan, plan->term_edges[term->edge_begin + child],
                variable_count, counts))
            return false;
    }
    return true;
}

bool pposlf_native_type_plan_v1_step_flow(
    const PPOSLFNativeTypePlanV1 *plan,
    uint32_t step_index,
    PPOSLFNativeStepFlowV1 *flow_out) {
    const PPOSLFNativeStepSchemaV1 *step;
    uint32_t *head_counts = NULL;
    uint32_t *body_counts = NULL;
    bool valid = false;

    if (!plan || !flow_out || step_index >= plan->step_schema_len ||
        !plan->step_schemas || !plan->terms ||
        (plan->body_root_len > 0u && !plan->body_roots))
        return false;
    step = &plan->step_schemas[step_index];
    if (step->head >= plan->term_len ||
        step->body_begin > plan->body_root_len ||
        step->body_len > plan->body_root_len - step->body_begin)
        return false;
    head_counts = calloc(
        step->variable_count ? step->variable_count : 1u,
        sizeof(*head_counts));
    body_counts = calloc(
        step->variable_count ? step->variable_count : 1u,
        sizeof(*body_counts));
    if (!head_counts || !body_counts ||
        !pposlf_native_type_plan_v1_count_variables(
            plan, step->head, step->variable_count, head_counts))
        goto done;
    for (uint32_t body = 0u; body < step->body_len; body++) {
        if (!pposlf_native_type_plan_v1_count_variables(
                plan, plan->body_roots[step->body_begin + body],
                step->variable_count, body_counts))
            goto done;
    }
    *flow_out = (PPOSLFNativeStepFlowV1){
        .body_variables_in_head = true,
        .head_linear = true,
    };
    for (uint32_t variable = 0u;
         variable < step->variable_count; variable++) {
        if (body_counts[variable] > 0u && head_counts[variable] == 0u)
            flow_out->body_variables_in_head = false;
        if (head_counts[variable] > 1u)
            flow_out->head_linear = false;
    }
    valid = true;

done:
    free(body_counts);
    free(head_counts);
    return valid;
}

bool pposlf_native_type_plan_v1_external_relation(
    const PPOSLFNativeTypePlanV1 *plan,
    const char *relation,
    uint32_t arity,
    uint32_t *relation_out) {
    uint32_t low = 0u;
    uint32_t high;

    if (!plan || !relation || !relation_out ||
        (plan->external_relation_len > 0u && !plan->external_relations))
        return false;
    high = plan->external_relation_len;
    while (low < high) {
        uint32_t middle = low + (high - low) / 2u;
        const PPOSLFNativeExternalRelationV1 *external =
            &plan->external_relations[middle];
        int comparison = strcmp(external->relation, relation);

        if (comparison == 0) {
            if (external->arity < arity)
                comparison = -1;
            else if (external->arity > arity)
                comparison = 1;
        }
        if (comparison < 0)
            low = middle + 1u;
        else
            high = middle;
    }
    if (low >= plan->external_relation_len ||
        strcmp(plan->external_relations[low].relation, relation) != 0 ||
        plan->external_relations[low].arity != arity)
        return false;
    *relation_out = low;
    return true;
}

bool pposlf_native_type_plan_v1_head_group(
    const PPOSLFNativeTypePlanV1 *plan,
    PPOSLFNativeTermKindV1 kind,
    const char *text,
    int64_t integer,
    uint32_t *group_out) {
    uint32_t low = 0u;
    uint32_t high;

    if (!plan || !group_out ||
        (pposlf_native_type_plan_v1_head_key_uses_text(kind) && !text) ||
        (plan->head_group_len > 0u && !plan->head_groups))
        return false;
    high = plan->head_group_len;
    while (low < high) {
        uint32_t middle = low + (high - low) / 2u;
        const PPOSLFNativeHeadGroupV1 *group =
            &plan->head_groups[middle];
        int comparison = pposlf_native_type_plan_v1_head_key_compare(
            group->kind, group->text, group->integer,
            kind, text, integer);
        if (comparison < 0)
            low = middle + 1u;
        else
            high = middle;
    }
    if (low >= plan->head_group_len ||
        pposlf_native_type_plan_v1_head_key_compare(
            plan->head_groups[low].kind,
            plan->head_groups[low].text,
            plan->head_groups[low].integer,
            kind, text, integer) != 0)
        return false;
    *group_out = low;
    return true;
}
