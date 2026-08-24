#include "gslt_rhometta_direct_v1.h"

#include "gslt_composition_v1.h"

#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define GSLT_RHOMETTA_DIRECT_DEPTH_LIMIT_V1 4096u

typedef struct {
    uint8_t *bytes;
    size_t len;
    size_t cap;
} RhoBufferV1;

typedef struct {
    const char *name;
    size_t arity;
} RhoRelationV1;

typedef struct {
    RhoRelationV1 *items;
    size_t len;
    size_t cap;
} RhoRelationsV1;

typedef struct {
    const char *name;
    size_t arity;
} RhoCapabilityV1;

typedef struct {
    RhoCapabilityV1 *items;
    size_t len;
    size_t cap;
} RhoCapabilitiesV1;

typedef struct {
    const CettaGsltCompositionV1 *composition;
    const uint8_t *active_rules;
    const RhoRelationsV1 *relations;
    const RhoCapabilitiesV1 *capabilities;
    const char *digest;
    Arena *scratch;
} RhoRenderContextV1;

static bool rho_render_rule_sequence_v1(
    RhoBufferV1 *buffer, const RhoRenderContextV1 *context,
    size_t rule_index, size_t body_index,
    const char *variable_prefix,
    const RhoBufferV1 *request_token,
    const RhoBufferV1 *path_token,
    const char *reply_variable);

static bool rho_render_relation_dispatch_instance_name_v1(
    RhoBufferV1 *buffer, const RhoRenderContextV1 *context,
    size_t relation_index);

static bool rho_error_v1(
    char *error, size_t error_size, const char *format, ...) {
    if (error != NULL && error_size > 0u) {
        va_list arguments;
        va_start(arguments, format);
        (void)vsnprintf(error, error_size, format, arguments);
        va_end(arguments);
    }
    return false;
}

static bool rho_reserve_v1(RhoBufferV1 *buffer, size_t extra) {
    size_t required;
    size_t next;
    uint8_t *grown;
    if (extra > SIZE_MAX - buffer->len)
        return false;
    required = buffer->len + extra;
    if (required <= buffer->cap)
        return true;
    next = buffer->cap == 0u ? 1024u : buffer->cap;
    while (next < required) {
        if (next > SIZE_MAX / 2u) {
            next = required;
            break;
        }
        next *= 2u;
    }
    grown = (uint8_t *)realloc(buffer->bytes, next);
    if (grown == NULL)
        return false;
    buffer->bytes = grown;
    buffer->cap = next;
    return true;
}

static bool rho_append_v1(
    RhoBufferV1 *buffer, const void *bytes, size_t len) {
    if (!rho_reserve_v1(buffer, len))
        return false;
    if (len > 0u)
        memcpy(buffer->bytes + buffer->len, bytes, len);
    buffer->len += len;
    return true;
}

static bool rho_literal_v1(RhoBufferV1 *buffer, const char *text) {
    return rho_append_v1(buffer, text, strlen(text));
}

static bool rho_fragment_v1(
    RhoBufferV1 *buffer, const RhoBufferV1 *fragment) {
    return rho_append_v1(buffer, fragment->bytes, fragment->len);
}

static bool rho_printf_v1(RhoBufferV1 *buffer, const char *format, ...) {
    char local[256];
    va_list arguments;
    int written;
    va_start(arguments, format);
    written = vsnprintf(local, sizeof(local), format, arguments);
    va_end(arguments);
    if (written < 0)
        return false;
    if ((size_t)written < sizeof(local))
        return rho_append_v1(buffer, local, (size_t)written);
    char *large = (char *)malloc((size_t)written + 1u);
    if (large == NULL)
        return false;
    va_start(arguments, format);
    int repeated = vsnprintf(large, (size_t)written + 1u, format, arguments);
    va_end(arguments);
    bool ok = repeated == written &&
              rho_append_v1(buffer, large, (size_t)written);
    free(large);
    return ok;
}

static bool rho_push_relation_v1(
    RhoRelationsV1 *relations, const char *name, size_t arity,
    char *error, size_t error_size) {
    for (size_t index = 0u; index < relations->len; index++) {
        if (relations->items[index].arity == arity &&
            strcmp(relations->items[index].name, name) == 0)
            return true;
    }
    if (relations->len == relations->cap) {
        size_t next = relations->cap == 0u ? 16u : relations->cap * 2u;
        RhoRelationV1 *grown;
        if (next < relations->cap ||
            next > SIZE_MAX / sizeof(*relations->items))
            return rho_error_v1(
                error, error_size, "GSLT rho relation table is too large");
        grown = (RhoRelationV1 *)realloc(
            relations->items, next * sizeof(*relations->items));
        if (grown == NULL)
            return rho_error_v1(
                error, error_size,
                "out of memory collecting GSLT rho relations");
        relations->items = grown;
        relations->cap = next;
    }
    relations->items[relations->len++] =
        (RhoRelationV1){.name = name, .arity = arity};
    return true;
}

static bool rho_has_relation_v1(
    const RhoRelationsV1 *relations, const char *name, size_t arity) {
    for (size_t index = 0u; index < relations->len; index++) {
        if (relations->items[index].arity == arity &&
            strcmp(relations->items[index].name, name) == 0)
            return true;
    }
    return false;
}

static bool rho_push_capability_v1(
    RhoCapabilitiesV1 *capabilities, const char *name, size_t arity,
    char *error, size_t error_size) {
    for (size_t index = 0u; index < capabilities->len; index++) {
        if (capabilities->items[index].arity == arity &&
            strcmp(capabilities->items[index].name, name) == 0)
            return true;
    }
    if (capabilities->len == capabilities->cap) {
        size_t next = capabilities->cap == 0u
            ? 8u : capabilities->cap * 2u;
        RhoCapabilityV1 *grown;
        if (next < capabilities->cap ||
            next > SIZE_MAX / sizeof(*capabilities->items))
            return rho_error_v1(
                error, error_size,
                "GSLT rho capability table is too large");
        grown = (RhoCapabilityV1 *)realloc(
            capabilities->items,
            next * sizeof(*capabilities->items));
        if (grown == NULL)
            return rho_error_v1(
                error, error_size,
                "out of memory collecting GSLT rho capabilities");
        capabilities->items = grown;
        capabilities->cap = next;
    }
    capabilities->items[capabilities->len++] =
        (RhoCapabilityV1){.name = name, .arity = arity};
    return true;
}

