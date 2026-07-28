#define _POSIX_C_SOURCE 200809L

#include "petta_document_pipeline_v1.h"

#include "../native/finite_horn_answer_stream_v1.h"
#include "../native/finite_horn_ground_term_v1.h"
#include "../native/parser_pack_abi_stream_v1.h"
#include "../native/parser_pack_gll_v1.h"
#include "../native/parser_pack_glr_v1.h"
#include "../native/parser_pack_guard_evidence_stream_v1.h"
#include "../native/parser_pack_native_api_v1.h"

#include "symbol.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    uint32_t *data;
    uint32_t len;
    uint32_t cap;
} U32Vec;

typedef struct {
    const char *kind;
    uint32_t *codepoints;
    uint32_t *byte_offsets;
    uint32_t scalar_len;
    uint32_t byte_len;
    uint32_t source_scalar_left;
    uint32_t source_scalar_right;
    uint32_t source_byte_left;
    uint32_t source_byte_right;
} PeTTaPipelineFormV1;

typedef struct {
    PeTTaPipelineFormV1 *data;
    uint32_t len;
    uint32_t cap;
} PeTTaPipelineFormVecV1;

typedef struct {
    uint32_t scalar_left;
    uint32_t scalar_right;
    uint32_t byte_left;
    uint32_t byte_right;
} PeTTaPipelineSpanV1;

typedef struct {
    PeTTaPipelineSpanV1 *data;
    uint32_t len;
    uint32_t cap;
} PeTTaPipelineSpanVecV1;

struct PeTTaDocumentPipelineV1Prepared {
    bool initialized;
    bool ready;
    bool lexical_shadow_enabled;
    uint32_t build_count;
    SymbolTable *owner_symbols;
    uint64_t owner_symbols_instance_id;
    PPGuardScalarExecV1Limits scalar_limits;
    PPGuardedLexExecV1Limits lexical_limits;
    PPABIV1Wire splitter_wire;
    PPABIV1Wire form_wire;
    PPABIV1Pack splitter_pack;
    PPABIV1Pack form_pack;
    FHAnswerStreamV1 guard_answers;
    RSNFAV1Plan guard_nfa;
    PPGuardEvidenceWireV1 evidence;
    PPGuardPlanV1 guard_plan;
    PPGuardScalarExecV1Plan exec_plan;
    FHAnswerStreamV1 lexical_answers;
    FHAnswerStreamV1 guarded_answers;
    RSNFAV1Plan lexical_nfa;
    PPLexV1Plan lexical_plan;
    PPGuardPlanV1 lexical_guard_plan;
    PPGuardedLexV1Plan guarded_plan;
    PPGuardedLexExecV1Plan lexical_exec_plan;
    PPNativeV1Prepared splitter_prepared;
    PeTTaDocumentPipelineV1LexicalShadowReceipt base_shadow_receipt;
};

void petta_document_pipeline_v1_response_init(
    PeTTaDocumentPipelineV1Response *response) {
    if (!response)
        return;
    memset(response, 0, sizeof(*response));
}

void petta_document_pipeline_v1_response_free(
    PeTTaDocumentPipelineV1Response *response) {
    if (!response)
        return;
    free(response->bytes);
    memset(response, 0, sizeof(*response));
}

void petta_document_pipeline_v1_lexical_shadow_receipt_init(
    PeTTaDocumentPipelineV1LexicalShadowReceipt *receipt) {
    if (!receipt)
        return;
    memset(receipt, 0, sizeof(*receipt));
}

static void prepared_state_init(PeTTaDocumentPipelineV1Prepared *prepared) {
    memset(prepared, 0, sizeof(*prepared));
    ppabi_v1_wire_init(&prepared->splitter_wire);
    ppabi_v1_wire_init(&prepared->form_wire);
    ppabi_v1_pack_init(&prepared->splitter_pack);
    ppabi_v1_pack_init(&prepared->form_pack);
    fh_answer_stream_v1_init(&prepared->guard_answers);
    rsnfa_v1_plan_init(&prepared->guard_nfa);
    ppguard_evidence_wire_v1_init(&prepared->evidence);
    ppguard_plan_v1_init(&prepared->guard_plan);
    ppguard_scalar_exec_v1_plan_init(&prepared->exec_plan);
    fh_answer_stream_v1_init(&prepared->lexical_answers);
    fh_answer_stream_v1_init(&prepared->guarded_answers);
    rsnfa_v1_plan_init(&prepared->lexical_nfa);
    pplex_v1_plan_init(&prepared->lexical_plan);
    ppguard_plan_v1_init(&prepared->lexical_guard_plan);
    ppguarded_lex_v1_plan_init(&prepared->guarded_plan);
    ppguarded_lex_exec_v1_plan_init(&prepared->lexical_exec_plan);
    ppnative_v1_prepared_init(&prepared->splitter_prepared);
    petta_document_pipeline_v1_lexical_shadow_receipt_init(
        &prepared->base_shadow_receipt);
    prepared->initialized = true;
}

static void prepared_state_release(
    PeTTaDocumentPipelineV1Prepared *prepared) {
    if (!prepared || !prepared->initialized)
        return;
    ppguarded_lex_exec_v1_plan_free(&prepared->lexical_exec_plan);
    ppnative_v1_prepared_free(&prepared->splitter_prepared);
    ppguarded_lex_v1_plan_free(&prepared->guarded_plan);
    ppguard_plan_v1_free(&prepared->lexical_guard_plan);
    pplex_v1_plan_free(&prepared->lexical_plan);
    rsnfa_v1_plan_free(&prepared->lexical_nfa);
    fh_answer_stream_v1_free(&prepared->guarded_answers);
    fh_answer_stream_v1_free(&prepared->lexical_answers);
    ppguard_scalar_exec_v1_plan_free(&prepared->exec_plan);
    ppguard_plan_v1_free(&prepared->guard_plan);
    ppguard_evidence_wire_v1_free(&prepared->evidence);
    rsnfa_v1_plan_free(&prepared->guard_nfa);
    fh_answer_stream_v1_free(&prepared->guard_answers);
    ppabi_v1_pack_free(&prepared->form_pack);
    ppabi_v1_pack_free(&prepared->splitter_pack);
    ppabi_v1_wire_free(&prepared->form_wire);
    ppabi_v1_wire_free(&prepared->splitter_wire);
    memset(prepared, 0, sizeof(*prepared));
}

PeTTaDocumentPipelineV1Prepared *
petta_document_pipeline_v1_prepared_new(void) {
    return calloc(1u, sizeof(PeTTaDocumentPipelineV1Prepared));
}

void petta_document_pipeline_v1_prepared_free(
    PeTTaDocumentPipelineV1Prepared *prepared) {
    if (!prepared)
        return;
    prepared_state_release(prepared);
    free(prepared);
}

uint32_t petta_document_pipeline_v1_prepared_build_count(
    const PeTTaDocumentPipelineV1Prepared *prepared) {
    return prepared && prepared->ready ? prepared->build_count : 0u;
}

static bool read_input(const char *path,
                       uint8_t **out,
                       size_t *out_len,
                       char *error,
                       size_t error_size) {
    FILE *input = NULL;
    uint8_t *bytes = NULL;
    size_t len = 0u;
    size_t cap = 0u;
    bool ok = false;

    *out = NULL;
    *out_len = 0u;
    input = fopen(path, "rb");
    if (!input) {
        (void)snprintf(error, error_size, "cannot open document input");
        goto done;
    }
    for (;;) {
        size_t amount;
        if (len == cap) {
            size_t next_cap = cap ? cap * 2u : 4096u;
            uint8_t *next;
            if (next_cap < cap) {
                (void)snprintf(error, error_size,
                               "document input is too large");
                goto done;
            }
            next = realloc(bytes, next_cap);
            if (!next) {
                (void)snprintf(error, error_size,
                               "failed to allocate document input");
                goto done;
            }
            bytes = next;
            cap = next_cap;
        }
        amount = fread(bytes + len, 1u, cap - len, input);
        len += amount;
        if (amount > 0u)
            continue;
        if (ferror(input)) {
            (void)snprintf(error, error_size,
                           "failed while reading document input");
            goto done;
        }
        break;
    }
    *out = bytes;
    *out_len = len;
    bytes = NULL;
    ok = true;

done:
    if (input)
        (void)fclose(input);
    free(bytes);
    return ok;
}

static bool expr_head(const Atom *atom,
                      const char *head,
                      CettaExprLen argument_len) {
    return atom && atom->kind == ATOM_EXPR &&
        atom->expr.len == argument_len + 1u &&
        atom_is_symbol(atom->expr.elems[0], head);
}

static bool scalar_valid(uint32_t scalar) {
    return scalar <= UINT32_C(0x10ffff) &&
        !(scalar >= UINT32_C(0xd800) && scalar <= UINT32_C(0xdfff));
}

