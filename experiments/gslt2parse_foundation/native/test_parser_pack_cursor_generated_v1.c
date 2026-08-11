#define _POSIX_C_SOURCE 200809L

#include "finite_horn_ground_term_v1.h"
#include "indexed_checker_effect_v1.h"
#include "indexed_inference_state_v1.h"
#include "parser_occurrence_file_resolver_v1.h"
#include "parser_occurrence_fold_v1.h"
#include "parser_occurrence_span_mask_v1.h"
#include "parser_pack_guarded_lexical_exec_v1.h"

#include "symbol.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef PP_CURSOR_GENERATED_PREFIX
#error "PP_CURSOR_GENERATED_PREFIX must name the generated program"
#endif

#ifndef PP_CURSOR_GENERATED_HAS_OCCURRENCE_FOLD
#define PP_CURSOR_GENERATED_HAS_OCCURRENCE_FOLD 0
#endif

#ifndef PP_CURSOR_GENERATED_HAS_INDEXED_CHECKER_EFFECTS
#define PP_CURSOR_GENERATED_HAS_INDEXED_CHECKER_EFFECTS 0
#endif

#ifndef PP_CURSOR_GENERATED_HAS_OCCURRENCE_SPAN_MASK
#define PP_CURSOR_GENERATED_HAS_OCCURRENCE_SPAN_MASK 0
#endif

#if PP_CURSOR_GENERATED_HAS_OCCURRENCE_SPAN_MASK && \
    !PP_CURSOR_GENERATED_HAS_OCCURRENCE_FOLD
#error "generated occurrence-span mask requires an occurrence fold"
#endif

#if PP_CURSOR_GENERATED_HAS_INDEXED_CHECKER_EFFECTS && \
    !PP_CURSOR_GENERATED_HAS_OCCURRENCE_FOLD
#error "generated indexed-checker effects require an occurrence fold"
#endif

#define PP_CURSOR_JOIN_RAW(left, right) left##right
#define PP_CURSOR_JOIN(left, right) PP_CURSOR_JOIN_RAW(left, right)
#define PP_CURSOR_GENERATED_INIT \
    PP_CURSOR_JOIN(PP_CURSOR_GENERATED_PREFIX, _program_init)
#define PP_CURSOR_GENERATED_DIGEST \
    PP_CURSOR_JOIN(PP_CURSOR_GENERATED_PREFIX, _program_digest)
#if PP_CURSOR_GENERATED_HAS_OCCURRENCE_FOLD
#define PP_CURSOR_GENERATED_FOLD_INIT \
    PP_CURSOR_JOIN(PP_CURSOR_GENERATED_PREFIX, _occurrence_fold_plan_init)
#define PP_CURSOR_GENERATED_FOLD_DIGEST \
    PP_CURSOR_JOIN(PP_CURSOR_GENERATED_PREFIX, \
                   _occurrence_fold_plan_digest)
#endif
#if PP_CURSOR_GENERATED_HAS_OCCURRENCE_SPAN_MASK
#define PP_CURSOR_GENERATED_SPAN_MASK_INIT \
    PP_CURSOR_JOIN(PP_CURSOR_GENERATED_PREFIX, \
                   _occurrence_span_mask_plan_init)
#define PP_CURSOR_GENERATED_SPAN_MASK_DIGEST \
    PP_CURSOR_JOIN(PP_CURSOR_GENERATED_PREFIX, \
                   _occurrence_span_mask_plan_digest)
#endif
#if PP_CURSOR_GENERATED_HAS_INDEXED_CHECKER_EFFECTS
#define PP_CURSOR_GENERATED_CHECKER_EFFECT_INIT \
    PP_CURSOR_JOIN(PP_CURSOR_GENERATED_PREFIX, \
                   _indexed_checker_effect_plan_init)
#define PP_CURSOR_GENERATED_CHECKER_EFFECT_DIGEST \
    PP_CURSOR_JOIN(PP_CURSOR_GENERATED_PREFIX, \
                   _indexed_checker_effect_plan_digest)
#endif

extern bool PP_CURSOR_GENERATED_INIT(
    PPGuardedLexCursorV1Program *out,
    char *error_buf,
    size_t error_buf_size);
extern const char *PP_CURSOR_GENERATED_DIGEST(void);
#if PP_CURSOR_GENERATED_HAS_OCCURRENCE_FOLD
extern bool PP_CURSOR_GENERATED_FOLD_INIT(
    const PPGuardedLexCursorV1Program *program,
    PPOccurrenceFoldV1Plan *out,
    char *error_buf,
    size_t error_buf_size);
extern const char *PP_CURSOR_GENERATED_FOLD_DIGEST(void);
#endif
#if PP_CURSOR_GENERATED_HAS_OCCURRENCE_SPAN_MASK
extern bool PP_CURSOR_GENERATED_SPAN_MASK_INIT(
    const PPGuardedLexCursorV1Program *program,
    const PPOccurrenceFoldV1Plan *fold,
    PPOccurrenceSpanMaskV1Plan *out,
    char *error_buf,
    size_t error_buf_size);
extern const char *PP_CURSOR_GENERATED_SPAN_MASK_DIGEST(void);
#endif
#if PP_CURSOR_GENERATED_HAS_INDEXED_CHECKER_EFFECTS
extern bool PP_CURSOR_GENERATED_CHECKER_EFFECT_INIT(
    const PPGuardedLexCursorV1Program *program,
    const PPOccurrenceFoldV1Plan *occurrence_plan,
    PPIndexedCheckerEffectV1Plan *out,
    char *error_buf,
    size_t error_buf_size);
