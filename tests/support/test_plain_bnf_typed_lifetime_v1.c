/* Test-only linker observation of the existing CLI and evaluator. No runtime
 * source is replaced. The selected top-level query uses the public detailed
 * evaluator API so finite coverage and emitted occurrences stay distinct.
 * A host abort is requested through the existing session API immediately
 * after the native structural-admission call returns. */
#include "eval.h"
#include "library.h"
#include "native_handle.h"
#include "petta_program.h"
#include <errno.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static bool observing;
static bool abort_requested;
static unsigned probes;
static unsigned opened_in_probe;

static unsigned live_handles(const CettaLibraryContext *context) {
    unsigned count = 0;
    for (uint32_t i = 0; context && i < context->native_handle_len; ++i)
        if (context->native_handles[i].resource) ++count;
    return count;
}

static bool has_head(const Atom *term, const char *head) {
    return term && term->kind == ATOM_EXPR && term->expr.len &&
        atom_is_symbol(term->expr.elems[0], head);
}

static int probe_fuel(void) {
    const char *text = getenv("BNF_LIFETIME_FUEL");
    if (!text) return -1;
    char *end = NULL;
    errno = 0;
    long value = strtol(text, &end, 10);
    if (errno || !end || *end || value < 1 || value > 10000000) {
        fputs("invalid test BNF_LIFETIME_FUEL\n", stderr);
        exit(2);
    }
    return (int)value;
}

void __real_eval_top_with_registry_petta_plan(
    Space *, Arena *, Arena *, Registry *, Atom *,
    const struct PettaPlanNode *, ResultSet *);
void __wrap_eval_top_with_registry_petta_plan(
    Space *space, Arena *arena, Arena *persistent, Registry *registry,
    Atom *expression, const struct PettaPlanNode *plan, ResultSet *results) {
    if (!has_head(expression, "typed-lifetime:probe")) {
        __real_eval_top_with_registry_petta_plan(
            space, arena, persistent, registry, expression, plan, results);
        return;
    }
    CettaLibraryContext *context = eval_current_library_context();
    if (!context || observing) abort();
    unsigned before = live_handles(context);
    int previous_fuel = context->session.options.fuel_limit;
    int previous_depth = context->session.options.max_stack_depth;
    context->session.options.max_stack_depth = -1;
    cetta_eval_session_set_fuel_limit(&context->session, probe_fuel());
    EvalOutcome outcome;
    eval_outcome_init(&outcome);
    observing = true;
    opened_in_probe = 0;
    eval_top_with_registry_petta_plan_outcome(
        space, arena, persistent, registry, expression, plan, &outcome);
    observing = false;
    bool exited = cetta_eval_session_process_exit_requested(&context->session);
    printf("(BnfLifetimeObservationV1 %s answers=%zu live-before=%u live-after=%u "
           "opened=%u steps=%" PRIu64 " abort-requested=%u exit-observed=%u)\n",
           eval_completion_reason(outcome.completion),
           (size_t)outcome.results.len, before, live_handles(context),
           opened_in_probe, outcome.steps_spent, abort_requested, exited);
    for (CettaCount i = 0; i < outcome.results.len; ++i) {
        fputs("(BnfLifetimeOccurrenceV1 ", stdout);
        atom_print(outcome.results.items[i], stdout);
        fputs(")\n", stdout);
    }
    /* This embedding observer resumes after an incomplete episode; it does
     * not turn incomplete coverage into an ordinary zero-answer result.
     * The observation above owns the actual occurrence/coverage evidence.
     * The CLI sees one test marker so its document loop can run the explicit
     * recovery query. This is not a claim about default CLI abort recovery. */
    result_set_add(results, atom_symbol(arena, "BnfLifetimeProbeObservedV1"));
    eval_outcome_free(&outcome);
    context->session.options.fuel_limit = previous_fuel;
    context->session.options.max_stack_depth = previous_depth;
    if (exited) cetta_eval_session_clear_process_exit(&context->session);
    ++probes;
}

Atom *__real_cetta_library_dispatch_native(
    CettaLibraryContext *, Space *, Arena *, Atom *, Atom **, uint32_t);
Atom *__wrap_cetta_library_dispatch_native(
    CettaLibraryContext *context, Space *space, Arena *arena,
    Atom *head, Atom **arguments, uint32_t count) {
    unsigned live_before = observing ? live_handles(context) : 0;
    Atom *answer = __real_cetta_library_dispatch_native(
        context, space, arena, head, arguments, count);
    if (observing) {
        unsigned live_after = live_handles(context);
        if (live_after > live_before)
            opened_in_probe += live_after - live_before;
    }
    if (observing && !abort_requested && getenv("BNF_LIFETIME_ABORT") &&
        atom_is_symbol(head, "__cetta_lib_language_def_term_admit")) {
        abort_requested = true;
        cetta_eval_session_request_process_exit(&context->session, 7);
    }
    return answer;
}

void __real_cetta_library_context_free(CettaLibraryContext *);
void __wrap_cetta_library_context_free(CettaLibraryContext *context) {
    unsigned before = live_handles(context);
    __real_cetta_library_context_free(context);
    printf("(BnfLifetimeContextTeardownV1 live-before=%u live-after=%u probes=%u)\n",
           before, live_handles(context), probes);
}
