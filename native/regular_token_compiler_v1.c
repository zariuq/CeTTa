#include "regular_token_compiler_v1.h"
#include "regular_span_nfa_v1.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

extern const uint8_t cetta_regular_token_compiler_v1_prepared[];
extern const size_t cetta_regular_token_compiler_v1_prepared_len;

struct CettaRegularTokenCompilerV1 {
    const PPABIV1Pack *classes;
    CettaDeterministicEquationPlanV1 *equations;
    uint32_t work_limit;
    uint32_t nesting;
    uint64_t remaining;
};

static bool error_message(char *error, size_t size, const char *message) {
    if (error && size) (void)snprintf(error, size, "%s", message);
    return false;
}

static bool form(Atom *value, const char *name, uint32_t arity) {
    return value && value->kind == ATOM_EXPR && value->expr.len == arity + 1u &&
        atom_is_symbol(value->expr.elems[0], name);
}

static Atom *unary(Arena *arena, const char *name, Atom *arg) {
    return atom_expr2(arena, atom_symbol(arena, name), arg);
}

static Atom *difference_state(Arena *arena, Atom *path, uint32_t id) {
    return atom_expr3(arena, atom_symbol(arena, "rn-difference"),
        atom_deep_copy(arena, path), atom_int(arena, id));
}

static CettaDeterministicPrimitiveClassV1 classify(
    void *context, const char *head, uint32_t count) {
    (void)context;
    return count == 3u && !strcmp(head, "rtv1:difference-edges")
        ? CETTA_DETERMINISTIC_PRIMITIVE_CLASS_V1_STRUCTURE
        : CETTA_DETERMINISTIC_PRIMITIVE_CLASS_V1_NONE;
}

static CettaDeterministicPrimitiveResultV1 primitive(
    void *context, const char *head, Atom *const *args, uint32_t count,
    Arena *arena, Atom **out, char *error, size_t error_size) {
    CettaRegularTokenCompilerV1 *compiler = context;
    Arena temporary;
    RSDFAV1Program left, right, difference;
    RSDFAV1BuildOutcome outcome;
    bool ok = false;
    if (count != 3u || strcmp(head, "rtv1:difference-edges"))
        return CETTA_DETERMINISTIC_PRIMITIVE_V1_NOT_HANDLED;
    if (compiler->nesting >= 256u || !compiler->remaining) {
        error_message(error, error_size, "regular-language difference work limit");
        return CETTA_DETERMINISTIC_PRIMITIVE_V1_FAULT;
    }
    arena_init(&temporary);
    temporary.hashcons = NULL;
    rsdfa_v1_program_init(&left);
    rsdfa_v1_program_init(&right);
    rsdfa_v1_program_init(&difference);
    ++compiler->nesting;
    Atom *tag = atom_symbol(&temporary, "difference-operand");
    if (!cetta_regular_token_compiler_v1_program(compiler, &temporary, tag, args[1],
            &left, error, error_size) ||
        !cetta_regular_token_compiler_v1_program(compiler, &temporary, tag, args[2],
            &right, error, error_size) ||
        !rsdfa_v1_program_tag_difference(&left, 0u, &right, 0u,
            compiler->work_limit, compiler->work_limit, &difference, &outcome, error, error_size)) goto done;
    if (outcome != RSDFA_V1_BUILD_COMPLETED ||
        (uint64_t)difference.state_len + difference.transition_len > compiler->remaining) {
        error_message(error, error_size, "regular-language difference product limit"); goto done;
    }
    compiler->remaining -= (uint64_t)difference.state_len + difference.transition_len;
    Atom *result = atom_symbol(arena, "LNil");
    Atom *exit = unary(arena, "rn-out", atom_deep_copy(arena, args[0]));
    for (uint32_t i = difference.state_len; i > 0u; --i) {
        uint32_t id = i - 1u;
        RSDFAV1ProgramState *state = &difference.states[id];
        Atom *from = difference_state(arena, args[0], id);
        if (state->accept_len) {
            Atom *edge = atom_expr3(arena, atom_symbol(arena, "nfa-epsilon"), from, exit);
            result = atom_expr3(arena, atom_symbol(arena, "LCons"), edge, result);
        }
        for (uint32_t j = state->transition_len; j > 0u; --j) {
            RSDFAV1ProgramTransition *transition = &difference.transitions[state->transition_begin + j - 1u];
            Atom *fields[] = {atom_symbol(arena, "nfa-range"), from,
                unary(arena, "cp", atom_int(arena, transition->low)),
                unary(arena, "cp", atom_int(arena, transition->high)),
                difference_state(arena, args[0], transition->target)};
            result = atom_expr3(arena, atom_symbol(arena, "LCons"),
                atom_expr(arena, fields, 5u), result);
        }
    }
    Atom *entry = atom_expr3(arena, atom_symbol(arena, "nfa-epsilon"),
        unary(arena, "rn-in", atom_deep_copy(arena, args[0])),
        difference_state(arena, args[0], difference.start_state));
    *out = atom_expr3(arena, atom_symbol(arena, "LCons"), entry, result);
    ok = true;
done:
    --compiler->nesting;
    rsdfa_v1_program_free(&left);
    rsdfa_v1_program_free(&right);
    rsdfa_v1_program_free(&difference);
    arena_free(&temporary);
    return ok ? CETTA_DETERMINISTIC_PRIMITIVE_V1_HANDLED : CETTA_DETERMINISTIC_PRIMITIVE_V1_FAULT;
}