static const RhoCapabilityV1 *rho_find_capability_v1(
    const RhoCapabilitiesV1 *capabilities,
    const char *name, size_t arity) {
    for (size_t index = 0u; index < capabilities->len; index++) {
        if (capabilities->items[index].arity == arity &&
            strcmp(capabilities->items[index].name, name) == 0)
            return &capabilities->items[index];
    }
    return NULL;
}

static bool rho_positive_arity_v1(const Atom *atom, size_t *out) {
    if (atom == NULL || out == NULL || atom->kind != ATOM_GROUNDED ||
        atom->ground.gkind != GV_INT || atom->ground.ival <= 0)
        return false;
    if ((uint64_t)atom->ground.ival > SIZE_MAX)
        return false;
    *out = (size_t)atom->ground.ival;
    return true;
}

static bool rho_render_term_v1(
    RhoBufferV1 *buffer, const Atom *term, const char *variable_prefix,
    Arena *scratch, size_t depth) {
    if (term == NULL || depth > GSLT_RHOMETTA_DIRECT_DEPTH_LIMIT_V1)
        return false;
    if (cetta_gslt_source_variable_v1(term))
        return rho_printf_v1(
            buffer, "$%s%s", variable_prefix,
            cetta_gslt_source_variable_name_v1(term));
    if (term->kind == ATOM_SYMBOL)
        return rho_literal_v1(buffer, atom_name_cstr((Atom *)term));
    if (term->kind == ATOM_VAR || term->kind == ATOM_GROUNDED) {
        const char *printed = atom_to_parseable_string(scratch, (Atom *)term);
        return printed != NULL && rho_literal_v1(buffer, printed);
    }
    if (term->kind == ATOM_EXPR && term->expr.len == 2u &&
        term->expr.elems[0]->kind == ATOM_SYMBOL &&
        strcmp(atom_name_cstr(term->expr.elems[0]), "metta-nullary") == 0) {
        if (term->expr.elems[1]->kind == ATOM_SYMBOL &&
            strcmp(atom_name_cstr(term->expr.elems[1]), "empty") == 0)
            return rho_literal_v1(buffer, "(superpose ())");
        return rho_literal_v1(buffer, "(") &&
               rho_render_term_v1(
                   buffer, term->expr.elems[1], variable_prefix,
                   scratch, depth + 1u) &&
               rho_literal_v1(buffer, ")");
    }
    if (term->kind != ATOM_EXPR || term->expr.len == 0u ||
        !rho_literal_v1(buffer, "("))
        return false;
    for (CettaExprIndex index = 0u; index < term->expr.len; index++) {
        if (index > 0u && !rho_literal_v1(buffer, " "))
            return false;
        if (!rho_render_term_v1(
                buffer, term->expr.elems[index], variable_prefix,
                scratch, depth + 1u))
            return false;
    }
    return rho_literal_v1(buffer, ")");
}

static bool rho_static_name_v1(
    RhoBufferV1 *buffer, const char *kind, const char *digest,
    size_t first, size_t second) {
    return rho_printf_v1(
        buffer,
        "(rho:quote (rho:val (gslt-rho:%s-v1 sha256-%s %zu %zu)))",
        kind, digest, first, second);
}

static bool rho_dynamic_name_v1(
    RhoBufferV1 *buffer, const char *kind, const char *digest,
    const RhoBufferV1 *token) {
    return rho_printf_v1(
               buffer,
               "(rho:quote (rho:val (gslt-rho:%s-v1 sha256-%s ",
               kind, digest) &&
           rho_fragment_v1(buffer, token) &&
           rho_literal_v1(buffer, ")))" );
}

static bool rho_eval_name_v1(
    RhoBufferV1 *buffer, const char *digest,
    size_t rule_index, size_t site_index, const char *request_variable) {
    return rho_printf_v1(
        buffer,
        "(rho:quote (rho:par "
        "(rho:val (gslt-rho:eval-site-v1 sha256-%s %zu %zu)) "
        "(rho:drop $%s)))",
        digest, rule_index, site_index, request_variable);
}

static bool rho_render_persistent_receive_open_v1(
    RhoBufferV1 *buffer, const RhoBufferV1 *channel,
    const char *request_variable) {
    return rho_literal_v1(buffer, "(rho:recv-persistent ") &&
           rho_fragment_v1(buffer, channel) &&
           rho_printf_v1(buffer, " $%s ", request_variable);
}

static bool rho_render_persistent_receive_close_v1(RhoBufferV1 *buffer) {
    return rho_literal_v1(buffer, ")");
}

static bool rho_render_eval_open_v1(
    RhoBufferV1 *buffer, const char *digest,
    size_t rule_index, size_t site_index,
    const char *request_variable) {
    return rho_literal_v1(buffer, "(rho:par (rho:send ") &&
           rho_eval_name_v1(
               buffer, digest, rule_index, site_index, request_variable) &&
           rho_literal_v1(buffer, " (rho:eval-payload (quote ");
}

static bool rho_render_eval_close_v1(
    RhoBufferV1 *buffer, const char *digest,
    size_t rule_index, size_t site_index,
    const char *request_variable, const char *result_variable) {
    return rho_literal_v1(buffer, "))) (rho:recv ") &&
           rho_eval_name_v1(
               buffer, digest, rule_index, site_index, request_variable) &&
           rho_printf_v1(
               buffer, " $%s (rho:drop $%s)))",
               result_variable, result_variable);
}

static bool rho_relation_for_goal_v1(
    const Atom *goal, const char **name_out, size_t *arity_out) {
    if (goal == NULL || goal->kind != ATOM_EXPR || goal->expr.len == 0u ||
        goal->expr.elems[0]->kind != ATOM_SYMBOL)
        return false;
    *name_out = atom_name_cstr(goal->expr.elems[0]);
    *arity_out = (size_t)goal->expr.len - 1u;
    return true;
}

static bool rho_goal_head_v1(
    const Atom *goal, const char *name, size_t arity) {
    const char *goal_name;
    size_t goal_arity;
    return rho_relation_for_goal_v1(goal, &goal_name, &goal_arity) &&
           goal_arity == arity && strcmp(goal_name, name) == 0;
}

static bool rho_target_equation_defines_capability_v1(
    const CettaGsltCompositionV1 *composition,
    const RhoCapabilityV1 *capability) {
    char wrapper[512];
    int written = snprintf(
        wrapper, sizeof(wrapper), "gslt:%s", capability->name);
    size_t definitions = 0u;
    if (written < 0 || (size_t)written >= sizeof(wrapper))
        return false;
    for (size_t index = 0u; index < composition->rewrite_count; index++) {
        const Atom *equation = composition->rewrites[index].head;
        const Atom *left;
        const Atom *relation;
        if (!rho_goal_head_v1(equation, "metta-equation", 2u) ||
            !(left = equation->expr.elems[1]) ||
            !rho_goal_head_v1(left, wrapper, 1u) ||
            !(relation = left->expr.elems[1]) ||
            !rho_goal_head_v1(
                relation, capability->name, capability->arity))
            continue;
        definitions++;
    }
    return definitions == 1u;
}