static uint32_t scalar_width(uint32_t scalar) {
    if (scalar <= UINT32_C(0x7f))
        return 1u;
    if (scalar <= UINT32_C(0x7ff))
        return 2u;
    if (scalar <= UINT32_C(0xffff))
        return 3u;
    return 4u;
}

static bool u32_push(U32Vec *vector, uint32_t value) {
    if (vector->len == vector->cap) {
        uint32_t next_cap = vector->cap ? vector->cap * 2u : 64u;
        uint32_t *next;
        if (next_cap < vector->cap)
            return false;
        next = realloc(vector->data,
                       sizeof(*next) * (size_t)next_cap);
        if (!next)
            return false;
        vector->data = next;
        vector->cap = next_cap;
    }
    vector->data[vector->len++] = value;
    return true;
}

static bool flatten_value(const Atom *term,
                          U32Vec *codepoints,
                          char *error,
                          size_t error_size) {
    if (term && term->kind == ATOM_SYMBOL &&
        atom_is_symbol((Atom *)term, "nil")) {
        return true;
    }
    if (expr_head(term, "cp", 1u)) {
        const Atom *value = term->expr.elems[1];
        int64_t integer;
        if (!value || value->kind != ATOM_GROUNDED ||
            value->ground.gkind != GV_INT ||
            (integer = value->ground.ival) < 0 ||
            integer > INT64_C(0x10ffff) ||
            !scalar_valid((uint32_t)integer) ||
            !u32_push(codepoints, (uint32_t)integer)) {
            (void)snprintf(error, error_size,
                           "splitter semantic cp is malformed");
            return false;
        }
        return true;
    }
    if (expr_head(term, "pair", 2u) || expr_head(term, "cons", 2u)) {
        return flatten_value(
                   term->expr.elems[1], codepoints, error, error_size) &&
            flatten_value(
                   term->expr.elems[2], codepoints, error, error_size);
    }
    if (expr_head(term, "node", 2u)) {
        return flatten_value(
            term->expr.elems[2], codepoints, error, error_size);
    }
    (void)snprintf(error, error_size,
                   "splitter semantic value has an unknown constructor");
    return false;
}

static void form_free(PeTTaPipelineFormV1 *form) {
    if (!form)
        return;
    free(form->codepoints);
    free(form->byte_offsets);
    memset(form, 0, sizeof(*form));
}

static void forms_free(PeTTaPipelineFormVecV1 *forms) {
    uint32_t index;

    if (!forms)
        return;
    for (index = 0u; index < forms->len; index++)
        form_free(&forms->data[index]);
    free(forms->data);
    memset(forms, 0, sizeof(*forms));
}

static bool form_finish(PeTTaPipelineFormV1 *form,
                        U32Vec *codepoints,
                        char *error,
                        size_t error_size) {
    uint32_t index;
    uint32_t byte_position = 0u;

    form->byte_offsets = malloc(
        sizeof(*form->byte_offsets) * ((size_t)codepoints->len + 1u));
    if (!form->byte_offsets) {
        (void)snprintf(error, error_size,
                       "failed to allocate filtered form index");
        return false;
    }
    form->byte_offsets[0] = 0u;
    for (index = 0u; index < codepoints->len; index++) {
        uint32_t width = scalar_width(codepoints->data[index]);
        if (byte_position > UINT32_MAX - width) {
            (void)snprintf(error, error_size,
                           "filtered form is too large");
            return false;
        }
        byte_position += width;
        form->byte_offsets[index + 1u] = byte_position;
    }
    form->codepoints = codepoints->data;
    form->scalar_len = codepoints->len;
    form->byte_len = byte_position;
    memset(codepoints, 0, sizeof(*codepoints));
    return true;
}

static bool form_push(PeTTaPipelineFormVecV1 *forms,
                      const Atom *node,
                      char *error,
                      size_t error_size) {
    PeTTaPipelineFormV1 form = {0};
    U32Vec codepoints = {0};
    PeTTaPipelineFormV1 *next;
    uint32_t next_cap;
    bool ok = false;

    if (!expr_head(node, "node", 2u) ||
        node->expr.elems[1]->kind != ATOM_SYMBOL ||
        !(atom_is_symbol(node->expr.elems[1], "form") ||
          atom_is_symbol(node->expr.elems[1], "runnable"))) {
        (void)snprintf(error, error_size,
                       "splitter document has a malformed form node");
        goto done;
    }
    form.kind = atom_is_symbol(node->expr.elems[1], "runnable")
        ? "runnable" : "form";
    if (!flatten_value(
            node->expr.elems[2], &codepoints, error, error_size) ||
        !form_finish(&form, &codepoints, error, error_size)) {
        goto done;
    }
    if (forms->len == forms->cap) {
        next_cap = forms->cap ? forms->cap * 2u : 16u;
        if (next_cap < forms->cap)
            goto done;
        next = realloc(forms->data,
                       sizeof(*next) * (size_t)next_cap);
        if (!next)
            goto done;
        forms->data = next;
        forms->cap = next_cap;
    }
    forms->data[forms->len++] = form;
    memset(&form, 0, sizeof(form));
    ok = true;

done:
    free(codepoints.data);
    form_free(&form);
    if (!ok && error && error_size > 0u && error[0] == '\0') {
        (void)snprintf(error, error_size,
                       "failed to retain splitter form");
    }
    return ok;
}

static bool extract_forms(const PPNativeV1Result *document,
                          PeTTaPipelineFormVecV1 *forms,
                          char *error,
                          size_t error_size) {
    const Atom *result;
    const Atom *document_node;
    const Atom *list;

    if (document->semantic_result_len != 1u ||
        !(result = document->semantic_results[0]) ||
        !expr_head(result, "result", 2u) ||
        !atom_is_symbol(result->expr.elems[2], "nil") ||
        !(document_node = result->expr.elems[1]) ||
        !expr_head(document_node, "node", 2u) ||
        !atom_is_symbol(document_node->expr.elems[1], "document")) {
        (void)snprintf(error, error_size,
                       "splitter semantic result is malformed");
        return false;
    }
    list = document_node->expr.elems[2];
    while (!atom_is_symbol((Atom *)list, "nil")) {
        if (!expr_head(list, "cons", 2u) ||
            !form_push(forms, list->expr.elems[1], error, error_size)) {
            if (!error[0]) {
                (void)snprintf(error, error_size,
                               "splitter form list is malformed");
            }
            return false;
        }
        list = list->expr.elems[2];
    }
    return true;
}

static int32_t named_definition_state(const PPABIV1Pack *pack,
                                      const char *name) {
    uint32_t index;

    for (index = 0u; index < pack->state_len; index++) {
        const Atom *identity = pack->states[index].identity;
        if (expr_head(identity, "pp-def", 1u) &&
            atom_is_symbol(identity->expr.elems[1], name)) {
            return (int32_t)pack->states[index].dense_id;
        }
    }
    return -1;
}

static bool span_push_unique(PeTTaPipelineSpanVecV1 *spans,
                             const CettaLpNativeUtf8ForestNode *node) {
    uint32_t index;

    for (index = 0u; index < spans->len; index++) {
        const PeTTaPipelineSpanV1 *span = &spans->data[index];
        if (span->scalar_left == node->scalar_left &&
            span->scalar_right == node->scalar_right &&
            span->byte_left == node->byte_left &&
            span->byte_right == node->byte_right) {
            return true;
        }
    }
    if (spans->len == spans->cap) {
        uint32_t next_cap = spans->cap ? spans->cap * 2u : 16u;
        PeTTaPipelineSpanV1 *next;
        if (next_cap < spans->cap)
            return false;
        next = realloc(spans->data,
                       sizeof(*next) * (size_t)next_cap);
        if (!next)
            return false;
        spans->data = next;
        spans->cap = next_cap;
    }
    spans->data[spans->len++] = (PeTTaPipelineSpanV1){
        .scalar_left = node->scalar_left,
        .scalar_right = node->scalar_right,
        .byte_left = node->byte_left,
        .byte_right = node->byte_right,
    };
    return true;
}

static int span_compare(const void *left, const void *right) {
    const PeTTaPipelineSpanV1 *lhs = left;
    const PeTTaPipelineSpanV1 *rhs = right;

#define SPAN_COMPARE(field) \
    do { \
        if (lhs->field != rhs->field) \
            return lhs->field < rhs->field ? -1 : 1; \
    } while (0)
    SPAN_COMPARE(scalar_left);
    SPAN_COMPARE(scalar_right);
    SPAN_COMPARE(byte_left);
    SPAN_COMPARE(byte_right);
#undef SPAN_COMPARE
    return 0;
}

