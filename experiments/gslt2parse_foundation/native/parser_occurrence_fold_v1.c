#include "parser_occurrence_fold_v1.h"

#include "finite_horn_ground_term_v1.h"
#include "native_sha256.h"
#include "parser_occurrence_span_mask_v1.h"

#include <ctype.h>
#include <inttypes.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum {
    PPOCCURRENCE_FOLD_V1_MAX_RECORDS = 65536u,
    PPOCCURRENCE_FOLD_V1_MAX_CONTRACT_DEPTH = 4096u,
    PPOCCURRENCE_FOLD_V1_MAX_PENDING_VALUES = 16u * 1024u * 1024u
};

typedef struct {
    Atom *tag;
    Atom *role;
    Atom *node;
    Atom *source_value_production_label;
    Atom *value_production_label;
    char *tag_name;
    char *role_name;
    char *node_name;
} PPOccurrenceFoldV1RawToken;

typedef struct {
    Atom *role;
    Atom *operation;
    char *role_name;
    char *operation_name;
} PPOccurrenceFoldV1RawShift;

typedef struct {
    Atom *label;
    Atom *node;
    Atom *operation;
    Atom *contract;
    char *label_name;
    char *node_name;
    char *operation_name;
} PPOccurrenceFoldV1RawProduction;

typedef struct {
    char **data;
    uint32_t len;
    uint32_t cap;
} PPOccurrenceFoldV1NameVec;

typedef struct {
    PPOccurrenceFoldV1Transition *data;
    uint32_t len;
    uint32_t cap;
} PPOccurrenceFoldV1TransitionVec;

typedef struct {
    uint32_t start;
    uint32_t accept;
} PPOccurrenceFoldV1Fragment;

typedef struct {
    const PPOccurrenceFoldV1Plan *plan;
    PPOccurrenceFoldV1TransitionVec *transitions;
    uint32_t next_state;
} PPOccurrenceFoldV1ContractCompiler;

typedef struct {
    const PPGuardedLexCursorV1Program *program;
    const PPOccurrenceFoldV1Plan *plan;
    const PPOccurrenceSpanMaskV1Plan *span_mask;
    const uint8_t *input;
    size_t input_len;
    PPOccurrenceFoldV1Backend backend;
    PPOccurrenceFoldV1Value *pending;
    uint32_t pending_len;
    uint32_t pending_cap;
    PPOccurrenceFoldV1Value *selected;
    uint32_t selected_cap;
    uint32_t *span_mask_actions;
    uint32_t span_mask_action_cap;
    PPOccurrenceFoldV1Receipt receipt;
    bool saw_accept;
    bool aborted;
    bool committed;
} PPOccurrenceFoldV1Run;