static bool rho_classify_metadata_v1(
    const CettaGsltCompositionV1 *composition,
    uint8_t *active_rules, size_t *active_rule_count,
    RhoCapabilitiesV1 *capabilities,
    char *error, size_t error_size) {
    for (size_t index = 0u; index < composition->rewrite_count; index++) {
        const CettaGsltRewriteV1 *rewrite = &composition->rewrites[index];
        if (rho_goal_head_v1(
                rewrite->head, "oslf-external-relation-decl-v1", 2u)) {
            size_t arity;
            if (rewrite->body->expr.len != 1u ||
                rewrite->head->expr.elems[1]->kind != ATOM_SYMBOL ||
                !rho_positive_arity_v1(
                    rewrite->head->expr.elems[2], &arity) ||
                !rho_push_capability_v1(
                    capabilities,
                    atom_name_cstr(rewrite->head->expr.elems[1]), arity,
                    error, error_size))
                return rho_error_v1(
                    error, error_size,
                    "direct Rhometta external relation declaration is malformed");
            if (active_rules[index]) {
                active_rules[index] = 0u;
                (*active_rule_count)--;
            }
        } else if (rho_goal_head_v1(
                       rewrite->head, "metta-equation", 2u)) {
            if (rewrite->body->expr.len != 1u ||
                !cetta_gslt_composition_validate_term_v1(
                    composition, rewrite->head->expr.elems[1],
                    GSLT_RHOMETTA_DIRECT_DEPTH_LIMIT_V1,
                    error, error_size) ||
                !cetta_gslt_composition_validate_term_v1(
                    composition, rewrite->head->expr.elems[2],
                    GSLT_RHOMETTA_DIRECT_DEPTH_LIMIT_V1,
                    error, error_size))
                return rho_error_v1(
                    error, error_size,
                    "direct Rhometta target equations must be unconditional");
            if (atom_eq(
                    rewrite->head->expr.elems[1],
                    rewrite->head->expr.elems[2]))
                return rho_error_v1(
                    error, error_size,
                    "direct Rhometta rejects a structurally identical target equation");
            if (active_rules[index]) {
                active_rules[index] = 0u;
                (*active_rule_count)--;
            }
        }
    }
    return true;
}

static bool rho_render_target_equations_v1(
    RhoBufferV1 *program, const RhoRenderContextV1 *context) {
    bool header = false;
    for (size_t index = 0u;
         index < context->composition->rewrite_count; index++) {
        const Atom *equation = context->composition->rewrites[index].head;
        if (!rho_goal_head_v1(equation, "metta-equation", 2u))
            continue;
        if ((!header &&
             !rho_literal_v1(program, "; authored target equations\n")) ||
            !rho_literal_v1(program, "(= ") ||
            !rho_render_term_v1(
                program, equation->expr.elems[1], "",
                context->scratch, 0u) ||
            !rho_literal_v1(program, " ") ||
            !rho_render_term_v1(
                program, equation->expr.elems[2], "",
                context->scratch, 0u) ||
            !rho_literal_v1(program, ")\n"))
            return false;
        header = true;
    }
    return !header || rho_literal_v1(program, "\n");
}

static const RhoCapabilityV1 *rho_goal_capability_v1(
    const RhoRenderContextV1 *context, const Atom *goal) {
    const char *relation;
    size_t arity;
    if (!rho_relation_for_goal_v1(goal, &relation, &arity))
        return NULL;
    for (size_t index = 0u;
         index < context->composition->rewrite_count; index++) {
        const Atom *head;
        const char *head_relation;
        size_t head_arity;
        if (!context->active_rules[index])
            continue;
        head = context->composition->rewrites[index].head;
        if (rho_relation_for_goal_v1(
                head, &head_relation, &head_arity) &&
            head_arity == arity && strcmp(head_relation, relation) == 0)
            return NULL;
    }
    return rho_find_capability_v1(
        context->capabilities, relation, arity);
}

static bool rho_render_child_token_v1(
    RhoBufferV1 *buffer, const RhoRenderContextV1 *context,
    const RhoBufferV1 *path_token, size_t rule_index, size_t body_index) {
    return rho_printf_v1(
               buffer,
               "(gslt-rho:child-token-v1 sha256-%s ", context->digest) &&
           rho_fragment_v1(buffer, path_token) &&
           rho_printf_v1(buffer, " %zu %zu)", rule_index, body_index);
}

static bool rho_render_next_path_token_v1(
    RhoBufferV1 *buffer, const char *returned_variable,
    const char *occurrence_variable) {
    /* The occurrence carries the child token, whose path is the prior path.
     * Repeating that path here unfolds the same history twice at every
     * premise even though the hash-consed graph contains little new data. */
    return rho_printf_v1(
        buffer, "(gslt-rho:path-token-v1 $%s $%s)",
        returned_variable, occurrence_variable);
}

static bool rho_rule_defines_relation_v1(
    const RhoRenderContextV1 *context, size_t rule_index,
    const char *relation, size_t arity) {
    const Atom *head;
    const char *head_relation;
    size_t head_arity;
    if (!context->active_rules[rule_index])
        return false;
    head = context->composition->rewrites[rule_index].head;
    return rho_relation_for_goal_v1(
               head, &head_relation, &head_arity) &&
           head_arity == arity && strcmp(head_relation, relation) == 0;
}

static bool rho_relation_index_v1(
    const RhoRelationsV1 *relations,
    const char *name, size_t arity, size_t *index_out) {
    for (size_t index = 0u; index < relations->len; index++) {
        if (relations->items[index].arity == arity &&
            strcmp(relations->items[index].name, name) == 0) {
            *index_out = index;
            return true;
        }
    }
    return false;
}

static bool rho_render_rule_request_v1(
    RhoBufferV1 *buffer, const RhoRenderContextV1 *context,
    size_t rule_index, const RhoBufferV1 *goal,
    const RhoBufferV1 *token, const RhoBufferV1 *reply) {
    return rho_literal_v1(buffer, "(rho:send ") &&
           rho_static_name_v1(
               buffer, "rule-channel", context->digest, rule_index, 0u) &&
           rho_literal_v1(
               buffer,
               " (rho:eval-payload (quote (gslt-rho:request-v1 ") &&
           rho_fragment_v1(buffer, token) &&
           rho_literal_v1(buffer, " ") &&
           rho_fragment_v1(buffer, goal) &&
           rho_literal_v1(buffer, " ") &&
           rho_fragment_v1(buffer, reply) &&
           rho_literal_v1(buffer, "))))");
}