static bool collect_form_spans(const PPABIV1Pack *pack,
                               const Atom *start_state,
                               const CettaLpNativeUtf8Forest *forest,
                               PeTTaPipelineSpanVecV1 *spans,
                               char *error,
                               size_t error_size) {
    int32_t start_id = ppnative_v1_state_find(pack, start_state);
    int32_t form_id = named_definition_state(
        pack, "petta-splitter-top-form");
    uint32_t full_root = UINT32_MAX;
    uint32_t full_root_count = 0u;
    bool *marked = NULL;
    uint32_t *stack = NULL;
    uint32_t stack_len = 0u;
    uint32_t index;
    bool ok = false;

    if (start_id < 0 || form_id < 0 || forest->node_len == 0u)
        goto malformed;
    for (index = 0u; index < forest->root_len; index++) {
        uint32_t node_index = forest->roots[index];
        if (node_index < forest->node_len &&
            forest->nodes[node_index].kind ==
                CETTA_LP_NATIVE_UTF8_FOREST_SYMBOL &&
            forest->nodes[node_index].symbol_id == (uint32_t)start_id &&
            forest->nodes[node_index].scalar_left == 0u &&
            forest->nodes[node_index].scalar_right == forest->scalar_len) {
            full_root = node_index;
            full_root_count++;
        }
    }
    if (full_root_count != 1u)
        goto malformed;
    marked = calloc(forest->node_len, sizeof(*marked));
    stack = malloc(sizeof(*stack) * (size_t)forest->node_len);
    if (!marked || !stack)
        goto done;
    marked[full_root] = true;
    stack[stack_len++] = full_root;
    while (stack_len > 0u) {
        uint32_t node_index = stack[--stack_len];
        const CettaLpNativeUtf8ForestNode *node =
            &forest->nodes[node_index];
        uint32_t choice_index;
        if (node->choice_begin > forest->choice_len ||
            node->choice_len > forest->choice_len - node->choice_begin) {
            goto malformed;
        }
        for (choice_index = node->choice_begin;
             choice_index < node->choice_begin + node->choice_len;
             choice_index++) {
            const CettaLpNativeUtf8ForestChoice *choice =
                &forest->choices[choice_index];
            const uint32_t children[2] = {
                choice->prefix_node, choice->child_node,
            };
            uint32_t child_index;
            if (choice->parent_node != node_index)
                goto malformed;
            for (child_index = 0u; child_index < 2u; child_index++) {
                uint32_t child = children[child_index];
                if (child == CETTA_LP_NATIVE_UTF8_FOREST_NONE)
                    continue;
                if (child >= forest->node_len)
                    goto malformed;
                if (!marked[child]) {
                    marked[child] = true;
                    stack[stack_len++] = child;
                }
            }
        }
    }
    for (index = 0u; index < forest->node_len; index++) {
        const CettaLpNativeUtf8ForestNode *node = &forest->nodes[index];
        if (marked[index] &&
            node->kind == CETTA_LP_NATIVE_UTF8_FOREST_SYMBOL &&
            node->symbol_id == (uint32_t)form_id &&
            !span_push_unique(spans, node)) {
            goto done;
        }
    }
    if (spans->len > 1u)
        qsort(spans->data, spans->len, sizeof(*spans->data), span_compare);
    for (index = 0u; index < spans->len; index++) {
        const PeTTaPipelineSpanV1 *span = &spans->data[index];
        if (span->scalar_left > span->scalar_right ||
            span->scalar_right > forest->scalar_len ||
            span->byte_left > span->byte_right ||
            span->byte_right > forest->input_byte_len ||
            span->byte_left != forest->byte_offsets[span->scalar_left] ||
            span->byte_right != forest->byte_offsets[span->scalar_right] ||
            (index > 0u &&
             spans->data[index - 1u].scalar_right > span->scalar_left)) {
            goto malformed;
        }
    }
    ok = true;
    goto done;

malformed:
    (void)snprintf(error, error_size,
                   "splitter forest has no unique form provenance path");
done:
    free(stack);
    free(marked);
    return ok;
}

static bool results_equal(const PPNativeV1Result *left,
                          const PPNativeV1Result *right) {
    uint32_t index;

    if (left->outcome != right->outcome ||
        left->accepted != right->accepted ||
        left->semantic_result_len != right->semantic_result_len ||
        strcmp(left->forest_digest, right->forest_digest) != 0) {
        return false;
    }
    for (index = 0u; index < left->semantic_result_len; index++) {
        if (!atom_eq(left->semantic_results[index],
                     right->semantic_results[index])) {
            return false;
        }
    }
    return true;
}

static bool semantic_results_equal(const PPNativeV1Result *left,
                                   const PPNativeV1Result *right) {
    uint32_t index;

    if (left->outcome != right->outcome ||
        left->accepted != right->accepted ||
        left->semantic_result_len != right->semantic_result_len) {
        return false;
    }
    for (index = 0u; index < left->semantic_result_len; index++) {
        if (!atom_eq(left->semantic_results[index],
                     right->semantic_results[index])) {
            return false;
        }
    }
    return true;
}

static void write_hex(FILE *stream, const uint8_t *bytes, size_t len) {
    size_t index;
    for (index = 0u; index < len; index++)
        (void)fprintf(stream, "%02x", bytes[index]);
}

static void write_scalar_hex(FILE *stream,
                             const uint32_t *codepoints,
                             uint32_t len) {
    uint32_t index;

    for (index = 0u; index < len; index++) {
        uint32_t scalar = codepoints[index];
        uint8_t bytes[4];
        uint32_t width;
        if (scalar <= UINT32_C(0x7f)) {
            bytes[0] = (uint8_t)scalar;
            width = 1u;
        } else if (scalar <= UINT32_C(0x7ff)) {
            bytes[0] = (uint8_t)(UINT32_C(0xc0) | (scalar >> 6u));
            bytes[1] = (uint8_t)(UINT32_C(0x80) | (scalar & UINT32_C(0x3f)));
            width = 2u;
        } else if (scalar <= UINT32_C(0xffff)) {
            bytes[0] = (uint8_t)(UINT32_C(0xe0) | (scalar >> 12u));
            bytes[1] = (uint8_t)(UINT32_C(0x80) |
                                 ((scalar >> 6u) & UINT32_C(0x3f)));
            bytes[2] = (uint8_t)(UINT32_C(0x80) | (scalar & UINT32_C(0x3f)));
            width = 3u;
        } else {
            bytes[0] = (uint8_t)(UINT32_C(0xf0) | (scalar >> 18u));
            bytes[1] = (uint8_t)(UINT32_C(0x80) |
                                 ((scalar >> 12u) & UINT32_C(0x3f)));
            bytes[2] = (uint8_t)(UINT32_C(0x80) |
                                 ((scalar >> 6u) & UINT32_C(0x3f)));
            bytes[3] = (uint8_t)(UINT32_C(0x80) | (scalar & UINT32_C(0x3f)));
            width = 4u;
        }
        write_hex(stream, bytes, width);
    }
}

static bool write_form_results(FILE *stream,
                               uint32_t form_index,
                               const PPNativeV1Result *result) {
    uint32_t index;

    for (index = 0u; index < result->semantic_result_len; index++) {
        uint8_t *canonical = NULL;
        size_t canonical_len = 0u;
        if (!fh_ground_term_v1_render(
                result->semantic_results[index], &canonical,
                &canonical_len, NULL, 0u)) {
            free(canonical);
            return false;
        }
        (void)fprintf(stream, "form-result\t%u\t", form_index);
        write_hex(stream, canonical, canonical_len);
        (void)fprintf(stream, "\n");
        free(canonical);
    }
    return true;
}

static Atom *pipeline_expr(Arena *arena,
                           const char *head,
                           Atom **arguments,
                           uint32_t argument_len) {
    Atom **elements;
    Atom *result;
    uint32_t index;

    elements = malloc(sizeof(*elements) * (size_t)(argument_len + 1u));
    if (!elements)
        return NULL;
    elements[0] = atom_symbol(arena, head);
    for (index = 0u; index < argument_len; index++)
        elements[index + 1u] = arguments[index];
    result = atom_expr(arena, elements, argument_len + 1u);
    free(elements);
    return result;
}

static Atom *pipeline_field(Arena *arena,
                            const char *name,
                            Atom *value) {
    return value ? pipeline_expr(arena, name, &value, 1u) : NULL;
}

static Atom *pipeline_list(Arena *arena,
                           Atom *const *items,
                           uint32_t len) {
    Atom *list = atom_symbol(arena, "nil");

    while (len > 0u) {
        Atom *arguments[2];
        len--;
        arguments[0] = items[len];
        arguments[1] = list;
        list = pipeline_expr(arena, "cons", arguments, 2u);
        if (!list)
            return NULL;
    }
    return list;
}

static Atom *pipeline_codepoint_list(Arena *arena,
                                     const uint32_t *codepoints,
                                     uint32_t len) {
    Atom *list = atom_symbol(arena, "nil");

    while (len > 0u) {
        Atom *cp_argument;
        Atom *cp;
        Atom *cons_arguments[2];
        len--;
        cp_argument = atom_int(arena, codepoints[len]);
        cp = pipeline_expr(arena, "cp", &cp_argument, 1u);
        if (!cp)
            return NULL;
        cons_arguments[0] = cp;
        cons_arguments[1] = list;
        list = pipeline_expr(arena, "cons", cons_arguments, 2u);
        if (!list)
            return NULL;
    }
    return list;
}

