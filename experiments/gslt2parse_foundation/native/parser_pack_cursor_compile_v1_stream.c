#define _POSIX_C_SOURCE 200809L

#include "finite_horn_answer_stream_v1.h"
#include "parser_occurrence_fold_v1.h"
#include "parser_occurrence_span_mask_v1.h"
#include "parser_source_resolution_control_v1.h"
#include "parser_pack_abi_stream_v1.h"
#include "parser_pack_cursor_c_emitter_v1.h"
#include "parser_pack_guard_evidence_stream_v1.h"
#include "parser_pack_guarded_lexical_exec_v1.h"
#include "relational_state_program_v1.h"

#include "symbol.h"

#include <errno.h>
#include <limits.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

typedef struct {
    const char *abi;
    const char *lexical_answers;
    const char *guard_answers;
    const char *guard_evidence;
    const char *guarded_answers;
    const char *regular_compiler_digest;
    const char *guarded_compiler_digest;
    const char *action_answers;
    const char *action_compiler_digest;
    const char *occurrence_fold_answers;
    const char *occurrence_span_mask_answers;
    const char *occurrence_span_mask_compiler_digest;
    const char *state_answers;
    const char *source_control_answers;
    const char *source_control_compiler_digest;
    const char *output_c;
    const char *identifier_prefix;
} PPCursorCompileV1Options;

static bool set_error(char *error, size_t error_size, const char *message) {
    if (error && error_size > 0u)
        (void)snprintf(error, error_size, "%s", message);
    return false;
}

static bool assign_option(const char **slot, const char *value,
                          char *error, size_t error_size) {
    if (*slot)
        return set_error(error, error_size,
                         "cursor compiler option is repeated");
    *slot = value;
    return true;
}

static bool read_options(int argc, char **argv,
                         PPCursorCompileV1Options *options,
                         char *error, size_t error_size) {
    int index;

    memset(options, 0, sizeof(*options));
    for (index = 1; index < argc; index++) {
        const char *option = argv[index];
        const char *value;
        const char **slot = NULL;

        if (index + 1 >= argc)
            return set_error(error, error_size,
                             "cursor compiler option lacks a value");
        value = argv[++index];
        if (strcmp(option, "--abi") == 0)
            slot = &options->abi;
        else if (strcmp(option, "--lexical-answers") == 0)
            slot = &options->lexical_answers;
        else if (strcmp(option, "--guard-answers") == 0)
            slot = &options->guard_answers;
        else if (strcmp(option, "--guard-evidence") == 0)
            slot = &options->guard_evidence;
        else if (strcmp(option, "--guarded-answers") == 0)
            slot = &options->guarded_answers;
        else if (strcmp(option, "--regular-compiler-digest") == 0)
            slot = &options->regular_compiler_digest;
        else if (strcmp(option, "--guarded-compiler-digest") == 0)
            slot = &options->guarded_compiler_digest;
        else if (strcmp(option, "--action-answers") == 0)
            slot = &options->action_answers;
        else if (strcmp(option, "--action-compiler-digest") == 0)
            slot = &options->action_compiler_digest;
        else if (strcmp(option, "--occurrence-fold-answers") == 0)
            slot = &options->occurrence_fold_answers;
        else if (strcmp(option, "--occurrence-span-mask-answers") == 0)
            slot = &options->occurrence_span_mask_answers;
        else if (strcmp(
                     option,
                     "--occurrence-span-mask-compiler-digest") == 0)
            slot = &options->occurrence_span_mask_compiler_digest;
        else if (strcmp(option, "--state-answers") == 0)
            slot = &options->state_answers;
        else if (strcmp(option, "--source-control-answers") == 0)
            slot = &options->source_control_answers;
        else if (strcmp(
                     option,
                     "--source-control-compiler-digest") == 0)
            slot = &options->source_control_compiler_digest;
        else if (strcmp(option, "--out-c") == 0)
            slot = &options->output_c;
        else if (strcmp(option, "--prefix") == 0)
            slot = &options->identifier_prefix;
        else
            return set_error(error, error_size,
                             "cursor compiler option is unknown");
        if (!assign_option(slot, value, error, error_size))
            return false;
    }
    if (!options->abi || !options->lexical_answers ||
        !options->guard_answers || !options->guard_evidence ||
        !options->guarded_answers ||
        !options->regular_compiler_digest ||
        !options->guarded_compiler_digest ||
        !options->action_answers || !options->action_compiler_digest ||
        !options->occurrence_fold_answers || !options->output_c ||
        !options->identifier_prefix) {
        return set_error(error, error_size,
                         "cursor compiler omits a required option");
    }
    if ((options->occurrence_span_mask_answers != NULL) !=
        (options->occurrence_span_mask_compiler_digest != NULL)) {
        return set_error(
            error, error_size,
            "cursor compiler occurrence span-mask options are incomplete");
    }
    if ((options->source_control_answers != NULL) !=
        (options->source_control_compiler_digest != NULL)) {
        return set_error(
            error, error_size,
            "cursor compiler source-control options are incomplete");
    }
    return true;
}