static bool rho_render_dispatch_candidates_v1(
    RhoBufferV1 *buffer, const RhoRenderContextV1 *context,
    const char *relation, size_t arity, const RhoBufferV1 *goal,
    const RhoBufferV1 *token, const RhoBufferV1 *reply) {
    size_t matches = 0u;
    if (!rho_literal_v1(buffer, "(rho:par"))
        return false;
    for (size_t rule_index = 0u;
         rule_index < context->composition->rewrite_count; rule_index++) {
        char head_variable_prefix[96];
        if (!rho_rule_defines_relation_v1(
                context, rule_index, relation, arity))
            continue;
        matches++;
        if (snprintf(
                head_variable_prefix, sizeof(head_variable_prefix),
                "__gslt_rho_dispatch_r%zu_", rule_index) < 0 ||
            !rho_literal_v1(
                buffer,
                " (if (== (collapse (unify ") ||
            !rho_fragment_v1(buffer, goal) ||
            !rho_literal_v1(buffer, " ") ||
            !rho_render_term_v1(
                buffer, context->composition->rewrites[rule_index].head,
                head_variable_prefix, context->scratch, 0u) ||
            !rho_literal_v1(
                buffer,
                " gslt-rho:compatible-v1 (superpose ()))) "
                "(gslt-rho:compatible-v1)) ") ||
            !rho_render_rule_request_v1(
                buffer, context, rule_index, goal, token, reply) ||
            !rho_literal_v1(buffer, " (rho:par))"))
            return false;
    }
    return matches > 0u && rho_literal_v1(buffer, ")");
}

static bool rho_render_relation_dispatch_v1(
    RhoBufferV1 *buffer, const RhoRenderContextV1 *context,
    size_t relation_index, const RhoBufferV1 *goal,
    const RhoBufferV1 *token, const RhoBufferV1 *reply,
    size_t owner_index, size_t site_index) {
    RhoBufferV1 dispatch_channel = {0};
    RhoBufferV1 request = {0};
    char result_variable[96];
    bool ok = false;
    if (snprintf(
            result_variable, sizeof(result_variable),
            "__gslt_rho_dispatch_result_%zu_%zu",
            owner_index, site_index) < 0 ||
        !rho_literal_v1(&request, "(gslt-rho:request-v1 ") ||
        !rho_fragment_v1(&request, token) ||
        !rho_literal_v1(&request, " ") ||
        !rho_fragment_v1(&request, goal) ||
        !rho_literal_v1(&request, " ") ||
        !rho_fragment_v1(&request, reply) ||
        !rho_literal_v1(&request, ")") ||
        !rho_dynamic_name_v1(
            &dispatch_channel, "dispatch-eval", context->digest, token) ||
        !rho_literal_v1(buffer, "(rho:par (rho:send ") ||
        !rho_fragment_v1(buffer, &dispatch_channel) ||
        !rho_literal_v1(
            buffer,
            " (rho:eval-payload (quote (") ||
        !rho_render_relation_dispatch_instance_name_v1(
            buffer, context, relation_index) ||
        !rho_literal_v1(buffer, " ") ||
        !rho_fragment_v1(buffer, &request) ||
        !rho_literal_v1(buffer, ")))) (rho:recv ") ||
        !rho_fragment_v1(buffer, &dispatch_channel) ||
        !rho_printf_v1(
            buffer, " $%s (rho:drop $%s)))",
            result_variable, result_variable))
        goto done;
    ok = true;

done:
    free(request.bytes);
    free(dispatch_channel.bytes);
    return ok;
}

static bool rho_render_relation_call_v1(
    RhoBufferV1 *buffer, const RhoRenderContextV1 *context,
    const Atom *goal, const char *variable_prefix,
    const RhoBufferV1 *token, const RhoBufferV1 *reply,
    size_t owner_index, size_t site_index) {
    const char *relation;
    size_t arity;
    size_t relation_index;
    RhoBufferV1 rendered_goal = {0};
    bool ok = false;
    if (!rho_relation_for_goal_v1(goal, &relation, &arity) ||
        !rho_relation_index_v1(
            context->relations, relation, arity, &relation_index) ||
        !rho_render_term_v1(
            &rendered_goal, goal, variable_prefix,
            context->scratch, 0u))
        goto done;
    ok = rho_render_relation_dispatch_v1(
        buffer, context, relation_index, &rendered_goal, token, reply,
        owner_index, site_index);

done:
    free(rendered_goal.bytes);
    return ok;
}

static bool rho_render_evidence_v1(
    RhoBufferV1 *buffer, const RhoRenderContextV1 *context,
    const CettaGsltRewriteV1 *rewrite, size_t rule_index,
    size_t evidence_count, const char *variable_prefix) {
    if (!rho_printf_v1(
            buffer, "(gslt-rho:derivation-v1 %s ", rewrite->name))
        return false;
    for (size_t index = 0u; index < evidence_count; index++) {
        const Atom *goal = rewrite->body->expr.elems[index + 1u];
        const RhoCapabilityV1 *capability =
            rho_goal_capability_v1(context, goal);
        if (!rho_literal_v1(buffer, "(gslt-rho:evidence-cons-v1 "))
            return false;
        if (capability != NULL) {
            if (!rho_printf_v1(
                    buffer, "(gslt-rho:capability-evidence-v1 %s ",
                    capability->name) ||
                !rho_render_term_v1(
                    buffer, goal, variable_prefix,
                    context->scratch, 0u) ||
                !rho_literal_v1(buffer, ") "))
                return false;
        } else if (!rho_printf_v1(
                       buffer, "$__gslt_rho_evidence_%zu_%zu ",
                       rule_index, index)) {
            return false;
        }
    }
    if (!rho_literal_v1(buffer, "gslt-rho:evidence-nil-v1"))
        return false;
    for (size_t index = 0u; index < evidence_count; index++) {
        if (!rho_literal_v1(buffer, ")"))
            return false;
    }
    return rho_literal_v1(buffer, ")");
}

