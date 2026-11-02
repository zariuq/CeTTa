#include <SWI-Prolog.h>
#include <stdint.h>
#include <stdio.h>
#include "eval_completion.h"
#include "stats.h"

extern void eval_mark_interrupted(CettaEvalCompletion completion);

/* Test the real installed eval/2 callback while its CeTTa owner is active. */
static uintptr_t interrupted;

static uint64_t live_callback_states(void) {
    CettaRuntimeStats stats;
    cetta_runtime_stats_snapshot(&stats);
    return stats.counters[CETTA_RUNTIME_COUNTER_PETTA_LIBPL_EVAL_STATE_LIVE];
}

static foreign_t inject_completion(term_t cause) {
    int code;
    if (!PL_get_integer(cause, &code) || code < CETTA_EVAL_INCOMPLETE_FUEL ||
        code > CETTA_EVAL_INCOMPLETE_HOST_FAILURE)
        return false;
    eval_mark_interrupted((CettaEvalCompletion)code);
    return true;
}

static foreign_t guest_yield(term_t args, int arity, control_t control) {
    (void)args;
    (void)arity;
    if (PL_foreign_control(control) == PL_PRUNED) {
        puts("callback-guest-pruned");
        return true;
    }
    PL_yield_address(&interrupted);
}

static foreign_t resume_probe(term_t args, int arity, control_t control) {
    int phase = PL_foreign_control(control);
    if (phase == PL_PRUNED)
        return true;
    if (phase == PL_RESUME)
        PL_yield_address(&interrupted);
    atom_t module_name;
    if (arity != 2 || !PL_get_atom(args, &module_name))
        return false;
    module_t module = PL_new_module(module_name);
    term_t argv = PL_new_term_refs(2);
    if (!argv || !PL_put_term(argv, args + 1))
        return false;
    const uint64_t before = live_callback_states();
    qid_t query = PL_open_query(module,
        PL_Q_NODEBUG | PL_Q_CATCH_EXCEPTION | PL_Q_EXT_STATUS | PL_Q_ALLOW_YIELD,
        PL_predicate("eval", 2, PL_atom_chars(module_name)), argv);
    if (!query)
        return false;
    for (unsigned index = 0; index < 3; index++) {
        int status = PL_next_solution(query);
        printf("callback-resume-%u=%d\n", index, status);
        if (status != PL_S_YIELD || live_callback_states() != before) {
            (void)PL_close_query(query);
            return PL_representation_error("callback_resume_status");
        }
    }
    int closed = PL_close_query(query);
    printf("callback-resume-close=%d\n", closed);
    if (closed != true || live_callback_states() != before)
        return false;
    PL_yield_address(&interrupted);
}

static foreign_t noyield_probe(term_t args, int arity, control_t control) {
    int phase = PL_foreign_control(control);
    if (phase == PL_PRUNED)
        return true;
    if (phase == PL_RESUME)
        PL_yield_address(&interrupted);
    atom_t module_name;
    if (arity != 2 || !PL_get_atom(args, &module_name))
        return false;
    term_t argv = PL_new_term_refs(2);
    if (!argv || !PL_put_term(argv, args + 1))
        return false;
    const uint64_t before = live_callback_states();
    qid_t query = PL_open_query(PL_new_module(module_name),
        PL_Q_NODEBUG | PL_Q_CATCH_EXCEPTION | PL_Q_EXT_STATUS,
        PL_predicate("callback_unwind_caught", 1, PL_atom_chars(module_name)), argv);
    if (!query)
        return false;
    int status = PL_next_solution(query);
    printf("callback-noyield-status=%d\n", status);
    int closed = PL_close_query(query);
    PL_clear_exception();
    printf("callback-noyield-close=%d\n", closed);
    if (status != PL_S_EXCEPTION || closed != true || live_callback_states() != before)
        return PL_representation_error("callback_noyield_status");
    PL_yield_address(&interrupted);
}

install_t install(void) {
    PL_register_foreign_in_module("user", "callback_inject_completion", 1,
                                 (pl_function_t)inject_completion, 0);
    PL_register_foreign_in_module("user", "callback_guest_yield", 1, (pl_function_t)guest_yield,
                        PL_FA_VARARGS | PL_FA_NONDETERMINISTIC);
    PL_register_foreign_in_module("user", "callback_resume_probe", 2, (pl_function_t)resume_probe,
                        PL_FA_VARARGS | PL_FA_NONDETERMINISTIC);
    PL_register_foreign_in_module("user", "callback_noyield_probe", 2, (pl_function_t)noyield_probe,
                        PL_FA_VARARGS | PL_FA_NONDETERMINISTIC);
}