static Atom *pipeline_result_list(Arena *arena,
                                  const PPNativeV1Result *result) {
    Atom **items = NULL;
    Atom *list = NULL;
    uint32_t index;

    if (result->semantic_result_len > 0u) {
        items = malloc(
            sizeof(*items) * (size_t)result->semantic_result_len);
        if (!items)
            return NULL;
    }
    for (index = 0u; index < result->semantic_result_len; index++) {
        items[index] = atom_deep_copy(
            arena, result->semantic_results[index]);
        if (!items[index])
            goto done;
    }
    list = pipeline_list(arena, items, result->semantic_result_len);

done:
    free(items);
    return list;
}

static Atom *pipeline_form_response(
    Arena *arena,
    uint32_t index,
    const PeTTaPipelineFormV1 *form,
    const PPGuardScalarExecV1Result *result) {
    Atom *fields[20];
    uint32_t field_len = 0u;

#define ADD_FORM_FIELD(name, value)                                         \
    do {                                                                     \
        Atom *field_ = pipeline_field(arena, (name), (value));               \
        if (!field_ ||                                                       \
            field_len >= (uint32_t)(sizeof(fields) / sizeof(*fields)))       \
            return NULL;                                                     \
        fields[field_len++] = field_;                                        \
    } while (0)

    ADD_FORM_FIELD("index", atom_int(arena, index));
    ADD_FORM_FIELD("kind", atom_symbol(arena, form->kind));
    ADD_FORM_FIELD("source-scalar-left",
                   atom_int(arena, form->source_scalar_left));
    ADD_FORM_FIELD("source-scalar-right",
                   atom_int(arena, form->source_scalar_right));
    ADD_FORM_FIELD("source-byte-left",
                   atom_int(arena, form->source_byte_left));
    ADD_FORM_FIELD("source-byte-right",
                   atom_int(arena, form->source_byte_right));
    ADD_FORM_FIELD("form-scalars", atom_int(arena, form->scalar_len));
    ADD_FORM_FIELD("form-bytes", atom_int(arena, form->byte_len));
    ADD_FORM_FIELD("filtered-scalars", pipeline_codepoint_list(
        arena, form->codepoints, form->scalar_len));
    ADD_FORM_FIELD("decision", atom_symbol(
        arena, result->gll.accepted ? "accepted" : "rejected"));
    ADD_FORM_FIELD("gll-forest-digest", atom_string(
        arena, result->gll.forest_digest));
    ADD_FORM_FIELD("glr-forest-digest", atom_string(
        arena, result->glr.forest_digest));
    ADD_FORM_FIELD("gll-guard-relation-digest", atom_string(
        arena, result->gll_relation.relation_digest));
    ADD_FORM_FIELD("glr-guard-relation-digest", atom_string(
        arena, result->glr_relation.relation_digest));
    ADD_FORM_FIELD("scan-source-passes", atom_int(
        arena, result->scan.source_pass_count));
    ADD_FORM_FIELD("gll-source-passes", atom_int(
        arena, result->gll_receipt.source_pass_count));
    ADD_FORM_FIELD("glr-source-passes", atom_int(
        arena, result->glr_receipt.source_pass_count));
    ADD_FORM_FIELD("guard-tokens", atom_int(
        arena, result->scan.token_len));
    ADD_FORM_FIELD("results", pipeline_result_list(arena, &result->gll));

#undef ADD_FORM_FIELD

    return pipeline_expr(
        arena, "petta-document-native-form-v1", fields, field_len);
}

static Atom *pipeline_document_response(
    Arena *arena,
    const PPABIV1Pack *splitter_pack,
    const PPABIV1Pack *form_pack,
    const PPGuardPlanV1 *guard_plan,
    const FHAnswerStreamV1 *guard_answers,
    const PPNativeV1Result *document_gll,
    const PPNativeV1Result *document_glr,
    Atom *const *form_responses,
    uint32_t form_len,
    uint32_t form_accept_len,
    uint32_t form_reject_len) {
    Atom *fields[24];
    uint32_t field_len = 0u;

#define ADD_DOCUMENT_FIELD(name, value)                                     \
    do {                                                                     \
        Atom *field_ = pipeline_field(arena, (name), (value));               \
        if (!field_ ||                                                       \
            field_len >= (uint32_t)(sizeof(fields) / sizeof(*fields)))       \
            return NULL;                                                     \
        fields[field_len++] = field_;                                        \
    } while (0)

    ADD_DOCUMENT_FIELD("splitter-pack-digest", atom_string(
        arena, splitter_pack->pack_digest));
    ADD_DOCUMENT_FIELD("form-pack-digest", atom_string(
        arena, form_pack->pack_digest));
    ADD_DOCUMENT_FIELD("guard-plan-digest", atom_string(
        arena, guard_plan->plan_digest));
    ADD_DOCUMENT_FIELD("guard-evidence-digest", atom_string(
        arena, guard_plan->evidence_digest));
    ADD_DOCUMENT_FIELD("guard-nfa-answer-digest", atom_string(
        arena, guard_answers->digest));
    ADD_DOCUMENT_FIELD("document-decision", atom_symbol(
        arena, document_gll->accepted ? "accepted" : "rejected"));
    ADD_DOCUMENT_FIELD("document-gll-forest-digest", atom_string(
        arena, document_gll->forest_digest));
    ADD_DOCUMENT_FIELD("document-glr-forest-digest", atom_string(
        arena, document_glr->forest_digest));
    ADD_DOCUMENT_FIELD("document-gll-source-passes", atom_int(
        arena, document_gll->forest.source_pass_count));
    ADD_DOCUMENT_FIELD("document-glr-source-passes", atom_int(
        arena, document_glr->forest.source_pass_count));
    ADD_DOCUMENT_FIELD("source-passes", atom_int(arena, 1));
    ADD_DOCUMENT_FIELD("input-bytes", atom_int(
        arena, document_gll->forest.input_byte_len));
    ADD_DOCUMENT_FIELD("input-scalars", atom_int(
        arena, document_gll->forest.scalar_len));
    ADD_DOCUMENT_FIELD("form-count", atom_int(arena, form_len));
    ADD_DOCUMENT_FIELD("form-accepted", atom_int(
        arena, form_accept_len));
    ADD_DOCUMENT_FIELD("form-rejected", atom_int(
        arena, form_reject_len));
    ADD_DOCUMENT_FIELD("document-results", pipeline_result_list(
        arena, document_gll));
    ADD_DOCUMENT_FIELD("forms", pipeline_list(
        arena, form_responses, form_len));

#undef ADD_DOCUMENT_FIELD

    return pipeline_expr(
        arena, "petta-document-native-response-v1", fields, field_len);
}