static bool rho_render_rule_sequence_v1(
    RhoBufferV1 *buffer, const RhoRenderContextV1 *context,
    size_t rule_index, size_t body_index,
    const char *variable_prefix,
    const RhoBufferV1 *request_token,
    const RhoBufferV1 *path_token,
    const char *reply_variable) {
    const CettaGsltRewriteV1 *rewrite =
        &context->composition->rewrites[rule_index];
    size_t body_count = (size_t)rewrite->body->expr.len - 1u;
    if (body_index == body_count) {
        if (!rho_printf_v1(buffer, "(rho:send $%s ", reply_variable) ||
            !rho_literal_v1(
                buffer,
                "(rho:eval-payload (quote (gslt-rho:answer-v1 ") ||
            !rho_fragment_v1(buffer, request_token) ||
            !rho_literal_v1(
                buffer, " (gslt-rho:answer-occurrence-v1 ") ||
            !rho_fragment_v1(buffer, request_token) ||
            !rho_printf_v1(buffer, " %s ", rewrite->name) ||
            !rho_render_evidence_v1(
                buffer, context, rewrite, rule_index, body_count,
                variable_prefix) ||
            !rho_literal_v1(buffer, ") ") ||
            !rho_render_term_v1(
                buffer, rewrite->head, variable_prefix,
                context->scratch, 0u) ||
            !rho_literal_v1(buffer, " ") ||
            !rho_render_evidence_v1(
                buffer, context, rewrite, rule_index, body_count,
                variable_prefix) ||
            !rho_literal_v1(buffer, "))))"))
            return false;
        return true;
    }

    Atom *goal = rewrite->body->expr.elems[body_index + 1u];
    const RhoCapabilityV1 *capability =
        rho_goal_capability_v1(context, goal);
    if (capability != NULL) {
        char capability_result[96];
        size_t capability_end = body_index;
        while (capability_end < body_count) {
            const Atom *current_goal =
                rewrite->body->expr.elems[capability_end + 1u];
            const RhoCapabilityV1 *current_capability =
                rho_goal_capability_v1(context, current_goal);
            if (current_capability == NULL)
                break;
            if (!rho_target_equation_defines_capability_v1(
                    context->composition, current_capability))
                return rho_literal_v1(buffer, "(rho:par)");
            capability_end++;
        }
        if (snprintf(
                capability_result, sizeof(capability_result),
                "__gslt_rho_capability_result_%zu_%zu",
                rule_index, body_index) < 0 ||
            !rho_render_eval_open_v1(
                buffer, context->digest, rule_index, body_index + 1u,
                reply_variable))
            return false;
        for (size_t index = body_index; index < capability_end; index++) {
            const Atom *current_goal =
                rewrite->body->expr.elems[index + 1u];
            const RhoCapabilityV1 *current_capability =
                rho_goal_capability_v1(context, current_goal);
            if (current_capability == NULL ||
                !rho_printf_v1(
                    buffer,
                    "(let $__gslt_rho_capability_value_%zu_%zu (gslt:",
                    rule_index, index) ||
                !rho_literal_v1(buffer, current_capability->name) ||
                !rho_literal_v1(buffer, " ") ||
                !rho_render_term_v1(
                    buffer, current_goal, variable_prefix,
                    context->scratch, 0u) ||
                !rho_literal_v1(buffer, ") "))
                return false;
        }
        if (!rho_render_rule_sequence_v1(
                buffer, context, rule_index, capability_end,
                variable_prefix, request_token, path_token,
                reply_variable))
            return false;
        for (size_t index = body_index; index < capability_end; index++) {
            if (!rho_literal_v1(buffer, ")"))
                return false;
        }
        if (!rho_render_eval_close_v1(
                buffer, context->digest, rule_index, body_index + 1u,
                reply_variable, capability_result))
            return false;
        return true;
    }
    RhoBufferV1 child_token = {0};
    RhoBufferV1 child_reply = {0};
    RhoBufferV1 next_path = {0};
    char response_variable[96];
    char result_variable[96];
    char returned_variable[96];
    char occurrence_variable[96];
    char evidence_variable[96];
    bool ok = false;

    if (snprintf(response_variable, sizeof(response_variable),
                 "__gslt_rho_response_%zu_%zu",
                 rule_index, body_index) < 0 ||
        snprintf(result_variable, sizeof(result_variable),
                 "__gslt_rho_cont_result_%zu_%zu",
                 rule_index, body_index) < 0 ||
        snprintf(returned_variable, sizeof(returned_variable),
                 "__gslt_rho_returned_%zu_%zu",
                 rule_index, body_index) < 0 ||
        snprintf(occurrence_variable, sizeof(occurrence_variable),
                 "__gslt_rho_occurrence_%zu_%zu",
                 rule_index, body_index) < 0 ||
        snprintf(evidence_variable, sizeof(evidence_variable),
                 "__gslt_rho_evidence_%zu_%zu",
                 rule_index, body_index) < 0)
        goto done;
    if (!rho_render_child_token_v1(
            &child_token, context, path_token, rule_index, body_index) ||
        !rho_dynamic_name_v1(
            &child_reply, "reply", context->digest, &child_token))
        goto done;

    if (!rho_literal_v1(buffer, "(rho:par ") ||
        !rho_render_persistent_receive_open_v1(
            buffer, &child_reply, response_variable) ||
        !rho_render_eval_open_v1(
            buffer, context->digest, rule_index, body_index + 1u,
            response_variable) ||
        !rho_literal_v1(
            buffer,
            "(let (rho:quote (rho:val (gslt-rho:answer-v1 ") ||
        !rho_fragment_v1(buffer, &child_token) ||
        !rho_printf_v1(
            buffer, " $%s $%s $%s))) $%s (unify $%s ",
            occurrence_variable, returned_variable, evidence_variable,
            response_variable, returned_variable) ||
        !rho_render_term_v1(
            buffer, goal, variable_prefix, context->scratch, 0u) ||
        !rho_literal_v1(buffer, " "))
        goto done;
    if (!rho_render_next_path_token_v1(
            &next_path, returned_variable, occurrence_variable) ||
        !rho_render_rule_sequence_v1(
            buffer, context, rule_index, body_index + 1u,
            variable_prefix, request_token, &next_path, reply_variable) ||
        !rho_literal_v1(buffer, " (superpose ())))") ||
        !rho_render_eval_close_v1(
            buffer, context->digest, rule_index, body_index + 1u,
            response_variable, result_variable) ||
        !rho_render_persistent_receive_close_v1(buffer) ||
        !rho_literal_v1(buffer, " ") ||
        !rho_render_relation_call_v1(
            buffer, context, goal, variable_prefix,
            &child_token, &child_reply, rule_index, body_index) ||
        !rho_literal_v1(buffer, ")"))
        goto done;
    ok = true;

done:
    free(next_path.bytes);
    free(child_reply.bytes);
    free(child_token.bytes);
    return ok;
}