static void ppof_v1_set_error(char *buf,
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

static bool ppof_v1_expr_head(const Atom *atom,
                              const char *head,
                              CettaExprLen argument_len) {
    return atom && atom->kind == ATOM_EXPR &&
        atom->expr.len == argument_len + 1u &&
        atom_is_symbol(atom->expr.elems[0], head);
}

static bool ppof_v1_digest_valid(const char *digest) {
    size_t index;

    if (!digest || strlen(digest) != 64u)
        return false;
    for (index = 0u; index < 64u; index++) {
        char value = digest[index];
        if (!((value >= '0' && value <= '9') ||
              (value >= 'a' && value <= 'f'))) {
            return false;
        }
    }
    return true;
}

static bool ppof_v1_array_fits(size_t count, size_t element_size) {
    return element_size == 0u || count <= SIZE_MAX / element_size;
}

static char *ppof_v1_render(const Atom *term) {
    uint8_t *bytes = NULL;
    size_t len = 0u;

    if (!fh_ground_term_v1_render(term, &bytes, &len, NULL, 0u))
        return NULL;
    return (char *)bytes;
}

static char *ppof_v1_text_dup(const char *text) {
    size_t len;
    char *copy;

    if (!text)
        return NULL;
    len = strlen(text);
    copy = malloc(len + 1u);
    if (copy)
        memcpy(copy, text, len + 1u);
    return copy;
}

static int ppof_v1_text_compare(const void *left, const void *right) {
    const char *const *lhs = left;
    const char *const *rhs = right;
    return strcmp(*lhs, *rhs);
}

static int ppof_v1_terminal_compare(const void *left, const void *right) {
    const PPOccurrenceFoldV1TerminalBinding *lhs = left;
    const PPOccurrenceFoldV1TerminalBinding *rhs = right;
    return lhs->source_index < rhs->source_index ? -1 :
        lhs->source_index > rhs->source_index ? 1 : 0;
}

static int ppof_v1_production_compare(const void *left, const void *right) {
    const PPOccurrenceFoldV1ProductionBinding *lhs = left;
    const PPOccurrenceFoldV1ProductionBinding *rhs = right;
    return lhs->production_index < rhs->production_index ? -1 :
        lhs->production_index > rhs->production_index ? 1 : 0;
}

static bool ppof_v1_name_vec_add(PPOccurrenceFoldV1NameVec *vec,
                                 const char *name) {
    uint32_t index;
    char **next;
    uint32_t cap;

    for (index = 0u; index < vec->len; index++) {
        if (strcmp(vec->data[index], name) == 0)
            return true;
    }
    if (vec->len == vec->cap) {
        cap = vec->cap ? vec->cap * 2u : 8u;
        if (cap < vec->cap ||
            !ppof_v1_array_fits(cap, sizeof(*vec->data))) {
            return false;
        }
        next = realloc(vec->data, sizeof(*next) * cap);
        if (!next)
            return false;
        vec->data = next;
        vec->cap = cap;
    }
    vec->data[vec->len] = ppof_v1_text_dup(name);
    if (!vec->data[vec->len])
        return false;
    vec->len++;
    return true;
}

static void ppof_v1_name_vec_free(PPOccurrenceFoldV1NameVec *vec) {
    uint32_t index;

    if (!vec)
        return;
    for (index = 0u; index < vec->len; index++)
        free(vec->data[index]);
    free(vec->data);
    memset(vec, 0, sizeof(*vec));
}

static int32_t ppof_v1_name_find(char *const *names,
                                 uint32_t len,
                                 const char *name) {
    uint32_t low = 0u;
    uint32_t high = len;

    while (low < high) {
        uint32_t middle = low + (high - low) / 2u;
        int comparison = strcmp(names[middle], name);
        if (comparison < 0)
            low = middle + 1u;
        else
            high = middle;
    }
    if (low >= len || strcmp(names[low], name) != 0)
        return -1;
    return (int32_t)low;
}

static bool ppof_v1_transition_push(
    PPOccurrenceFoldV1TransitionVec *vec,
    PPOccurrenceFoldV1Transition transition) {
    PPOccurrenceFoldV1Transition *next;
    uint32_t cap;

    if (vec->len == vec->cap) {
        cap = vec->cap ? vec->cap * 2u : 16u;
        if (cap < vec->cap ||
            !ppof_v1_array_fits(cap, sizeof(*vec->data))) {
            return false;
        }
        next = realloc(vec->data, sizeof(*next) * cap);
        if (!next)
            return false;
        vec->data = next;
        vec->cap = cap;
    }
    vec->data[vec->len++] = transition;
    return true;
}

static bool ppof_v1_new_state(PPOccurrenceFoldV1ContractCompiler *compiler,
                              uint32_t *out) {
    if (compiler->next_state == UINT32_MAX)
        return false;
    *out = compiler->next_state++;
    return true;
}

static bool ppof_v1_compile_contract(
    PPOccurrenceFoldV1ContractCompiler *compiler,
    const Atom *term,
    unsigned depth,
    PPOccurrenceFoldV1Fragment *out) {
    PPOccurrenceFoldV1Fragment first;
    PPOccurrenceFoldV1Fragment second;
    char *role_name = NULL;
    int32_t role_id;
    bool ok = false;

    if (depth > PPOCCURRENCE_FOLD_V1_MAX_CONTRACT_DEPTH)
        return false;
    if (atom_is_symbol((Atom *)term, "fold-none")) {
        return ppof_v1_new_state(compiler, &out->start) &&
            ((out->accept = out->start), true);
    }
    if (ppof_v1_expr_head(term, "fold-one", 1u) ||
        ppof_v1_expr_head(term, "fold-plus", 1u) ||
        ppof_v1_expr_head(term, "fold-star", 1u)) {
        role_name = ppof_v1_render(term->expr.elems[1]);
        if (!role_name)
            goto done;
        role_id = ppof_v1_name_find(
            compiler->plan->roles, compiler->plan->role_len, role_name);
        if (role_id < 0)
            goto done;
        if (ppof_v1_expr_head(term, "fold-star", 1u)) {
            if (!ppof_v1_new_state(compiler, &out->start))
                goto done;
            out->accept = out->start;
            ok = ppof_v1_transition_push(
                compiler->transitions,
                (PPOccurrenceFoldV1Transition){
                    .kind = PPOCCURRENCE_FOLD_V1_TRANSITION_ROLE,
                    .source = out->start,
                    .target = out->start,
                    .role_id = (uint32_t)role_id,
                });
            goto done;
        }
        if (!ppof_v1_new_state(compiler, &out->start) ||
            !ppof_v1_new_state(compiler, &out->accept) ||
            !ppof_v1_transition_push(
                compiler->transitions,
                (PPOccurrenceFoldV1Transition){
                    .kind = PPOCCURRENCE_FOLD_V1_TRANSITION_ROLE,
                    .source = out->start,
                    .target = out->accept,
                    .role_id = (uint32_t)role_id,
                })) {
            goto done;
        }
        if (ppof_v1_expr_head(term, "fold-plus", 1u) &&
            !ppof_v1_transition_push(
                compiler->transitions,
                (PPOccurrenceFoldV1Transition){
                    .kind = PPOCCURRENCE_FOLD_V1_TRANSITION_ROLE,
                    .source = out->accept,
                    .target = out->accept,
                    .role_id = (uint32_t)role_id,
                })) {
            goto done;
        }
        ok = true;
        goto done;
    }
    if (!ppof_v1_expr_head(term, "fold-seq", 2u) ||
        !ppof_v1_compile_contract(
            compiler, term->expr.elems[1], depth + 1u, &first) ||
        !ppof_v1_compile_contract(
            compiler, term->expr.elems[2], depth + 1u, &second) ||
        !ppof_v1_transition_push(
            compiler->transitions,
            (PPOccurrenceFoldV1Transition){
                .kind = PPOCCURRENCE_FOLD_V1_TRANSITION_EPSILON,
                .source = first.accept,
                .target = second.start,
                .role_id = UINT32_MAX,
            })) {
        goto done;
    }
    out->start = first.start;
    out->accept = second.accept;
    ok = true;

done:
    free(role_name);
    return ok;
}

static void ppof_v1_sha_u32(CettaNativeSha256 *sha, uint32_t value) {
    const uint8_t bytes[4] = {
        (uint8_t)(value >> 24u),
        (uint8_t)(value >> 16u),
        (uint8_t)(value >> 8u),
        (uint8_t)value,
    };
    cetta_native_sha256_update(sha, bytes, sizeof(bytes));
}

static void ppof_v1_sha_bytes(CettaNativeSha256 *sha,
                              const uint8_t *bytes,
                              size_t len) {
    uint64_t value = (uint64_t)len;
    uint8_t length[8];
    uint32_t index;

    for (index = 0u; index < 8u; index++)
        length[7u - index] = (uint8_t)(value >> (index * 8u));
    cetta_native_sha256_update(sha, length, sizeof(length));
    cetta_native_sha256_update(sha, bytes, len);
}

static void ppof_v1_sha_text(CettaNativeSha256 *sha, const char *text) {
    ppof_v1_sha_bytes(sha, (const uint8_t *)text, strlen(text));
}

static bool ppof_v1_answer_set_digest(Atom *const *records,
                                      uint32_t record_len,
                                      char out[65]) {
    static const char domain[] = "FiniteHornAnswerSetV1\n";
    CettaNativeSha256 sha;
    char **canonical = NULL;
    uint32_t index;
    bool ok = false;

    canonical = calloc(record_len ? record_len : 1u, sizeof(*canonical));
    if (!canonical)
        goto done;
    for (index = 0u; index < record_len; index++) {
        canonical[index] = ppof_v1_render(records[index]);
        if (!canonical[index])
            goto done;
    }
    if (record_len > 1u) {
        qsort(canonical, record_len, sizeof(*canonical),
              ppof_v1_text_compare);
    }
    for (index = 1u; index < record_len; index++) {
        if (strcmp(canonical[index - 1u], canonical[index]) == 0)
            goto done;
    }
    cetta_native_sha256_init(&sha);
    cetta_native_sha256_update(
        &sha, (const uint8_t *)domain, sizeof(domain) - 1u);
    for (index = 0u; index < record_len; index++) {
        cetta_native_sha256_update(
            &sha, (const uint8_t *)canonical[index],
            strlen(canonical[index]));
        cetta_native_sha256_update(&sha, (const uint8_t *)"\n", 1u);
    }
    cetta_native_sha256_finish_hex(&sha, out);
    ok = true;

done:
    if (canonical) {
        for (index = 0u; index < record_len; index++)
            free(canonical[index]);
    }
    free(canonical);
    return ok;
}

static bool ppof_v1_compute_plan_digest(
    const PPOccurrenceFoldV1Plan *plan,
    char out[65]) {
    static const char domain[] = "ParserOccurrenceFoldPlanV1";
    CettaNativeSha256 sha;
    uint32_t index;

    cetta_native_sha256_init(&sha);
    ppof_v1_sha_bytes(
        &sha, (const uint8_t *)domain, sizeof(domain) - 1u);
    ppof_v1_sha_text(&sha, plan->base_pack_digest);
    ppof_v1_sha_text(&sha, plan->lexical_plan_digest);
    ppof_v1_sha_text(&sha, plan->cursor_program_digest);
    ppof_v1_sha_text(&sha, plan->compiler_answer_digest);
    ppof_v1_sha_u32(&sha, plan->role_len);
    for (index = 0u; index < plan->role_len; index++)
        ppof_v1_sha_text(&sha, plan->roles[index]);
    ppof_v1_sha_u32(&sha, plan->node_len);
    for (index = 0u; index < plan->node_len; index++)
        ppof_v1_sha_text(&sha, plan->nodes[index]);
    ppof_v1_sha_u32(&sha, plan->operation_len);
    for (index = 0u; index < plan->operation_len; index++)
        ppof_v1_sha_text(&sha, plan->operations[index]);
    ppof_v1_sha_u32(&sha, plan->terminal_len);
    for (index = 0u; index < plan->terminal_len; index++) {
        const PPOccurrenceFoldV1TerminalBinding *binding =
            &plan->terminals[index];
        ppof_v1_sha_u32(&sha, binding->source_index);
        ppof_v1_sha_u32(&sha, binding->role_id);
        ppof_v1_sha_u32(&sha, binding->shift_operation_id);
        ppof_v1_sha_u32(&sha, binding->value_production_label);
    }
    ppof_v1_sha_u32(&sha, plan->production_len);
    for (index = 0u; index < plan->production_len; index++) {
        const PPOccurrenceFoldV1ProductionBinding *binding =
            &plan->productions[index];
        ppof_v1_sha_u32(&sha, binding->production_index);
        ppof_v1_sha_u32(&sha, binding->production_label);
        ppof_v1_sha_u32(&sha, binding->node_id);
        ppof_v1_sha_u32(&sha, binding->operation_id);
        ppof_v1_sha_u32(&sha, binding->contract_index);
    }
    ppof_v1_sha_u32(&sha, plan->contract_len);
    for (index = 0u; index < plan->contract_len; index++) {
        const PPOccurrenceFoldV1Contract *contract =
            &plan->contracts[index];
        ppof_v1_sha_u32(&sha, contract->state_len);
        ppof_v1_sha_u32(&sha, contract->start_state);
        ppof_v1_sha_u32(&sha, contract->accept_state);
        ppof_v1_sha_u32(&sha, contract->transition_begin);
        ppof_v1_sha_u32(&sha, contract->transition_len);
    }
    ppof_v1_sha_u32(&sha, plan->transition_len);
    for (index = 0u; index < plan->transition_len; index++) {
        const PPOccurrenceFoldV1Transition *transition =
            &plan->transitions[index];
        ppof_v1_sha_u32(&sha, (uint32_t)transition->kind);
        ppof_v1_sha_u32(&sha, transition->source);
        ppof_v1_sha_u32(&sha, transition->target);
        ppof_v1_sha_u32(&sha, transition->role_id);
    }
    cetta_native_sha256_finish_hex(&sha, out);
    return true;
}

void ppoccurrence_fold_v1_plan_init(PPOccurrenceFoldV1Plan *plan) {
    if (plan)
        memset(plan, 0, sizeof(*plan));
}

void ppoccurrence_fold_v1_plan_free(PPOccurrenceFoldV1Plan *plan) {
    uint32_t index;

    if (!plan)
        return;
    for (index = 0u; index < plan->role_len; index++)
        free(plan->roles[index]);
    for (index = 0u; index < plan->node_len; index++)
        free(plan->nodes[index]);
    for (index = 0u; index < plan->operation_len; index++)
        free(plan->operations[index]);
    free(plan->roles);
    free(plan->nodes);
    free(plan->operations);
    free(plan->terminals);
    free(plan->productions);
    free(plan->contracts);
    free(plan->transitions);
    free(plan->terminal_binding_by_source);
    free(plan->production_binding_by_index);
    memset(plan, 0, sizeof(*plan));
}

static void ppof_v1_raw_free(PPOccurrenceFoldV1RawToken *tokens,
                             uint32_t token_len,
                             PPOccurrenceFoldV1RawShift *shifts,
                             uint32_t shift_len,
                             PPOccurrenceFoldV1RawProduction *productions,
                             uint32_t production_len) {
    uint32_t index;

    for (index = 0u; index < token_len; index++) {
        free(tokens[index].tag_name);
        free(tokens[index].role_name);
        free(tokens[index].node_name);
    }
    for (index = 0u; index < shift_len; index++) {
        free(shifts[index].role_name);
        free(shifts[index].operation_name);
    }
    for (index = 0u; index < production_len; index++) {
        free(productions[index].label_name);
        free(productions[index].node_name);
        free(productions[index].operation_name);
    }
    free(tokens);
    free(shifts);
    free(productions);
}

static bool ppof_v1_names_sorted_unique(char *const *names, uint32_t len) {
    uint32_t index;

    for (index = 0u; index < len; index++) {
        if (!names[index] ||
            (index > 0u && strcmp(names[index - 1u], names[index]) >= 0)) {
            return false;
        }
    }
    return true;
}

static int32_t ppof_v1_lexical_entry(
    const PPLexV1Plan *lexical_plan,
    const Atom *tag) {
    uint32_t index;
    int32_t found = -1;

    for (index = 0u; index < lexical_plan->entry_len; index++) {
        if (!atom_eq(lexical_plan->entries[index].tag, (Atom *)tag))
            continue;
        if (found >= 0)
            return -2;
        found = (int32_t)index;
    }
    return found;
}

static int32_t ppof_v1_cursor_source(
    const PPGuardedLexCursorV1Program *program,
    uint32_t dfa_tag) {
    uint32_t index;
    int32_t found = -1;

    for (index = 0u; index < program->terminal_len; index++) {
        if (program->terminals[index].dfa_tag != dfa_tag)
            continue;
        if (found >= 0)
            return -2;
        found = (int32_t)index;
    }
    return found;
}

static int32_t ppof_v1_pack_production(
    const PPABIV1Pack *pack,
    const Atom *label) {
    uint32_t index;
    int32_t found = -1;

    for (index = 0u; index < pack->production_len; index++) {
        Atom *identity = pack->productions[index].identity;
        if (!ppof_v1_expr_head(identity, "pp-production", 4u) ||
            !atom_eq(identity->expr.elems[1], (Atom *)label)) {
            continue;
        }
        if (found >= 0)
            return -2;
        found = (int32_t)index;
    }
    return found;
}

static bool ppof_v1_source_occurrence_count(
    const Atom *term,
    const char *source_digest,
    uint32_t depth,
    uint32_t *matches) {
    CettaExprLen index;

    if (!term || !source_digest || !matches ||
        depth > PPOCCURRENCE_FOLD_V1_MAX_CONTRACT_DEPTH) {
        return false;
    }
    if (ppof_v1_expr_head(
            term, "pp-inline-source-occurrence", 1u)) {
        Atom *reference = term->expr.elems[1];
        if (reference->kind == ATOM_SYMBOL &&
            strcmp(atom_name_cstr(reference), source_digest) == 0) {
            if (*matches == UINT32_MAX)
                return false;
            (*matches)++;
        }
        return true;
    }
    if (term->kind != ATOM_EXPR)
        return true;
    for (index = 0u; index < term->expr.len; index++) {
        if (!ppof_v1_source_occurrence_count(
                term->expr.elems[index], source_digest,
                depth + 1u, matches)) {
            return false;
        }
    }
    return true;
}

static bool ppof_v1_pack_production_has_source_occurrence(
    const PPABIV1Pack *pack,
    uint32_t production_index,
    const Atom *source_label) {
    const PPABIV1Production *production;
    CettaNativeSha256 sha;
    char source_digest[65];
    char *canonical = NULL;
    uint32_t evidence_index;
    uint32_t matches = 0u;
    bool ok = false;

    if (!pack || production_index >= pack->production_len || !source_label)
        return false;
    canonical = ppof_v1_render(source_label);
    if (!canonical)
        return false;
    cetta_native_sha256_init(&sha);
    cetta_native_sha256_update(
        &sha, (const uint8_t *)canonical, strlen(canonical));
    cetta_native_sha256_finish_hex(&sha, source_digest);
    production = &pack->productions[production_index];
    for (evidence_index = 0u;
         evidence_index < production->evidence_len; evidence_index++) {
        uint32_t derivation_index =
            production->evidence_begin + evidence_index;
        const PPABIV1Derivation *derivation;
        const Atom *certificate;

        if (derivation_index >= pack->derivation_len)
            goto done;
        derivation = &pack->derivations[derivation_index];
        certificate = derivation->certificate;
        if (derivation->kind != PPABI_V1_EVIDENCE_PRODUCTION ||
            !ppof_v1_expr_head(
                certificate,
                "pp-transparent-inline-certificate-v1", 7u)) {
            goto done;
        }
        if (!ppof_v1_source_occurrence_count(
                certificate->expr.elems[7], source_digest, 0u,
                &matches)) {
            goto done;
        }
    }
    ok = matches == 1u;

done:
    free(canonical);
    return ok;
}

static int32_t ppof_v1_cursor_production(
    const PPGuardedLexCursorV1Program *program,
    uint32_t production_label) {
    uint32_t index;
    int32_t found = -1;

    for (index = 0u;
         index < program->final_slr.production_len; index++) {
        const CettaLpNativeSlrProgramProduction *production =
            &program->final_slr.productions[index];
        if (!production->authored ||
            production->label != production_label) {
            continue;
        }
        if (found >= 0)
            return -2;
        found = (int32_t)index;
    }
    return found;
}

static bool ppof_v1_value_program_has_production(
    const PPGuardedLexCursorV1Program *program,
    uint32_t source_index,
    uint32_t production_label) {
    const PPGuardedLexCursorV1Terminal *terminal;
    const PPGuardedLexCursorV1ValueProgram *value_program;
    uint32_t index;
    uint32_t matches = 0u;

    if (!program || source_index >= program->terminal_len)
        return false;
    terminal = &program->terminals[source_index];
    if (terminal->semantic_program_index >= program->value_program_len) {
        return false;
    }
    value_program =
        &program->value_programs[terminal->semantic_program_index];
    for (index = 0u; index < value_program->slr.production_len; index++) {
        const CettaLpNativeSlrProgramProduction *production =
            &value_program->slr.productions[index];
        if (production->authored &&
            production->label == production_label) {
            matches++;
        }
    }
    return matches == 1u;
}

bool ppoccurrence_fold_v1_plan_build(
    const PPABIV1Pack *pack,
    const PPLexV1Plan *lexical_plan,
    const PPGuardedLexCursorV1Program *program,
    Atom *const *records,
    size_t record_len,
    const char *compiler_answer_digest,
    PPOccurrenceFoldV1Plan *out,
    char *error_buf,
    size_t error_buf_size) {
    PPOccurrenceFoldV1Plan result;
    PPOccurrenceFoldV1RawToken *tokens = NULL;
    PPOccurrenceFoldV1RawShift *shifts = NULL;
    PPOccurrenceFoldV1RawProduction *productions = NULL;
    PPOccurrenceFoldV1NameVec roles = {0};
    PPOccurrenceFoldV1NameVec nodes = {0};
    PPOccurrenceFoldV1NameVec operations = {0};
    PPOccurrenceFoldV1TransitionVec transitions = {0};
    uint32_t token_len = 0u;
    uint32_t shift_len = 0u;
    uint32_t production_len = 0u;
    uint32_t token_write = 0u;
    uint32_t shift_write = 0u;
    uint32_t production_write = 0u;
    uint32_t index;
    char computed_answers[65];
    bool ok = false;

    ppoccurrence_fold_v1_plan_init(&result);
    if (error_buf && error_buf_size > 0u)
        error_buf[0] = '\0';
    if (!pack || !lexical_plan || !program || !out ||
        (!records && record_len > 0u) ||
        record_len == 0u ||
        record_len > PPOCCURRENCE_FOLD_V1_MAX_RECORDS ||
        record_len > UINT32_MAX ||
        !ppof_v1_digest_valid(compiler_answer_digest) ||
        !ppguarded_lex_cursor_v1_program_validate(
            program, error_buf, error_buf_size) ||
        strcmp(program->base_pack_digest, pack->pack_digest) != 0 ||
        strcmp(program->lexical_plan_digest,
               lexical_plan->plan_digest) != 0 ||
        !ppof_v1_answer_set_digest(
            records, (uint32_t)record_len, computed_answers) ||
        strcmp(computed_answers, compiler_answer_digest) != 0) {
        if (error_buf && error_buf_size > 0u && error_buf[0] == '\0') {
            ppof_v1_set_error(
                error_buf, error_buf_size,
                "bad or unauthenticated occurrence-fold plan input");
        }
        goto done;
    }
    for (index = 0u; index < (uint32_t)record_len; index++) {
        Atom *record = records[index];
        if (ppof_v1_expr_head(record, "occurrence-fold-token-v1", 2u) ||
            ppof_v1_expr_head(record, "occurrence-fold-token-v3", 5u) ||
            ppof_v1_expr_head(record, "occurrence-fold-token-v4", 4u))
            token_len++;
        else if (ppof_v1_expr_head(
                     record, "occurrence-fold-shift-v1", 2u))
            shift_len++;
        else if (ppof_v1_expr_head(
                     record, "occurrence-fold-production-v1", 4u))
            production_len++;
        else {
            ppof_v1_set_error(
                error_buf, error_buf_size,
                "occurrence-fold record %u has an unknown shape", index);
            goto done;
        }
    }
    if (token_len == 0u || production_len == 0u ||
        !ppof_v1_array_fits(token_len, sizeof(*tokens)) ||
        !ppof_v1_array_fits(shift_len, sizeof(*shifts)) ||
        !ppof_v1_array_fits(production_len, sizeof(*productions))) {
        ppof_v1_set_error(
            error_buf, error_buf_size,
            "occurrence-fold record inventory is incomplete or too large");
        goto done;
    }
    tokens = calloc(token_len, sizeof(*tokens));
    shifts = calloc(shift_len ? shift_len : 1u, sizeof(*shifts));
    productions = calloc(production_len, sizeof(*productions));
    if (!tokens || !shifts || !productions)
        goto done;
    for (index = 0u; index < (uint32_t)record_len; index++) {
        Atom *record = records[index];
        if (ppof_v1_expr_head(record, "occurrence-fold-token-v1", 2u) ||
            ppof_v1_expr_head(record, "occurrence-fold-token-v3", 5u) ||
            ppof_v1_expr_head(record, "occurrence-fold-token-v4", 4u)) {
            PPOccurrenceFoldV1RawToken *row = &tokens[token_write++];
            row->tag = record->expr.elems[1];
            row->role = record->expr.elems[2];
            if (record->expr.len == 6u) {
                row->node = record->expr.elems[3];
                row->source_value_production_label =
                    record->expr.elems[4];
                row->value_production_label = record->expr.elems[5];
            } else if (record->expr.len == 5u) {
                row->node = record->expr.elems[3];
                row->value_production_label = record->expr.elems[4];
            }
            row->tag_name = ppof_v1_render(row->tag);
            row->role_name = ppof_v1_render(row->role);
            row->node_name = row->node ? ppof_v1_render(row->node) : NULL;
            if (!row->tag_name || !row->role_name ||
                (row->node && !row->node_name) ||
                !ppof_v1_name_vec_add(&roles, row->role_name)) {
                goto done;
            }
        } else if (ppof_v1_expr_head(
                       record, "occurrence-fold-shift-v1", 2u)) {
            PPOccurrenceFoldV1RawShift *row = &shifts[shift_write++];
            row->role = record->expr.elems[1];
            row->operation = record->expr.elems[2];
            row->role_name = ppof_v1_render(row->role);
            row->operation_name = ppof_v1_render(row->operation);
            if (!row->role_name || !row->operation_name ||
                !ppof_v1_name_vec_add(
                    &operations, row->operation_name)) {
                goto done;
            }
        } else {
            PPOccurrenceFoldV1RawProduction *row =
                &productions[production_write++];
            row->label = record->expr.elems[1];
            row->node = record->expr.elems[2];
            row->operation = record->expr.elems[3];
            row->contract = record->expr.elems[4];
            row->label_name = ppof_v1_render(row->label);
            row->node_name = ppof_v1_render(row->node);
            row->operation_name = ppof_v1_render(row->operation);
            if (!row->label_name || !row->node_name ||
                !row->operation_name ||
                !ppof_v1_name_vec_add(&nodes, row->node_name) ||
                !ppof_v1_name_vec_add(
                    &operations, row->operation_name)) {
                goto done;
            }
        }
    }
    qsort(roles.data, roles.len, sizeof(*roles.data),
          ppof_v1_text_compare);
    qsort(nodes.data, nodes.len, sizeof(*nodes.data),
          ppof_v1_text_compare);
    qsort(operations.data, operations.len, sizeof(*operations.data),
          ppof_v1_text_compare);
    result.roles = roles.data;
    result.role_len = roles.len;
    memset(&roles, 0, sizeof(roles));
    result.nodes = nodes.data;
    result.node_len = nodes.len;
    memset(&nodes, 0, sizeof(nodes));
    result.operations = operations.data;
    result.operation_len = operations.len;
    memset(&operations, 0, sizeof(operations));
    result.cursor_terminal_len = program->terminal_len;
    result.cursor_production_len = program->final_slr.production_len;
    result.terminal_len = token_len;
    result.production_len = production_len;
    result.contract_len = production_len;
    result.terminals = calloc(token_len, sizeof(*result.terminals));
    result.productions =
        calloc(production_len, sizeof(*result.productions));
    result.contracts =
        calloc(production_len, sizeof(*result.contracts));
    result.terminal_binding_by_source = malloc(
        sizeof(*result.terminal_binding_by_source) *
        (program->terminal_len ? program->terminal_len : 1u));
    result.production_binding_by_index = malloc(
        sizeof(*result.production_binding_by_index) *
        (program->final_slr.production_len
            ? program->final_slr.production_len : 1u));
    if (!result.terminals || !result.productions || !result.contracts ||
        !result.terminal_binding_by_source ||
        !result.production_binding_by_index) {
        goto done;
    }
    for (index = 0u; index < program->terminal_len; index++)
        result.terminal_binding_by_source[index] = UINT32_MAX;
    for (index = 0u; index < program->final_slr.production_len; index++)
        result.production_binding_by_index[index] = UINT32_MAX;
    for (index = 0u; index < shift_len; index++) {
        uint32_t other;
        if (ppof_v1_name_find(
                result.roles, result.role_len,
                shifts[index].role_name) < 0) {
            ppof_v1_set_error(
                error_buf, error_buf_size,
                "shift operation refers to an unmapped token role");
            goto done;
        }
        for (other = 0u; other < index; other++) {
            if (strcmp(shifts[index].role_name,
                       shifts[other].role_name) == 0) {
                ppof_v1_set_error(
                    error_buf, error_buf_size,
                    "token role has multiple shift operations");
                goto done;
            }
        }
    }
    for (index = 0u; index < token_len; index++) {
        int32_t lexical_index;
        int32_t source_index;
        int32_t role_id;
        uint32_t shift_index;
        uint32_t shift_operation_id = UINT32_MAX;
        uint32_t value_production_label = UINT32_MAX;
        uint32_t other;

        for (other = 0u; other < index; other++) {
            if (strcmp(tokens[index].tag_name,
                       tokens[other].tag_name) == 0) {
                ppof_v1_set_error(
                    error_buf, error_buf_size,
                    "lexical tag has multiple occurrence-fold roles");
                goto done;
            }
        }
        lexical_index = ppof_v1_lexical_entry(
            lexical_plan, tokens[index].tag);
        if (lexical_index < 0) {
            ppof_v1_set_error(
                error_buf, error_buf_size,
                "occurrence-fold token does not select one lexical entry");
            goto done;
        }
        source_index = ppof_v1_cursor_source(
            program,
            lexical_plan->entries[lexical_index].tag_index);
        role_id = ppof_v1_name_find(
            result.roles, result.role_len, tokens[index].role_name);
        if (source_index < 0 || role_id < 0 ||
            result.terminal_binding_by_source[source_index] != UINT32_MAX) {
            ppof_v1_set_error(
                error_buf, error_buf_size,
                "occurrence-fold token does not select one cursor source");
            goto done;
        }
        for (shift_index = 0u; shift_index < shift_len; shift_index++) {
            int32_t operation_id;
            if (strcmp(tokens[index].role_name,
                       shifts[shift_index].role_name) != 0) {
                continue;
            }
            operation_id = ppof_v1_name_find(
                result.operations, result.operation_len,
                shifts[shift_index].operation_name);
            if (operation_id < 0)
                goto done;
            shift_operation_id = (uint32_t)operation_id;
        }
        if (tokens[index].value_production_label) {
            int32_t production_label = ppof_v1_pack_production(
                pack, tokens[index].value_production_label);
            if (production_label < 0) {
                ppof_v1_set_error(
                    error_buf, error_buf_size,
                    "semantic token normalized production is absent or "
                    "ambiguous");
                goto done;
            }
            if (shift_operation_id != UINT32_MAX) {
                ppof_v1_set_error(
                    error_buf, error_buf_size,
                    "semantic token is also bound to a shift operation");
                goto done;
            }
            if (tokens[index].source_value_production_label &&
                !ppof_v1_pack_production_has_source_occurrence(
                    pack, (uint32_t)production_label,
                    tokens[index].source_value_production_label)) {
                ppof_v1_set_error(
                    error_buf, error_buf_size,
                    "semantic token normalized production lacks its unique "
                    "certified source occurrence");
                goto done;
            }
            value_production_label = (uint32_t)production_label;
        }
        result.terminals[index] =
            (PPOccurrenceFoldV1TerminalBinding){
                .source_index = (uint32_t)source_index,
                .role_id = (uint32_t)role_id,
                .shift_operation_id = shift_operation_id,
                .value_production_label = value_production_label,
            };
    }
    qsort(result.terminals, result.terminal_len,
          sizeof(*result.terminals), ppof_v1_terminal_compare);
    for (index = 0u; index < result.terminal_len; index++) {
        result.terminal_binding_by_source[
            result.terminals[index].source_index] = index;
    }
    for (index = 0u; index < production_len; index++) {
        PPOccurrenceFoldV1ContractCompiler compiler = {
            .plan = &result,
            .transitions = &transitions,
        };
        PPOccurrenceFoldV1Fragment fragment;
        int32_t pack_index;
        int32_t cursor_index;
        int32_t node_id;
        int32_t operation_id;
        uint32_t other;

        for (other = 0u; other < index; other++) {
            if (strcmp(productions[index].label_name,
                       productions[other].label_name) == 0 ||
                strcmp(productions[index].node_name,
                       productions[other].node_name) == 0) {
                ppof_v1_set_error(
                    error_buf, error_buf_size,
                    "occurrence-fold production label or node is repeated");
                goto done;
            }
        }
        pack_index = ppof_v1_pack_production(
            pack, productions[index].label);
        cursor_index = pack_index >= 0
            ? ppof_v1_cursor_production(program, (uint32_t)pack_index)
            : -1;
        node_id = ppof_v1_name_find(
            result.nodes, result.node_len,
            productions[index].node_name);
        operation_id = ppof_v1_name_find(
            result.operations, result.operation_len,
            productions[index].operation_name);
        if (pack_index < 0 || cursor_index < 0 ||
            node_id < 0 || operation_id < 0 ||
            result.production_binding_by_index[cursor_index] != UINT32_MAX) {
            ppof_v1_set_error(
                error_buf, error_buf_size,
                "occurrence-fold production does not select one cursor row");
            goto done;
        }
        result.contracts[index].transition_begin = transitions.len;
        if (!ppof_v1_compile_contract(
                &compiler, productions[index].contract, 0u, &fragment)) {
            ppof_v1_set_error(
                error_buf, error_buf_size,
                "occurrence-fold contract is malformed or uses an "
                "unmapped role");
            goto done;
        }
        result.contracts[index].state_len = compiler.next_state;
        result.contracts[index].start_state = fragment.start;
        result.contracts[index].accept_state = fragment.accept;
        result.contracts[index].transition_len =
            transitions.len -
            result.contracts[index].transition_begin;
        result.productions[index] =
            (PPOccurrenceFoldV1ProductionBinding){
                .production_index = (uint32_t)cursor_index,
                .production_label = (uint32_t)pack_index,
                .node_id = (uint32_t)node_id,
                .operation_id = (uint32_t)operation_id,
                .contract_index = index,
            };
    }
    result.transitions = transitions.data;
    result.transition_len = transitions.len;
    memset(&transitions, 0, sizeof(transitions));
    qsort(result.productions, result.production_len,
          sizeof(*result.productions), ppof_v1_production_compare);
    for (index = 0u; index < result.production_len; index++) {
        result.production_binding_by_index[
            result.productions[index].production_index] = index;
    }
    memcpy(result.base_pack_digest, pack->pack_digest, 65u);
    memcpy(result.lexical_plan_digest, lexical_plan->plan_digest, 65u);
    memcpy(result.cursor_program_digest, program->program_digest, 65u);
    memcpy(result.compiler_answer_digest, compiler_answer_digest, 65u);
    if (!ppof_v1_compute_plan_digest(&result, result.plan_digest) ||
        !ppoccurrence_fold_v1_plan_validate(
            pack, lexical_plan, program, &result,
            error_buf, error_buf_size)) {
        goto done;
    }
    ppoccurrence_fold_v1_plan_free(out);
    *out = result;
    memset(&result, 0, sizeof(result));
    ok = true;

done:
    if (!ok && error_buf && error_buf_size > 0u &&
        error_buf[0] == '\0') {
        ppof_v1_set_error(
            error_buf, error_buf_size,
            "failed to build occurrence-fold plan");
    }
    free(transitions.data);
    ppof_v1_name_vec_free(&operations);
    ppof_v1_name_vec_free(&nodes);
    ppof_v1_name_vec_free(&roles);
    ppof_v1_raw_free(
        tokens, token_write, shifts, shift_write,
        productions, production_write);
    ppoccurrence_fold_v1_plan_free(&result);
    return ok;
}

bool ppoccurrence_fold_v1_plan_validate_program(
    const PPGuardedLexCursorV1Program *program,
    const PPOccurrenceFoldV1Plan *plan,
    char *error_buf,
    size_t error_buf_size) {
    uint32_t index;
    char digest[65];

    if (error_buf && error_buf_size > 0u)
        error_buf[0] = '\0';
    if (!program || !plan ||
        !ppguarded_lex_cursor_v1_program_validate(
            program, error_buf, error_buf_size) ||
        !ppof_v1_digest_valid(plan->base_pack_digest) ||
        !ppof_v1_digest_valid(plan->lexical_plan_digest) ||
        !ppof_v1_digest_valid(plan->cursor_program_digest) ||
        !ppof_v1_digest_valid(plan->compiler_answer_digest) ||
        !ppof_v1_digest_valid(plan->plan_digest) ||
        strcmp(plan->base_pack_digest, program->base_pack_digest) != 0 ||
        strcmp(
            plan->lexical_plan_digest,
            program->lexical_plan_digest) != 0 ||
        strcmp(plan->cursor_program_digest, program->program_digest) != 0 ||
        plan->cursor_terminal_len != program->terminal_len ||
        plan->cursor_production_len !=
            program->final_slr.production_len ||
        plan->role_len == 0u || !plan->roles ||
        plan->node_len == 0u || !plan->nodes ||
        plan->operation_len == 0u || !plan->operations ||
        plan->terminal_len == 0u || !plan->terminals ||
        plan->production_len == 0u || !plan->productions ||
        plan->contract_len != plan->production_len ||
        !plan->contracts || !plan->terminal_binding_by_source ||
        !plan->production_binding_by_index ||
        !ppof_v1_names_sorted_unique(plan->roles, plan->role_len) ||
        !ppof_v1_names_sorted_unique(plan->nodes, plan->node_len) ||
        !ppof_v1_names_sorted_unique(
            plan->operations, plan->operation_len)) {
        if (error_buf && error_buf_size > 0u && error_buf[0] == '\0') {
            ppof_v1_set_error(
                error_buf, error_buf_size,
                "invalid occurrence-fold plan header or identity");
        }
        return false;
    }
    for (index = 0u; index < plan->cursor_terminal_len; index++) {
        uint32_t binding_index = plan->terminal_binding_by_source[index];
        if (binding_index == UINT32_MAX)
            continue;
        if (binding_index >= plan->terminal_len ||
            plan->terminals[binding_index].source_index != index) {
            ppof_v1_set_error(
                error_buf, error_buf_size,
                "occurrence-fold terminal reverse index is invalid");
            return false;
        }
    }
    for (index = 0u; index < plan->terminal_len; index++) {
        const PPOccurrenceFoldV1TerminalBinding *binding =
            &plan->terminals[index];
        if (binding->source_index >= plan->cursor_terminal_len ||
            binding->role_id >= plan->role_len ||
            (binding->shift_operation_id != UINT32_MAX &&
             binding->shift_operation_id >= plan->operation_len) ||
            (binding->value_production_label != UINT32_MAX &&
             (binding->shift_operation_id != UINT32_MAX ||
              !ppof_v1_value_program_has_production(
                  program, binding->source_index,
                  binding->value_production_label))) ||
            plan->terminal_binding_by_source[binding->source_index] !=
                index ||
            (index > 0u &&
             plan->terminals[index - 1u].source_index >=
                 binding->source_index)) {
            ppof_v1_set_error(
                error_buf, error_buf_size,
                "occurrence-fold terminal binding is invalid");
            return false;
        }
    }
    for (index = 0u; index < plan->cursor_production_len; index++) {
        uint32_t binding_index =
            plan->production_binding_by_index[index];
        if (binding_index == UINT32_MAX)
            continue;
        if (binding_index >= plan->production_len ||
            plan->productions[binding_index].production_index != index) {
            ppof_v1_set_error(
                error_buf, error_buf_size,
                "occurrence-fold production reverse index is invalid");
            return false;
        }
    }
    for (index = 0u; index < plan->production_len; index++) {
        const PPOccurrenceFoldV1ProductionBinding *binding =
            &plan->productions[index];
        const CettaLpNativeSlrProgramProduction *production;
        if (binding->production_index >= plan->cursor_production_len ||
            binding->node_id >= plan->node_len ||
            binding->operation_id >= plan->operation_len ||
            binding->contract_index >= plan->contract_len ||
            plan->production_binding_by_index[
                binding->production_index] != index ||
            (index > 0u &&
             plan->productions[index - 1u].production_index >=
                 binding->production_index)) {
            ppof_v1_set_error(
                error_buf, error_buf_size,
                "occurrence-fold production binding is invalid");
            return false;
        }
        production = &program->final_slr.productions[
            binding->production_index];
        if (!production->authored ||
            production->label != binding->production_label) {
            ppof_v1_set_error(
                error_buf, error_buf_size,
                "occurrence-fold production identity changed");
            return false;
        }
    }
    for (index = 0u; index < plan->contract_len; index++) {
        const PPOccurrenceFoldV1Contract *contract =
            &plan->contracts[index];
        uint32_t transition_index;
        if (contract->state_len == 0u ||
            contract->start_state >= contract->state_len ||
            contract->accept_state >= contract->state_len ||
            contract->transition_begin > plan->transition_len ||
            contract->transition_len >
                plan->transition_len - contract->transition_begin) {
            ppof_v1_set_error(
                error_buf, error_buf_size,
                "occurrence-fold contract header is invalid");
            return false;
        }
        for (transition_index = 0u;
             transition_index < contract->transition_len;
             transition_index++) {
            const PPOccurrenceFoldV1Transition *transition =
                &plan->transitions[
                    contract->transition_begin + transition_index];
            if (transition->source >= contract->state_len ||
                transition->target >= contract->state_len ||
                (transition->kind ==
                     PPOCCURRENCE_FOLD_V1_TRANSITION_ROLE &&
                 transition->role_id >= plan->role_len) ||
                (transition->kind ==
                     PPOCCURRENCE_FOLD_V1_TRANSITION_EPSILON &&
                 transition->role_id != UINT32_MAX) ||
                (transition->kind !=
                     PPOCCURRENCE_FOLD_V1_TRANSITION_ROLE &&
                 transition->kind !=
                     PPOCCURRENCE_FOLD_V1_TRANSITION_EPSILON)) {
                ppof_v1_set_error(
                    error_buf, error_buf_size,
                    "occurrence-fold contract transition is invalid");
                return false;
            }
        }
    }
    if (!ppof_v1_compute_plan_digest(plan, digest) ||
        strcmp(digest, plan->plan_digest) != 0) {
        ppof_v1_set_error(
            error_buf, error_buf_size,
            "occurrence-fold plan digest changed");
        return false;
    }
    return true;
}

bool ppoccurrence_fold_v1_plan_validate(
    const PPABIV1Pack *pack,
    const PPLexV1Plan *lexical_plan,
    const PPGuardedLexCursorV1Program *program,
    const PPOccurrenceFoldV1Plan *plan,
    char *error_buf,
    size_t error_buf_size) {
    uint32_t index;

    if (error_buf && error_buf_size > 0u)
        error_buf[0] = '\0';
    if (!pack || !lexical_plan || !program || !plan ||
        strcmp(plan->base_pack_digest, pack->pack_digest) != 0 ||
        strcmp(plan->lexical_plan_digest, lexical_plan->plan_digest) != 0 ||
        !ppoccurrence_fold_v1_plan_validate_program(
            program, plan, error_buf, error_buf_size)) {
        if (error_buf && error_buf_size > 0u && error_buf[0] == '\0') {
            ppof_v1_set_error(
                error_buf, error_buf_size,
                "occurrence-fold source identities changed");
        }
        return false;
    }
    for (index = 0u; index < plan->production_len; index++) {
        if (plan->productions[index].production_label >=
                pack->production_len) {
            ppof_v1_set_error(
                error_buf, error_buf_size,
                "occurrence-fold production is outside its source pack");
            return false;
        }
    }
    for (index = 0u; index < plan->terminal_len; index++) {
        if (plan->terminals[index].value_production_label != UINT32_MAX &&
            plan->terminals[index].value_production_label >=
                pack->production_len) {
            ppof_v1_set_error(
                error_buf, error_buf_size,
                "occurrence-fold value production is outside its source "
                "pack");
            return false;
        }
    }
    return true;
}

const char *ppoccurrence_fold_v1_role_name(
    const PPOccurrenceFoldV1Plan *plan,
    uint32_t role_id) {
    return plan && role_id < plan->role_len
        ? plan->roles[role_id] : NULL;
}

const char *ppoccurrence_fold_v1_node_name(
    const PPOccurrenceFoldV1Plan *plan,
    uint32_t node_id) {
    return plan && node_id < plan->node_len
        ? plan->nodes[node_id] : NULL;
}

const char *ppoccurrence_fold_v1_operation_name(
    const PPOccurrenceFoldV1Plan *plan,
    uint32_t operation_id) {
    return plan && operation_id < plan->operation_len
        ? plan->operations[operation_id] : NULL;
}

void ppoccurrence_fold_v1_trace_init(
    PPOccurrenceFoldV1Trace *trace,
    const PPOccurrenceFoldV1Plan *plan) {
    static const char domain[] = "ParserOccurrenceFoldExecutionV1";

    if (!trace)
        return;
    memset(trace, 0, sizeof(*trace));
    trace->plan = plan;
    trace->active = plan != NULL;
    cetta_native_sha256_init(&trace->trace);
    if (!plan)
        return;
    ppof_v1_sha_bytes(
        &trace->trace, (const uint8_t *)domain, sizeof(domain) - 1u);
    ppof_v1_sha_text(&trace->trace, plan->plan_digest);
}

static bool ppof_v1_trace_apply(
    void *context,
    const PPOccurrenceFoldV1Step *step,
    char *error_buf,
    size_t error_buf_size) {
    PPOccurrenceFoldV1Trace *trace = context;
    const char *operation;
    const char *node = "";
    uint32_t index;

    if (!trace || !trace->active || !step ||
        !(operation = ppoccurrence_fold_v1_operation_name(
              trace->plan, step->operation_id)) ||
        (step->node_id != UINT32_MAX &&
         !(node = ppoccurrence_fold_v1_node_name(
               trace->plan, step->node_id)))) {
        ppof_v1_set_error(
            error_buf, error_buf_size,
            "occurrence-fold trace received an invalid step");
        return false;
    }
    ppof_v1_sha_u32(&trace->trace, (uint32_t)step->kind);
    ppof_v1_sha_text(&trace->trace, operation);
    ppof_v1_sha_text(&trace->trace, node);
    ppof_v1_sha_u32(&trace->trace, step->production_label);
    ppof_v1_sha_u32(&trace->trace, step->left_byte);
    ppof_v1_sha_u32(&trace->trace, step->right_byte);
    ppof_v1_sha_u32(&trace->trace, step->value_len);
    for (index = 0u; index < step->value_len; index++) {
        const PPOccurrenceFoldV1Value *value = &step->values[index];
        const char *role = ppoccurrence_fold_v1_role_name(
            trace->plan, value->role_id);
        if (!role) {
            ppof_v1_set_error(
                error_buf, error_buf_size,
                "occurrence-fold trace received an invalid value role");
            return false;
        }
        ppof_v1_sha_text(&trace->trace, role);
        ppof_v1_sha_u32(&trace->trace, value->left_byte);
        ppof_v1_sha_u32(&trace->trace, value->right_byte);
        ppof_v1_sha_bytes(
            &trace->trace, value->bytes, value->byte_len);
    }
    trace->step_len++;
    trace->value_len += step->value_len;
    return true;
}

static bool ppof_v1_trace_commit(
    void *context,
    char *error_buf,
    size_t error_buf_size) {
    PPOccurrenceFoldV1Trace *trace = context;

    if (!trace || !trace->active || trace->aborted) {
        ppof_v1_set_error(
            error_buf, error_buf_size,
            "occurrence-fold trace cannot commit");
        return false;
    }
    cetta_native_sha256_finish_hex(&trace->trace, trace->digest);
    trace->active = false;
    trace->committed = true;
    return true;
}

static void ppof_v1_trace_abort(void *context) {
    PPOccurrenceFoldV1Trace *trace = context;

    if (!trace || !trace->active)
        return;
    trace->active = false;
    trace->aborted = true;
}

PPOccurrenceFoldV1Backend ppoccurrence_fold_v1_trace_backend(
    PPOccurrenceFoldV1Trace *trace) {
    return (PPOccurrenceFoldV1Backend){
        .context = trace,
        .apply = ppof_v1_trace_apply,
        .commit = ppof_v1_trace_commit,
        .abort = ppof_v1_trace_abort,
    };
}

static bool ppof_v1_answer_stream_hex(
    FILE *output, const uint8_t *bytes, size_t byte_len) {
    static const char digits[] = "0123456789abcdef";
    size_t index;

    if (!output || (!bytes && byte_len > 0u) || fputs("hex-", output) < 0)
        return false;
    for (index = 0u; index < byte_len; index++) {
        if (fputc(digits[bytes[index] >> 4u], output) == EOF ||
            fputc(digits[bytes[index] & 0x0fu], output) == EOF) {
            return false;
        }
    }
    return true;
}

typedef struct {
    uint8_t *bytes;
    size_t byte_len;
    uint64_t hash;
    uint32_t id;
    bool occupied;
} PPOccurrenceFoldV1AnswerValue;

typedef struct {
    PPOccurrenceFoldV1AnswerValue *slots;
    size_t slot_len;
    size_t entry_len;
} PPOccurrenceFoldV1AnswerStreamImpl;

static uint64_t ppof_v1_answer_value_hash(
    const uint8_t *bytes, size_t byte_len) {
    uint64_t hash = UINT64_C(14695981039346656037);
    size_t index;

    for (index = 0u; index < byte_len; index++) {
        hash ^= bytes[index];
        hash *= UINT64_C(1099511628211);
    }
    hash ^= (uint64_t)byte_len;
    hash *= UINT64_C(1099511628211);
    return hash;
}

static bool ppof_v1_answer_value_insert(
    PPOccurrenceFoldV1AnswerValue *slots,
    size_t slot_len,
    PPOccurrenceFoldV1AnswerValue value) {
    size_t index;
    size_t mask;

    if (!slots || slot_len == 0u || (slot_len & (slot_len - 1u)) != 0u)
        return false;
    mask = slot_len - 1u;
    index = (size_t)value.hash & mask;
    while (slots[index].occupied)
        index = (index + 1u) & mask;
    slots[index] = value;
    return true;
}

static bool ppof_v1_answer_value_grow(
    PPOccurrenceFoldV1AnswerStreamImpl *implementation,
    char *error_buf,
    size_t error_buf_size) {
    PPOccurrenceFoldV1AnswerValue *next;
    size_t next_len;
    size_t index;

    if (!implementation)
        return false;
    next_len = implementation->slot_len
        ? implementation->slot_len * 2u : 64u;
    if (next_len < implementation->slot_len ||
        next_len > SIZE_MAX / sizeof(*next)) {
        ppof_v1_set_error(
            error_buf, error_buf_size,
            "occurrence answer value interner is too large");
        return false;
    }
    next = calloc(next_len, sizeof(*next));
    if (!next) {
        ppof_v1_set_error(
            error_buf, error_buf_size,
            "cannot grow occurrence answer value interner");
        return false;
    }
    for (index = 0u; index < implementation->slot_len; index++) {
        if (implementation->slots[index].occupied &&
            !ppof_v1_answer_value_insert(
                next, next_len, implementation->slots[index])) {
            free(next);
            return false;
        }
    }
    free(implementation->slots);
    implementation->slots = next;
    implementation->slot_len = next_len;
    return true;
}

static size_t ppof_v1_answer_value_grow_at(size_t slot_len) {
    size_t quotient = slot_len / 10u;
    size_t remainder = slot_len % 10u;

    /* ceil(slot_len * 7 / 10), without overflowing size_t */
    return quotient * 7u + (remainder * 7u + 9u) / 10u;
}

static bool ppof_v1_answer_value_intern(
    PPOccurrenceFoldV1AnswerStream *stream,
    const uint8_t *bytes,
    size_t byte_len,
    uint32_t *id_out,
    char *error_buf,
    size_t error_buf_size) {
    PPOccurrenceFoldV1AnswerStreamImpl *implementation;
    uint64_t hash;
    size_t mask;
    size_t index;
    uint8_t *owned = NULL;

    if (!stream || !(implementation = stream->implementation) ||
        (!bytes && byte_len > 0u) || !id_out) {
        return false;
    }
    if (implementation->entry_len >= UINT32_MAX) {
        ppof_v1_set_error(
            error_buf, error_buf_size,
            "occurrence answer value inventory is too large");
        return false;
    }
    if (implementation->slot_len == 0u ||
        implementation->entry_len + 1u >=
            ppof_v1_answer_value_grow_at(implementation->slot_len)) {
        if (!ppof_v1_answer_value_grow(
                implementation, error_buf, error_buf_size)) {
            return false;
        }
    }
    hash = ppof_v1_answer_value_hash(bytes, byte_len);
    mask = implementation->slot_len - 1u;
    index = (size_t)hash & mask;
    while (implementation->slots[index].occupied) {
        const PPOccurrenceFoldV1AnswerValue *entry =
            &implementation->slots[index];
        if (entry->hash == hash && entry->byte_len == byte_len &&
            (byte_len == 0u ||
             memcmp(entry->bytes, bytes, byte_len) == 0)) {
            *id_out = entry->id;
            return true;
        }
        index = (index + 1u) & mask;
    }
    if (byte_len > 0u) {
        owned = malloc(byte_len);
        if (!owned) {
            ppof_v1_set_error(
                error_buf, error_buf_size,
                "cannot retain occurrence answer value bytes");
            return false;
        }
        memcpy(owned, bytes, byte_len);
    }
    implementation->slots[index] = (PPOccurrenceFoldV1AnswerValue){
        .bytes = owned,
        .byte_len = byte_len,
        .hash = hash,
        .id = (uint32_t)implementation->entry_len,
        .occupied = true,
    };
    *id_out = (uint32_t)implementation->entry_len;
    implementation->entry_len++;
    stream->unique_value_len = (uint32_t)implementation->entry_len;
    return true;
}

static bool ppof_v1_answer_stream_apply(
    void *context,
    const PPOccurrenceFoldV1Step *step,
    char *error_buf,
    size_t error_buf_size) {
    PPOccurrenceFoldV1AnswerStream *stream = context;
    const char *operation;
    const char *node = "ParserOccurrenceNoNodeV1";
    const char *kind;
    uint32_t index;

    if (!stream || !stream->active || stream->committed ||
        stream->aborted || !stream->staging || !step ||
        !(operation = ppoccurrence_fold_v1_operation_name(
              stream->plan, step->operation_id)) ||
        (step->node_id != UINT32_MAX &&
         !(node = ppoccurrence_fold_v1_node_name(
               stream->plan, step->node_id)))) {
        ppof_v1_set_error(
            error_buf, error_buf_size,
            "occurrence answer stream received an invalid step");
        return false;
    }
    if (step->kind == PPOCCURRENCE_FOLD_V1_STEP_SHIFT) {
        kind = "ParserOccurrenceShiftV1";
    } else if (step->kind == PPOCCURRENCE_FOLD_V1_STEP_REDUCE) {
        kind = "ParserOccurrenceReduceV1";
    } else {
        ppof_v1_set_error(
            error_buf, error_buf_size,
            "occurrence answer stream received an unknown step kind");
        return false;
    }
    if (stream->source_graph) {
        if (fprintf(
                stream->staging,
                "(ParserOccurrenceV4 %" PRIu32 " %" PRIu32
                " %s %s %s ",
                stream->step_len, step->source_id,
                kind, operation, node) < 0) {
            goto write_failed;
        }
        if (step->source_id > stream->maximum_source_id)
            stream->maximum_source_id = step->source_id;
    } else if (fprintf(
                   stream->staging,
                   "(ParserOccurrenceV3 %" PRIu32 " %s %s %s ",
                   stream->step_len, kind, operation, node) < 0) {
        goto write_failed;
    }
    if (step->production_label == UINT32_MAX) {
        if (fputs("ParserOccurrenceNoProductionV1 ", stream->staging) < 0)
            goto write_failed;
    } else if (fprintf(
                   stream->staging, "%" PRIu32 " ",
                   step->production_label) < 0) {
        goto write_failed;
    }
    if (fprintf(
            stream->staging,
            "%" PRIu32 " %" PRIu32 " %" PRIu32 " %" PRIu32 " ",
            step->left_scalar, step->right_scalar,
            step->left_byte, step->right_byte) < 0) {
        goto write_failed;
    }
    for (index = 0u; index < step->value_len; index++) {
        const PPOccurrenceFoldV1Value *value = &step->values[index];
        const char *role = ppoccurrence_fold_v1_role_name(
            stream->plan, value->role_id);
        uint32_t value_id;
        if (!role || !ppof_v1_answer_value_intern(
                stream, value->bytes, value->byte_len, &value_id,
                error_buf, error_buf_size) ||
            fprintf(
                stream->staging,
                "(ParserOccurrenceValuesConsV1 "
                "(ParserOccurrenceValueV1 %s %" PRIu32 " %" PRIu32
                " %" PRIu32 " %" PRIu32 " %" PRIu32 " %" PRIu32
                " (SourceBytesHexV1 ",
                role, value_id, value->source_index,
                value->left_scalar, value->right_scalar,
                value->left_byte, value->right_byte) < 0 ||
            !ppof_v1_answer_stream_hex(
                stream->staging, value->bytes, value->byte_len) ||
            fputs(")) ", stream->staging) < 0) {
            goto write_failed;
        }
    }
    if (fputs("ParserOccurrenceValuesNilV1", stream->staging) < 0)
        goto write_failed;
    for (index = 0u; index < step->value_len; index++) {
        if (fputc(')', stream->staging) == EOF)
            goto write_failed;
    }
    if (fputc(' ', stream->staging) == EOF)
        goto write_failed;
    {
        uint32_t empty_role_len = 0u;
        if (step->kind == PPOCCURRENCE_FOLD_V1_STEP_REDUCE) {
            uint32_t binding_index;
            const PPOccurrenceFoldV1ProductionBinding *binding;
            const PPOccurrenceFoldV1Contract *contract;
            uint32_t role_id;

            if (step->production_index >=
                    stream->plan->cursor_production_len ||
                (binding_index =
                     stream->plan->production_binding_by_index[
                         step->production_index]) == UINT32_MAX ||
                binding_index >= stream->plan->production_len) {
                ppof_v1_set_error(
                    error_buf, error_buf_size,
                    "occurrence answer stream cannot recover a fold contract");
                return false;
            }
            binding = &stream->plan->productions[binding_index];
            if (binding->contract_index >= stream->plan->contract_len) {
                ppof_v1_set_error(
                    error_buf, error_buf_size,
                    "occurrence answer stream fold contract is invalid");
                return false;
            }
            contract = &stream->plan->contracts[binding->contract_index];
            for (role_id = 0u; role_id < stream->plan->role_len; role_id++) {
                bool admitted = false;
                bool present = false;
                uint32_t transition_index;
                uint32_t value_index;

                for (transition_index = 0u;
                     transition_index < contract->transition_len;
                     transition_index++) {
                    const PPOccurrenceFoldV1Transition *transition =
                        &stream->plan->transitions[
                            contract->transition_begin + transition_index];
                    if (transition->kind ==
                            PPOCCURRENCE_FOLD_V1_TRANSITION_ROLE &&
                        transition->role_id == role_id) {
                        admitted = true;
                        break;
                    }
                }
                for (value_index = 0u;
                     admitted && value_index < step->value_len;
                     value_index++) {
                    if (step->values[value_index].role_id == role_id) {
                        present = true;
                        break;
                    }
                }
                if (!admitted || present)
                    continue;
                if (fprintf(
                        stream->staging,
                        "(ParserOccurrenceEmptyRolesConsV1 %s ",
                        stream->plan->roles[role_id]) < 0) {
                    goto write_failed;
                }
                empty_role_len++;
            }
        }
        if (fputs(
                "ParserOccurrenceEmptyRolesNilV1",
                stream->staging) < 0) {
            goto write_failed;
        }
        for (index = 0u; index < empty_role_len; index++) {
            if (fputc(')', stream->staging) == EOF)
                goto write_failed;
        }
    }
    if (fputs(")\n", stream->staging) < 0)
        goto write_failed;
    if (stream->step_len == UINT32_MAX ||
        UINT32_MAX - stream->value_len < step->value_len) {
        ppof_v1_set_error(
            error_buf, error_buf_size,
            "occurrence answer stream is too large");
        return false;
    }
    stream->step_len++;
    stream->value_len += step->value_len;
    return true;

write_failed:
    ppof_v1_set_error(
        error_buf, error_buf_size,
        "cannot stage occurrence answer stream");
    return false;
}

static bool ppof_v1_answer_stream_commit(
    void *context,
    char *error_buf,
    size_t error_buf_size) {
    PPOccurrenceFoldV1AnswerStream *stream = context;

    if (!stream || !stream->active || stream->aborted ||
        !stream->staging || fflush(stream->staging) != 0) {
        ppof_v1_set_error(
            error_buf, error_buf_size,
            "occurrence answer stream cannot commit");
        return false;
    }
    stream->active = false;
    stream->committed = true;
    return true;
}

static void ppof_v1_answer_stream_abort(void *context) {
    PPOccurrenceFoldV1AnswerStream *stream = context;

    if (!stream || !stream->active)
        return;
    stream->active = false;
    stream->aborted = true;
}

bool ppoccurrence_fold_v1_answer_stream_init(
    PPOccurrenceFoldV1AnswerStream *stream,
    const PPOccurrenceFoldV1Plan *plan,
    const uint8_t *source_bytes,
    size_t source_byte_len,
    char *error_buf,
    size_t error_buf_size) {
    CettaNativeSha256 source_hash;
    PPOccurrenceFoldV1AnswerStreamImpl *implementation;

    if (error_buf && error_buf_size > 0u)
        error_buf[0] = '\0';
    if (!stream || !plan || (!source_bytes && source_byte_len > 0u)) {
        ppof_v1_set_error(
            error_buf, error_buf_size,
            "bad occurrence answer stream arguments");
        return false;
    }
    memset(stream, 0, sizeof(*stream));
    implementation = calloc(1u, sizeof(*implementation));
    if (!implementation) {
        ppof_v1_set_error(
            error_buf, error_buf_size,
            "cannot allocate occurrence answer value interner");
        return false;
    }
    stream->staging = tmpfile();
    if (!stream->staging) {
        free(implementation);
        ppof_v1_set_error(
            error_buf, error_buf_size,
            "cannot allocate occurrence answer staging stream");
        return false;
    }
    cetta_native_sha256_init(&source_hash);
    cetta_native_sha256_update(
        &source_hash, source_bytes, source_byte_len);
    cetta_native_sha256_finish_hex(
        &source_hash, stream->source_digest);
    stream->plan = plan;
    stream->implementation = implementation;
    stream->active = true;
    return true;
}

bool ppoccurrence_fold_v1_answer_stream_init_source_graph(
    PPOccurrenceFoldV1AnswerStream *stream,
    const PPOccurrenceFoldV1Plan *plan,
    const uint8_t *root_source_bytes,
    size_t root_source_byte_len,
    char *error_buf,
    size_t error_buf_size) {
    if (!ppoccurrence_fold_v1_answer_stream_init(
            stream, plan, root_source_bytes, root_source_byte_len,
            error_buf, error_buf_size)) {
        return false;
    }
    stream->source_graph = true;
    return true;
}

static bool ppof_v1_sha256_hex_valid(const char digest[65]) {
    size_t index;

    if (!digest || digest[64] != '\0')
        return false;
    for (index = 0u; index < 64u; index++) {
        if (!((digest[index] >= '0' && digest[index] <= '9') ||
              (digest[index] >= 'a' && digest[index] <= 'f'))) {
            return false;
        }
    }
    return true;
}

bool ppoccurrence_fold_v1_answer_stream_bind_source_graph(
    PPOccurrenceFoldV1AnswerStream *stream,
    const char source_program_digest[65],
    const char source_dag_digest[65],
    uint32_t source_len,
    char *error_buf,
    size_t error_buf_size) {
    if (error_buf && error_buf_size > 0u)
        error_buf[0] = '\0';
    if (!stream || !stream->source_graph || !stream->committed ||
        stream->active || stream->aborted ||
        stream->source_graph_identity_ready || source_len == 0u ||
        (stream->step_len > 0u &&
         stream->maximum_source_id >= source_len) ||
        !ppof_v1_sha256_hex_valid(source_program_digest) ||
        !ppof_v1_sha256_hex_valid(source_dag_digest)) {
        ppof_v1_set_error(
            error_buf, error_buf_size,
            "invalid source-graph identity for occurrence answers");
        return false;
    }
    memcpy(stream->source_program_digest, source_program_digest, 65u);
    memcpy(stream->source_digest, source_dag_digest, 65u);
    stream->source_len = source_len;
    stream->source_graph_identity_ready = true;
    return true;
}

void ppoccurrence_fold_v1_answer_stream_free(
    PPOccurrenceFoldV1AnswerStream *stream) {
    if (!stream)
        return;
    if (stream->staging)
        (void)fclose(stream->staging);
    if (stream->implementation) {
        PPOccurrenceFoldV1AnswerStreamImpl *implementation =
            stream->implementation;
        size_t index;
        for (index = 0u; index < implementation->slot_len; index++) {
            if (implementation->slots[index].occupied)
                free(implementation->slots[index].bytes);
        }
        free(implementation->slots);
        free(implementation);
    }
    memset(stream, 0, sizeof(*stream));
}

PPOccurrenceFoldV1Backend ppoccurrence_fold_v1_answer_stream_backend(
    PPOccurrenceFoldV1AnswerStream *stream) {
    return (PPOccurrenceFoldV1Backend){
        .context = stream,
        .apply = ppof_v1_answer_stream_apply,
        .commit = ppof_v1_answer_stream_commit,
        .abort = ppof_v1_answer_stream_abort,
    };
}

static int ppof_v1_answer_line_compare(
    const void *left,
    const void *right) {
    const char *const *left_line = left;
    const char *const *right_line = right;

    return strcmp(*left_line, *right_line);
}

bool ppoccurrence_fold_v1_answer_stream_write(
    PPOccurrenceFoldV1AnswerStream *stream,
    FILE *output,
    char *error_buf,
    size_t error_buf_size) {
    char *records = NULL;
    char **lines = NULL;
    long staged_size_long;
    size_t staged_size;
    size_t line_count = 0u;
    size_t offset = 0u;
    size_t index;
    bool ok = false;

    if (error_buf && error_buf_size > 0u)
        error_buf[0] = '\0';
    if (!stream || !stream->committed || stream->active ||
        stream->aborted || !stream->plan || !stream->staging || !output) {
        ppof_v1_set_error(
            error_buf, error_buf_size,
            "occurrence answer stream is not publishable");
        return false;
    }
    if (stream->source_graph && !stream->source_graph_identity_ready) {
        ppof_v1_set_error(
            error_buf, error_buf_size,
            "occurrence source graph has no final identity");
        return false;
    }

    /* The consumer's finite-answer format is a canonical set: records must
     * be bytewise ordered even though the fold produces them chronologically.
     * Occurrence indices and Next facts retain the authored source order. */
    if (fseek(stream->staging, 0L, SEEK_END) != 0 ||
        (staged_size_long = ftell(stream->staging)) < 0 ||
        (uintmax_t)staged_size_long > (uintmax_t)(SIZE_MAX - 1u) ||
        fseek(stream->staging, 0L, SEEK_SET) != 0) {
        ppof_v1_set_error(
            error_buf, error_buf_size,
            "cannot size occurrence answer staging stream");
        goto done;
    }
    staged_size = (size_t)staged_size_long;
    records = malloc(staged_size + 1u);
    lines = malloc(
        (stream->step_len ? (size_t)stream->step_len : 1u) *
        sizeof(*lines));
    if (!records || !lines) {
        ppof_v1_set_error(
            error_buf, error_buf_size,
            "cannot allocate canonical occurrence answer stream");
        goto done;
    }
    if (staged_size > 0u &&
        fread(records, 1u, staged_size, stream->staging) != staged_size) {
        ppof_v1_set_error(
            error_buf, error_buf_size,
            "cannot read occurrence answer staging stream");
        goto done;
    }
    if (ferror(stream->staging)) {
        ppof_v1_set_error(
            error_buf, error_buf_size,
            "failed while reading occurrence answer staging stream");
        goto done;
    }
    records[staged_size] = '\0';
    while (offset < staged_size) {
        char *line = records + offset;
        char *newline = memchr(line, '\n', staged_size - offset);
        size_t line_len;

        if (!newline || line_count >= stream->step_len) {
            ppof_v1_set_error(
                error_buf, error_buf_size,
                "occurrence answer staging framing is invalid");
            goto done;
        }
        line_len = (size_t)(newline - line);
        if (line_len == 0u || memchr(line, '\0', line_len) != NULL) {
            ppof_v1_set_error(
                error_buf, error_buf_size,
                "occurrence answer staging record is invalid");
            goto done;
        }
        *newline = '\0';
        lines[line_count++] = line;
        offset += line_len + 1u;
    }
    if (line_count != stream->step_len) {
        ppof_v1_set_error(
            error_buf, error_buf_size,
            "occurrence answer staging count is inconsistent");
        goto done;
    }
    if (line_count > 1u) {
        qsort(
            lines, line_count, sizeof(*lines),
            ppof_v1_answer_line_compare);
    }
    for (index = 1u; index < line_count; index++) {
        if (strcmp(lines[index - 1u], lines[index]) >= 0) {
            ppof_v1_set_error(
                error_buf, error_buf_size,
                "occurrence answer records are not unique");
            goto done;
        }
    }
    if (stream->source_graph) {
        if (fprintf(
                output,
                "(ParserOccurrenceStreamV4 sha256-%s sha256-%s sha256-%s %" PRIu32
                " %" PRIu32 " %" PRIu32 " %" PRIu32 ")\n",
                stream->plan->plan_digest, stream->source_program_digest,
                stream->source_digest,
                stream->step_len, stream->value_len,
                stream->unique_value_len, stream->source_len) < 0) {
            ppof_v1_set_error(
                error_buf, error_buf_size,
                "cannot publish occurrence source-graph header");
            goto done;
        }
    } else if (fprintf(
                   output,
                   "(ParserOccurrenceStreamV3 sha256-%s sha256-%s %" PRIu32
                   " %" PRIu32 " %" PRIu32 ")\n",
                   stream->plan->plan_digest, stream->source_digest,
                   stream->step_len, stream->value_len,
                   stream->unique_value_len) < 0) {
        ppof_v1_set_error(
            error_buf, error_buf_size,
            "cannot publish occurrence answer stream header");
        goto done;
    }
    for (index = 0u; index < line_count; index++) {
        if (fputs(lines[index], output) < 0 || fputc('\n', output) == EOF) {
            ppof_v1_set_error(
                error_buf, error_buf_size,
                "cannot publish occurrence answer stream record");
            goto done;
        }
    }
    if (fflush(output) != 0) {
        ppof_v1_set_error(
            error_buf, error_buf_size,
            "cannot finish occurrence answer stream publication");
        goto done;
    }
    ok = true;

done:
    free(lines);
    free(records);
    return ok;
}

static void ppof_v1_abort(PPOccurrenceFoldV1Run *run) {
    if (!run || run->aborted || run->committed)
        return;
    run->backend.abort(run->backend.context);
    run->aborted = true;
}

static bool ppof_v1_pending_push(PPOccurrenceFoldV1Run *run,
                                 PPOccurrenceFoldV1Value value) {
    PPOccurrenceFoldV1Value *next;
    uint32_t cap;

    if (run->pending_len >= PPOCCURRENCE_FOLD_V1_MAX_PENDING_VALUES)
        return false;
    if (run->pending_len == run->pending_cap) {
        cap = run->pending_cap ? run->pending_cap * 2u : 16u;
        if (cap < run->pending_cap ||
            cap > PPOCCURRENCE_FOLD_V1_MAX_PENDING_VALUES) {
            cap = PPOCCURRENCE_FOLD_V1_MAX_PENDING_VALUES;
        }
        next = realloc(run->pending, sizeof(*next) * cap);
        if (!next)
            return false;
        run->pending = next;
        run->pending_cap = cap;
    }
    run->pending[run->pending_len++] = value;
    if (run->pending_len > run->receipt.max_pending_value_len)
        run->receipt.max_pending_value_len = run->pending_len;
    return true;
}

static bool ppof_v1_epsilon_closure(
    const PPOccurrenceFoldV1Plan *plan,
    const PPOccurrenceFoldV1Contract *contract,
    bool *states) {
    bool changed = true;

    while (changed) {
        uint32_t index;
        changed = false;
        for (index = 0u; index < contract->transition_len; index++) {
            const PPOccurrenceFoldV1Transition *transition =
                &plan->transitions[
                    contract->transition_begin + index];
            if (transition->kind !=
                    PPOCCURRENCE_FOLD_V1_TRANSITION_EPSILON ||
                !states[transition->source] ||
                states[transition->target]) {
                continue;
            }
            states[transition->target] = true;
            changed = true;
        }
    }
    return true;
}

static bool ppof_v1_contract_matches(
    const PPOccurrenceFoldV1Plan *plan,
    const PPOccurrenceFoldV1Contract *contract,
    const PPOccurrenceFoldV1Value *values,
    uint32_t value_len) {
    bool *active = NULL;
    bool *next = NULL;
    uint32_t value_index;
    bool accepted = false;

    active = calloc(contract->state_len, sizeof(*active));
    next = calloc(contract->state_len, sizeof(*next));
    if (!active || !next)
        goto done;
    active[contract->start_state] = true;
    (void)ppof_v1_epsilon_closure(plan, contract, active);
    for (value_index = 0u; value_index < value_len; value_index++) {
        uint32_t transition_index;
        memset(next, 0, sizeof(*next) * contract->state_len);
        for (transition_index = 0u;
             transition_index < contract->transition_len;
             transition_index++) {
            const PPOccurrenceFoldV1Transition *transition =
                &plan->transitions[
                    contract->transition_begin + transition_index];
            if (transition->kind ==
                    PPOCCURRENCE_FOLD_V1_TRANSITION_ROLE &&
                transition->role_id == values[value_index].role_id &&
                active[transition->source]) {
                next[transition->target] = true;
            }
        }
        (void)ppof_v1_epsilon_closure(plan, contract, next);
        {
            bool *swap = active;
            active = next;
            next = swap;
        }
    }
    accepted = active[contract->accept_state];

done:
    free(next);
    free(active);
    return accepted;
}

static bool ppof_v1_spans_overlap(
    const PPOccurrenceFoldV1Value *value,
    const PPGuardedLexCursorV1Event *event) {
    return value->left_byte < event->right_byte &&
        event->left_byte < value->right_byte;
}

static bool ppof_v1_value_inside(
    const PPOccurrenceFoldV1Value *value,
    const PPGuardedLexCursorV1Event *event) {
    return value->left_byte >= event->left_byte &&
        value->right_byte <= event->right_byte;
}

static bool ppof_v1_select_values(
    PPOccurrenceFoldV1Run *run,
    const PPGuardedLexCursorV1Event *event,
    uint32_t *out_len,
    char *error_buf,
    size_t error_buf_size) {
    uint32_t index;
    uint32_t selected_len = 0u;

    if (run->pending_len > run->selected_cap) {
        PPOccurrenceFoldV1Value *next = realloc(
            run->selected,
            sizeof(*next) * (size_t)run->pending_len);
        if (!next)
            return false;
        run->selected = next;
        run->selected_cap = run->pending_len;
    }
    for (index = 0u; index < run->pending_len; index++) {
        const PPOccurrenceFoldV1Value *value = &run->pending[index];
        bool inside = ppof_v1_value_inside(value, event);
        if (!inside && ppof_v1_spans_overlap(value, event)) {
            ppof_v1_set_error(
                error_buf, error_buf_size,
                "fold reduction cuts through a token occurrence");
            return false;
        }
        if (inside)
            run->selected[selected_len++] = *value;
    }
    *out_len = selected_len;
    return true;
}

static void ppof_v1_consume_values(
    PPOccurrenceFoldV1Run *run,
    const PPGuardedLexCursorV1Event *event) {
    uint32_t read;
    uint32_t write = 0u;

    for (read = 0u; read < run->pending_len; read++) {
        if (!ppof_v1_value_inside(&run->pending[read], event))
            run->pending[write++] = run->pending[read];
    }
    run->pending_len = write;
}

static bool ppof_v1_emit(void *context,
                         const PPGuardedLexCursorV1Event *event,
                         char *error_buf,
                         size_t error_buf_size) {
    PPOccurrenceFoldV1Run *run = context;

    if (!run || !event || run->saw_accept ||
        event->right_byte > run->input_len) {
        ppof_v1_set_error(
            error_buf, error_buf_size,
            "invalid occurrence-fold event sequence");
        if (run)
            ppof_v1_abort(run);
        return false;
    }
    if (event->kind ==
            PPGUARDED_LEX_CURSOR_V1_EVENT_SEMANTIC_REDUCE) {
        uint32_t binding_index;
        const PPOccurrenceFoldV1TerminalBinding *binding;
        PPOccurrenceFoldV1Value value;

        if (event->source_index >= run->plan->cursor_terminal_len ||
            event->production_index != UINT32_MAX ||
            !event->authored) {
            goto malformed;
        }
        binding_index =
            run->plan->terminal_binding_by_source[event->source_index];
        if (binding_index == UINT32_MAX)
            return true;
        binding = &run->plan->terminals[binding_index];
        if (binding->value_production_label == UINT32_MAX ||
            event->production_label !=
                binding->value_production_label) {
            return true;
        }
        value = (PPOccurrenceFoldV1Value){
            .role_id = binding->role_id,
            .source_index = event->source_index,
            .left_scalar = event->left_scalar,
            .right_scalar = event->right_scalar,
            .left_byte = event->left_byte,
            .right_byte = event->right_byte,
            .bytes = run->input + event->left_byte,
            .byte_len =
                (size_t)(event->right_byte - event->left_byte),
        };
        if (!ppof_v1_pending_push(run, value)) {
            ppof_v1_set_error(
                error_buf, error_buf_size,
                "occurrence-fold pending value limit exceeded");
            ppof_v1_abort(run);
            return false;
        }
        return true;
    }
    if (event->kind == PPGUARDED_LEX_CURSOR_V1_EVENT_SHIFT) {
        uint32_t binding_index;
        const PPOccurrenceFoldV1TerminalBinding *binding;
        PPOccurrenceFoldV1Value value;

        if (event->source_index >= run->plan->cursor_terminal_len)
            goto malformed;
        binding_index =
            run->plan->terminal_binding_by_source[event->source_index];
        if (binding_index == UINT32_MAX)
            return true;
        binding = &run->plan->terminals[binding_index];
        if (binding->value_production_label != UINT32_MAX)
            return true;
        value = (PPOccurrenceFoldV1Value){
            .role_id = binding->role_id,
            .source_index = event->source_index,
            .left_scalar = event->left_scalar,
            .right_scalar = event->right_scalar,
            .left_byte = event->left_byte,
            .right_byte = event->right_byte,
            .bytes = run->input + event->left_byte,
            .byte_len = (size_t)(event->right_byte - event->left_byte),
        };
        if (binding->shift_operation_id != UINT32_MAX) {
            PPOccurrenceFoldV1Step step = {
                .kind = PPOCCURRENCE_FOLD_V1_STEP_SHIFT,
                .operation_id = binding->shift_operation_id,
                .node_id = UINT32_MAX,
                .production_index = UINT32_MAX,
                .production_label = UINT32_MAX,
                .left_scalar = event->left_scalar,
                .right_scalar = event->right_scalar,
                .left_byte = event->left_byte,
                .right_byte = event->right_byte,
                .values = &value,
                .value_len = 1u,
            };
            if (!run->backend.apply(
                    run->backend.context, &step,
                    error_buf, error_buf_size)) {
                ppof_v1_abort(run);
                return false;
            }
            run->receipt.step_len++;
            run->receipt.shift_step_len++;
            run->receipt.value_len++;
            return true;
        }
        if (!ppof_v1_pending_push(run, value)) {
            ppof_v1_set_error(
                error_buf, error_buf_size,
                "occurrence-fold pending value limit exceeded");
            ppof_v1_abort(run);
            return false;
        }
        return true;
    }
    if (event->kind == PPGUARDED_LEX_CURSOR_V1_EVENT_REDUCE) {
        uint32_t binding_index;
        const PPOccurrenceFoldV1ProductionBinding *binding;
        const PPOccurrenceFoldV1Contract *contract;
        PPOccurrenceFoldV1Step step;
        uint32_t selected_len;

        if (event->production_index >=
                run->plan->cursor_production_len) {
            goto malformed;
        }
        binding_index = run->plan->production_binding_by_index[
            event->production_index];
        if (binding_index == UINT32_MAX)
            return true;
        binding = &run->plan->productions[binding_index];
        if (!event->authored ||
            event->production_label != binding->production_label) {
            goto malformed;
        }
        if (!ppof_v1_select_values(
                run, event, &selected_len,
                error_buf, error_buf_size)) {
            ppof_v1_abort(run);
            return false;
        }
        contract = &run->plan->contracts[binding->contract_index];
        if (!ppof_v1_contract_matches(
                run->plan, contract, run->selected, selected_len)) {
            ppof_v1_set_error(
                error_buf, error_buf_size,
                "parser occurrence violates its authored fold contract");
            ppof_v1_abort(run);
            return false;
        }
        step = (PPOccurrenceFoldV1Step){
            .kind = PPOCCURRENCE_FOLD_V1_STEP_REDUCE,
            .operation_id = binding->operation_id,
            .node_id = binding->node_id,
            .production_index = binding->production_index,
            .production_label = binding->production_label,
            .left_scalar = event->left_scalar,
            .right_scalar = event->right_scalar,
            .left_byte = event->left_byte,
            .right_byte = event->right_byte,
            .values = run->selected,
            .value_len = selected_len,
        };
        if (!run->backend.apply(
                run->backend.context, &step,
                error_buf, error_buf_size)) {
            ppof_v1_abort(run);
            return false;
        }
        ppof_v1_consume_values(run, event);
        run->receipt.step_len++;
        run->receipt.reduce_step_len++;
        run->receipt.value_len += selected_len;
        return true;
    }
    if (event->kind == PPGUARDED_LEX_CURSOR_V1_EVENT_ACCEPT) {
        if (run->pending_len != 0u ||
            event->left_byte != 0u ||
            event->right_byte != run->input_len) {
            ppof_v1_set_error(
                error_buf, error_buf_size,
                "accepted occurrence fold retains unconsumed values");
            ppof_v1_abort(run);
            return false;
        }
        run->saw_accept = true;
        return true;
    }

malformed:
    ppof_v1_set_error(
        error_buf, error_buf_size,
        "cursor event does not match its occurrence-fold plan");
    ppof_v1_abort(run);
    return false;
}

static bool ppof_v1_semantic_occurrence_required(
    void *context,
    uint32_t source_index) {
    const PPOccurrenceFoldV1Run *run = context;
    uint32_t binding_index;

    if (!run || !run->plan ||
        source_index >= run->plan->cursor_terminal_len) {
        return false;
    }
    binding_index =
        run->plan->terminal_binding_by_source[source_index];
    return binding_index != UINT32_MAX &&
        binding_index < run->plan->terminal_len &&
        run->plan->terminals[
            binding_index].value_production_label != UINT32_MAX;
}

static bool ppof_v1_semantic_occurrence_project(
    void *context,
    const CettaLpNativeUtf8ScalarView *view,
    uint32_t source_index,
    uint32_t left_scalar,
    uint32_t right_scalar,
    uint64_t work_limit,
    PPGuardedLexCursorV1SemanticOccurrenceProjection *out,
    char *error_buf,
    size_t error_buf_size) {
    PPOccurrenceFoldV1Run *run = context;
    PPOccurrenceSpanMaskV1Result projected;
    uint32_t needed;

    if (out)
        memset(out, 0, sizeof(*out));
    if (!run || !run->span_mask || !view || !out ||
        left_scalar > right_scalar || right_scalar > view->scalar_len) {
        ppof_v1_set_error(
            error_buf, error_buf_size,
            "bad occurrence-fold span projection request");
        return false;
    }
    needed = right_scalar - left_scalar;
    if (needed > run->span_mask_action_cap) {
        uint32_t next_cap = run->span_mask_action_cap
            ? run->span_mask_action_cap : 16u;
        uint32_t *next;

        while (next_cap < needed) {
            if (next_cap > UINT32_MAX / 2u) {
                next_cap = needed;
                break;
            }
            next_cap *= 2u;
        }
        if ((size_t)next_cap > SIZE_MAX / sizeof(*next))
            return false;
        next = realloc(
            run->span_mask_actions, sizeof(*next) * (size_t)next_cap);
        if (!next)
            return false;
        run->span_mask_actions = next;
        run->span_mask_action_cap = next_cap;
    }
    if (!ppoccurrence_span_mask_v1_project_prevalidated(
            run->span_mask, view, source_index,
            left_scalar, right_scalar, work_limit,
            run->span_mask_actions, run->span_mask_action_cap,
            &projected, error_buf, error_buf_size) ||
        UINT64_MAX -
                run->receipt.semantic_occurrence_projection_work_item_len <
            projected.work_item_len) {
        if (error_buf && error_buf_size > 0u && error_buf[0] == '\0') {
            ppof_v1_set_error(
                error_buf, error_buf_size,
                "occurrence-fold span projection work receipt overflow");
        }
        return false;
    }
    run->receipt.semantic_occurrence_projection_work_item_len +=
        projected.work_item_len;
    if (projected.outcome == PPOCCURRENCE_SPAN_MASK_V1_WORK_LIMIT) {
        out->outcome =
            PPGUARDED_LEX_CURSOR_V1_OCCURRENCE_PROJECTION_WORK_LIMIT;
        out->work_item_len = projected.work_item_len;
        return true;
    }
    if (projected.outcome == PPOCCURRENCE_SPAN_MASK_V1_UNHANDLED) {
        out->outcome =
            PPGUARDED_LEX_CURSOR_V1_OCCURRENCE_PROJECTION_UNHANDLED;
        out->work_item_len = projected.work_item_len;
        return true;
    }
    if (projected.outcome != PPOCCURRENCE_SPAN_MASK_V1_ACCEPTED) {
        ppof_v1_set_error(
            error_buf, error_buf_size,
            "occurrence-fold span projection returned an unknown outcome");
        return false;
    }
    run->receipt.semantic_occurrence_projection_len++;
    out->outcome =
        PPGUARDED_LEX_CURSOR_V1_OCCURRENCE_PROJECTION_ACCEPTED;
    out->production_label = projected.value_production_label;
    out->left_scalar = projected.left_scalar;
    out->right_scalar = projected.right_scalar;
    out->work_item_len = projected.work_item_len;
    return true;
}

static bool ppof_v1_event_liveness_build(
    const PPOccurrenceFoldV1Plan *plan,
    uint8_t **shift_live_out,
    uint8_t **reduction_live_out,
    char *error_buf,
    size_t error_buf_size) {
    uint8_t *shift_live = NULL;
    uint8_t *reduction_live = NULL;
    uint32_t index;

    if (!plan || !shift_live_out || !reduction_live_out ||
        !plan->terminal_binding_by_source ||
        !plan->production_binding_by_index ||
        !ppof_v1_array_fits(plan->cursor_terminal_len, 1u) ||
        !ppof_v1_array_fits(plan->cursor_production_len, 1u)) {
        ppof_v1_set_error(
            error_buf, error_buf_size,
            "bad occurrence-fold event-liveness inputs");
        return false;
    }
    *shift_live_out = NULL;
    *reduction_live_out = NULL;
    shift_live = calloc(
        plan->cursor_terminal_len ? plan->cursor_terminal_len : 1u,
        sizeof(*shift_live));
    reduction_live = calloc(
        plan->cursor_production_len ? plan->cursor_production_len : 1u,
        sizeof(*reduction_live));
    if (!shift_live || !reduction_live) {
        ppof_v1_set_error(
            error_buf, error_buf_size,
            "cannot allocate occurrence-fold event-liveness masks");
        goto fail;
    }
    for (index = 0u; index < plan->cursor_terminal_len; index++) {
        uint32_t binding_index =
            plan->terminal_binding_by_source[index];

        if (binding_index == UINT32_MAX)
            continue;
        if (binding_index >= plan->terminal_len ||
            plan->terminals[binding_index].source_index != index) {
            ppof_v1_set_error(
                error_buf, error_buf_size,
                "occurrence-fold terminal liveness disagrees with its plan");
            goto fail;
        }
        if (plan->terminals[binding_index].value_production_label ==
                UINT32_MAX) {
            shift_live[index] = 1u;
        }
    }
    for (index = 0u; index < plan->cursor_production_len; index++) {
        uint32_t binding_index =
            plan->production_binding_by_index[index];

        if (binding_index == UINT32_MAX)
            continue;
        if (binding_index >= plan->production_len ||
            plan->productions[binding_index].production_index != index) {
            ppof_v1_set_error(
                error_buf, error_buf_size,
                "occurrence-fold reduction liveness disagrees with its plan");
            goto fail;
        }
        reduction_live[index] = 1u;
    }
    *shift_live_out = shift_live;
    *reduction_live_out = reduction_live;
    return true;

fail:
    free(reduction_live);
    free(shift_live);
    return false;
}

static bool ppof_v1_run_bytes_impl(
    const PPGuardedLexCursorV1Program *program,
    const PPOccurrenceFoldV1Plan *plan,
    const PPOccurrenceSpanMaskV1Plan *span_mask,
    const uint8_t *input_bytes,
    size_t input_byte_len,
    const PPOccurrenceFoldV1Backend *backend,
    PPGuardedLexCursorV1Observation observation,
    uint64_t work_limit,
    PPOccurrenceFoldV1Receipt *out,
    bool prevalidated,
    char *error_buf,
    size_t error_buf_size) {
    PPOccurrenceFoldV1Run run;
    PPGuardedLexCursorV1EventSink sink;
    uint8_t *shift_event_live = NULL;
    uint8_t *reduction_event_live = NULL;
    bool parser_ok;
    bool ok = false;

    if (error_buf && error_buf_size > 0u)
        error_buf[0] = '\0';
    if (!program || !plan || (!input_bytes && input_byte_len > 0u) ||
        !backend || !backend->apply || !backend->commit ||
        !backend->abort || !out || work_limit == 0u ||
        input_byte_len > UINT32_MAX ||
        (!prevalidated &&
         (!ppoccurrence_fold_v1_plan_validate_program(
              program, plan, error_buf, error_buf_size) ||
          (span_mask &&
           !ppoccurrence_span_mask_v1_plan_validate(
               program, plan, span_mask,
               error_buf, error_buf_size))))) {
        if (error_buf && error_buf_size > 0u && error_buf[0] == '\0') {
            ppof_v1_set_error(
                error_buf, error_buf_size,
                "bad occurrence-fold execution arguments");
        }
        return false;
    }
    memset(&run, 0, sizeof(run));
    if (!ppof_v1_event_liveness_build(
            plan, &shift_event_live, &reduction_event_live,
            error_buf, error_buf_size)) {
        goto done;
    }
    run.program = program;
    run.plan = plan;
    run.span_mask = span_mask;
    run.input = input_bytes;
    run.input_len = input_byte_len;
    run.backend = *backend;
    sink = (PPGuardedLexCursorV1EventSink){
        .context = &run,
        .emit = ppof_v1_emit,
        .shift_event_live = shift_event_live,
        .shift_event_live_len = plan->cursor_terminal_len,
        .reduction_event_live = reduction_event_live,
        .reduction_event_live_len = plan->cursor_production_len,
        .semantic_occurrences = true,
        .semantic_occurrence_required =
            ppof_v1_semantic_occurrence_required,
        .semantic_occurrence_project = span_mask
            ? ppof_v1_semantic_occurrence_project : NULL,
    };
    parser_ok = ppguarded_lex_cursor_v1_program_run_events_bytes_prevalidated(
        program, input_bytes, input_byte_len, &sink, observation,
        work_limit, &run.receipt.parser_receipt,
        error_buf, error_buf_size);
    if (!parser_ok)
        goto done;
    if (run.receipt.parser_receipt.outcome !=
            PPGUARDED_LEX_CURSOR_V1_ACCEPTED) {
        ppof_v1_abort(&run);
        *out = run.receipt;
        ok = true;
        goto done;
    }
    if (!run.saw_accept || run.aborted ||
        !run.backend.commit(
            run.backend.context, error_buf, error_buf_size)) {
        if (!run.aborted)
            ppof_v1_abort(&run);
        goto done;
    }
    run.committed = true;
    run.receipt.committed = true;
    *out = run.receipt;
    ok = true;

done:
    if (!ok)
        ppof_v1_abort(&run);
    free(reduction_event_live);
    free(shift_event_live);
    free(run.selected);
    free(run.pending);
    free(run.span_mask_actions);
    if (!ok && error_buf && error_buf_size > 0u &&
        error_buf[0] == '\0') {
        ppof_v1_set_error(
            error_buf, error_buf_size,
            "occurrence-fold execution failed");
    }
    return ok;
}

bool ppoccurrence_fold_v1_run_bytes_prevalidated(
    const PPGuardedLexCursorV1Program *program,
    const PPOccurrenceFoldV1Plan *plan,
    const uint8_t *input_bytes,
    size_t input_byte_len,
    const PPOccurrenceFoldV1Backend *backend,
    PPGuardedLexCursorV1Observation observation,
    uint64_t work_limit,
    PPOccurrenceFoldV1Receipt *out,
    char *error_buf,
    size_t error_buf_size) {
    return ppof_v1_run_bytes_impl(
        program, plan, NULL, input_bytes, input_byte_len, backend,
        observation, work_limit, out, true,
        error_buf, error_buf_size);
}

bool ppoccurrence_fold_v1_run_bytes(
    const PPGuardedLexCursorV1Program *program,
    const PPOccurrenceFoldV1Plan *plan,
    const uint8_t *input_bytes,
    size_t input_byte_len,
    const PPOccurrenceFoldV1Backend *backend,
    PPGuardedLexCursorV1Observation observation,
    uint64_t work_limit,
    PPOccurrenceFoldV1Receipt *out,
    char *error_buf,
    size_t error_buf_size) {
    return ppof_v1_run_bytes_impl(
        program, plan, NULL, input_bytes, input_byte_len, backend,
        observation, work_limit, out, false,
        error_buf, error_buf_size);
}

bool ppoccurrence_fold_v1_run_bytes_with_span_mask(
    const PPGuardedLexCursorV1Program *program,
    const PPOccurrenceFoldV1Plan *plan,
    const PPOccurrenceSpanMaskV1Plan *span_mask,
    const uint8_t *input_bytes,
    size_t input_byte_len,
    const PPOccurrenceFoldV1Backend *backend,
    PPGuardedLexCursorV1Observation observation,
    uint64_t work_limit,
    PPOccurrenceFoldV1Receipt *out,
    char *error_buf,
    size_t error_buf_size) {
    return ppof_v1_run_bytes_impl(
        program, plan, span_mask, input_bytes, input_byte_len, backend,
        observation, work_limit, out, false,
        error_buf, error_buf_size);
}

bool ppoccurrence_fold_v1_run_bytes_with_span_mask_prevalidated(
    const PPGuardedLexCursorV1Program *program,
    const PPOccurrenceFoldV1Plan *plan,
    const PPOccurrenceSpanMaskV1Plan *span_mask,
    const uint8_t *input_bytes,
    size_t input_byte_len,
    const PPOccurrenceFoldV1Backend *backend,
    PPGuardedLexCursorV1Observation observation,
    uint64_t work_limit,
    PPOccurrenceFoldV1Receipt *out,
    char *error_buf,
    size_t error_buf_size) {
    return ppof_v1_run_bytes_impl(
        program, plan, span_mask, input_bytes, input_byte_len, backend,
        observation, work_limit, out, true,
        error_buf, error_buf_size);
}

typedef struct {
    FILE *output;
    const char *prefix;
    char *error_buf;
    size_t error_buf_size;
    bool failed;
} PPOccurrenceFoldV1CEmitter;

static void ppof_v1_c_error(
    PPOccurrenceFoldV1CEmitter *emitter,
    const char *format,
    ...) {
    va_list arguments;

    if (!emitter || emitter->failed)
        return;
    emitter->failed = true;
    if (!emitter->error_buf || emitter->error_buf_size == 0u)
        return;
    va_start(arguments, format);
    (void)vsnprintf(
        emitter->error_buf, emitter->error_buf_size, format, arguments);
    va_end(arguments);
}

static void ppof_v1_c_write(
    PPOccurrenceFoldV1CEmitter *emitter,
    const char *format,
    ...) {
    va_list arguments;

    if (!emitter || emitter->failed)
        return;
    va_start(arguments, format);
    if (vfprintf(emitter->output, format, arguments) < 0)
        ppof_v1_c_error(emitter, "failed to write generated fold C");
    va_end(arguments);
}

static bool ppof_v1_c_identifier_valid(const char *identifier) {
    size_t index;

    if (!identifier || !identifier[0] ||
        !(identifier[0] == '_' ||
          isalpha((unsigned char)identifier[0]))) {
        return false;
    }
    for (index = 1u; identifier[index]; index++) {
        if (!(identifier[index] == '_' ||
              isalnum((unsigned char)identifier[index]))) {
            return false;
        }
    }
    return true;
}

static void ppof_v1_c_string(
    PPOccurrenceFoldV1CEmitter *emitter,
    const char *text) {
    const unsigned char *cursor = (const unsigned char *)text;

    ppof_v1_c_write(emitter, "\"");
    while (cursor && *cursor) {
        unsigned char ch = *cursor++;
        if (ch == '\\' || ch == '"') {
            ppof_v1_c_write(emitter, "\\%c", (int)ch);
        } else if (ch >= 0x20u && ch <= 0x7eu) {
            ppof_v1_c_write(emitter, "%c", (int)ch);
        } else {
            ppof_v1_c_write(emitter, "\\%03o", (unsigned int)ch);
        }
    }
    ppof_v1_c_write(emitter, "\"");
}

static void ppof_v1_c_names(
    PPOccurrenceFoldV1CEmitter *emitter,
    const char *suffix,
    char *const *names,
    uint32_t len) {
    uint32_t index;

    ppof_v1_c_write(
        emitter,
        "static const char *const %s_occurrence_fold_%s[%u] = {\n",
        emitter->prefix, suffix, len);
    for (index = 0u; index < len; index++) {
        ppof_v1_c_write(emitter, "    ");
        ppof_v1_c_string(emitter, names[index]);
        ppof_v1_c_write(
            emitter, "%s\n", index + 1u == len ? "" : ",");
    }
    ppof_v1_c_write(emitter, "};\n\n");
}

static void ppof_v1_c_u32_array(
    PPOccurrenceFoldV1CEmitter *emitter,
    const char *suffix,
    const uint32_t *values,
    uint32_t len) {
    uint32_t index;

    ppof_v1_c_write(
        emitter,
        "static const uint32_t %s_occurrence_fold_%s[%u] = {\n    ",
        emitter->prefix, suffix, len);
    for (index = 0u; index < len; index++) {
        ppof_v1_c_write(
            emitter, "UINT32_C(%u)%s", values[index],
            index + 1u == len ? "" : ",");
        if (index + 1u != len) {
            ppof_v1_c_write(
                emitter, (index + 1u) % 8u == 0u ? "\n    " : " ");
        }
    }
    ppof_v1_c_write(emitter, "\n};\n\n");
}

static void ppof_v1_c_data(
    PPOccurrenceFoldV1CEmitter *emitter,
    const PPOccurrenceFoldV1Plan *plan) {
    uint32_t index;

    ppof_v1_c_names(
        emitter, "roles", plan->roles, plan->role_len);
    ppof_v1_c_names(
        emitter, "nodes", plan->nodes, plan->node_len);
    ppof_v1_c_names(
        emitter, "operations", plan->operations, plan->operation_len);
    ppof_v1_c_write(
        emitter,
        "static const PPOccurrenceFoldV1TerminalBinding "
        "%s_occurrence_fold_terminals[%u] = {\n",
        emitter->prefix, plan->terminal_len);
    for (index = 0u; index < plan->terminal_len; index++) {
        const PPOccurrenceFoldV1TerminalBinding *binding =
            &plan->terminals[index];
        ppof_v1_c_write(
            emitter,
            "    { UINT32_C(%u), UINT32_C(%u), UINT32_C(%u), "
            "UINT32_C(%u) }%s\n",
            binding->source_index, binding->role_id,
            binding->shift_operation_id,
            binding->value_production_label,
            index + 1u == plan->terminal_len ? "" : ",");
    }
    ppof_v1_c_write(emitter, "};\n\n");
    ppof_v1_c_write(
        emitter,
        "static const PPOccurrenceFoldV1ProductionBinding "
        "%s_occurrence_fold_productions[%u] = {\n",
        emitter->prefix, plan->production_len);
    for (index = 0u; index < plan->production_len; index++) {
        const PPOccurrenceFoldV1ProductionBinding *binding =
            &plan->productions[index];
        ppof_v1_c_write(
            emitter,
            "    { UINT32_C(%u), UINT32_C(%u), UINT32_C(%u), "
            "UINT32_C(%u), UINT32_C(%u) }%s\n",
            binding->production_index, binding->production_label,
            binding->node_id, binding->operation_id,
            binding->contract_index,
            index + 1u == plan->production_len ? "" : ",");
    }
    ppof_v1_c_write(emitter, "};\n\n");
    ppof_v1_c_write(
        emitter,
        "static const PPOccurrenceFoldV1Contract "
        "%s_occurrence_fold_contracts[%u] = {\n",
        emitter->prefix, plan->contract_len);
    for (index = 0u; index < plan->contract_len; index++) {
        const PPOccurrenceFoldV1Contract *contract =
            &plan->contracts[index];
        ppof_v1_c_write(
            emitter,
            "    { UINT32_C(%u), UINT32_C(%u), UINT32_C(%u), "
            "UINT32_C(%u), UINT32_C(%u) }%s\n",
            contract->state_len, contract->start_state,
            contract->accept_state, contract->transition_begin,
            contract->transition_len,
            index + 1u == plan->contract_len ? "" : ",");
    }
    ppof_v1_c_write(emitter, "};\n\n");
    if (plan->transition_len > 0u) {
        ppof_v1_c_write(
            emitter,
            "static const PPOccurrenceFoldV1Transition "
            "%s_occurrence_fold_transitions[%u] = {\n",
            emitter->prefix, plan->transition_len);
        for (index = 0u; index < plan->transition_len; index++) {
            const PPOccurrenceFoldV1Transition *transition =
                &plan->transitions[index];
            ppof_v1_c_write(
                emitter,
                "    { (PPOccurrenceFoldV1TransitionKind)%u, "
                "UINT32_C(%u), UINT32_C(%u), UINT32_C(%u) }%s\n",
                (unsigned int)transition->kind,
                transition->source, transition->target,
                transition->role_id,
                index + 1u == plan->transition_len ? "" : ",");
        }
        ppof_v1_c_write(emitter, "};\n\n");
    }
    ppof_v1_c_u32_array(
        emitter, "terminal_reverse",
        plan->terminal_binding_by_source, plan->cursor_terminal_len);
    ppof_v1_c_u32_array(
        emitter, "production_reverse",
        plan->production_binding_by_index,
        plan->cursor_production_len);
}

static void ppof_v1_c_assign_names(
    PPOccurrenceFoldV1CEmitter *emitter,
    const char *field,
    const char *suffix,
    uint32_t len) {
    ppof_v1_c_write(
        emitter,
        "    result.%s = calloc(UINT32_C(%u), sizeof(*result.%s));\n"
        "    if (!result.%s) goto fail;\n"
        "    for (index = 0u; index < UINT32_C(%u); index++) {\n"
        "        result.%s[index] = %s_occurrence_fold_text_dup(\n"
        "            %s_occurrence_fold_%s[index]);\n"
        "        if (!result.%s[index]) goto fail;\n"
        "    }\n",
        field, len, field, field, len, field, emitter->prefix,
        emitter->prefix, suffix, field);
}

static void ppof_v1_c_init(
    PPOccurrenceFoldV1CEmitter *emitter,
    const PPOccurrenceFoldV1Plan *plan) {
    ppof_v1_c_write(
        emitter,
        "const char *%s_occurrence_fold_plan_digest(void) {\n"
        "    return ",
        emitter->prefix);
    ppof_v1_c_string(emitter, plan->plan_digest);
    ppof_v1_c_write(
        emitter,
        ";\n}\n\n"
        "static bool %s_occurrence_fold_copy(\n"
        "    void **target, const void *source, size_t size,\n"
        "    char *error_buf, size_t error_buf_size) {\n"
        "    void *copy;\n"
        "    if (!target || !source || size == 0u) return false;\n"
        "    copy = malloc(size);\n"
        "    if (!copy) {\n"
        "        if (error_buf && error_buf_size > 0u)\n"
        "            snprintf(error_buf, error_buf_size,\n"
        "                     \"cannot allocate generated fold plan\");\n"
        "        return false;\n"
        "    }\n"
        "    memcpy(copy, source, size);\n"
        "    *target = copy;\n"
        "    return true;\n"
        "}\n\n"
        "static char *%s_occurrence_fold_text_dup(const char *text) {\n"
        "    size_t len;\n"
        "    char *copy;\n"
        "    if (!text) return NULL;\n"
        "    len = strlen(text);\n"
        "    copy = malloc(len + 1u);\n"
        "    if (copy) memcpy(copy, text, len + 1u);\n"
        "    return copy;\n"
        "}\n\n",
        emitter->prefix, emitter->prefix);
    ppof_v1_c_data(emitter, plan);
    ppof_v1_c_write(
        emitter,
        "bool %s_occurrence_fold_plan_init(\n"
        "    const PPGuardedLexCursorV1Program *program,\n"
        "    PPOccurrenceFoldV1Plan *out,\n"
        "    char *error_buf, size_t error_buf_size) {\n"
        "    PPOccurrenceFoldV1Plan result;\n"
        "    uint32_t index;\n\n"
        "    if (error_buf && error_buf_size > 0u) error_buf[0] = '\\0';\n"
        "    if (!program || !out) {\n"
        "        if (error_buf && error_buf_size > 0u)\n"
        "            snprintf(error_buf, error_buf_size,\n"
        "                     \"missing generated fold output\");\n"
        "        return false;\n"
        "    }\n"
        "    ppoccurrence_fold_v1_plan_init(&result);\n",
        emitter->prefix);
    ppof_v1_c_assign_names(
        emitter, "roles", "roles", plan->role_len);
    ppof_v1_c_assign_names(
        emitter, "nodes", "nodes", plan->node_len);
    ppof_v1_c_assign_names(
        emitter, "operations", "operations", plan->operation_len);
    ppof_v1_c_write(
        emitter,
        "    if (!%s_occurrence_fold_copy(\n"
        "            (void **)&result.terminals,\n"
        "            %s_occurrence_fold_terminals,\n"
        "            sizeof(%s_occurrence_fold_terminals),\n"
        "            error_buf, error_buf_size)) goto fail;\n"
        "    if (!%s_occurrence_fold_copy(\n"
        "            (void **)&result.productions,\n"
        "            %s_occurrence_fold_productions,\n"
        "            sizeof(%s_occurrence_fold_productions),\n"
        "            error_buf, error_buf_size)) goto fail;\n"
        "    if (!%s_occurrence_fold_copy(\n"
        "            (void **)&result.contracts,\n"
        "            %s_occurrence_fold_contracts,\n"
        "            sizeof(%s_occurrence_fold_contracts),\n"
        "            error_buf, error_buf_size)) goto fail;\n",
        emitter->prefix, emitter->prefix, emitter->prefix,
        emitter->prefix, emitter->prefix, emitter->prefix,
        emitter->prefix, emitter->prefix, emitter->prefix);
    if (plan->transition_len > 0u) {
        ppof_v1_c_write(
            emitter,
            "    if (!%s_occurrence_fold_copy(\n"
            "            (void **)&result.transitions,\n"
            "            %s_occurrence_fold_transitions,\n"
            "            sizeof(%s_occurrence_fold_transitions),\n"
            "            error_buf, error_buf_size)) goto fail;\n",
            emitter->prefix, emitter->prefix, emitter->prefix);
    }
    ppof_v1_c_write(
        emitter,
        "    if (!%s_occurrence_fold_copy(\n"
        "            (void **)&result.terminal_binding_by_source,\n"
        "            %s_occurrence_fold_terminal_reverse,\n"
        "            sizeof(%s_occurrence_fold_terminal_reverse),\n"
        "            error_buf, error_buf_size)) goto fail;\n"
        "    if (!%s_occurrence_fold_copy(\n"
        "            (void **)&result.production_binding_by_index,\n"
        "            %s_occurrence_fold_production_reverse,\n"
        "            sizeof(%s_occurrence_fold_production_reverse),\n"
        "            error_buf, error_buf_size)) goto fail;\n"
        "    result.role_len = UINT32_C(%u);\n"
        "    result.node_len = UINT32_C(%u);\n"
        "    result.operation_len = UINT32_C(%u);\n"
        "    result.terminal_len = UINT32_C(%u);\n"
        "    result.production_len = UINT32_C(%u);\n"
        "    result.contract_len = UINT32_C(%u);\n"
        "    result.transition_len = UINT32_C(%u);\n"
        "    result.cursor_terminal_len = UINT32_C(%u);\n"
        "    result.cursor_production_len = UINT32_C(%u);\n",
        emitter->prefix, emitter->prefix, emitter->prefix,
        emitter->prefix, emitter->prefix, emitter->prefix,
        plan->role_len, plan->node_len, plan->operation_len,
        plan->terminal_len, plan->production_len, plan->contract_len,
        plan->transition_len, plan->cursor_terminal_len,
        plan->cursor_production_len);
#define PPOF_V1_EMIT_DIGEST(field) \
    do { \
        ppof_v1_c_write(emitter, "    memcpy(result." #field ", "); \
        ppof_v1_c_string(emitter, plan->field); \
        ppof_v1_c_write( \
            emitter, ", sizeof(result." #field "));\n"); \
    } while (0)
    PPOF_V1_EMIT_DIGEST(base_pack_digest);
    PPOF_V1_EMIT_DIGEST(lexical_plan_digest);
    PPOF_V1_EMIT_DIGEST(cursor_program_digest);
    PPOF_V1_EMIT_DIGEST(compiler_answer_digest);
    PPOF_V1_EMIT_DIGEST(plan_digest);
#undef PPOF_V1_EMIT_DIGEST
    ppof_v1_c_write(
        emitter,
        "    if (!ppoccurrence_fold_v1_plan_validate_program(\n"
        "            program, &result, error_buf, error_buf_size))\n"
        "        goto fail;\n"
        "    ppoccurrence_fold_v1_plan_free(out);\n"
        "    *out = result;\n"
        "    return true;\n\n"
        "fail:\n"
        "    ppoccurrence_fold_v1_plan_free(&result);\n"
        "    return false;\n"
        "}\n");
}

bool ppoccurrence_fold_v1_emit_c(
    const PPGuardedLexCursorV1Program *program,
    const PPOccurrenceFoldV1Plan *plan,
    FILE *output,
    const char *identifier_prefix,
    char *error_buf,
    size_t error_buf_size) {
    PPOccurrenceFoldV1CEmitter emitter;
    char validation_error[256] = {0};

    if (error_buf && error_buf_size > 0u)
        error_buf[0] = '\0';
    memset(&emitter, 0, sizeof(emitter));
    emitter.output = output;
    emitter.prefix = identifier_prefix;
    emitter.error_buf = error_buf;
    emitter.error_buf_size = error_buf_size;
    if (!program || !plan || !output ||
        !ppof_v1_c_identifier_valid(identifier_prefix)) {
        ppof_v1_c_error(
            &emitter, "bad occurrence-fold C-emitter arguments");
        return false;
    }
    if (!ppoccurrence_fold_v1_plan_validate_program(
            program, plan, validation_error, sizeof(validation_error))) {
        ppof_v1_c_error(
            &emitter, "cannot emit invalid occurrence-fold plan: %s",
            validation_error[0] ? validation_error : "validation failed");
        return false;
    }
    ppof_v1_c_write(
        &emitter,
        "\n/* Generated from ParserOccurrenceFoldPlanV1. */\n"
        "#include \"parser_occurrence_fold_v1.h\"\n\n");
    ppof_v1_c_init(&emitter, plan);
    if (!emitter.failed && ferror(output))
        ppof_v1_c_error(&emitter, "generated fold C stream failed");
    return !emitter.failed;
}