static bool run_core(const char *splitter_abi_path,
                     const char *form_abi_path,
                     const char *guard_nfa_path,
                     const char *guard_evidence_path,
                     const char *lexical_nfa_path,
                     const char *guarded_nfa_path,
                     const uint8_t *input,
                     size_t input_len,
                     const char *regular_compiler_digest,
                     const char *guarded_compiler_digest,
                     const PPGuardScalarExecV1Limits *scalar_limits,
                     const PPGuardedLexExecV1Limits *lexical_limits,
                     PeTTaDocumentPipelineV1LexicalShadowReceipt *shadow_receipt,
                     FILE *stream,
                     PeTTaDocumentPipelineV1Response *out,
                     char *error_buf,
                     size_t error_buf_size,
                     PeTTaDocumentPipelineV1Prepared *prepared_context,
                     bool prepare_only) {
    PeTTaDocumentPipelineV1Prepared local_prepared;
    PeTTaDocumentPipelineV1Prepared *state = prepared_context
        ? prepared_context
        : &local_prepared;
    PPGuardPlanV1ProvenanceInput provenance;
    PeTTaDocumentPipelineV1LexicalShadowReceipt local_shadow_receipt;
    PPNativeV1Result document_gll;
    PPNativeV1Result document_glr;
    PeTTaPipelineFormVecV1 forms = {0};
    PeTTaPipelineSpanVecV1 spans = {0};
    Arena response_arena;
    Atom **form_responses = NULL;
    Atom *response_atom = NULL;
    uint8_t *response_bytes = NULL;
    size_t response_len = 0u;
    uint32_t form_index;
    uint32_t form_accept_len = 0u;
    uint32_t form_reject_len = 0u;
    char error[512] = {0};
    const bool owns_state = prepared_context == NULL;
    const bool building_state = owns_state || prepare_only;
    bool state_initialized_here = false;
    bool lexical_shadow_enabled = false;
    bool ok = false;

#define splitter_wire (state->splitter_wire)
#define form_wire (state->form_wire)
#define splitter_pack (state->splitter_pack)
#define form_pack (state->form_pack)
#define guard_answers (state->guard_answers)
#define guard_nfa (state->guard_nfa)
#define evidence (state->evidence)
#define guard_plan (state->guard_plan)
#define exec_plan (state->exec_plan)
#define lexical_answers (state->lexical_answers)
#define guarded_answers (state->guarded_answers)
#define lexical_nfa (state->lexical_nfa)
#define lexical_plan (state->lexical_plan)
#define lexical_guard_plan (state->lexical_guard_plan)
#define guarded_plan (state->guarded_plan)
#define lexical_exec_plan (state->lexical_exec_plan)
#define splitter_prepared (state->splitter_prepared)

    memset(&local_prepared, 0, sizeof(local_prepared));
    petta_document_pipeline_v1_lexical_shadow_receipt_init(
        &local_shadow_receipt);
    ppnative_v1_result_init(&document_gll);
    ppnative_v1_result_init(&document_glr);
    arena_init(&response_arena);

    if (building_state) {
        if (prepare_only && state->ready) {
            (void)snprintf(error, sizeof(error),
                           "prepared document pipeline is already bound");
            goto rejected;
        }
        if (prepare_only && state->initialized)
            prepared_state_release(state);
        if (!scalar_limits ||
            (lexical_nfa_path != NULL && !lexical_limits)) {
            (void)snprintf(error, sizeof(error),
                           "incomplete prepared pipeline limits");
            goto rejected;
        }
        prepared_state_init(state);
        state_initialized_here = true;
        lexical_shadow_enabled = lexical_nfa_path != NULL;
        state->lexical_shadow_enabled = lexical_shadow_enabled;
        state->owner_symbols = g_symbols;
        state->owner_symbols_instance_id =
            symbol_table_instance_id(g_symbols);
        state->scalar_limits = *scalar_limits;
        if (lexical_shadow_enabled)
            state->lexical_limits = *lexical_limits;
    } else {
        if (!state->ready || state->owner_symbols != g_symbols ||
            state->owner_symbols_instance_id !=
                symbol_table_instance_id(g_symbols)) {
            (void)snprintf(
                error, sizeof(error),
                "prepared document pipeline has the wrong atom runtime");
            goto rejected;
        }
        lexical_shadow_enabled = state->lexical_shadow_enabled;
        scalar_limits = &state->scalar_limits;
        lexical_limits = lexical_shadow_enabled
            ? &state->lexical_limits
            : NULL;
    }
    if (building_state) {
        if (!ppabi_v1_wire_read(
                &splitter_wire, splitter_abi_path, error, sizeof(error)) ||
            !ppabi_v1_wire_load_pack(
                &splitter_wire, &splitter_pack, error, sizeof(error)) ||
            !ppabi_v1_wire_read(
                &form_wire, form_abi_path, error, sizeof(error)) ||
            !ppabi_v1_wire_load_pack(
                &form_wire, &form_pack, error, sizeof(error)) ||
            !fh_answer_stream_v1_read(
                &guard_answers, guard_nfa_path, error, sizeof(error)) ||
            guard_answers.len == 0u ||
            guard_answers.len > UINT32_MAX ||
            !rsnfa_v1_plan_load(
                &form_pack, guard_answers.terms, guard_answers.len,
                &guard_nfa, error, sizeof(error)) ||
            !ppguard_evidence_wire_v1_read(
                &evidence, guard_evidence_path, error, sizeof(error))) {
            goto rejected;
        }
        provenance = (PPGuardPlanV1ProvenanceInput){
            .source_digest = evidence.source_digest,
            .pre_reflection_digest = evidence.pre_reflection_digest,
            .environment_digest = evidence.environment_digest,
            .answer_set_digest = evidence.answer_set_digest,
            .regular_compiler_digest = regular_compiler_digest,
            .guard_nfa_answer_digest = guard_answers.digest,
            .guard_nfa_tags = guard_nfa.tags,
            .guard_nfa_tag_len = guard_nfa.nfa.tag_len,
            .derivations = evidence.derivations,
            .derivation_len = evidence.derivation_len,
        };
        if (!ppguard_plan_v1_build(
                &form_pack, NULL, &provenance, &guard_plan,
                error, sizeof(error)) ||
            !ppguard_scalar_exec_v1_plan_build(
                &form_pack, form_wire.start, &guard_plan, &guard_nfa,
                scalar_limits, &exec_plan, error, sizeof(error)) ||
            !ppnative_v1_prepare(
                &splitter_prepared, &splitter_pack, splitter_wire.start,
                error, sizeof(error))) {
            goto rejected;
        }
        local_shadow_receipt.splitter_parser_table_build_count =
            ppnative_v1_prepared_table_build_count(&splitter_prepared);
        local_shadow_receipt.scalar_form_parser_table_build_count =
            exec_plan.parser_table_build_count;
        local_shadow_receipt.scalar_form_witness_parser_family_build_count =
            exec_plan.guard_witness_parser_family_build_count;
        local_shadow_receipt.scalar_form_witness_prepared_start_len =
            exec_plan.guard_witness_prepared_start_len;
        if (lexical_shadow_enabled) {
            if (!guarded_nfa_path || !guarded_compiler_digest ||
                !lexical_limits) {
                (void)snprintf(
                    error, sizeof(error),
                    "incomplete lexical shadow configuration");
                goto rejected;
            }
            if (!fh_answer_stream_v1_read(
                    &lexical_answers, lexical_nfa_path,
                    error, sizeof(error)) ||
                lexical_answers.len == 0u ||
                lexical_answers.len > UINT32_MAX ||
                !rsnfa_v1_plan_load(
                    &form_pack, lexical_answers.terms, lexical_answers.len,
                    &lexical_nfa, error, sizeof(error)) ||
                !pplex_v1_plan_build(
                    &form_pack, lexical_nfa.tags, lexical_nfa.nfa.tag_len,
                    regular_compiler_digest, lexical_answers.digest,
                    &lexical_plan, error, sizeof(error)) ||
                !fh_answer_stream_v1_read(
                    &guarded_answers, guarded_nfa_path,
                    error, sizeof(error)) ||
                guarded_answers.len == 0u ||
                guarded_answers.len > UINT32_MAX ||
                !ppguard_plan_v1_build(
                    &form_pack, &lexical_plan, &provenance,
                    &lexical_guard_plan, error, sizeof(error)) ||
                !ppguarded_lex_v1_plan_build(
                    &form_pack, &lexical_plan, &lexical_guard_plan,
                    guarded_answers.terms, guarded_answers.len,
                    guarded_compiler_digest, guarded_answers.digest,
                    &guarded_plan, error, sizeof(error)) ||
                !ppguarded_lex_exec_v1_plan_build(
                    &form_pack, form_wire.start,
                    &lexical_plan, &lexical_guard_plan,
                    &guarded_plan, &lexical_nfa, &guard_nfa,
                    lexical_limits, &lexical_exec_plan,
                    error, sizeof(error))) {
                goto rejected;
            }
            (void)snprintf(
                local_shadow_receipt.lexical_plan_digest,
                sizeof(local_shadow_receipt.lexical_plan_digest), "%s",
                lexical_plan.plan_digest);
            (void)snprintf(
                local_shadow_receipt.guard_plan_digest,
                sizeof(local_shadow_receipt.guard_plan_digest), "%s",
                lexical_guard_plan.plan_digest);
            (void)snprintf(
                local_shadow_receipt.guarded_plan_digest,
                sizeof(local_shadow_receipt.guarded_plan_digest), "%s",
                guarded_plan.plan_digest);
            (void)snprintf(
                local_shadow_receipt.execution_plan_digest,
                sizeof(local_shadow_receipt.execution_plan_digest), "%s",
                lexical_exec_plan.execution_plan_digest);
            local_shadow_receipt.lexical_final_parser_table_build_count =
                lexical_exec_plan.final_parser_table_build_count;
            if (lexical_exec_plan.ordinary_witness_parser_family_build_count >
                    UINT32_MAX -
                        lexical_exec_plan
                            .guard_witness_parser_family_build_count ||
                lexical_exec_plan
                            .ordinary_witness_parser_family_build_count +
                        lexical_exec_plan
                            .guard_witness_parser_family_build_count >
                    UINT32_MAX -
                        lexical_exec_plan
                            .guarded_witness_parser_family_build_count ||
                lexical_exec_plan.ordinary_witness_prepared_start_len >
                    UINT32_MAX -
                        lexical_exec_plan.guard_witness_prepared_start_len ||
                lexical_exec_plan.ordinary_witness_prepared_start_len +
                        lexical_exec_plan.guard_witness_prepared_start_len >
                    UINT32_MAX -
                        lexical_exec_plan.guarded_witness_prepared_start_len) {
                (void)snprintf(
                    error, sizeof(error),
                    "lexical witness preparation receipt overflow");
                goto rejected;
            }
            local_shadow_receipt.lexical_witness_parser_family_build_count =
                lexical_exec_plan.ordinary_witness_parser_family_build_count +
                lexical_exec_plan.guard_witness_parser_family_build_count +
                lexical_exec_plan.guarded_witness_parser_family_build_count;
            local_shadow_receipt.lexical_witness_prepared_start_len =
                lexical_exec_plan.ordinary_witness_prepared_start_len +
                lexical_exec_plan.guard_witness_prepared_start_len +
                lexical_exec_plan.guarded_witness_prepared_start_len;
        }
        state->base_shadow_receipt = local_shadow_receipt;
        state->build_count = 1u;
        state->ready = true;
        if (prepare_only) {
            ok = true;
            goto done;
        }
    } else {
        local_shadow_receipt = state->base_shadow_receipt;
    }
    if (!ppgll_v1_prepared_parse(
            &splitter_prepared, input, input_len,
            scalar_limits->parse_work_limit, scalar_limits->replay_depth,
            scalar_limits->result_limit, &document_gll,
            error, sizeof(error))) {
        goto rejected;
    }
    {
        const CettaLpNativeUtf8ScalarView document_view = {
            .codepoints = document_gll.forest.codepoints,
            .byte_offsets = document_gll.forest.byte_offsets,
            .scalar_len = document_gll.forest.scalar_len,
            .input_byte_len = document_gll.forest.input_byte_len,
            .decoded_byte_len = 0u,
            .source_pass_count = 0u,
        };
        if (!ppglr_v1_prepared_parse_scalar_view(
                &splitter_prepared, &document_view,
                scalar_limits->parse_work_limit, scalar_limits->replay_depth,
                scalar_limits->result_limit, &document_glr,
                error, sizeof(error)) ||
            document_gll.outcome != PPNATIVE_V1_COMPLETED ||
            document_glr.outcome != PPNATIVE_V1_COMPLETED ||
            document_gll.forest.source_pass_count != 1u ||
            document_gll.forest.decoded_byte_len != input_len ||
            document_glr.forest.source_pass_count != 0u ||
            document_glr.forest.decoded_byte_len != 0u ||
            !results_equal(&document_gll, &document_glr)) {
            if (!error[0]) {
                (void)snprintf(
                    error, sizeof(error),
                    "single-decode GLL and GLR splitter executions differ");
            }
            goto rejected;
        }
    }
    if (document_gll.accepted) {
        if (!extract_forms(
                &document_gll, &forms, error, sizeof(error)) ||
            !collect_form_spans(
                &splitter_pack, splitter_wire.start,
                &document_gll.forest, &spans, error, sizeof(error)) ||
            forms.len != spans.len) {
            if (!error[0]) {
                (void)snprintf(error, sizeof(error),
                               "splitter semantic forms and spans differ");
            }
            goto rejected;
        }
    } else if (document_gll.semantic_result_len != 0u) {
        (void)snprintf(error, sizeof(error),
                       "rejected splitter retained semantic forms");
        goto rejected;
    }

    if (out && forms.len > 0u) {
        form_responses = calloc(forms.len, sizeof(*form_responses));
        if (!form_responses) {
            (void)snprintf(error, sizeof(error),
                           "failed to allocate document response forms");
            goto rejected;
        }
    }

    if (stream) {
        (void)fprintf(stream, "petta-document-pipeline-v1\n");
        (void)fprintf(stream, "splitter-pack-digest\t%s\n",
                      splitter_pack.pack_digest);
        (void)fprintf(stream, "form-pack-digest\t%s\n",
                      form_pack.pack_digest);
        (void)fprintf(stream, "guard-plan-digest\t%s\n",
                      guard_plan.plan_digest);
        (void)fprintf(stream, "guard-evidence-digest\t%s\n",
                      guard_plan.evidence_digest);
        (void)fprintf(stream, "guard-nfa-answer-digest\t%s\n",
                      guard_answers.digest);
        (void)fprintf(stream, "document-decision\t%s\n",
                      document_gll.accepted ? "accepted" : "rejected");
        (void)fprintf(stream, "document-gll-forest-digest\t%s\n",
                      document_gll.forest_digest);
        (void)fprintf(stream, "document-glr-forest-digest\t%s\n",
                      document_glr.forest_digest);
        (void)fprintf(stream, "document-gll-source-passes\t%u\n",
                      document_gll.forest.source_pass_count);
        (void)fprintf(stream, "document-glr-source-passes\t%u\n",
                      document_glr.forest.source_pass_count);
        (void)fprintf(stream, "source-passes\t1\n");
        (void)fprintf(stream, "input-bytes\t%u\n",
                      document_gll.forest.input_byte_len);
        (void)fprintf(stream, "input-scalars\t%u\n",
                      document_gll.forest.scalar_len);
        (void)fprintf(stream, "form-count\t%u\n", forms.len);
    }

    for (form_index = 0u; form_index < forms.len; form_index++) {
        PeTTaPipelineFormV1 *form = &forms.data[form_index];
        const PeTTaPipelineSpanV1 *span = &spans.data[form_index];
        const CettaLpNativeUtf8ScalarView view = {
            .codepoints = form->codepoints,
            .byte_offsets = form->byte_offsets,
            .scalar_len = form->scalar_len,
            .input_byte_len = form->byte_len,
            .decoded_byte_len = 0u,
            .source_pass_count = 0u,
        };
        PPGuardScalarExecV1Result result;
        PPGuardedLexExecV1Result shadow_result;

        form->source_scalar_left = span->scalar_left;
        form->source_scalar_right = span->scalar_right;
        form->source_byte_left = span->byte_left;
        form->source_byte_right = span->byte_right;
        ppguard_scalar_exec_v1_result_init(&result);
        ppguarded_lex_exec_v1_result_init(&shadow_result);
        if (!ppguard_scalar_exec_v1_run(
                &form_pack, form_wire.start, &guard_plan,
                &exec_plan, &view, scalar_limits, &result,
                error, sizeof(error)) ||
            result.gll.outcome != PPNATIVE_V1_COMPLETED ||
            result.glr.outcome != PPNATIVE_V1_COMPLETED ||
            result.scan.source_pass_count != 0u ||
            result.gll_receipt.source_pass_count != 0u ||
            result.glr_receipt.source_pass_count != 0u) {
            ppguarded_lex_exec_v1_result_free(&shadow_result);
            ppguard_scalar_exec_v1_result_free(&result);
            if (!error[0]) {
                (void)snprintf(error, sizeof(error),
                               "filtered form execution did not complete");
            }
            goto rejected;
        }
        if (lexical_shadow_enabled) {
            if (!ppguarded_lex_exec_v1_run(
                    &form_pack, form_wire.start, &lexical_plan,
                    &lexical_guard_plan, &guarded_plan,
                    &lexical_exec_plan, &view, lexical_limits,
                    &shadow_result, error, sizeof(error)) ||
                shadow_result.gll.outcome != PPNATIVE_V1_COMPLETED ||
                shadow_result.glr.outcome != PPNATIVE_V1_COMPLETED ||
                shadow_result.receipt.source_pass_count != 0u ||
                shadow_result.receipt.dfa_scan_count != 1u ||
                shadow_result.receipt.input_byte_len != form->byte_len ||
                shadow_result.receipt.input_scalar_len != form->scalar_len ||
                !semantic_results_equal(&result.gll, &shadow_result.gll) ||
                !semantic_results_equal(&result.glr, &shadow_result.glr)) {
                ppguarded_lex_exec_v1_result_free(&shadow_result);
                ppguard_scalar_exec_v1_result_free(&result);
                if (!error[0]) {
                    (void)snprintf(
                        error, sizeof(error),
                        "guarded lexical shadow changed form semantics");
                }
                goto rejected;
            }
            local_shadow_receipt.form_execution_count++;
            local_shadow_receipt.semantic_agreement_count++;
            local_shadow_receipt.source_pass_count +=
                shadow_result.receipt.source_pass_count;
            local_shadow_receipt.dfa_scan_count +=
                shadow_result.receipt.dfa_scan_count;
        }
        if (result.gll.accepted)
            form_accept_len++;
        else
            form_reject_len++;
        if (stream) {
            (void)fprintf(
                stream, "form\t%u\t%s\t%u\t%u\t%u\t%u\t",
                form_index, form->kind,
                form->source_scalar_left, form->source_scalar_right,
                form->source_byte_left, form->source_byte_right);
            write_scalar_hex(stream, form->codepoints, form->scalar_len);
            (void)fprintf(stream, "\t");
            write_hex(
                stream, input + form->source_byte_left,
                (size_t)(form->source_byte_right - form->source_byte_left));
            (void)fprintf(
                stream, "\t%s\t%s\t%s\t%u\t%u\n",
                result.gll.accepted ? "accepted" : "rejected",
                result.gll.forest_digest,
                result.gll_relation.relation_digest,
                result.scan.source_pass_count,
                result.scan.token_len);
        }
        if (stream && !write_form_results(
                stream, form_index, &result.gll)) {
            ppguarded_lex_exec_v1_result_free(&shadow_result);
            ppguard_scalar_exec_v1_result_free(&result);
            (void)snprintf(error, sizeof(error),
                           "failed to write filtered form results");
            goto rejected;
        }
        if (out) {
            form_responses[form_index] = pipeline_form_response(
                &response_arena, form_index, form, &result);
            if (!form_responses[form_index]) {
                ppguarded_lex_exec_v1_result_free(&shadow_result);
                ppguard_scalar_exec_v1_result_free(&result);
                (void)snprintf(error, sizeof(error),
                               "failed to construct document response form");
                goto rejected;
            }
        }
        ppguarded_lex_exec_v1_result_free(&shadow_result);
        ppguard_scalar_exec_v1_result_free(&result);
    }
    if (stream) {
        (void)fprintf(stream, "form-accepted\t%u\n", form_accept_len);
        (void)fprintf(stream, "form-rejected\t%u\n", form_reject_len);
        (void)fprintf(stream, "end\n");
    }
    if (out) {
        response_atom = pipeline_document_response(
            &response_arena, &splitter_pack, &form_pack, &guard_plan,
            &guard_answers, &document_gll, &document_glr,
            form_responses, forms.len, form_accept_len, form_reject_len);
        if (!response_atom ||
            !fh_ground_term_v1_render(
                response_atom, &response_bytes, &response_len,
                error, sizeof(error))) {
            if (!error[0]) {
                (void)snprintf(error, sizeof(error),
                               "failed to render document response");
            }
            goto rejected;
        }
        petta_document_pipeline_v1_response_free(out);
        out->bytes = response_bytes;
        out->len = response_len;
        response_bytes = NULL;
    }
    if (lexical_shadow_enabled)
        *shadow_receipt = local_shadow_receipt;
    ok = true;
    goto done;

rejected:
    if (stream) {
        (void)fprintf(stderr, "PeTTa document pipeline rejected: %s\n",
                      error[0] ? error : "unknown rejection");
    }
    if (error_buf && error_buf_size > 0u) {
        (void)snprintf(error_buf, error_buf_size, "%s",
                       error[0] ? error : "unknown document rejection");
    }

done:
    free(response_bytes);
    free(form_responses);
    arena_free(&response_arena);
    free(spans.data);
    forms_free(&forms);
    ppnative_v1_result_free(&document_glr);
    ppnative_v1_result_free(&document_gll);
    if (owns_state || (prepare_only && state_initialized_here && !ok))
        prepared_state_release(state);

#undef splitter_wire
#undef form_wire
#undef splitter_pack
#undef form_pack
#undef guard_answers
#undef guard_nfa
#undef evidence
#undef guard_plan
#undef exec_plan
#undef lexical_answers
#undef guarded_answers
#undef lexical_nfa
#undef lexical_plan
#undef lexical_guard_plan
#undef guarded_plan
#undef lexical_exec_plan
#undef splitter_prepared

    return ok;
}