static bool emit_atomic(
    const PPGuardedLexCursorV1Program *program,
    const PPOccurrenceFoldV1Plan *fold_plan,
    const PPOccurrenceSpanMaskV1Plan *span_mask_plan,
    const PPRelationalStateProgramV1Plan *state_plan,
    const PPSourceResolutionControlV1Plan *source_control_plan,
    const char *output_path, const char *identifier_prefix,
    char *error, size_t error_size) {
    char temporary[PATH_MAX] = {0};
    int descriptor = -1;
    FILE *stream = NULL;
    bool emitted = false;

    if (snprintf(temporary, sizeof(temporary), "%s.tmp.XXXXXX",
                 output_path) >= (int)sizeof(temporary)) {
        return set_error(error, error_size,
                         "generated cursor path is too long");
    }
    descriptor = mkstemp(temporary);
    if (descriptor < 0) {
        (void)snprintf(error, error_size,
                       "cannot create generated cursor temporary: %s",
                       strerror(errno));
        return false;
    }
    stream = fdopen(descriptor, "wb");
    if (!stream) {
        (void)snprintf(error, error_size,
                       "cannot open generated cursor temporary: %s",
                       strerror(errno));
        (void)close(descriptor);
        (void)unlink(temporary);
        return false;
    }
    descriptor = -1;
    emitted = ppguarded_lex_cursor_v1_emit_c(
        program, stream, identifier_prefix, error, error_size);
    if (emitted) {
        emitted = ppoccurrence_fold_v1_emit_c(
            program, fold_plan, stream, identifier_prefix,
            error, error_size);
    }
    if (emitted && span_mask_plan) {
        emitted = ppoccurrence_span_mask_v1_emit_c(
            program, fold_plan, span_mask_plan, stream,
            identifier_prefix, error, error_size);
    }
    if (emitted && state_plan) {
        emitted = pprelational_state_program_v1_emit_c(
            fold_plan, state_plan, stream, identifier_prefix,
            error, error_size);
    }
    if (emitted && source_control_plan) {
        emitted = ppsource_resolution_control_v1_emit_c(
            source_control_plan, stream, identifier_prefix,
            error, error_size);
    }
    if (!emitted || fflush(stream) != 0 || fsync(fileno(stream)) != 0 ||
        fclose(stream) != 0) {
        stream = NULL;
        if (!error[0]) {
            (void)snprintf(error, error_size,
                           "cannot finish generated cursor output: %s",
                           strerror(errno));
        }
        (void)unlink(temporary);
        return false;
    }
    stream = NULL;
    if (rename(temporary, output_path) != 0) {
        (void)snprintf(error, error_size,
                       "cannot publish generated cursor output: %s",
                       strerror(errno));
        (void)unlink(temporary);
        return false;
    }
    return true;
}