bool cetta_regular_token_compiler_v1_load(
    const PPABIV1Pack *classes, uint32_t work_limit,
    CettaRegularTokenCompilerV1 **out, char *error, size_t error_size) {
    return cetta_regular_token_compiler_v1_restore(classes, work_limit,
        cetta_regular_token_compiler_v1_prepared, cetta_regular_token_compiler_v1_prepared_len,
        false, out, error, error_size);
}

bool cetta_regular_token_compiler_v1_store(const CettaRegularTokenCompilerV1 *compiler,
    uint8_t **bytes, size_t *length, char *error, size_t error_size) {
    return compiler && cetta_deterministic_equation_plan_v1_store(compiler->equations,
        "regular-token-v1", bytes, length, error, error_size);
}

bool cetta_regular_token_compiler_v1_restore(
    const PPABIV1Pack *classes, uint32_t work_limit, const uint8_t *bytes,
    size_t length, bool audit, CettaRegularTokenCompilerV1 **out, char *error, size_t error_size) {
    if (!classes || !work_limit || !out)
        return error_message(error, error_size, "bad regular-token compiler arguments");
    CettaRegularTokenCompilerV1 *compiler = calloc(1u, sizeof(*compiler));
    if (!compiler) return error_message(error, error_size, "regular-token compiler allocation failed");
    compiler->classes = classes;
    compiler->work_limit = work_limit;
    CettaDeterministicVocabularyV1 vocabulary = {primitive, classify, compiler};
    if (!cetta_deterministic_equation_plan_v1_restore(bytes,
            length, "regular-token-v1", &vocabulary, audit,
            &compiler->equations, error, error_size)) {
        cetta_regular_token_compiler_v1_free(compiler); return false;
    }
    cetta_regular_token_compiler_v1_free(*out);
    *out = compiler;
    return true;
}

void cetta_regular_token_compiler_v1_free(CettaRegularTokenCompilerV1 *compiler) {
    if (!compiler) return;
    cetta_deterministic_equation_plan_v1_free(compiler->equations);
    free(compiler);
}

bool cetta_regular_token_compiler_v1_answers(
    CettaRegularTokenCompilerV1 *compiler, Arena *arena,
    Atom *tag, Atom *regex, Atom **out, char *error, size_t error_size) {
    if (!compiler || !arena || !tag || !regex || !out)
        return error_message(error, error_size, "bad regular-token compilation arguments");
    if (!compiler->nesting) compiler->remaining = compiler->work_limit;
    Atom *call = atom_expr3(arena, atom_symbol(arena, "rtv1:compile"), tag, regex);
    uint64_t used = 0u;
    CettaDeterministicEquationStatusV1 status;
    bool ok = cetta_deterministic_equation_plan_v1_apply_counted(compiler->equations,
        call, primitive, compiler, arena, 2048u, compiler->remaining, &used,
        out, &status, error, error_size);
    if (used > compiler->remaining) return error_message(error, error_size, "regular-token compilation work limit");
    compiler->remaining -= used;
    return ok;
}

bool cetta_regular_token_compiler_v1_program(
    CettaRegularTokenCompilerV1 *compiler, Arena *arena,
    Atom *tag, Atom *regex, RSDFAV1Program *out,
    char *error, size_t error_size) {
    Atom *answers = NULL;
    Atom **terms = NULL;
    uint32_t len = 0u;
    RSNFAV1Plan nfa;
    RSDFAV1Plan dfa;
    RSDFAV1BuildOutcome outcome;
    bool ok = false;
    rsnfa_v1_plan_init(&nfa);
    rsdfa_v1_plan_init(&dfa);
    if (!cetta_regular_token_compiler_v1_answers(compiler, arena, tag, regex,
            &answers, error, error_size)) goto done;
    for (Atom *p = answers; form(p, "LCons", 2u); p = p->expr.elems[2]) ++len;
    if (!len || len > compiler->work_limit) {
        error_message(error, error_size, "regular-token answer set is empty or over its limit"); goto done;
    }
    terms = calloc(len, sizeof(*terms));
    if (!terms) goto done;
    Atom *p = answers;
    for (uint32_t i = 0u; i < len; ++i, p = p->expr.elems[2]) terms[i] = p->expr.elems[1];
    if (!atom_is_symbol(p, "LNil") ||
        !rsnfa_v1_plan_load(compiler->classes, terms, len, &nfa, error, error_size) ||
        !rsdfa_v1_plan_build(&nfa.nfa, compiler->work_limit, compiler->work_limit,
            &dfa, &outcome, error, error_size)) goto done;
    if (outcome != RSDFA_V1_BUILD_COMPLETED) {
        error_message(error, error_size, "regular-token determinization limit"); goto done;
    }
    ok = rsdfa_v1_plan_export_program(&dfa, out, error, error_size);
done:
    free(terms);
    rsdfa_v1_plan_free(&dfa);
    rsnfa_v1_plan_free(&nfa);
    return ok;
}