static bool rho_render_relation_dispatch_instance_name_v1(
    RhoBufferV1 *buffer, const RhoRenderContextV1 *context,
    size_t relation_index) {
    return rho_printf_v1(
        buffer, "gslt-rho:relation-instance-sha256-%s-q%zu-v1",
        context->digest, relation_index);
}

/* Factoring the dispatch expression keeps one authored branch table per
 * relation.  Invocation still happens in the caller's fresh eval scope; the
 * relation is not turned into a shared stateful service. */
static bool rho_render_relation_dispatch_instance_equation_v1(
    RhoBufferV1 *buffer, const RhoRenderContextV1 *context,
    const RhoRelationV1 *relation, size_t relation_index) {
    RhoBufferV1 request_token = {0};
    RhoBufferV1 goal = {0};
    RhoBufferV1 reply = {0};
    char request_variable[96];
    char token_variable[96];
    char goal_variable[96];
    char reply_variable[96];
    bool ok = false;

    if (snprintf(request_variable, sizeof(request_variable),
                 "__gslt_rho_relation_request_%zu", relation_index) < 0 ||
        snprintf(token_variable, sizeof(token_variable),
                 "__gslt_rho_relation_token_%zu", relation_index) < 0 ||
        snprintf(goal_variable, sizeof(goal_variable),
                 "__gslt_rho_relation_goal_%zu", relation_index) < 0 ||
        snprintf(reply_variable, sizeof(reply_variable),
                 "__gslt_rho_relation_reply_%zu", relation_index) < 0 ||
        !rho_printf_v1(&request_token, "$%s", token_variable) ||
        !rho_printf_v1(&goal, "$%s", goal_variable) ||
        !rho_printf_v1(&reply, "$%s", reply_variable) ||
        !rho_literal_v1(buffer, "(= (") ||
        !rho_render_relation_dispatch_instance_name_v1(
            buffer, context, relation_index) ||
        !rho_printf_v1(
            buffer,
            " $%s)\n   (let "
            "(gslt-rho:request-v1 $%s $%s $%s) $%s ",
            request_variable, token_variable, goal_variable,
            reply_variable, request_variable) ||
        !rho_render_dispatch_candidates_v1(
            buffer, context, relation->name, relation->arity,
            &goal, &request_token, &reply) ||
        !rho_literal_v1(buffer, "))\n\n"))
        goto done;
    ok = true;

done:
    free(reply.bytes);
    free(goal.bytes);
    free(request_token.bytes);
    return ok;
}

static bool rho_render_rule_instance_name_v1(
    RhoBufferV1 *buffer, const RhoRenderContextV1 *context,
    size_t rule_index) {
    return rho_printf_v1(
        buffer, "gslt-rho:rule-instance-sha256-%s-r%zu-v1",
        context->digest, rule_index);
}

/* The persistent listener contains no logical rule variables.  Evaluating
 * this target equation creates an ordinary fresh instance for every request,
 * including recursive requests for the same authored rule. */
static bool rho_render_rule_instance_equation_v1(
    RhoBufferV1 *buffer, const RhoRenderContextV1 *context,
    size_t rule_index) {
    const CettaGsltRewriteV1 *rewrite =
        &context->composition->rewrites[rule_index];
    RhoBufferV1 request_token = {0};
    char request_variable[96];
    char token_variable[96];
    char goal_variable[96];
    char reply_variable[96];
    char variable_prefix[96];
    bool ok = false;

    if (snprintf(request_variable, sizeof(request_variable),
                 "__gslt_rho_instance_request_%zu", rule_index) < 0 ||
        snprintf(token_variable, sizeof(token_variable),
                 "__gslt_rho_instance_token_%zu", rule_index) < 0 ||
        snprintf(goal_variable, sizeof(goal_variable),
                 "__gslt_rho_instance_goal_%zu", rule_index) < 0 ||
        snprintf(reply_variable, sizeof(reply_variable),
                 "__gslt_rho_instance_reply_%zu", rule_index) < 0 ||
        snprintf(variable_prefix, sizeof(variable_prefix),
                 "__gslt_rho_instance_r%zu_", rule_index) < 0 ||
        !rho_printf_v1(&request_token, "$%s", token_variable))
        goto done;

    if (!rho_literal_v1(buffer, "(= (") ||
        !rho_render_rule_instance_name_v1(
            buffer, context, rule_index) ||
        !rho_printf_v1(
            buffer,
            " $%s)\n   (let "
            "(rho:quote (rho:val (gslt-rho:request-v1 "
            "$%s $%s $%s))) $%s (unify $%s ",
            request_variable, token_variable, goal_variable,
            reply_variable, request_variable, goal_variable) ||
        !rho_render_term_v1(
            buffer, rewrite->head, variable_prefix,
            context->scratch, 0u) ||
        !rho_literal_v1(buffer, " ") ||
        !rho_render_rule_sequence_v1(
            buffer, context, rule_index, 0u, variable_prefix,
            &request_token, &request_token, reply_variable) ||
        !rho_literal_v1(buffer, " (superpose ()))))\n\n"))
        goto done;
    ok = true;

done:
    free(request_token.bytes);
    return ok;
}

static bool rho_render_rule_listener_v1(
    RhoBufferV1 *buffer, const RhoRenderContextV1 *context,
    size_t rule_index) {
    RhoBufferV1 channel = {0};
    char request_variable[96];
    char result_variable[96];
    bool ok = false;

    if (snprintf(request_variable, sizeof(request_variable),
                 "__gslt_rho_request_%zu", rule_index) < 0 ||
        snprintf(result_variable, sizeof(result_variable),
                 "__gslt_rho_rule_result_%zu", rule_index) < 0)
        goto done;
    if (!rho_static_name_v1(
            &channel, "rule-channel", context->digest, rule_index, 0u))
        goto done;
    if (!rho_render_persistent_receive_open_v1(
            buffer, &channel, request_variable) ||
        !rho_render_eval_open_v1(
            buffer, context->digest, rule_index, 0u, request_variable) ||
        !rho_literal_v1(buffer, "(") ||
        !rho_render_rule_instance_name_v1(
            buffer, context, rule_index) ||
        !rho_printf_v1(buffer, " $%s)", request_variable) ||
        !rho_render_eval_close_v1(
            buffer, context->digest, rule_index, 0u,
            request_variable, result_variable) ||
        !rho_render_persistent_receive_close_v1(buffer))
        goto done;
    ok = true;

done:
    free(channel.bytes);
    return ok;
}