typedef struct {
    const char *splitter_abi_path;
    const char *form_abi_path;
    const char *guard_nfa_path;
    const char *guard_evidence_path;
    const char *lexical_nfa_path;
    const char *guarded_nfa_path;
    const uint8_t *input_bytes;
    size_t input_byte_len;
    const char *regular_compiler_digest;
    const char *guarded_compiler_digest;
    const PPGuardScalarExecV1Limits *scalar_limits;
    const PPGuardedLexExecV1Limits *lexical_limits;
    PeTTaDocumentPipelineV1LexicalShadowReceipt *shadow_receipt;
    PeTTaDocumentPipelineV1Response *out;
    char *error_buf;
    size_t error_buf_size;
    PeTTaDocumentPipelineV1Prepared *prepared_context;
    bool prepare_only;
} PeTTaDocumentPipelineV1Call;

static bool petta_document_pipeline_v1_runtime_call(void *raw_context) {
    PeTTaDocumentPipelineV1Call *context = raw_context;

    return run_core(
        context->splitter_abi_path,
        context->form_abi_path,
        context->guard_nfa_path,
        context->guard_evidence_path,
        context->lexical_nfa_path,
        context->guarded_nfa_path,
        context->input_bytes,
        context->input_byte_len,
        context->regular_compiler_digest,
        context->guarded_compiler_digest,
        context->scalar_limits,
        context->lexical_limits,
        context->shadow_receipt,
        NULL,
        context->out,
        context->error_buf,
        context->error_buf_size,
        context->prepared_context,
        context->prepare_only);
}