static bool compile_cursor(const PPCursorCompileV1Options *options,
                           char *error, size_t error_size) {
    const PPGuardedLexExecV1Limits limits = {
        .dfa_state_limit = UINT32_C(65536),
        .dfa_transition_limit = UINT32_C(2000000),
        .scan_work_limit = UINT64_C(20000000),
        .scan_token_limit = UINT32_C(2000000),
        .witness_work_limit = UINT32_C(10000000),
        .parse_work_limit = UINT32_C(50000000),
        .replay_depth = UINT32_C(4096),
        .result_limit = UINT32_C(1000000),
    };
    PPABIV1Wire pack_wire;
    PPABIV1Pack pack;
    FHAnswerStreamV1 lexical_answers;
    FHAnswerStreamV1 guard_answers;
    FHAnswerStreamV1 guarded_answers;
    FHAnswerStreamV1 action_answers;
    FHAnswerStreamV1 fold_answers;
    FHAnswerStreamV1 span_mask_answers;
    FHAnswerStreamV1 state_answers;
    FHAnswerStreamV1 source_control_answers;
    RSNFAV1Plan lexical_nfa;
    RSNFAV1Plan guard_nfa;
    PPLexV1Plan lexical_plan;
    PPGuardEvidenceWireV1 evidence;
    PPGuardPlanV1 guard_plan;
    PPGuardPlanV1ProvenanceInput provenance;
    PPGuardedLexV1Plan guarded_plan;
    PPGuardedLexExecV1Plan execution_plan;
    PPGuardedLexCursorV1Program program;
    PPOccurrenceFoldV1Plan fold_plan;
    PPOccurrenceSpanMaskV1Plan span_mask_plan;
    PPRelationalStateProgramV1Plan state_plan;
    PPSourceResolutionControlV1Plan source_control_plan;
    bool ok = false;

    ppabi_v1_wire_init(&pack_wire);
    ppabi_v1_pack_init(&pack);
    fh_answer_stream_v1_init(&lexical_answers);
    fh_answer_stream_v1_init(&guard_answers);
    fh_answer_stream_v1_init(&guarded_answers);
    fh_answer_stream_v1_init(&action_answers);
    fh_answer_stream_v1_init(&fold_answers);
    fh_answer_stream_v1_init(&span_mask_answers);
    fh_answer_stream_v1_init(&state_answers);
    fh_answer_stream_v1_init(&source_control_answers);
    rsnfa_v1_plan_init(&lexical_nfa);
    rsnfa_v1_plan_init(&guard_nfa);
    pplex_v1_plan_init(&lexical_plan);
    ppguard_evidence_wire_v1_init(&evidence);
    ppguard_plan_v1_init(&guard_plan);
    ppguarded_lex_v1_plan_init(&guarded_plan);
    ppguarded_lex_exec_v1_plan_init(&execution_plan);
    ppguarded_lex_cursor_v1_program_init(&program);
    ppoccurrence_fold_v1_plan_init(&fold_plan);
    ppoccurrence_span_mask_v1_plan_init(&span_mask_plan);
    pprelational_state_program_v1_plan_init(&state_plan);
    ppsource_resolution_control_v1_plan_init(&source_control_plan);

    if (!ppabi_v1_wire_read(
            &pack_wire, options->abi, error, error_size) ||
        !ppabi_v1_wire_load_pack(
            &pack_wire, &pack, error, error_size) ||
        !fh_answer_stream_v1_read(
            &lexical_answers, options->lexical_answers,
            error, error_size) ||
        lexical_answers.len == 0u ||
        lexical_answers.len > UINT32_MAX ||
        !rsnfa_v1_plan_load(
            &pack, lexical_answers.terms, lexical_answers.len,
            &lexical_nfa, error, error_size) ||
        !pplex_v1_plan_build(
            &pack, lexical_nfa.tags, lexical_nfa.nfa.tag_len,
            options->regular_compiler_digest,
            lexical_answers.digest, &lexical_plan,
            error, error_size) ||
        !fh_answer_stream_v1_read(
            &guard_answers, options->guard_answers,
            error, error_size) ||
        guard_answers.len > UINT32_MAX ||
        (guard_answers.len > 0u &&
         !rsnfa_v1_plan_load(
             &pack, guard_answers.terms, guard_answers.len,
             &guard_nfa, error, error_size)) ||
        !ppguard_evidence_wire_v1_read(
            &evidence, options->guard_evidence,
            error, error_size) ||
        !fh_answer_stream_v1_read(
            &guarded_answers, options->guarded_answers,
            error, error_size)) {
        goto done;
    }

    provenance = (PPGuardPlanV1ProvenanceInput){
        .source_digest = evidence.source_digest,
        .pre_reflection_digest = evidence.pre_reflection_digest,
        .environment_digest = evidence.environment_digest,
        .answer_set_digest = evidence.answer_set_digest,
        .regular_compiler_digest = options->regular_compiler_digest,
        .guard_nfa_answer_digest = guard_answers.digest,
        .guard_nfa_tags = guard_nfa.tags,
        .guard_nfa_tag_len = guard_nfa.nfa.tag_len,
        .derivations = evidence.derivations,
        .derivation_len = evidence.derivation_len,
    };
    if (!ppguard_plan_v1_build(
            &pack, &lexical_plan, &provenance, &guard_plan,
            error, error_size) ||
        !ppguarded_lex_v1_plan_build(
            &pack, &lexical_plan, &guard_plan,
            guarded_answers.terms, guarded_answers.len,
            options->guarded_compiler_digest,
            guarded_answers.digest, &guarded_plan,
            error, error_size) ||
        !ppguarded_lex_exec_v1_plan_build(
            &pack, pack_wire.start, &lexical_plan,
            &guard_plan, &guarded_plan, &lexical_nfa, &guard_nfa,
            &limits, &execution_plan, error, error_size)) {
        goto done;
    }
    if (!execution_plan.cursor_certificate.eligible) {
        set_error(error, error_size,
                  "execution plan is not eligible for cursor emission");
        goto done;
    }
    if (!ppguarded_lex_cursor_v1_program_build(
            &pack, pack_wire.start, &lexical_plan,
            &guard_plan, &guarded_plan, &execution_plan,
            &program, error, error_size) ||
        !fh_answer_stream_v1_read(
            &action_answers, options->action_answers,
            error, error_size) ||
        !ppguarded_lex_cursor_v1_program_bind_actions(
            &program, &pack, &lexical_plan, &guard_plan,
            &guarded_plan, action_answers.terms,
            action_answers.len, options->action_compiler_digest,
            action_answers.digest, error, error_size) ||
        !ppguarded_lex_cursor_v1_program_validate_bound(
            &program, &pack, &lexical_plan, &guard_plan,
            &guarded_plan, error, error_size)) {
        goto done;
    }
    if (!program.actions_bound || !program.value_programs_complete) {
        set_error(error, error_size,
                  "cursor action program is not complete");
        goto done;
    }
    if (!fh_answer_stream_v1_read(
            &fold_answers, options->occurrence_fold_answers,
            error, error_size) ||
        !ppoccurrence_fold_v1_plan_build(
            &pack, &lexical_plan, &program,
            fold_answers.terms, fold_answers.len,
            fold_answers.digest, &fold_plan,
            error, error_size) ||
        !ppoccurrence_fold_v1_plan_validate(
            &pack, &lexical_plan, &program, &fold_plan,
            error, error_size)) {
        goto done;
    }
    if (options->occurrence_span_mask_answers &&
        (!fh_answer_stream_v1_read(
             &span_mask_answers,
             options->occurrence_span_mask_answers,
             error, error_size) ||
         !ppoccurrence_span_mask_v1_plan_build(
             &pack, &program, &fold_plan,
             span_mask_answers.terms, span_mask_answers.len,
             options->occurrence_span_mask_compiler_digest,
             span_mask_answers.digest,
             limits.dfa_state_limit, limits.dfa_transition_limit,
             &span_mask_plan, error, error_size) ||
         !ppoccurrence_span_mask_v1_plan_validate(
             &program, &fold_plan, &span_mask_plan,
             error, error_size))) {
        goto done;
    }
    if (options->state_answers &&
        (!fh_answer_stream_v1_read(
             &state_answers, options->state_answers,
             error, error_size) ||
         !pprelational_state_program_v1_plan_build(
             &fold_plan, state_answers.terms, state_answers.len,
             state_answers.digest, &state_plan,
             error, error_size) ||
         !pprelational_state_program_v1_plan_validate(
             &fold_plan, &state_plan, error, error_size))) {
        goto done;
    }
    if (options->source_control_answers &&
        (!fh_answer_stream_v1_read(
             &source_control_answers,
             options->source_control_answers,
             error, error_size) ||
         !ppsource_resolution_control_v1_plan_build(
             source_control_answers.terms,
             source_control_answers.len,
             options->source_control_compiler_digest,
             source_control_answers.digest,
             &source_control_plan, error, error_size) ||
         !ppsource_resolution_control_v1_plan_validate(
             &source_control_plan, error, error_size))) {
        goto done;
    }
    if (!emit_atomic(&program, &fold_plan,
                     options->occurrence_span_mask_answers
                         ? &span_mask_plan : NULL,
                     options->state_answers ? &state_plan : NULL,
                     options->source_control_answers
                         ? &source_control_plan : NULL,
                     options->output_c,
                     options->identifier_prefix,
                     error, error_size)) {
        goto done;
    }

    printf("parser-pack-cursor-compile-v1\n");
    printf("source-digest\t%s\n", pack_wire.source_digest);
    printf("pack-digest\t%s\n", pack.pack_digest);
    printf("lexical-plan-digest\t%s\n", lexical_plan.plan_digest);
    printf("guard-plan-digest\t%s\n", guard_plan.plan_digest);
    printf("guarded-plan-digest\t%s\n", guarded_plan.plan_digest);
    printf("cursor-program-digest\t%s\n", program.program_digest);
    printf("action-answer-digest\t%s\n", action_answers.digest);
    printf("occurrence-fold-plan-digest\t%s\n", fold_plan.plan_digest);
    if (options->occurrence_span_mask_answers)
        printf("occurrence-span-mask-plan-digest\t%s\n",
               span_mask_plan.plan_digest);
    if (options->state_answers)
        printf("relational-state-plan-digest\t%s\n",
               state_plan.plan_digest);
    if (options->source_control_answers)
        printf("source-resolution-control-plan-digest\t%s\n",
               source_control_plan.plan_digest);
    printf("terminals\t%u\n", program.terminal_len);
    printf("value-programs\t%u\n", program.value_program_len);
    printf("fold-transitions\t%u\n", fold_plan.transition_len);
    printf("end\n");
    ok = true;

done:
    pprelational_state_program_v1_plan_free(&state_plan);
    ppoccurrence_span_mask_v1_plan_free(&span_mask_plan);
    ppoccurrence_fold_v1_plan_free(&fold_plan);
    ppguarded_lex_cursor_v1_program_free(&program);
    ppguarded_lex_exec_v1_plan_free(&execution_plan);
    ppguarded_lex_v1_plan_free(&guarded_plan);
    ppguard_plan_v1_free(&guard_plan);
    ppguard_evidence_wire_v1_free(&evidence);
    pplex_v1_plan_free(&lexical_plan);
    rsnfa_v1_plan_free(&guard_nfa);
    rsnfa_v1_plan_free(&lexical_nfa);
    fh_answer_stream_v1_free(&fold_answers);
    fh_answer_stream_v1_free(&span_mask_answers);
    fh_answer_stream_v1_free(&state_answers);
    fh_answer_stream_v1_free(&source_control_answers);
    fh_answer_stream_v1_free(&action_answers);
    fh_answer_stream_v1_free(&guarded_answers);
    fh_answer_stream_v1_free(&guard_answers);
    fh_answer_stream_v1_free(&lexical_answers);
    ppabi_v1_pack_free(&pack);
    ppabi_v1_wire_free(&pack_wire);
    return ok;
}