extern const char *PP_CURSOR_GENERATED_CHECKER_EFFECT_DIGEST(void);
#endif

#if PP_CURSOR_GENERATED_HAS_INDEXED_CHECKER_EFFECTS
static uint32_t indexed_checker_effect_kind_len(
    const PPIndexedCheckerEffectV1Plan *plan) {
    bool seen[PPINDEXED_CHECKER_EFFECT_V1_COUNT] = {false};
    uint32_t count = 0u;
    uint32_t index;

    if (!plan || !plan->effect_by_operation)
        return 0u;
    for (index = 0u; index < plan->operation_len; index++) {
        uint32_t kind = (uint32_t)plan->effect_by_operation[index];
        if (kind >= PPINDEXED_CHECKER_EFFECT_V1_COUNT || seen[kind])
            continue;
        seen[kind] = true;
        count++;
    }
    return count;
}
#endif

static bool read_input(const char *path, uint8_t **out, size_t *out_len) {
    FILE *input = NULL;
    uint8_t *bytes = NULL;
    size_t len = 0u;
    size_t cap = 0u;
    bool ok = false;

    if (!path || !out || !out_len)
        return false;
    *out = NULL;
    *out_len = 0u;
    input = fopen(path, "rb");
    if (!input)
        goto done;
    for (;;) {
        size_t amount;
        if (len == cap) {
            size_t next_cap = cap ? cap * 2u : 4096u;
            uint8_t *next;
            if (next_cap < cap)
                goto done;
            next = realloc(bytes, next_cap);
            if (!next)
                goto done;
            bytes = next;
            cap = next_cap;
        }
        amount = fread(bytes + len, 1u, cap - len, input);
        len += amount;
        if (amount > 0u)
            continue;
        if (ferror(input))
            goto done;
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

static const char *outcome_name(PPGuardedLexCursorV1Outcome outcome) {
    switch (outcome) {
    case PPGUARDED_LEX_CURSOR_V1_ACCEPTED:
        return "accepted";
    case PPGUARDED_LEX_CURSOR_V1_REJECTED:
        return "rejected";
    case PPGUARDED_LEX_CURSOR_V1_WORK_LIMIT:
        return "work-limit";
    }
    return "unknown";
}

static bool write_term(const Atom *term) {
    uint8_t *canonical = NULL;
    size_t canonical_len = 0u;
    bool ok;

    if (!term)
        return true;
    if (!fh_ground_term_v1_render(
            term, &canonical, &canonical_len, NULL, 0u)) {
        return false;
    }
    ok = printf("semantic-result\t") >= 0 &&
        fwrite(canonical, 1u, canonical_len, stdout) == canonical_len &&
        printf("\n") >= 0;
    free(canonical);
    return ok;
}

typedef struct {
    const PPGuardedLexCursorV1Program *program;
    size_t input_byte_len;
    uint32_t shift_len;
    uint32_t reduce_len;
    uint32_t accept_len;
    uint32_t event_len;
    uint32_t max_right_scalar;
    bool accepted;
    bool invalid;
    bool reject_first;
} CursorEventAudit;

static bool audit_cursor_event(
    void *raw,
    const PPGuardedLexCursorV1Event *event,
    char *error_buf,
    size_t error_buf_size) {
    CursorEventAudit *audit = raw;

    if (!audit || !audit->program || !event || audit->accepted ||
        event->left_scalar > event->right_scalar ||
        event->left_byte > event->right_byte ||
        event->right_byte > audit->input_byte_len) {
        if (audit)
            audit->invalid = true;
        return false;
    }
    if (audit->reject_first && audit->event_len == 0u) {
        if (error_buf && error_buf_size > 0u) {
            (void)snprintf(
                error_buf, error_buf_size,
                "intentional cursor occurrence mutation");
        }
        return false;
    }
    if (event->right_scalar > audit->max_right_scalar)
        audit->max_right_scalar = event->right_scalar;
    audit->event_len++;
    if (event->kind == PPGUARDED_LEX_CURSOR_V1_EVENT_SHIFT) {
        const PPGuardedLexCursorV1Terminal *terminal;

        if (event->source_index >= audit->program->terminal_len ||
            event->production_index != UINT32_MAX ||
            event->production_label != UINT32_MAX ||
            event->authored) {
            audit->invalid = true;
            return false;
        }
        terminal =
            &audit->program->terminals[event->source_index];
        if (event->symbol_id != terminal->terminal_id) {
            audit->invalid = true;
            return false;
        }
        audit->shift_len++;
        return true;
    }
    if (event->kind == PPGUARDED_LEX_CURSOR_V1_EVENT_REDUCE) {
        const CettaLpNativeSlrProgramProduction *production;

        if (event->source_index != UINT32_MAX ||
            event->production_index >=
                audit->program->final_slr.production_len) {
            audit->invalid = true;
            return false;
        }
        production = &audit->program->final_slr.productions[
            event->production_index];
        if (event->symbol_id != production->lhs ||
            event->production_label != production->label ||
            event->rhs_len != production->rhs_len ||
            event->authored != production->authored) {
            audit->invalid = true;
            return false;
        }
        audit->reduce_len++;
        return true;
    }
    if (event->kind == PPGUARDED_LEX_CURSOR_V1_EVENT_ACCEPT) {
        if (event->symbol_id !=
                audit->program->final_slr.start_nonterminal ||
            event->source_index != UINT32_MAX ||
            event->production_index != UINT32_MAX ||
            event->production_label != UINT32_MAX ||
            event->left_scalar != 0u ||
            event->left_byte != 0u ||
            event->right_byte != audit->input_byte_len) {
            audit->invalid = true;
            return false;
        }
        audit->accept_len++;
        audit->accepted = true;
        return true;
    }
    audit->invalid = true;
    return false;
}

static bool receipts_equal(
    const PPGuardedLexCursorV1Receipt *left,
    const PPGuardedLexCursorV1Receipt *right) {
    return left->outcome == right->outcome &&
        left->input_byte_len == right->input_byte_len &&
        left->input_scalar_len == right->input_scalar_len &&
        left->farthest_scalar == right->farthest_scalar &&
        left->farthest_byte == right->farthest_byte &&
        left->token_len == right->token_len &&
        left->scalar_token_len == right->scalar_token_len &&
        left->ordinary_span_token_len ==
            right->ordinary_span_token_len &&
        left->guarded_span_token_len ==
            right->guarded_span_token_len &&
        left->shift_len == right->shift_len &&
        left->reduce_len == right->reduce_len &&
        left->epsilon_reduce_len == right->epsilon_reduce_len &&
        left->unit_reduce_len == right->unit_reduce_len &&
        left->reselect_reduce_len == right->reselect_reduce_len &&
        left->unit_reselect_reduce_len ==
            right->unit_reselect_reduce_len &&
        left->cursor_scan_len == right->cursor_scan_len &&
        left->max_stack_len == right->max_stack_len &&
        left->source_pass_count == right->source_pass_count &&
        left->dfa_work_item_len == right->dfa_work_item_len &&
        left->parser_work_item_len == right->parser_work_item_len &&
        strcmp(left->trace_digest, right->trace_digest) == 0;
}

int main(int argc, char **argv) {
    SymbolTable symbols;
    PPGuardedLexCursorV1Program program;
    PPGuardedLexCursorV1DispatchIndex dispatch_index;
    PPGuardedLexCursorV1SemanticResult result;
    PPGuardedLexCursorV1Receipt receipt = {0};
    PPGuardedLexCursorV1Receipt ordinary_receipt = {0};
    CursorEventAudit event_audit = {0};
#if PP_CURSOR_GENERATED_HAS_OCCURRENCE_FOLD
    PPOccurrenceFoldV1Plan occurrence_fold_plan;
    PPOccurrenceFoldV1Receipt occurrence_fold_receipt = {0};
    PPOccurrenceFoldV1Trace occurrence_fold_trace;
    uint32_t occurrence_fold_mutation_killed = 0u;
#endif
#if PP_CURSOR_GENERATED_HAS_OCCURRENCE_SPAN_MASK
    PPOccurrenceSpanMaskV1Plan occurrence_span_mask_plan;
    uint32_t occurrence_span_mask_mutation_killed = 0u;
#endif
#if PP_CURSOR_GENERATED_HAS_INDEXED_CHECKER_EFFECTS
    PPIndexedCheckerEffectV1Plan indexed_checker_effect_plan;
    PPIndexedInferenceV1State indexed_inference_state;
    PPIndexedInferenceV1Fold indexed_inference_fold;
    PPIndexedInferenceV1Summary indexed_inference_summary;
    PPOccurrenceFileResolverV1 source_resolver;
    PPIndexedInferenceV1SourceResolver source_resolver_interface;
    uint32_t indexed_checker_effect_mutation_killed = 0u;
    uint32_t indexed_checker_policy_mutation_killed = 0u;
    bool indexed_inference_fold_initialized = false;
    bool indexed_inference_summary_built = false;
#endif
    uint8_t *input = NULL;
    uint8_t *masked_shift_events = NULL;
    uint8_t *masked_reduction_events = NULL;
    size_t input_len = 0u;
    char error[512] = {0};
    uint32_t saved_transition_begin = 0u;
    uint32_t mutation_killed = 0u;
    uint32_t semantic_required_sources = 0u;
    uint32_t terminal_index;
    uint64_t work_limit = UINT64_C(50000000);
    bool recognition_only = false;
    bool events_only = false;
    bool occurrence_fold_mode = false;
    bool verdict_exit = false;
    bool suppress_semantic_render = false;
    bool ok = false;
    const char *stage = "argument-validation";

    if (argc == 3 && strcmp(argv[2], "--recognition-only") == 0) {
        recognition_only = true;
        work_limit = UINT64_MAX;
    } else if (argc == 3 &&
               strcmp(argv[2], "--recognition-verdict") == 0) {
        recognition_only = true;
        verdict_exit = true;
        work_limit = UINT64_MAX;
    } else if (argc == 3 && strcmp(argv[2], "--semantic-full") == 0) {
        suppress_semantic_render = true;
        work_limit = UINT64_MAX;
    } else if (argc == 3 && strcmp(argv[2], "--events") == 0) {
        recognition_only = true;
        events_only = true;
        work_limit = UINT64_MAX;
    } else if (argc == 3 &&
               strcmp(argv[2], "--occurrence-fold") == 0) {
        recognition_only = true;
        occurrence_fold_mode = true;
        work_limit = UINT64_MAX;
    } else if (argc != 2) {
        fprintf(
            stderr,
            "usage: generated-cursor-test INPUT "
            "[--recognition-only | --recognition-verdict | "
            "--semantic-full | --events | --occurrence-fold]\n");
        return 1;
    }
    symbol_table_init(&symbols);
    symbol_table_init_builtins(&symbols, &g_builtin_syms);
    g_symbols = &symbols;
    g_hashcons = NULL;
    g_var_intern = NULL;
    ppguarded_lex_cursor_v1_program_init(&program);
    ppguarded_lex_cursor_v1_dispatch_index_init(&dispatch_index);
    ppguarded_lex_cursor_v1_semantic_result_init(&result);
#if PP_CURSOR_GENERATED_HAS_OCCURRENCE_FOLD
    ppoccurrence_fold_v1_plan_init(&occurrence_fold_plan);
#endif
#if PP_CURSOR_GENERATED_HAS_OCCURRENCE_SPAN_MASK
    ppoccurrence_span_mask_v1_plan_init(
        &occurrence_span_mask_plan);
#endif
#if PP_CURSOR_GENERATED_HAS_INDEXED_CHECKER_EFFECTS
    ppindexed_checker_effect_v1_plan_init(
        &indexed_checker_effect_plan);
    ppindexed_inference_v1_state_init(&indexed_inference_state);
    ppoccurrence_file_resolver_v1_init(&source_resolver);
    memset(
        &source_resolver_interface, 0,
        sizeof(source_resolver_interface));
    memset(&indexed_inference_fold, 0, sizeof(indexed_inference_fold));
    memset(&indexed_inference_summary, 0, sizeof(indexed_inference_summary));
#endif
    if (!read_input(argv[1], &input, &input_len) ||
        !PP_CURSOR_GENERATED_INIT(&program, error, sizeof(error)) ||
        strcmp(PP_CURSOR_GENERATED_DIGEST(), program.program_digest) != 0) {
        goto done;
    }
    stage = "cursor-dispatch-index";
    if (!ppguarded_lex_cursor_v1_dispatch_index_build(
            &program, &dispatch_index, error, sizeof(error)) ||
        !ppguarded_lex_cursor_v1_dispatch_index_validate(
            &program, &dispatch_index, error, sizeof(error))) {
        goto done;
    }
    {
        uint32_t saved_offset = dispatch_index.scalar_offsets[0];

        stage = "cursor-dispatch-index-mutation";
        dispatch_index.scalar_offsets[0] = saved_offset + 1u;
        error[0] = '\0';
        if (ppguarded_lex_cursor_v1_dispatch_index_validate(
                &program, &dispatch_index, error, sizeof(error))) {
            goto done;
        }
        dispatch_index.scalar_offsets[0] = saved_offset;
        error[0] = '\0';
        if (!ppguarded_lex_cursor_v1_dispatch_index_validate(
                &program, &dispatch_index, error, sizeof(error))) {
            goto done;
        }
    }
    stage = "occurrence-fold-plan";
#if PP_CURSOR_GENERATED_HAS_OCCURRENCE_FOLD
    if (occurrence_fold_mode &&
        (!PP_CURSOR_GENERATED_FOLD_INIT(
             &program, &occurrence_fold_plan, error, sizeof(error)) ||
         strcmp(
             PP_CURSOR_GENERATED_FOLD_DIGEST(),
             occurrence_fold_plan.plan_digest) != 0)) {
        goto done;
    }
#else
    if (occurrence_fold_mode) {
        (void)snprintf(
            error, sizeof(error),
            "generated cursor has no occurrence-fold plan");
        goto done;
    }
#endif
#if PP_CURSOR_GENERATED_HAS_OCCURRENCE_SPAN_MASK
    stage = "occurrence-span-mask-plan";
    if (occurrence_fold_mode &&
        (!PP_CURSOR_GENERATED_SPAN_MASK_INIT(
             &program, &occurrence_fold_plan,
             &occurrence_span_mask_plan,
             error, sizeof(error)) ||
         strcmp(
             PP_CURSOR_GENERATED_SPAN_MASK_DIGEST(),
             occurrence_span_mask_plan.plan_digest) != 0)) {
        goto done;
    }
#endif
#if PP_CURSOR_GENERATED_HAS_INDEXED_CHECKER_EFFECTS
    if (occurrence_fold_mode &&
        (!PP_CURSOR_GENERATED_CHECKER_EFFECT_INIT(
             &program, &occurrence_fold_plan,
             &indexed_checker_effect_plan,
             error, sizeof(error)) ||
         strcmp(
             PP_CURSOR_GENERATED_CHECKER_EFFECT_DIGEST(),
             indexed_checker_effect_plan.plan_digest) != 0)) {
        goto done;
    }
#endif
    if (occurrence_fold_mode) {
        stage = "occurrence-fold-execution";
#if PP_CURSOR_GENERATED_HAS_OCCURRENCE_FOLD
        PPOccurrenceFoldV1Backend backend;

#if PP_CURSOR_GENERATED_HAS_INDEXED_CHECKER_EFFECTS
        if (!ppoccurrence_file_resolver_v1_configure(
                &source_resolver,
                &program, &occurrence_fold_plan,
                argv[1], input, input_len,
                PPGUARDED_LEX_CURSOR_V1_EXACT_TRACE,
                work_limit, UINT64_C(0), UINT32_C(4096),
                error, sizeof(error))) {
            goto done;
        }
        source_resolver_interface =
            ppoccurrence_file_resolver_v1_interface(
                &source_resolver);
        if (!ppindexed_inference_v1_fold_init(
                &indexed_inference_fold,
                &program, &occurrence_fold_plan,
                &indexed_checker_effect_plan,
                &source_resolver_interface,
                &indexed_inference_state,
                error, sizeof(error))) {
            goto done;
        }
        indexed_inference_fold_initialized = true;
        backend = ppindexed_inference_v1_fold_backend(
            &indexed_inference_fold);
#else
        ppoccurrence_fold_v1_trace_init(
            &occurrence_fold_trace, &occurrence_fold_plan);
        backend = ppoccurrence_fold_v1_trace_backend(
            &occurrence_fold_trace);
#endif
#if PP_CURSOR_GENERATED_HAS_OCCURRENCE_SPAN_MASK
        if (!ppoccurrence_fold_v1_run_bytes_with_span_mask(
                &program, &occurrence_fold_plan,
                &occurrence_span_mask_plan,
                input, input_len, &backend,
                PPGUARDED_LEX_CURSOR_V1_EXACT_TRACE, work_limit,
                &occurrence_fold_receipt, error, sizeof(error))) {
            goto done;
        }
#else
        if (!ppoccurrence_fold_v1_run_bytes(
                &program, &occurrence_fold_plan,
                input, input_len, &backend,
                PPGUARDED_LEX_CURSOR_V1_EXACT_TRACE, work_limit,
                &occurrence_fold_receipt, error, sizeof(error))) {
            goto done;
        }
#endif
#if PP_CURSOR_GENERATED_HAS_INDEXED_CHECKER_EFFECTS
        occurrence_fold_trace =
            indexed_inference_fold.occurrence_trace;
        indexed_inference_summary_built =
            indexed_inference_fold.receipt.committed &&
            ppindexed_inference_v1_state_summary(
                &indexed_inference_state,
                &indexed_inference_summary);
        if (indexed_inference_fold.receipt.committed &&
            !indexed_inference_summary_built) {
            (void)snprintf(
                error, sizeof(error),
                "committed indexed inference state has no summary");
            goto done;
        }
#endif
        receipt = occurrence_fold_receipt.parser_receipt;
#endif
    } else if (events_only) {
        PPGuardedLexCursorV1EventSink sink;
        PPGuardedLexCursorV1EventSink masked_sink;
        PPGuardedLexCursorV1EventSink rejecting_sink;
        CursorEventAudit masked_audit = {0};
        CursorEventAudit rejecting_audit = {0};
        PPGuardedLexCursorV1Receipt masked_receipt = {0};
        PPGuardedLexCursorV1Receipt rejected_receipt = {0};

        event_audit.program = &program;
        event_audit.input_byte_len = input_len;
        sink = (PPGuardedLexCursorV1EventSink){
            .context = &event_audit,
            .emit = audit_cursor_event,
        };
        if (!ppguarded_lex_cursor_v1_program_run_bytes(
                &program, input, input_len, work_limit,
                &ordinary_receipt, error, sizeof(error)) ||
            !ppguarded_lex_cursor_v1_program_run_events_bytes(
                &program, input, input_len, &sink,
                PPGUARDED_LEX_CURSOR_V1_EXACT_TRACE, work_limit,
                &receipt, error, sizeof(error)) ||
            !receipts_equal(&ordinary_receipt, &receipt) ||
            event_audit.invalid ||
            event_audit.shift_len != receipt.shift_len ||
            event_audit.reduce_len != receipt.reduce_len ||
            event_audit.accept_len !=
                (receipt.outcome == PPGUARDED_LEX_CURSOR_V1_ACCEPTED
                    ? 1u : 0u) ||
            event_audit.max_right_scalar > receipt.input_scalar_len) {
            goto done;
        }
        stage = "cursor-event-liveness";
        masked_shift_events = calloc(
            program.terminal_len ? program.terminal_len : 1u,
            sizeof(*masked_shift_events));
        masked_reduction_events = calloc(
            program.final_slr.production_len
                ? program.final_slr.production_len : 1u,
            sizeof(*masked_reduction_events));
        if (!masked_shift_events || !masked_reduction_events)
            goto done;
        masked_audit.program = &program;
        masked_audit.input_byte_len = input_len;
        masked_sink = (PPGuardedLexCursorV1EventSink){
            .context = &masked_audit,
            .emit = audit_cursor_event,
            .shift_event_live = masked_shift_events,
            .shift_event_live_len = program.terminal_len,
            .reduction_event_live = masked_reduction_events,
            .reduction_event_live_len =
                program.final_slr.production_len,
        };
        if (!ppguarded_lex_cursor_v1_program_run_events_bytes(
                &program, input, input_len, &masked_sink,
                PPGUARDED_LEX_CURSOR_V1_EXACT_TRACE, work_limit,
                &masked_receipt, error, sizeof(error)) ||
            !receipts_equal(&ordinary_receipt, &masked_receipt) ||
            masked_audit.invalid || masked_audit.shift_len != 0u ||
            masked_audit.reduce_len != 0u ||
            masked_audit.accept_len !=
                (masked_receipt.outcome ==
                     PPGUARDED_LEX_CURSOR_V1_ACCEPTED ? 1u : 0u)) {
            goto done;
        }
        stage = "cursor-event-liveness-domain-mutation";
        error[0] = '\0';
        if (program.terminal_len > 0u) {
            masked_sink.shift_event_live_len--;
        } else if (program.final_slr.production_len > 0u) {
            masked_sink.reduction_event_live_len--;
        } else {
            (void)snprintf(
                error, sizeof(error),
                "generated cursor has no event-liveness domain");
            goto done;
        }
        if (ppguarded_lex_cursor_v1_program_run_events_bytes(
                &program, input, input_len, &masked_sink,
                PPGUARDED_LEX_CURSOR_V1_COUNTERS_ONLY, work_limit,
                &rejected_receipt, error, sizeof(error)) ||
            error[0] == '\0') {
            goto done;
        }
        error[0] = '\0';
        stage = "cursor-event-consumer-mutation";
        rejecting_audit.program = &program;
        rejecting_audit.input_byte_len = input_len;
        rejecting_audit.reject_first = true;
        rejecting_sink = (PPGuardedLexCursorV1EventSink){
            .context = &rejecting_audit,
            .emit = audit_cursor_event,
        };
        error[0] = '\0';
        if (ppguarded_lex_cursor_v1_program_run_events_bytes(
                &program, input, input_len, &rejecting_sink,
                PPGUARDED_LEX_CURSOR_V1_COUNTERS_ONLY, work_limit,
                &rejected_receipt, error, sizeof(error)) ||
            rejecting_audit.event_len != 0u ||
            error[0] == '\0') {
            goto done;
        }
        error[0] = '\0';
    } else if (!recognition_only && program.value_programs_complete) {
        if (!ppguarded_lex_cursor_v1_program_run_semantic_direct_bytes(
                &program, input, input_len, work_limit,
                &result, error, sizeof(error))) {
            goto done;
        }
        receipt = result.receipt;
    } else if (!ppguarded_lex_cursor_v1_program_run_bytes(
                   &program, input, input_len, work_limit,
                   &receipt, error, sizeof(error))) {
        goto done;
    }
    for (terminal_index = 0u;
         terminal_index < program.terminal_len;
         terminal_index++) {
        if (program.terminals[terminal_index].semantic_value_required)
            semantic_required_sources++;
    }
    if (program.dfa.state_len == 0u || !program.dfa.states)
        goto done;
    stage = "cursor-plan-mutation";
    saved_transition_begin = program.dfa.states[0].transition_begin;
    program.dfa.states[0].transition_begin ^= UINT32_C(1);
    if (!ppguarded_lex_cursor_v1_program_validate(
            &program, error, sizeof(error))) {
        mutation_killed = 1u;
    }
    program.dfa.states[0].transition_begin = saved_transition_begin;
    if (!mutation_killed ||
        !ppguarded_lex_cursor_v1_program_validate(
            &program, error, sizeof(error))) {
        goto done;
    }
#if PP_CURSOR_GENERATED_HAS_OCCURRENCE_FOLD
    stage = "occurrence-fold-plan-mutation";
    if (occurrence_fold_mode) {
        uint32_t source_index;
        uint32_t saved_binding;

        if (occurrence_fold_plan.terminal_len == 0u)
            goto done;
        source_index =
            occurrence_fold_plan.terminals[0].source_index;
        saved_binding =
            occurrence_fold_plan.terminal_binding_by_source[
                source_index];
        occurrence_fold_plan.terminal_binding_by_source[
            source_index] = UINT32_MAX;
        error[0] = '\0';
        if (!ppoccurrence_fold_v1_plan_validate_program(
                &program, &occurrence_fold_plan,
                error, sizeof(error))) {
            occurrence_fold_mutation_killed = 1u;
        }
        occurrence_fold_plan.terminal_binding_by_source[
            source_index] = saved_binding;
        error[0] = '\0';
        if (!occurrence_fold_mutation_killed ||
            !ppoccurrence_fold_v1_plan_validate_program(
                &program, &occurrence_fold_plan,
                error, sizeof(error))) {
            goto done;
        }
    }
#endif
#if PP_CURSOR_GENERATED_HAS_OCCURRENCE_SPAN_MASK
    stage = "occurrence-span-mask-plan-mutation";
    if (occurrence_fold_mode) {
        uint32_t bound_index = 0u;
        uint32_t saved_tag;

        if (occurrence_span_mask_plan.terminal_len == 0u)
            goto done;
        while (bound_index < occurrence_span_mask_plan.terminal_len &&
               occurrence_span_mask_plan.terminal_tags[bound_index] ==
                   UINT32_MAX) {
            bound_index++;
        }
        if (bound_index == occurrence_span_mask_plan.terminal_len)
            goto done;
        saved_tag =
            occurrence_span_mask_plan.terminal_tags[bound_index];
        occurrence_span_mask_plan.terminal_tags[bound_index] =
            UINT32_MAX;
        error[0] = '\0';
        if (!ppoccurrence_span_mask_v1_plan_validate(
                &program, &occurrence_fold_plan,
                &occurrence_span_mask_plan,
                error, sizeof(error))) {
            occurrence_span_mask_mutation_killed = 1u;
        }
        occurrence_span_mask_plan.terminal_tags[bound_index] =
            saved_tag;
        error[0] = '\0';
        if (!occurrence_span_mask_mutation_killed ||
            !ppoccurrence_span_mask_v1_plan_validate(
                &program, &occurrence_fold_plan,
                &occurrence_span_mask_plan,
                error, sizeof(error))) {
            goto done;
        }
    }
#endif
#if PP_CURSOR_GENERATED_HAS_INDEXED_CHECKER_EFFECTS
    if (occurrence_fold_mode) {
        PPIndexedCheckerEffectV1Kind saved_effect;
        bool saved_policy;

        if (indexed_checker_effect_plan.operation_len == 0u)
            goto done;
        saved_effect =
            indexed_checker_effect_plan.effect_by_operation[0];
        indexed_checker_effect_plan.effect_by_operation[0] =
            (PPIndexedCheckerEffectV1Kind)(
                ((uint32_t)saved_effect + 1u) %
                PPINDEXED_CHECKER_EFFECT_V1_COUNT);
        error[0] = '\0';
        if (!ppindexed_checker_effect_v1_plan_validate(
                &program, &occurrence_fold_plan,
                &indexed_checker_effect_plan,
                error, sizeof(error))) {
            indexed_checker_effect_mutation_killed = 1u;
        }
        indexed_checker_effect_plan.effect_by_operation[0] =
            saved_effect;
        error[0] = '\0';
        if (!indexed_checker_effect_mutation_killed ||
            !ppindexed_checker_effect_v1_plan_validate(
                &program, &occurrence_fold_plan,
                &indexed_checker_effect_plan,
                error, sizeof(error))) {
            goto done;
        }
        saved_policy =
            indexed_checker_effect_plan.policy.reject_unknown_steps;
        indexed_checker_effect_plan.policy.reject_unknown_steps =
            !saved_policy;
        error[0] = '\0';
        if (!ppindexed_checker_effect_v1_plan_validate(
                &program, &occurrence_fold_plan,
                &indexed_checker_effect_plan,
                error, sizeof(error))) {
            indexed_checker_policy_mutation_killed = 1u;
        }
        indexed_checker_effect_plan.policy.reject_unknown_steps =
            saved_policy;
        error[0] = '\0';
        if (!indexed_checker_policy_mutation_killed ||
            !ppindexed_checker_effect_v1_plan_validate(
                &program, &occurrence_fold_plan,
                &indexed_checker_effect_plan,
                error, sizeof(error))) {
            goto done;
        }
    }
#endif

    stage = "result-emission";
    printf("parser-pack-cursor-generated-v1\n");
    printf("program-digest\t%s\n", program.program_digest);
    printf("outcome\t%s\n", outcome_name(receipt.outcome));
    printf("trace-digest\t%s\n", receipt.trace_digest);
    printf("source-passes\t%u\n", receipt.source_pass_count);
    printf("execution-mode\t%s\n",
           occurrence_fold_mode ? "occurrence-fold" :
           events_only ? "events" :
           recognition_only ? "recognition" : "semantic");
    printf("tokens\t%u\n", receipt.token_len);
    printf("shifts\t%u\n", receipt.shift_len);
    printf("reductions\t%u\n", receipt.reduce_len);
    printf("epsilon-reductions\t%u\n", receipt.epsilon_reduce_len);
    printf("unit-reductions\t%u\n", receipt.unit_reduce_len);
    printf("reselect-reductions\t%u\n",
           receipt.reselect_reduce_len);
    printf("unit-reselect-reductions\t%u\n",
           receipt.unit_reselect_reduce_len);
    printf("cursor-scans\t%u\n", receipt.cursor_scan_len);
    printf("dfa-work-items\t%llu\n",
           (unsigned long long)receipt.dfa_work_item_len);
    printf("parser-work-items\t%llu\n",
           (unsigned long long)receipt.parser_work_item_len);
    printf("max-stack\t%u\n", receipt.max_stack_len);
    printf("semantic-complete\t%u\n",
           program.value_programs_complete ? 1u : 0u);
    printf("semantic-required-sources\t%u\n",
           semantic_required_sources);
    printf("semantic-result-present\t%u\n",
           result.semantic_result ? 1u : 0u);
    printf("value-program-runs\t%u\n", result.value_program_run_len);
    printf("mutation-killed\t%u\n", mutation_killed);
#if PP_CURSOR_GENERATED_HAS_OCCURRENCE_FOLD
    if (occurrence_fold_mode) {
        printf("occurrence-fold-plan-digest\t%s\n",
               occurrence_fold_plan.plan_digest);
        printf("occurrence-fold-roles\t%u\n",
               occurrence_fold_plan.role_len);
        printf("occurrence-fold-nodes\t%u\n",
               occurrence_fold_plan.node_len);
        printf("occurrence-fold-operations\t%u\n",
               occurrence_fold_plan.operation_len);
        printf("occurrence-fold-terminal-bindings\t%u\n",
               occurrence_fold_plan.terminal_len);
        printf("occurrence-fold-production-bindings\t%u\n",
               occurrence_fold_plan.production_len);
        printf("occurrence-fold-steps\t%u\n",
               occurrence_fold_receipt.step_len);
        printf("occurrence-fold-shift-steps\t%u\n",
               occurrence_fold_receipt.shift_step_len);
        printf("occurrence-fold-reduce-steps\t%u\n",
               occurrence_fold_receipt.reduce_step_len);
        printf("occurrence-fold-values\t%u\n",
               occurrence_fold_receipt.value_len);
        printf("occurrence-fold-max-pending-values\t%u\n",
               occurrence_fold_receipt.max_pending_value_len);
        printf("occurrence-fold-committed\t%u\n",
               occurrence_fold_receipt.committed ? 1u : 0u);
        printf("occurrence-fold-trace-digest\t%s\n",
               occurrence_fold_trace.digest);
        printf("occurrence-fold-mutation-killed\t%u\n",
               occurrence_fold_mutation_killed);
#if PP_CURSOR_GENERATED_HAS_OCCURRENCE_SPAN_MASK
        printf("occurrence-span-mask-plan-digest\t%s\n",
               occurrence_span_mask_plan.plan_digest);
        printf("occurrence-span-mask-bound-terminals\t%u\n",
               occurrence_span_mask_plan.bound_terminal_len);
        printf("semantic-occurrence-projections\t%u\n",
               occurrence_fold_receipt
                   .semantic_occurrence_projection_len);
        printf("semantic-occurrence-projection-work-items\t%llu\n",
               (unsigned long long)occurrence_fold_receipt
                   .semantic_occurrence_projection_work_item_len);
        printf("occurrence-span-mask-mutation-killed\t%u\n",
               occurrence_span_mask_mutation_killed);
#endif
    }
#endif
#if PP_CURSOR_GENERATED_HAS_INDEXED_CHECKER_EFFECTS
    if (occurrence_fold_mode) {
        printf("indexed-checker-effect-plan-digest\t%s\n",
               indexed_checker_effect_plan.plan_digest);
        printf("indexed-checker-effect-operations\t%u\n",
               indexed_checker_effect_plan.operation_len);
        printf("indexed-checker-effect-kinds\t%u\n",
               indexed_checker_effect_kind_len(
                   &indexed_checker_effect_plan));
        printf("indexed-checker-effect-mutation-killed\t%u\n",
               indexed_checker_effect_mutation_killed);
        printf("indexed-checker-policy-mutation-killed\t%u\n",
               indexed_checker_policy_mutation_killed);
        printf("indexed-inference-committed\t%u\n",
               indexed_inference_fold.receipt.committed ? 1u : 0u);
        printf("indexed-inference-state-digest\t%s\n",
               indexed_inference_fold.receipt.committed
                   ? indexed_inference_fold.receipt.state_digest : "");
        printf("indexed-inference-occurrence-digest\t%s\n",
               indexed_inference_fold.receipt.committed
                   ? indexed_inference_fold.receipt.occurrence_digest : "");
        printf("indexed-inference-effects\t%u\n",
               indexed_inference_fold.receipt.effect_len);
        printf("indexed-inference-scope-opens\t%u\n",
               indexed_inference_fold.receipt.scope_open_len);
        printf("indexed-inference-scope-closes\t%u\n",
               indexed_inference_fold.receipt.scope_close_len);
        printf("indexed-inference-scope-completions\t%u\n",
               indexed_inference_fold.receipt.scope_complete_len);
        printf("indexed-inference-proof-steps\t%u\n",
               indexed_inference_fold.receipt.proof_step_len);
        printf("indexed-inference-rule-applications\t%u\n",
               indexed_inference_fold.receipt.proof_application_len);
        printf("indexed-inference-unknown-steps\t%u\n",
               indexed_inference_fold.receipt.unknown_step_len);
        printf("indexed-inference-proof-digest\t%s\n",
               indexed_inference_fold.receipt.committed
                   ? indexed_inference_fold.receipt.proof_digest : "");
        printf("indexed-inference-source-digest\t%s\n",
               indexed_inference_fold.receipt.committed
                   ? indexed_inference_fold.receipt.source_digest : "");
        printf("indexed-inference-sources\t%u\n",
               source_resolver.source_len);
        printf("indexed-inference-skipped-sources\t%u\n",
               source_resolver.skipped_source_len);
        printf("indexed-inference-source-max-depth\t%u\n",
               source_resolver.max_depth);
        printf("indexed-inference-atoms\t%u\n",
               indexed_inference_summary_built
                   ? indexed_inference_summary.atom_len : 0u);
        printf("indexed-inference-objects\t%u\n",
               indexed_inference_summary_built
                   ? indexed_inference_summary.object_len : 0u);
        printf("indexed-inference-constants\t%u\n",
               indexed_inference_summary_built
                   ? indexed_inference_summary.constant_len : 0u);
        printf("indexed-inference-variables\t%u\n",
               indexed_inference_summary_built
                   ? indexed_inference_summary.variable_len : 0u);
        printf("indexed-inference-hypotheses\t%u\n",
               indexed_inference_summary_built
                   ? indexed_inference_summary.hypothesis_len : 0u);
        printf("indexed-inference-rules\t%u\n",
               indexed_inference_summary_built
                   ? indexed_inference_summary.rule_len : 0u);
        printf("indexed-inference-active-hypotheses\t%u\n",
               indexed_inference_summary_built
                   ? indexed_inference_summary.active_hypothesis_len : 0u);
        printf("indexed-inference-active-distinct\t%u\n",
               indexed_inference_summary_built
                   ? indexed_inference_summary.active_distinct_len : 0u);
        printf("indexed-inference-scope-depth\t%u\n",
               indexed_inference_summary_built
                   ? indexed_inference_summary.scope_depth : 0u);
    }
#endif
    if (events_only) {
        printf("event-shifts\t%u\n", event_audit.shift_len);
        printf("event-reductions\t%u\n", event_audit.reduce_len);
        printf("event-accepts\t%u\n", event_audit.accept_len);
        printf("event-consumer-mutation-killed\t1\n");
    }
    if (!suppress_semantic_render && !write_term(result.semantic_result))
        goto done;
    printf("end\n");
    ok = true;

done:
    if (!ok)
        fprintf(stderr, "generated cursor test failed at %s: %s\n",
                stage, error[0] ? error : "unknown failure");
    free(input);
    free(masked_reduction_events);
    free(masked_shift_events);
    ppguarded_lex_cursor_v1_semantic_result_free(&result);
    ppguarded_lex_cursor_v1_dispatch_index_free(&dispatch_index);
#if PP_CURSOR_GENERATED_HAS_OCCURRENCE_FOLD
    ppoccurrence_fold_v1_plan_free(&occurrence_fold_plan);
#endif
#if PP_CURSOR_GENERATED_HAS_OCCURRENCE_SPAN_MASK
    ppoccurrence_span_mask_v1_plan_free(
        &occurrence_span_mask_plan);
#endif
#if PP_CURSOR_GENERATED_HAS_INDEXED_CHECKER_EFFECTS
    if (indexed_inference_fold_initialized)
        ppindexed_inference_v1_fold_free(&indexed_inference_fold);
    ppoccurrence_file_resolver_v1_free(&source_resolver);
    ppindexed_inference_v1_state_free(&indexed_inference_state);
    ppindexed_checker_effect_v1_plan_free(
        &indexed_checker_effect_plan);
#endif
    ppguarded_lex_cursor_v1_program_free(&program);
    symbol_table_free(&symbols);
    g_symbols = NULL;
    return ok &&
        (!verdict_exit ||
         receipt.outcome == PPGUARDED_LEX_CURSOR_V1_ACCEPTED)
        ? 0 : 1;
}