static bool pipeline_limits_valid(
    const PPGuardScalarExecV1Limits *limits) {
    return limits &&
        limits->dfa_state_limit > 0u &&
        limits->dfa_transition_limit > 0u &&
        limits->scan_work_limit > 0u &&
        limits->scan_token_limit > 0u &&
        limits->witness_work_limit > 0u &&
        limits->parse_work_limit > 0u &&
        limits->replay_depth > 0u &&
        limits->result_limit > 0u;
}

static bool pipeline_lexical_limits_valid(
    const PPGuardedLexExecV1Limits *limits) {
    return limits &&
        limits->dfa_state_limit > 0u &&
        limits->dfa_transition_limit > 0u &&
        limits->scan_work_limit > 0u &&
        limits->scan_token_limit > 0u &&
        limits->witness_work_limit > 0u &&
        limits->parse_work_limit > 0u &&
        limits->replay_depth > 0u &&
        limits->result_limit > 0u;
}

bool petta_document_pipeline_v1_prepare(
    PeTTaDocumentPipelineV1Prepared *prepared,
    const char *splitter_abi_path,
    const char *form_abi_path,
    const char *guard_nfa_path,
    const char *guard_evidence_path,
    const char *regular_compiler_digest,
    const PPGuardScalarExecV1Limits *limits,
    char *error_buf,
    size_t error_buf_size) {
    PeTTaDocumentPipelineV1Call context;

    if (error_buf && error_buf_size > 0u)
        error_buf[0] = '\0';
    if (!prepared || !splitter_abi_path ||
        !form_abi_path || !guard_nfa_path || !guard_evidence_path ||
        !regular_compiler_digest || !pipeline_limits_valid(limits)) {
        if (error_buf && error_buf_size > 0u) {
            (void)snprintf(error_buf, error_buf_size,
                           "bad prepared PeTTa document configuration");
        }
        return false;
    }
    context = (PeTTaDocumentPipelineV1Call){
        .splitter_abi_path = splitter_abi_path,
        .form_abi_path = form_abi_path,
        .guard_nfa_path = guard_nfa_path,
        .guard_evidence_path = guard_evidence_path,
        .regular_compiler_digest = regular_compiler_digest,
        .scalar_limits = limits,
        .error_buf = error_buf,
        .error_buf_size = error_buf_size,
        .prepared_context = prepared,
        .prepare_only = true,
    };
    return ppnative_api_v1_with_runtime(
        petta_document_pipeline_v1_runtime_call,
        &context,
        error_buf,
        error_buf_size);
}

bool petta_document_pipeline_v1_prepare_lexical_shadow(
    PeTTaDocumentPipelineV1Prepared *prepared,
    const char *splitter_abi_path,
    const char *form_abi_path,
    const char *lexical_nfa_path,
    const char *guard_nfa_path,
    const char *guard_evidence_path,
    const char *guarded_nfa_path,
    const char *regular_compiler_digest,
    const char *guarded_compiler_digest,
    const PPGuardScalarExecV1Limits *scalar_limits,
    const PPGuardedLexExecV1Limits *lexical_limits,
    char *error_buf,
    size_t error_buf_size) {
    PeTTaDocumentPipelineV1Call context;

    if (error_buf && error_buf_size > 0u)
        error_buf[0] = '\0';
    if (!prepared || !splitter_abi_path ||
        !form_abi_path || !lexical_nfa_path || !guard_nfa_path ||
        !guard_evidence_path || !guarded_nfa_path ||
        !regular_compiler_digest || !guarded_compiler_digest ||
        !pipeline_limits_valid(scalar_limits) ||
        !pipeline_lexical_limits_valid(lexical_limits)) {
        if (error_buf && error_buf_size > 0u) {
            (void)snprintf(
                error_buf, error_buf_size,
                "bad prepared PeTTa document lexical configuration");
        }
        return false;
    }
    context = (PeTTaDocumentPipelineV1Call){
        .splitter_abi_path = splitter_abi_path,
        .form_abi_path = form_abi_path,
        .guard_nfa_path = guard_nfa_path,
        .guard_evidence_path = guard_evidence_path,
        .lexical_nfa_path = lexical_nfa_path,
        .guarded_nfa_path = guarded_nfa_path,
        .regular_compiler_digest = regular_compiler_digest,
        .guarded_compiler_digest = guarded_compiler_digest,
        .scalar_limits = scalar_limits,
        .lexical_limits = lexical_limits,
        .error_buf = error_buf,
        .error_buf_size = error_buf_size,
        .prepared_context = prepared,
        .prepare_only = true,
    };
    return ppnative_api_v1_with_runtime(
        petta_document_pipeline_v1_runtime_call,
        &context,
        error_buf,
        error_buf_size);
}