static void usage(const char *program) {
    fprintf(
        stderr,
        "usage: %s --abi FILE --lexical-answers FILE "
        "--guard-answers FILE --guard-evidence FILE "
        "--guarded-answers FILE --regular-compiler-digest SHA256 "
        "--guarded-compiler-digest SHA256 --action-answers FILE "
        "--action-compiler-digest SHA256 "
        "--occurrence-fold-answers FILE "
        "[--occurrence-span-mask-answers FILE "
        "--occurrence-span-mask-compiler-digest SHA256] "
        "[--state-answers FILE] "
        "[--source-control-answers FILE "
        "--source-control-compiler-digest SHA256] "
        "--out-c FILE --prefix NAME\n",
        program);
}

int main(int argc, char **argv) {
    PPCursorCompileV1Options options;
    SymbolTable symbols;
    char error[1024] = {0};
    bool ok;

    if (!read_options(argc, argv, &options, error, sizeof(error))) {
        usage(argv[0]);
        fprintf(stderr, "cursor compilation rejected: %s\n", error);
        return 2;
    }
    symbol_table_init(&symbols);
    symbol_table_init_builtins(&symbols, &g_builtin_syms);
    g_symbols = &symbols;
    g_hashcons = NULL;
    g_var_intern = NULL;
    ok = compile_cursor(&options, error, sizeof(error));
    symbol_table_free(&symbols);
    g_symbols = NULL;
    if (!ok)
        fprintf(stderr, "cursor compilation rejected: %s\n",
                error[0] ? error : "unknown rejection");
    return ok ? 0 : 1;
}