static bool rho_render_query_goal_v1(
    RhoBufferV1 *buffer, const RhoRelationV1 *relation,
    size_t relation_index) {
    if (!rho_printf_v1(buffer, "(%s", relation->name))
        return false;
    for (size_t argument = 0u; argument < relation->arity; argument++) {
        if (!rho_printf_v1(
                buffer, " $__gslt_rho_query_%zu_%zu",
                relation_index, argument))
            return false;
    }
    return rho_literal_v1(buffer, ")");
}

static bool rho_validate_composition_v1(
    const CettaGsltCompositionV1 *composition,
    const uint8_t *active_rules,
    RhoRelationsV1 *relations,
    const RhoCapabilitiesV1 *capabilities,
    char *error, size_t error_size) {
    if (composition->equation_count != 0u)
        return rho_error_v1(
            error, error_size,
            "direct Rhometta v1 has not chosen an orientation for authored equations");
    if (composition->rewrite_count == 0u)
        return rho_error_v1(
            error, error_size, "direct Rhometta composition has no rewrites");
    for (size_t index = 0u; index < composition->rewrite_count; index++) {
        if (!active_rules[index])
            continue;
        const CettaGsltRewriteV1 *rewrite = &composition->rewrites[index];
        const char *relation;
        size_t arity;
        if (!rho_relation_for_goal_v1(rewrite->head, &relation, &arity))
            return rho_error_v1(
                error, error_size,
                "direct Rhometta rule head must be an application");
        if (strcmp(relation, "metta-equation") == 0)
            return rho_error_v1(
                error, error_size,
                "PeTTa target equations are not Rhometta rewrite semantics");
        if (!cetta_gslt_composition_validate_term_v1(
                composition, rewrite->head,
                GSLT_RHOMETTA_DIRECT_DEPTH_LIMIT_V1,
                error, error_size) ||
            !rho_push_relation_v1(
                relations, relation, arity, error, error_size))
            return false;
        for (CettaExprIndex body_index = 1u;
             body_index < rewrite->body->expr.len; body_index++) {
            if (!cetta_gslt_composition_validate_term_v1(
                    composition, rewrite->body->expr.elems[body_index],
                    GSLT_RHOMETTA_DIRECT_DEPTH_LIMIT_V1,
                    error, error_size))
                return false;
        }
    }
    for (size_t index = 0u; index < composition->rewrite_count; index++) {
        if (!active_rules[index])
            continue;
        const CettaGsltRewriteV1 *rewrite = &composition->rewrites[index];
        for (CettaExprIndex body_index = 1u;
             body_index < rewrite->body->expr.len; body_index++) {
            const char *relation;
            size_t arity;
            if (!rho_relation_for_goal_v1(
                    rewrite->body->expr.elems[body_index],
                    &relation, &arity))
                return rho_error_v1(
                    error, error_size,
                    "direct Rhometta selected body contains a malformed application");
            if (!rho_has_relation_v1(relations, relation, arity) &&
                rho_find_capability_v1(
                    capabilities, relation, arity) == NULL)
                return rho_error_v1(
                    error, error_size,
                    "direct Rhometta v1 requires an authored definition or declared target capability for %s/%zu",
                    relation, arity);
        }
    }
    return true;
}