bool petta_document_pipeline_v1_prepared_parse(
    PeTTaDocumentPipelineV1Prepared *prepared,
    const uint8_t *input_bytes,
    size_t input_byte_len,
    PeTTaDocumentPipelineV1Response *out,
    char *error_buf,
    size_t error_buf_size) {
    PeTTaDocumentPipelineV1Call context;

    if (error_buf && error_buf_size > 0u)
        error_buf[0] = '\0';
    if (!prepared || !prepared->ready ||
        prepared->lexical_shadow_enabled || !out ||
        input_byte_len > UINT32_MAX ||
        (input_byte_len > 0u && !input_bytes)) {
        if (error_buf && error_buf_size > 0u) {
            (void)snprintf(error_buf, error_buf_size,
                           "bad prepared PeTTa document parse arguments");
        }
        return false;
    }
    context = (PeTTaDocumentPipelineV1Call){
        .input_bytes = input_bytes,
        .input_byte_len = input_byte_len,
        .out = out,
        .error_buf = error_buf,
        .error_buf_size = error_buf_size,
        .prepared_context = prepared,
    };
    return ppnative_api_v1_with_runtime(
        petta_document_pipeline_v1_runtime_call,
        &context,
        error_buf,
        error_buf_size);
}

bool petta_document_pipeline_v1_prepared_parse_lexical_shadow(
    PeTTaDocumentPipelineV1Prepared *prepared,
    const uint8_t *input_bytes,
    size_t input_byte_len,
    PeTTaDocumentPipelineV1LexicalShadowReceipt *receipt,
    PeTTaDocumentPipelineV1Response *out,
    char *error_buf,
    size_t error_buf_size) {
    PeTTaDocumentPipelineV1Call context;

    if (error_buf && error_buf_size > 0u)
        error_buf[0] = '\0';
    petta_document_pipeline_v1_lexical_shadow_receipt_init(receipt);
    if (!prepared || !prepared->ready ||
        !prepared->lexical_shadow_enabled || !receipt || !out ||
        input_byte_len > UINT32_MAX ||
        (input_byte_len > 0u && !input_bytes)) {
        if (error_buf && error_buf_size > 0u) {
            (void)snprintf(
                error_buf, error_buf_size,
                "bad prepared PeTTa document lexical parse arguments");
        }
        return false;
    }
    context = (PeTTaDocumentPipelineV1Call){
        .input_bytes = input_bytes,
        .input_byte_len = input_byte_len,
        .shadow_receipt = receipt,
        .out = out,
        .error_buf = error_buf,
        .error_buf_size = error_buf_size,
        .prepared_context = prepared,
    };
    return ppnative_api_v1_with_runtime(
        petta_document_pipeline_v1_runtime_call,
        &context,
        error_buf,
        error_buf_size);
}

bool petta_document_pipeline_v1_parse(
    const char *splitter_abi_path,
    const char *form_abi_path,
    const char *guard_nfa_path,
    const char *guard_evidence_path,
    const uint8_t *input_bytes,
    size_t input_byte_len,
    const char *regular_compiler_digest,
    const PPGuardScalarExecV1Limits *limits,
    PeTTaDocumentPipelineV1Response *out,
    char *error_buf,
    size_t error_buf_size) {
    PeTTaDocumentPipelineV1Call context;

    if (error_buf && error_buf_size > 0u)
        error_buf[0] = '\0';
    if (!splitter_abi_path || !form_abi_path || !guard_nfa_path ||
        !guard_evidence_path || !regular_compiler_digest || !out ||
        !pipeline_limits_valid(limits) || input_byte_len > UINT32_MAX ||
        (input_byte_len > 0u && !input_bytes)) {
        if (error_buf && error_buf_size > 0u) {
            (void)snprintf(error_buf, error_buf_size,
                           "bad PeTTa document pipeline arguments");
        }
        return false;
    }
    context = (PeTTaDocumentPipelineV1Call){
        .splitter_abi_path = splitter_abi_path,
        .form_abi_path = form_abi_path,
        .guard_nfa_path = guard_nfa_path,
        .guard_evidence_path = guard_evidence_path,
        .input_bytes = input_bytes,
        .input_byte_len = input_byte_len,
        .regular_compiler_digest = regular_compiler_digest,
        .scalar_limits = limits,
        .out = out,
        .error_buf = error_buf,
        .error_buf_size = error_buf_size,
    };
    return ppnative_api_v1_with_runtime(
        petta_document_pipeline_v1_runtime_call,
        &context,
        error_buf,
        error_buf_size);
}

bool petta_document_pipeline_v1_parse_lexical_shadow(
    const char *splitter_abi_path,
    const char *form_abi_path,
    const char *lexical_nfa_path,
    const char *guard_nfa_path,
    const char *guard_evidence_path,
    const char *guarded_nfa_path,
    const uint8_t *input_bytes,
    size_t input_byte_len,
    const char *regular_compiler_digest,
    const char *guarded_compiler_digest,
    const PPGuardScalarExecV1Limits *scalar_limits,
    const PPGuardedLexExecV1Limits *lexical_limits,
    PeTTaDocumentPipelineV1LexicalShadowReceipt *receipt,
    PeTTaDocumentPipelineV1Response *out,
    char *error_buf,
    size_t error_buf_size) {
    PeTTaDocumentPipelineV1Call context;

    if (error_buf && error_buf_size > 0u)
        error_buf[0] = '\0';
    petta_document_pipeline_v1_lexical_shadow_receipt_init(receipt);
    if (!splitter_abi_path || !form_abi_path || !lexical_nfa_path ||
        !guard_nfa_path || !guard_evidence_path || !guarded_nfa_path ||
        !regular_compiler_digest || !guarded_compiler_digest || !receipt ||
        !out || !pipeline_limits_valid(scalar_limits) ||
        !pipeline_lexical_limits_valid(lexical_limits) ||
        input_byte_len > UINT32_MAX ||
        (input_byte_len > 0u && !input_bytes)) {
        if (error_buf && error_buf_size > 0u) {
            (void)snprintf(
                error_buf, error_buf_size,
                "bad PeTTa document lexical shadow arguments");
        }
        return false;
    }
    context = (PeTTaDocumentPipelineV1Call){
        .splitter_abi_path = splitter_abi_path,
        .form_abi_path = form_abi_path,
        .guard_nfa_path = guard_nfa_path,
        .guard_evidence_path = guard_evidence_path,
        .lexical_nfa_path = lexical_nfa_path,
        .guarded_nfa_path = guarded_nfa_path,
        .input_bytes = input_bytes,
        .input_byte_len = input_byte_len,
        .regular_compiler_digest = regular_compiler_digest,
        .guarded_compiler_digest = guarded_compiler_digest,
        .scalar_limits = scalar_limits,
        .lexical_limits = lexical_limits,
        .shadow_receipt = receipt,
        .out = out,
        .error_buf = error_buf,
        .error_buf_size = error_buf_size,
    };
    return ppnative_api_v1_with_runtime(
        petta_document_pipeline_v1_runtime_call,
        &context,
        error_buf,
        error_buf_size);
}

int main(int argc, char **argv) {
    static const PPGuardScalarExecV1Limits limits = {
        .dfa_state_limit = UINT32_C(65536),
        .dfa_transition_limit = UINT32_C(2000000),
        .scan_work_limit = UINT64_C(20000000),
        .scan_token_limit = UINT32_C(2000000),
        .witness_work_limit = UINT32_C(10000000),
        .parse_work_limit = UINT32_C(50000000),
        .replay_depth = 4096u,
        .result_limit = UINT32_C(1000000),
    };
    SymbolTable symbols;
    uint8_t *input = NULL;
    size_t input_len = 0u;
    char error[512] = {0};
    bool ok;

    if (argc != 7) {
        fprintf(stderr,
                "usage: petta_document_pipeline_v1 SPLITTER_ABI FORM_ABI "
                "GUARD_NFA GUARD_EVIDENCE INPUT REGULAR_COMPILER_SHA256\n");
        return 1;
    }
    if (!read_input(argv[5], &input, &input_len, error, sizeof(error))) {
        (void)fprintf(stderr, "PeTTa document pipeline rejected: %s\n",
                      error[0] ? error : "cannot read document input");
        return 1;
    }
    symbol_table_init(&symbols);
    symbol_table_init_builtins(&symbols, &g_builtin_syms);
    g_symbols = &symbols;
    g_hashcons = NULL;
    g_var_intern = NULL;
    ok = run_core(
        argv[1], argv[2], argv[3], argv[4],
        NULL, NULL, input, input_len, argv[6], NULL,
        &limits, NULL, NULL,
        stdout, NULL, error, sizeof(error), NULL, false);
    symbol_table_free(&symbols);
    g_symbols = NULL;
    free(input);
    return ok ? 0 : 1;
}