bool cetta_gslt_rhometta_direct_selected_v1(
    Atom *const *presentations,
    size_t presentation_count,
    const char target_package_digest[65],
    const char *const *entry_rule_names,
    size_t entry_rule_count,
    uint8_t **program_out,
    size_t *program_len_out,
    size_t *rule_count_out,
    size_t *relation_count_out,
    char source_digest_out[65],
    char *error,
    size_t error_size) {
    CettaGsltCompositionV1 composition = {0};
    RhoRelationsV1 relations = {0};
    RhoCapabilitiesV1 capabilities = {0};
    RhoBufferV1 program = {0};
    uint8_t *active_rules = NULL;
    size_t active_rule_count = 0u;
    Arena scratch;
    RhoRenderContextV1 context;
    bool ok = false;

    if (presentations == NULL || presentation_count == 0u ||
        target_package_digest == NULL ||
        strlen(target_package_digest) != 64u ||
        program_out == NULL || program_len_out == NULL ||
        rule_count_out == NULL || relation_count_out == NULL ||
        source_digest_out == NULL)
        return rho_error_v1(
            error, error_size, "invalid direct Rhometta compilation request");
    *program_out = NULL;
    *program_len_out = 0u;
    *rule_count_out = 0u;
    *relation_count_out = 0u;
    source_digest_out[0] = '\0';
    arena_init(&scratch);

    if (!cetta_gslt_composition_build_v1(
            presentations, presentation_count, &composition,
            error, error_size) ||
        !cetta_gslt_composition_select_rewrite_closure_v1(
            &composition, entry_rule_names, entry_rule_count,
            &active_rules, &active_rule_count, error, error_size) ||
        !rho_classify_metadata_v1(
            &composition, active_rules, &active_rule_count,
            &capabilities, error, error_size) ||
        !rho_validate_composition_v1(
            &composition, active_rules, &relations, &capabilities,
            error, error_size) ||
        !cetta_gslt_composition_digest_v1(
            presentations, presentation_count, source_digest_out,
            error, error_size))
        goto done;
    context = (RhoRenderContextV1){
        .composition = &composition,
        .active_rules = active_rules,
        .relations = &relations,
        .capabilities = &capabilities,
        .digest = source_digest_out,
        .scratch = &scratch,
    };
    if (!rho_literal_v1(
            &program,
            "; generated by direct compositional GSLT-to-Rhometta lowering\n"
            "; rho scheduling and continuations are native; target equations freshen rule instances\n"
            "; source-composition-sha256 ") ||
        !rho_literal_v1(&program, source_digest_out) ||
        !rho_literal_v1(&program, "\n; target-package-sha256 ") ||
        !rho_literal_v1(&program, target_package_digest) ||
        !rho_literal_v1(&program, "\n") )
        goto allocation_failure;
    for (size_t entry_index = 0u;
         entry_index < entry_rule_count; entry_index++) {
        if (!rho_literal_v1(&program, "; selected-entry-rule ") ||
            !rho_literal_v1(&program, entry_rule_names[entry_index]) ||
            !rho_literal_v1(&program, "\n"))
            goto allocation_failure;
    }
    if (!rho_literal_v1(&program, "\n!(import! &self rhometta)\n\n") ||
        !rho_render_target_equations_v1(&program, &context))
        goto allocation_failure;
    for (size_t relation_index = 0u;
         relation_index < relations.len; relation_index++) {
        if (!rho_printf_v1(
                &program, "; fresh relation dispatcher for %s/%zu\n",
                relations.items[relation_index].name,
                relations.items[relation_index].arity) ||
            !rho_render_relation_dispatch_instance_equation_v1(
                &program, &context, &relations.items[relation_index],
                relation_index))
            goto allocation_failure;
    }
    for (size_t rule_index = 0u;
         rule_index < composition.rewrite_count; rule_index++) {
        if (!active_rules[rule_index])
            continue;
        if (!rho_printf_v1(
                &program, "; fresh request instance for %s / %s\n",
                composition.rewrites[rule_index].presentation_name,
                composition.rewrites[rule_index].name) ||
            !rho_render_rule_instance_equation_v1(
                &program, &context, rule_index))
            goto allocation_failure;
    }
    if (!rho_literal_v1(
            &program, "(= (gslt-rho:network-v1)\n   (rho:par\n"))
        goto allocation_failure;
    for (size_t rule_index = 0u;
         rule_index < composition.rewrite_count; rule_index++) {
        if (!active_rules[rule_index])
            continue;
        if (!rho_printf_v1(
                &program, "     ; rule service %s / %s\n     ",
                composition.rewrites[rule_index].presentation_name,
                composition.rewrites[rule_index].name) ||
            !rho_render_rule_listener_v1(
                &program, &context, rule_index) ||
            !rho_literal_v1(&program, "\n"))
            goto allocation_failure;
    }
    if (!rho_literal_v1(&program, "   ))\n\n"))
        goto allocation_failure;

    /* Query renderers are emitted in a second pass below. */
    for (size_t relation_index = 0u;
         relation_index < relations.len; relation_index++) {
        const RhoRelationV1 *relation = &relations.items[relation_index];
        RhoBufferV1 token = {0};
        RhoBufferV1 reply = {0};
        RhoBufferV1 query_goal = {0};
        char observer_variable[96];
        char result_variable[96];
        char occurrence_variable[96];
        char answer_variable[96];
        char evidence_variable[96];
        bool rendered = false;

        if (snprintf(observer_variable, sizeof(observer_variable),
                     "__gslt_rho_observer_%zu", relation_index) < 0 ||
            snprintf(result_variable, sizeof(result_variable),
                     "__gslt_rho_observer_result_%zu", relation_index) < 0 ||
            snprintf(occurrence_variable, sizeof(occurrence_variable),
                     "__gslt_rho_query_occurrence_%zu", relation_index) < 0 ||
            snprintf(answer_variable, sizeof(answer_variable),
                     "__gslt_rho_query_answer_%zu", relation_index) < 0 ||
            snprintf(evidence_variable, sizeof(evidence_variable),
                     "__gslt_rho_query_evidence_%zu", relation_index) < 0 ||
            !rho_literal_v1(&token, "$__gslt_rho_query_token") ||
            !rho_dynamic_name_v1(
                &reply, "reply", context.digest, &token) ||
            !rho_render_query_goal_v1(
                &query_goal, relation, relation_index))
            goto query_done;
        if (!rho_printf_v1(
                &program,
                "(= (gslt-rho:process:%s $__gslt_rho_query_token ",
                relation->name) ||
            !rho_fragment_v1(&program, &query_goal) ||
            !rho_literal_v1(
                &program,
                ")\n   (rho:par\n"
                "       (gslt-rho:network-v1)\n       ") ||
            !rho_render_persistent_receive_open_v1(
                &program, &reply, observer_variable) ||
            !rho_render_eval_open_v1(
                &program, context.digest,
                composition.rewrite_count + relation_index,
                0u, observer_variable) ||
            !rho_literal_v1(
                &program,
                "(let (rho:quote (rho:val (gslt-rho:answer-v1 "
                "$__gslt_rho_query_token ") ||
            !rho_printf_v1(
                &program,
                "$%s $%s $%s))) $%s "
                "(rho:val (gslt-rho:observed-v1 $%s $%s $%s)))",
                occurrence_variable, answer_variable, evidence_variable,
                observer_variable,
                occurrence_variable, answer_variable, evidence_variable) ||
            !rho_render_eval_close_v1(
                &program, context.digest,
                composition.rewrite_count + relation_index,
                0u, observer_variable, result_variable) ||
            !rho_render_persistent_receive_close_v1(&program) ||
            !rho_literal_v1(&program, "\n       "))
            goto query_done;
        if (!rho_render_relation_dispatch_v1(
                &program, &context, relation_index,
                &query_goal, &token, &reply,
                composition.rewrite_count + relation_index, 1u))
            goto query_done;
        if (!rho_literal_v1(&program, "))\n\n") ||
            !rho_printf_v1(
                &program,
                "(= (gslt-rho:query:%s $__gslt_rho_query_token ",
                relation->name) ||
            !rho_fragment_v1(&program, &query_goal) ||
            !rho_printf_v1(
                &program,
                ")\n   (rhometta:values\n"
                "     (rhometta:run-canonical\n"
                "       (gslt-rho:process:%s $__gslt_rho_query_token ",
                relation->name) ||
            !rho_fragment_v1(&program, &query_goal) ||
            !rho_literal_v1(&program, "))))\n\n"))
            goto query_done;
        rendered = true;

query_done:
        free(query_goal.bytes);
        free(reply.bytes);
        free(token.bytes);
        if (!rendered)
            goto allocation_failure;
    }

    while (program.len >= 2u &&
           program.bytes[program.len - 1u] == (uint8_t)'\n' &&
           program.bytes[program.len - 2u] == (uint8_t)'\n') {
        program.len--;
    }

    *program_out = program.bytes;
    *program_len_out = program.len;
    *rule_count_out = active_rule_count;
    *relation_count_out = relations.len;
    program.bytes = NULL;
    ok = true;
    goto done;

allocation_failure:
    (void)rho_error_v1(
        error, error_size,
        "out of memory rendering direct Rhometta program");

done:
    free(program.bytes);
    free(active_rules);
    free(capabilities.items);
    free(relations.items);
    cetta_gslt_composition_free_v1(&composition);
    arena_free(&scratch);
    return ok;
}

bool cetta_gslt_rhometta_direct_v1(
    Atom *const *presentations,
    size_t presentation_count,
    const char target_package_digest[65],
    uint8_t **program_out,
    size_t *program_len_out,
    size_t *rule_count_out,
    size_t *relation_count_out,
    char source_digest_out[65],
    char *error,
    size_t error_size) {
    return cetta_gslt_rhometta_direct_selected_v1(
        presentations, presentation_count, target_package_digest, NULL, 0u,
        program_out, program_len_out, rule_count_out, relation_count_out,
        source_digest_out, error, error_size);
}
