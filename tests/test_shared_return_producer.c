#define _POSIX_C_SOURCE 200809L
#include "shared_return_producer.h"
#include "grounded.h"
#include "library.h"
#include "parser.h"
#include "prepared_pure_machine.h"
#include "binding/frame_identity.h"

#ifdef NDEBUG
#undef NDEBUG
#endif
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static unsigned fail_malloc, fail_calloc;
static unsigned native_mode, native_calls;
static Atom *malformed_frontier(Arena *arena, Atom *value) {
    Atom *prototype = atom_expr2(arena, value, value);
    Atom *frontier = arena_alloc(arena, sizeof(*frontier));
    *frontier = *prototype;
    frontier->arena_id = arena->identity;
    frontier->expr.elems = arena_alloc(arena, 2u * sizeof(Atom *));
    frontier->expr.elems[0] = value;
    frontier->expr.elems[1] = NULL;
    return frontier;
}
bool __real_grounded_call(Arena *arena, Atom *head, Atom **args,
                          uint32_t arity, CettaCallOutcome *outcome);
bool __wrap_grounded_call(Arena *arena, Atom *head, Atom **args,
                         uint32_t arity, CettaCallOutcome *outcome) {
    if (!native_mode || !atom_is_symbol_id(head, g_builtin_syms.readln_bang))
        return __real_grounded_call(arena, head, args, arity, outcome);
    ++native_calls;
    /* A broken boundary must fail the count assertion rather than loop. */
    if (native_calls > 3u) {
        *outcome = cetta_call_interrupted();
        return true;
    }
    size_t position = 0u;
    Atom *code = parse_sexpr(arena, "(function (return (readln!)))", &position);
    Atom *error = atom_error(arena, atom_symbol(arena, "native"),
                            atom_symbol(arena, "datum"));
    switch (native_mode) {
    case 1: *outcome = cetta_call_completed_value(code); break;
    case 2:
        *outcome = cetta_call_completed_frontier(
            atom_expr(arena, (Atom *[]){code, code, error}, 3u));
        break;
    case 3: *outcome = cetta_call_failure(); break;
    case 4: *outcome = cetta_call_interrupted(); break;
    case 5: *outcome = cetta_call_value(NULL); break;
    case 6:
    case 7:
        *outcome = cetta_call_suspended(atom_int(arena, 9), native_mode == 6u
            ? atom_expr2(arena, atom_unit(arena), atom_unit(arena))
            : atom_symbol(arena, "malformed-delayed-payload"));
        break;
    case 8: *outcome = cetta_call_raised(error); break;
    case 9: *outcome = cetta_call_completed_value(error); break;
    case 10:
        *outcome = (CettaCallOutcome){.kind = (CettaCallOutcomeKind)99,
                                    .term = atom_int(arena, 9)};
        break;
    case 11:
        *outcome = (CettaCallOutcome){.kind = CETTA_CALL_VALUE,
            .term = atom_int(arena, 9), .result_form = (CettaCallResultForm)99};
        break;
    case 12: {
        *outcome = cetta_call_completed_frontier(malformed_frontier(arena, code));
        break;
    }
    default: abort();
    }
    return true;
}
void *__real_malloc(size_t size);
void *__real_calloc(size_t count, size_t size);
void *__wrap_malloc(size_t size) {
    if (fail_malloc && !--fail_malloc)
        return NULL;
    return __real_malloc(size);
}
void *__wrap_calloc(size_t count, size_t size) {
    if (fail_calloc && !--fail_calloc)
        return NULL;
    return __real_calloc(count, size);
}

typedef struct {
    unsigned next_calls, emitted, effects, captures, freed, released, cursor_polls;
    bool compatible, refuse_capture, fail_log_reserve, change_during_capture;
    Space *space;
    SpaceProgramToken token;
} Account;

typedef struct {
    CettaOwnedReturn returned;
    Arena arena;
    Bindings bindings;
    Account *account;
    unsigned shares;
} Captured;

static Atom *parse(Arena *arena, const char *text) {
    size_t position = 0u;
    Atom *term = parse_sexpr(arena, text, &position);
    assert(term);
    return term;
}

static void release_capture(void *owner) {
    Captured *captured = owner;
    assert(captured->shares);
    if (--captured->shares)
        return;
    ++captured->account->freed;
    bindings_free(&captured->bindings);
    arena_free(&captured->arena);
    free(captured);
}

/* The control's capture adapter owns ordinary Atom graphs and a flat logical
 * environment. Production adapters also account for their own capabilities,
 * delayed services and any other roots outside this control's fragment. */
static bool capture(void *context, const CettaOwnedReturn *borrowed,
                    CettaSharedReturnRow *out) {
    Account *account = context;
    ++account->captures;
    if (account->refuse_capture)
        return false;
    Captured *captured = calloc(1u, sizeof(*captured));
    if (!captured)
        return false;
    captured->shares = 1u;
    captured->account = account;
    arena_init(&captured->arena);
    arena_set_hashcons(&captured->arena, NULL);
    bindings_init(&captured->bindings);
    captured->returned.outcome = borrowed->outcome;
    AtomDeepCopySession *session = atom_deep_copy_session_new(&captured->arena);
    bool ok = session != NULL;
    if (ok && borrowed->outcome.term) {
        captured->returned.outcome.term = atom_deep_copy_session_copy(
            session, borrowed->outcome.term);
        ok = captured->returned.outcome.term != NULL;
    }
    if (ok && borrowed->outcome.delayed) {
        captured->returned.outcome.delayed = atom_deep_copy_session_copy(
            session, borrowed->outcome.delayed);
        ok = captured->returned.outcome.delayed != NULL;
    }
    if (ok && borrowed->environment) {
        ok = bindings_copy(&captured->bindings, borrowed->environment) &&
            bindings_promote_logical_atoms_with_session(
                &captured->bindings, session);
        captured->returned.environment = &captured->bindings;
    }
    atom_deep_copy_session_free(session);
    if (!ok) {
        release_capture(captured);
        return false;
    }
    *out = (CettaSharedReturnRow){
        .returned = &captured->returned, .owner = captured,
        .release = release_capture,
    };
    if (account->fail_log_reserve) {
        account->fail_log_reserve = false;
        fail_malloc = 1u;
    }
    if (account->change_during_capture)
        account->compatible = false;
    return true;
}

static bool compatible(void *context) {
    Account *account = context;
    return account->compatible && (!account->space ||
        space_program_token_matches_live_space(account->token, account->space));
}

typedef struct {
    const CettaOwnedReturn *rows[8];
    uint64_t positions[8];
    size_t count;
    bool refuse;
    CettaSharedReturnProducer *producer;
    size_t consumer;
} Recipient;

static bool publish(void *context, uint64_t position,
                    const CettaOwnedReturn *returned) {
    Recipient *recipient = context;
    if (recipient->refuse)
        return false;
    uint64_t before = UINT64_MAX;
    assert(cetta_occurrence_log_position(
        cetta_shared_return_producer_log(recipient->producer),
        recipient->consumer, &before) && before == position);
    assert(recipient->count < 8u);
    Captured *captured = (Captured *)returned;
    ++captured->shares;
    recipient->rows[recipient->count] = returned;
    recipient->positions[recipient->count++] = position;
    return true;
}

static void release_recipient(Recipient *recipient) {
    for (size_t i = 0u; i < recipient->count; ++i)
        release_capture((Captured *)recipient->rows[i]);
    recipient->count = 0u;
}

static CettaSharedDelivery take_answer(
    CettaSharedReturnProducer *producer, size_t consumer,
    Recipient *recipient, uint64_t position) {
    CettaSharedDelivery reply = cetta_shared_return_producer_take(
        producer, consumer, publish, recipient);
    assert(reply.kind == CETTA_SHARED_DELIVERY_ANSWER && reply.position == position);
    return reply;
}

typedef struct {
    Account *account;
    Arena arena;
    Bindings bindings;
    CettaOwnedReturn packet;
    unsigned count;
    bool pause_once, incomplete, handoff, change_during_next;
} Computation;

static CettaOwnedExecutionStep next_return(
    void *context, CettaOwnedReturn *returned, CettaEvalCompletion *completion) {
    Computation *computation = context;
    Account *account = computation->account;
    ++account->next_calls;
    if (computation->pause_once) {
        computation->pause_once = false;
        return CETTA_OWNED_EXECUTION_PAUSED;
    }
    if (computation->handoff) {
        *completion = CETTA_EVAL_INCOMPLETE_CAPACITY;
        return CETTA_OWNED_EXECUTION_HANDOFF;
    }
    if (account->emitted == computation->count) {
        *completion = computation->incomplete
            ? CETTA_EVAL_INCOMPLETE_HOST_FAILURE : CETTA_EVAL_COMPLETE;
        return CETTA_OWNED_EXECUTION_COMPLETE;
    }
    /* This one computation owns the effect. Refusing capture must never
     * replay it; publishing its return does not run it in each recipient. */
    ++account->effects;
    ++account->emitted;
    if (computation->change_during_next)
        account->compatible = false;
    *returned = computation->packet;
    return CETTA_OWNED_EXECUTION_RETURN;
}

static void release_computation(void *context) {
    Computation *computation = context;
    ++computation->account->released;
    bindings_free(&computation->bindings);
    arena_free(&computation->arena);
    free(computation);
}

static CettaOwnedExecution new_execution(Account *account, unsigned count) {
    Computation *computation = calloc(1u, sizeof(*computation));
    assert(computation);
    computation->account = account;
    computation->count = count;
    arena_init(&computation->arena);
    arena_set_hashcons(&computation->arena, NULL);
    bindings_init(&computation->bindings);
    Atom *packet = parse(&computation->arena,
        "(packet $x $y (closure $y $x $x) (check $y))");
    Atom *x = packet->expr.elems[1];
    Atom *y = packet->expr.elems[2];
    assert(bindings_add_var(&computation->bindings, x, y));
    assert(bindings_add_var(&computation->bindings, y,
                            parse(&computation->arena, "7")));
    computation->packet = (CettaOwnedReturn){
        .outcome = cetta_call_suspended(
            packet->expr.elems[3], packet->expr.elems[4]),
        .environment = &computation->bindings,
    };
    CettaOwnedExecution execution = {0};
    cetta_owned_execution_init(&execution, computation, next_return,
                               release_computation);
    execution.relocatable = true;
    return execution;
}

static CettaOccurrenceLogInfo log_info(CettaSharedReturnProducer *producer) {
    CettaOccurrenceLogInfo info;
    assert(cetta_occurrence_log_info(
        cetta_shared_return_producer_log(producer), &info));
    assert(cetta_occurrence_log_valid(cetta_shared_return_producer_log(producer)));
    return info;
}

static void test_refusal_and_owned_graph(void) {
    Account account = {.compatible = true, .refuse_capture = true};
    CettaOwnedExecution execution = new_execution(&account, 3u);
    CettaSharedReturnProducer *producer = cetta_shared_return_producer_new(
        &execution, 2u, 1u, capture, compatible, &account);
    assert(producer && !execution.computation);
    assert(cetta_shared_return_producer_advance(producer, 0u) ==
           CETTA_SHARED_RETURN_PAUSED && account.next_calls == 0u);
    assert(cetta_shared_return_producer_advance(producer, 1u) ==
           CETTA_SHARED_RETURN_CAPTURE_REFUSED);
    const CettaOwnedExecution *owned = cetta_shared_return_producer_execution(producer);
    assert(owned->has_pending && owned->accepted == 0u);
    assert(account.next_calls == 1u && account.effects == 1u);
    account.refuse_capture = false;
    account.fail_log_reserve = true;
    assert(cetta_shared_return_producer_advance(producer, 1u) ==
           CETTA_SHARED_RETURN_CAPACITY);
    assert(owned->has_pending && log_info(producer).rows == 0u);
    assert(account.next_calls == 1u && account.effects == 1u && account.freed == 1u);
    assert(cetta_shared_return_producer_advance(producer, 1u) == CETTA_SHARED_RETURN_PAUSED);
    assert(!owned->has_pending && owned->accepted == 1u);
    assert(cetta_shared_return_producer_advance(producer, 3u) ==
           CETTA_SHARED_RETURN_BACKPRESSURE && account.next_calls == 1u);
    Recipient a = {.producer = producer, .consumer = 0u, .refuse = true};
    Recipient b = {.producer = producer, .consumer = 1u};
    assert(cetta_shared_return_producer_take(producer, 0u, publish, &a).kind ==
           CETTA_SHARED_DELIVERY_PUBLICATION_REFUSED);
    assert(cetta_shared_return_producer_peek(producer, 0u).position == 0u);
    a.refuse = false;
    assert(cetta_shared_return_producer_take(producer, 0u, publish, &a).kind ==
           CETTA_SHARED_DELIVERY_ANSWER);
    assert(cetta_shared_return_producer_trim(producer) == 0u);
    assert(cetta_shared_return_producer_cancel(producer, 0u));
    assert(cetta_shared_return_producer_peek(producer, 0u).kind ==
           CETTA_SHARED_DELIVERY_CANCELLED);
    for (uint64_t i = 0u; i < 3u; ++i) {
        CettaSharedDelivery reply = cetta_shared_return_producer_take(producer, 1u, publish, &b);
        assert(reply.kind == CETTA_SHARED_DELIVERY_ANSWER && reply.position == i);
        assert(cetta_shared_return_producer_trim(producer) == 1u);
        assert(cetta_shared_return_producer_advance(producer, 1u) ==
               (i == 2u ? CETTA_SHARED_RETURN_COMPLETE : CETTA_SHARED_RETURN_PAUSED));
    }
    assert(cetta_shared_return_producer_peek(producer, 1u).kind ==
           CETTA_SHARED_DELIVERY_COMPLETED);
    assert(account.effects == 3u && account.next_calls == 4u);
    cetta_shared_return_producer_free(producer);
    assert(account.released == 1u && account.freed == 1u);
    for (size_t i = 0u; i < b.count; ++i) {
        const CettaOwnedReturn *row = b.rows[i];
        assert(row->outcome.kind == CETTA_CALL_SUSPENDED && row->outcome.delayed);
        Captured *owner = (Captured *)row;
        Atom *value = bindings_apply((Bindings *)row->environment,
                                     &owner->arena, row->outcome.term);
        if (!value || strcmp(atom_to_string(&owner->arena, value), "(closure 7 7 7)"))
            fprintf(stderr, "captured environment result: %s\n",
                    value ? atom_to_string(&owner->arena, value) : "NULL");
        assert(strcmp(atom_to_string(&owner->arena, value), "(closure 7 7 7)") == 0);
        value = bindings_apply((Bindings *)row->environment,
                               &owner->arena, row->outcome.delayed);
        assert(strcmp(atom_to_string(&owner->arena, value), "(check 7)") == 0);
    }
    release_recipient(&b);
    assert(account.freed == 3u);
    release_recipient(&a);
    assert(account.freed == 4u);
    puts("PASS: shared owned returns retain pending effects, bindings, delayed roots and independent delivery");
}

static void test_authority_and_detachment(void) {
    for (unsigned phase = 0u; phase < 3u; ++phase) {
        Account account = {.compatible = phase != 0u};
        CettaOwnedExecution execution = new_execution(&account, 2u);
        Computation *computation = execution.computation;
        computation->change_during_next = phase == 1u;
        account.change_during_capture = phase == 2u;
        CettaSharedReturnProducer *producer = cetta_shared_return_producer_new(
            &execution, 2u, 3u, capture, compatible, &account);
        assert(producer);
        assert(cetta_shared_return_producer_advance(producer, 1u) ==
               CETTA_SHARED_RETURN_CONTEXT_REFUSED);
        const CettaOwnedExecution *owned = cetta_shared_return_producer_execution(producer);
        assert(owned->has_pending == (phase != 0u));
        assert(account.next_calls == (phase == 0u ? 0u : 1u));
        assert(log_info(producer).rows == 0u);
        assert(account.freed == (phase == 2u ? 1u : 0u));
        account.compatible = true;
        account.change_during_capture = false;
        computation->change_during_next = false;
        assert(cetta_shared_return_producer_advance(producer, 1u) ==
               CETTA_SHARED_RETURN_PAUSED);
        account.compatible = false;
        Recipient recipient = {.producer = producer};
        assert(cetta_shared_return_producer_take(producer, 0u, publish, &recipient).kind ==
               CETTA_SHARED_DELIVERY_CONTEXT_REFUSED && recipient.count == 0u);
        CettaOwnedExecution resumed = {0};
        assert(cetta_shared_return_producer_detach_execution(producer, &resumed));
        assert(resumed.computation == computation && resumed.accepted == 1u);
        assert(cetta_shared_return_producer_advance(producer, 1u) ==
               CETTA_SHARED_RETURN_DETACHED);
        assert(!log_info(producer).completed);
        account.compatible = true;
        assert(cetta_shared_return_producer_take(producer, 0u, publish, &recipient).kind ==
               CETTA_SHARED_DELIVERY_ANSWER);
        cetta_shared_return_producer_free(producer);
        assert(account.released == 0u);
        cetta_owned_execution_cancel(&resumed);
        assert(account.released == 1u);
        release_recipient(&recipient);
    }
    puts("PASS: source validation guards staging and buffered delivery; detachment retains the complete execution");
}

static void test_construction_and_terminal_states(void) {
    Account account = {.compatible = true};
    CettaOwnedExecution execution = new_execution(&account, 0u);
    execution.relocatable = false;
    CettaOwnedExecution unqualified = execution, destination = {0};
    assert(!cetta_owned_execution_move(&destination, &execution));
    assert(!cetta_shared_return_producer_new(&execution, 2u, 3u,
                                            capture, compatible, &account));
    assert(memcmp(&execution, &unqualified, sizeof(execution)) == 0);
    assert(!destination.computation && !destination.has_pending && !account.released);
    execution.relocatable = true;
    CettaOwnedExecution before = execution;
    for (unsigned fault = 0u; fault < 3u; ++fault) {
        fail_malloc = fault == 0u ? 1u : 0u;
        fail_calloc = fault;
        assert(!cetta_shared_return_producer_new(&execution, 2u, 3u,
                                                capture, compatible, &account));
        assert(memcmp(&before, &execution, sizeof(execution)) == 0);
    }
    assert(!cetta_shared_return_producer_new(&execution, 2u, 3u, capture, NULL, &account));
    Computation *computation = execution.computation;
    computation->pause_once = true;
    computation->incomplete = true;
    CettaSharedReturnProducer *producer = cetta_shared_return_producer_new(
        &execution, 1u, 2u, capture, compatible, &account);
    assert(producer);
    assert(cetta_shared_return_producer_advance(producer, 1u) == CETTA_SHARED_RETURN_PAUSED);
    assert(!log_info(producer).completed);
    assert(cetta_shared_return_producer_advance(producer, 1u) == CETTA_SHARED_RETURN_HANDOFF);
    assert(!log_info(producer).completed);
    assert(cetta_shared_return_producer_peek(producer, 0u).kind == CETTA_SHARED_DELIVERY_PAUSED);
    unsigned calls = account.next_calls;
    assert(cetta_shared_return_producer_advance(producer, 1u) == CETTA_SHARED_RETURN_HANDOFF);
    assert(account.next_calls == calls);
    cetta_shared_return_producer_free(producer);
    execution = new_execution(&account, 1u);
    producer = cetta_shared_return_producer_new(&execution, 1u, 0u, capture, compatible, &account);
    assert(cetta_shared_return_producer_advance(producer, 1u) == CETTA_SHARED_RETURN_BACKPRESSURE);
    assert(account.next_calls == calls);
    assert(cetta_shared_return_producer_cancel(producer, 0u));
    assert(cetta_shared_return_producer_advance(producer, 1u) == CETTA_SHARED_RETURN_NO_CONSUMERS);
    assert(account.next_calls == calls && !log_info(producer).completed);
    cetta_shared_return_producer_free(producer);
    puts("PASS: allocation refusal leaves the original owner intact; pauses, no demand and incomplete frontiers never certify completion");
}

static void test_pending_return_detachment(void) {
    Account account = {.compatible = true, .refuse_capture = true};
    CettaOwnedExecution execution = new_execution(&account, 1u);
    CettaSharedReturnProducer *producer = cetta_shared_return_producer_new(
        &execution, 2u, 3u, capture, compatible, &account);
    assert(producer);
    assert(cetta_shared_return_producer_advance(producer, 1u) ==
           CETTA_SHARED_RETURN_CAPTURE_REFUSED);
    const CettaOwnedExecution *owned = cetta_shared_return_producer_execution(producer);
    CettaOwnedReturn pending = owned->pending;
    void *computation = owned->computation;
    Account occupied_account = {.compatible = true};
    CettaOwnedExecution occupied = new_execution(&occupied_account, 1u);
    CettaOwnedExecution occupied_before = occupied, owned_before = *owned;
    assert(!cetta_shared_return_producer_detach_execution(producer, &occupied));
    assert(memcmp(&occupied, &occupied_before, sizeof(occupied)) == 0);
    assert(memcmp(owned, &owned_before, sizeof(*owned)) == 0);
    cetta_owned_execution_cancel(&occupied);
    assert(occupied_account.released == 1u && account.released == 0u);
    CettaOwnedExecution resumed = {0};
    assert(cetta_shared_return_producer_detach_execution(producer, &resumed));
    assert(resumed.has_pending && resumed.computation == computation);
    assert(resumed.pending.outcome.term == pending.outcome.term &&
           resumed.pending.environment == pending.environment);
    assert(resumed.accepted == 0u && account.effects == 1u && account.next_calls == 1u);
    cetta_shared_return_producer_free(producer);
    assert(account.released == 0u);
    CettaOwnedExecution before = resumed, moved = {0};
    assert(!cetta_owned_execution_move(&resumed, &resumed));
    assert(memcmp(&before, &resumed, sizeof(resumed)) == 0);
    assert(!cetta_owned_execution_move(NULL, &resumed));
    moved.has_pending = true;
    assert(!cetta_owned_execution_move(&moved, &resumed));
    assert(memcmp(&before, &resumed, sizeof(resumed)) == 0);
    moved.has_pending = false;
    assert(cetta_owned_execution_move(&moved, &resumed));
    assert(memcmp(&before, &moved, sizeof(moved)) == 0);
    assert(!resumed.computation && !resumed.has_pending && resumed.terminal);
    assert(!cetta_owned_execution_move(&resumed, &resumed));
    cetta_owned_execution_cancel(&resumed);
    assert(account.released == 0u && account.effects == 1u && account.next_calls == 1u);
    account.refuse_capture = false;
    producer = cetta_shared_return_producer_new(
        &moved, 1u, 3u, capture, compatible, &account);
    assert(producer && !moved.computation && !moved.has_pending);
    assert(cetta_shared_return_producer_advance(producer, 1u) == CETTA_SHARED_RETURN_PAUSED);
    assert(account.effects == 1u && account.next_calls == 1u);
    Recipient recipient = {.producer = producer};
    take_answer(producer, 0u, &recipient, 0u);
    assert(cetta_shared_return_producer_trim(producer) == 1u);
    assert(cetta_shared_return_producer_advance(producer, 1u) == CETTA_SHARED_RETURN_COMPLETE);
    cetta_shared_return_producer_free(producer);
    assert(account.released == 1u && account.freed == 0u);
    release_recipient(&recipient);
    assert(account.freed == 1u);
    puts("PASS: detaching and re-owning a pending return preserves its exact loan and never replays producer work");
}

static bool poll_prepared_producer(void *context) {
    ((Account *)context)->cursor_polls++;
    return false;
}

static CettaSharedReturnStep advance_prepared_producer(
    CettaSharedReturnProducer *producer, Account *account,
    uint64_t allowance, bool bounded) {
    uint64_t target = log_info(producer).past_end + allowance;
    for (unsigned slice = 0u; slice < 64u; ++slice) {
        uint64_t past_end = log_info(producer).past_end;
        unsigned polls = account->cursor_polls;
        CettaSharedReturnStep step = cetta_shared_return_producer_advance(
            producer, bounded ? 1u : target - past_end);
        if (bounded)
            assert(account->cursor_polls - polls <= 1u);
        if (step != CETTA_SHARED_RETURN_PAUSED || log_info(producer).past_end == target)
            return step;
    }
    assert(!"prepared producer failed to yield within its finite fixture");
    return CETTA_SHARED_RETURN_INVALID;
}

static void test_real_prepared_producer(bool bounded) {
    Arena source;
    arena_init(&source);
    arena_set_hashcons(&source, NULL);
    TermUniverse universe;
    term_universe_init(&universe);
    term_universe_set_persistent_arena(&universe, &source);
    Space space;
    space_init_with_universe(&space, &universe);
    const char *equations[] = {
        "(= (pick (Cons $x $xs)) $x)",
        "(= (pick (Cons $x (Cons $y $ys))) (pick (Cons $y $ys)))",
    };
    for (size_t i = 0u; i < 2u; ++i)
        assert(space_admit_atom(&space, &source, parse(&source, equations[i])));
    CettaPreparedPureProgram *program = cetta_prepared_pure_program_compile_closed_answers(
        &space, parse(&source, "(pick (Cons (pair 7 7) (Cons (pair 7 7) (Cons (pair 9 9) Nil))))"),
        CETTA_GSLT_PURE_CALL_EAGER, atom_bool, atom_expr,
        cetta_prepared_pure_constructs_expression_always,
        NULL, NULL, NULL, NULL, NULL, true, true,
        (CettaMatchDecisionSemanticIdentity){
            .compiler_identity = cetta_match_decision_compiler_identity(),
        });
    assert(program);
    Account account = {.compatible = true, .space = &space};
    CettaPreparedPureAnswerCursorOptions options = {
        .limits = {.max_transitions = 1024u, .max_scratch_bytes = 1024u * 1024u,
                   .construct_allocation_bound = atom_expr_allocation_bound},
        .unmatched_call = CETTA_PREPARED_PURE_UNMATCHED_FAILS,
        .allow_continuations = true,
        .interrupt_poll = poll_prepared_producer, .interrupt_context = &account,
        .interrupt_poll_interval = 1u,
    };
    CettaPreparedPureAnswerCursor *cursor =
        cetta_prepared_pure_answer_cursor_open(program, &options);
    assert(cursor);
    account.token = cetta_prepared_pure_answer_cursor_program_token(cursor);
    CettaOwnedExecution execution = {0};
    if (bounded)
        assert(cetta_prepared_pure_answer_cursor_own_execution_bounded(&execution, cursor, 1u));
    else
        assert(cetta_prepared_pure_answer_cursor_own_execution(&execution, cursor));
    CettaSharedReturnProducer *producer = cetta_shared_return_producer_new(
        &execution, 2u, 2u, capture, compatible, &account);
    assert(producer);
    CettaOwnedExecution duplicate = {0};
    assert(!cetta_prepared_pure_answer_cursor_own_execution(&duplicate, cursor));
    assert(!cetta_prepared_pure_answer_cursor_own_execution_bounded(&duplicate, cursor, 0u));
    assert(cetta_shared_return_producer_advance(producer, 0u) == CETTA_SHARED_RETURN_PAUSED);
    assert(account.cursor_polls == 0u);
    Recipient a = {.producer = producer, .consumer = 0u};
    Recipient b = {.producer = producer, .consumer = 1u};
    assert(advance_prepared_producer(producer, &account, 2u, bounded) == CETTA_SHARED_RETURN_PAUSED);
    unsigned polls = account.cursor_polls;
    assert(cetta_shared_return_producer_advance(producer, 1u) == CETTA_SHARED_RETURN_BACKPRESSURE);
    assert(account.cursor_polls == polls);
    for (uint64_t i = 0u; i < 2u; ++i) {
        take_answer(producer, 0u, &a, i);
        assert(cetta_shared_return_producer_trim(producer) == 0u);
    }
    assert(cetta_shared_return_producer_cancel(producer, 0u));
    take_answer(producer, 1u, &b, 0u);
    assert(cetta_shared_return_producer_trim(producer) == 1u);
    assert(account.cursor_polls == polls);
    assert(space_admit_atom(&space, &source, parse(&source, "(unrelated fact)")));
    assert(compatible(&account));
    assert(advance_prepared_producer(producer, &account, 1u, bounded) == CETTA_SHARED_RETURN_PAUSED);
    polls = account.cursor_polls;
    for (uint64_t i = 1u; i < 3u; ++i) {
        CettaSharedDelivery reply = take_answer(producer, 1u, &b, i);
        assert(reply.returned->outcome.result_form == CETTA_CALL_RESULT_COMPLETED_VALUE);
        assert(cetta_shared_return_producer_trim(producer) == 1u);
    }
    assert(account.cursor_polls == polls);
    assert(advance_prepared_producer(producer, &account, 1u, bounded) == CETTA_SHARED_RETURN_COMPLETE);
    assert(log_info(producer).completed);
    /* Program changes refuse even a cached completion. Published caller
     * answers are independently retained and remain readable afterward. */
    assert(space_admit_atom(&space, &source, parse(&source, "(= (pick Nil) extra)")));
    assert(cetta_shared_return_producer_peek(producer, 1u).kind == CETTA_SHARED_DELIVERY_CONTEXT_REFUSED);
    cetta_shared_return_producer_free(producer);
    cetta_prepared_pure_program_free(program);
    space_free(&space);
    term_universe_free(&universe);
    arena_free(&source);
    const char *expected[] = {"(pair 7 7)", "(pair 7 7)", "(pair 9 9)"};
    for (size_t i = 0u; i < b.count; ++i) {
        Captured *owner = (Captured *)b.rows[i];
        assert(strcmp(atom_to_string(&owner->arena, b.rows[i]->outcome.term), expected[i]) == 0);
        assert(b.positions[i] == i);
    }
    release_recipient(&a);
    release_recipient(&b);
    assert(account.captures == 3u && account.freed == 3u);
    puts("PASS: one real prepared relation supplies ordered duplicate occurrences to independent consumers without retaining source storage");
}

typedef struct {
    Atom **values;
    const Bindings *environment;
    size_t count, attempts;
    bool refuse;
} NativeRecipient;

static bool accept_native_return(void *context,
                                 const CettaOwnedReturn *returned) {
    NativeRecipient *recipient = context;
    ++recipient->attempts;
    assert(returned->environment == recipient->environment);
    assert(returned->outcome.kind == CETTA_CALL_VALUE);
    if (recipient->refuse)
        return false;
    assert(returned->outcome.term == recipient->values[recipient->count]);
    ++recipient->count;
    return true;
}

static void test_native_return_cursor(void) {
    Arena arena;
    arena_init(&arena);
    Bindings environment;
    bindings_init(&environment);
    Atom *code = parse(&arena, "(function (return (println! held)))");
    Atom *error_data = parse(&arena, "(Error payload reason)");
    Atom *empty_data = parse(&arena, "Empty");
    Atom *variable = parse(&arena, "$shared");
    Atom *delayed = atom_expr2(&arena, atom_symbol(&arena, "check"), variable);
    Atom *values[] = {code, error_data, empty_data, variable, variable};
    Atom *frontier = atom_expr(&arena, values, 5u);
    Atom *empty_frontier = atom_expr(&arena, NULL, 0u);
    Atom *broken_frontier = malformed_frontier(&arena, code);
    struct {
        CettaCallOutcome call;
        Atom **values;
        size_t count;
        CettaCallOutcomeKind kind;
        CettaCallResultForm form;
        CettaEvalCompletion completion;
        Atom *delayed;
    } cases[] = {
        {cetta_call_value(code), values, 1u, CETTA_CALL_VALUE,
         CETTA_CALL_RESULT_EXPRESSION, CETTA_EVAL_COMPLETE, NULL},
        {cetta_call_completed_value(code), values, 1u, CETTA_CALL_VALUE,
         CETTA_CALL_RESULT_COMPLETED_VALUE, CETTA_EVAL_COMPLETE, NULL},
        {cetta_call_completed_value(error_data), values + 1u, 1u, CETTA_CALL_VALUE,
         CETTA_CALL_RESULT_COMPLETED_VALUE, CETTA_EVAL_COMPLETE, NULL},
        {cetta_call_completed_value(empty_data), values + 2u, 1u, CETTA_CALL_VALUE,
         CETTA_CALL_RESULT_COMPLETED_VALUE, CETTA_EVAL_COMPLETE, NULL},
        {cetta_call_completed_frontier(frontier), values, 5u, CETTA_CALL_VALUE,
         CETTA_CALL_RESULT_COMPLETED_VALUE, CETTA_EVAL_COMPLETE, NULL},
        {cetta_call_completed_value(frontier), &frontier, 1u, CETTA_CALL_VALUE,
         CETTA_CALL_RESULT_COMPLETED_VALUE, CETTA_EVAL_COMPLETE, NULL},
        {cetta_call_completed_frontier(empty_frontier), NULL, 0u, CETTA_CALL_VALUE,
         CETTA_CALL_RESULT_COMPLETED_VALUE, CETTA_EVAL_COMPLETE, NULL},
        {cetta_call_failure(), NULL, 0u, CETTA_CALL_FAILURE,
         CETTA_CALL_RESULT_EXPRESSION, CETTA_EVAL_COMPLETE, NULL},
        {cetta_call_raised(error_data), values + 1u, 1u, CETTA_CALL_RAISED,
         CETTA_CALL_RESULT_EXPRESSION, CETTA_EVAL_COMPLETE, NULL},
        {cetta_call_suspended(variable, delayed), values + 3u, 1u,
         CETTA_CALL_SUSPENDED, CETTA_CALL_RESULT_EXPRESSION,
         CETTA_EVAL_COMPLETE, delayed},
        {cetta_call_interrupted(), NULL, 0u, CETTA_CALL_INTERRUPTED,
         CETTA_CALL_RESULT_EXPRESSION, CETTA_EVAL_INCOMPLETE_CANCELLED, NULL},
        {cetta_call_completed_frontier(code->expr.elems[0]), NULL, 0u,
         CETTA_CALL_VALUE, CETTA_CALL_RESULT_COMPLETED_VALUE,
         CETTA_EVAL_INCOMPLETE_HOST_FAILURE, NULL},
        {cetta_call_value(NULL), NULL, 0u, CETTA_CALL_VALUE,
         CETTA_CALL_RESULT_EXPRESSION, CETTA_EVAL_INCOMPLETE_HOST_FAILURE, NULL},
        {{.kind = CETTA_CALL_VALUE, .term = code,
          .result_form = (CettaCallResultForm)99}, NULL, 0u, CETTA_CALL_VALUE,
         CETTA_CALL_RESULT_EXPRESSION, CETTA_EVAL_INCOMPLETE_HOST_FAILURE, NULL},
        {{.kind = (CettaCallOutcomeKind)99, .term = code}, NULL, 0u,
         CETTA_CALL_VALUE, CETTA_CALL_RESULT_EXPRESSION,
         CETTA_EVAL_INCOMPLETE_HOST_FAILURE, NULL},
        {cetta_call_completed_frontier(broken_frontier), values, 1u,
         CETTA_CALL_VALUE, CETTA_CALL_RESULT_COMPLETED_VALUE,
         CETTA_EVAL_INCOMPLETE_HOST_FAILURE, NULL},
    };
    size_t checks = 0u;
    for (size_t i = 0u; i < sizeof(cases) / sizeof(cases[0]); ++i) {
        CettaCallReturnCursor cursor = {
            .outcome = cases[i].call, .environment = &environment,
        };
        CettaEvalCompletion completion = CETTA_EVAL_COMPLETE;
        CettaOwnedReturn returned;
        for (size_t j = 0u; j < cases[i].count; ++j) {
            assert(cetta_call_return_next(&cursor, &returned, &completion) ==
                   CETTA_OWNED_EXECUTION_RETURN);
            assert(returned.outcome.kind == cases[i].kind);
            assert(returned.outcome.term == cases[i].values[j]);
            assert(returned.outcome.result_form == cases[i].form);
            assert(returned.outcome.delayed == cases[i].delayed);
            assert(returned.environment == &environment);
            assert(returned.final == (cases[i].completion == CETTA_EVAL_COMPLETE &&
                                      j + 1u == cases[i].count));
            ++checks;
        }
        CettaOwnedExecutionStep terminal = cases[i].completion == CETTA_EVAL_COMPLETE
            ? CETTA_OWNED_EXECUTION_COMPLETE : CETTA_OWNED_EXECUTION_INTERRUPTED;
        assert(cetta_call_return_next(&cursor, &returned, &completion) == terminal);
        assert(completion == cases[i].completion);
        assert(cetta_call_return_next(&cursor, &returned, &completion) == terminal);
        ++checks;
    }
    CettaCallReturnCursor cursor = {
        .outcome = cetta_call_completed_frontier(frontier),
        .environment = &environment,
    };
    CettaOwnedExecution execution;
    cetta_owned_execution_init(&execution, &cursor, cetta_call_return_next, NULL);
    NativeRecipient recipient = {values, &environment, 0u, 0u, true};
    fail_malloc = fail_calloc = 1u;
    assert(cetta_owned_execution_sequence(&execution, 0u,
        accept_native_return, &recipient) == CETTA_OWNED_EXECUTION_PAUSED);
    assert(!cursor.position && !recipient.attempts);
    assert(cetta_owned_execution_sequence(&execution, 5u,
        accept_native_return, &recipient) == CETTA_OWNED_EXECUTION_PAUSED);
    assert(cursor.position == 1u && execution.has_pending && !recipient.count);
    assert(cetta_owned_execution_sequence(&execution, 0u,
        accept_native_return, &recipient) == CETTA_OWNED_EXECUTION_PAUSED);
    assert(recipient.attempts == 1u && cursor.position == 1u);
    recipient.refuse = false;
    assert(cetta_owned_execution_sequence(&execution, 1u,
        accept_native_return, &recipient) == CETTA_OWNED_EXECUTION_PAUSED);
    assert(recipient.count == 1u && cursor.position == 1u);
    assert(cetta_owned_execution_sequence(&execution, 4u,
        accept_native_return, &recipient) == CETTA_OWNED_EXECUTION_COMPLETE);
    assert(recipient.count == 5u && recipient.attempts == 6u);
    assert(fail_malloc == 1u && fail_calloc == 1u);
    fail_malloc = fail_calloc = 0u;
    cetta_owned_execution_cancel(&execution);
    assert(execution.completion == CETTA_EVAL_INCOMPLETE_CANCELLED);
    assert(cursor.position == 5u && recipient.count == 5u);

    cursor = (CettaCallReturnCursor){
        .outcome = cetta_call_completed_frontier(frontier),
        .environment = &environment, .position = 3u,
    };
    CettaOwnedReturn returned;
    CettaEvalCompletion completion = CETTA_EVAL_INCOMPLETE_FUEL;
    assert(cetta_call_return_next(&cursor, &returned, &completion) ==
           CETTA_OWNED_EXECUTION_RETURN);
    assert(returned.outcome.term == variable);
    assert(bindings_add_var(&environment, variable, atom_int(&arena, 17)));
    assert(cetta_call_return_next(&cursor, &returned, &completion) ==
           CETTA_OWNED_EXECUTION_RETURN);
    Atom *bound = bindings_lookup_value_id((Bindings *)returned.environment,
                                           variable->var_id).skeleton;
    assert(bound && bound->kind == ATOM_GROUNDED && bound->ground.ival == 17);
    assert(completion == CETTA_EVAL_INCOMPLETE_FUEL);
    for (unsigned reason = CETTA_EVAL_COMPLETE;
         reason <= CETTA_EVAL_INCOMPLETE_HOST_FAILURE; ++reason) {
        for (unsigned fault = 0u; fault < 2u; ++fault) {
            cursor = (CettaCallReturnCursor){
                .outcome = fault ? cetta_call_value(NULL) : cetta_call_interrupted(),
                .environment = &environment,
            };
            completion = (CettaEvalCompletion)reason;
            assert(cetta_call_return_next(&cursor, &returned, &completion) ==
                   CETTA_OWNED_EXECUTION_INTERRUPTED);
            CettaEvalCompletion expected = reason ? (CettaEvalCompletion)reason
                : fault ? CETTA_EVAL_INCOMPLETE_HOST_FAILURE
                        : CETTA_EVAL_INCOMPLETE_CANCELLED;
            assert(completion == expected);
            ++checks;
        }
    }
    bindings_free(&environment);
    arena_free(&arena);
    printf("PASS: %zu native return observations preserve provenance, control, "
           "ordered aliases, pending acceptance and live bindings without allocation\n",
           checks);
}

static void test_native_evaluator_boundary(void) {
    for (unsigned dialect = 0u; dialect < 2u; dialect++) {
        CettaLibraryContext *context = calloc(1u, sizeof(*context));
        assert(context);
        cetta_library_context_init_for_language_profile(context,
            dialect ? CETTA_LANGUAGE_HE : CETTA_LANGUAGE_PETTA, NULL);
        eval_set_library_context(context);
        Arena arena;
        arena_init(&arena);
        arena_set_hashcons(&arena, NULL);
        TermUniverse universe;
        term_universe_init(&universe);
        term_universe_set_persistent_arena(&universe, &arena);
        Space space;
        space_init_with_universe(&space, &universe);
        Registry registry;
        registry_init(&registry);
        registry_bind_id(&registry, g_builtin_syms.self, atom_space(&arena, &space));
        for (unsigned mode = 1u; mode <= 12u; mode++) {
            if (dialect && mode == 7u)
                continue;
            native_mode = mode;
            native_calls = 0u;
            Atom *source = parse(&arena, (mode >= 3u && mode <= 5u) || mode >= 10u
                ? "(let $ignored (readln!) (readln!))" : "(readln!)");
            EvalOutcome outcome;
            eval_outcome_init(&outcome);
            eval_top_with_registry_outcome(&space, &arena, &arena, &registry,
                                          source, &outcome, NULL, NULL);
            if (native_calls != 1u)
                fprintf(stderr, "native boundary dialect=%u mode=%u calls=%u\n",
                        dialect, mode, native_calls);
            assert(native_calls == 1u);
            if (mode == 1u || mode == 2u) {
                assert(outcome.completion == CETTA_EVAL_COMPLETE);
                /* HE's enclosing completion prefers successful alternatives
                 * to Error values. The shared producer still lends all three
                 * occurrences, and PeTTa publishes all three. */
                CettaCount expected_count = mode == 1u ? 1u : dialect ? 2u : 3u;
                if (outcome.results.len != expected_count) {
                    fprintf(stderr, "native boundary dialect=%u mode=%u rows=%llu\n",
                            dialect, mode, (unsigned long long)outcome.results.len);
                    for (CettaCount i = 0u; i < outcome.results.len; i++) {
                        atom_print(outcome.results.items[i], stderr);
                        fputc('\n', stderr);
                    }
                }
                assert(outcome.results.len == expected_count);
                Atom *expected = parse(&arena, "(function (return (readln!)))");
                assert(atom_eq(outcome.results.items[0], expected));
                if (mode == 2u) {
                    assert(atom_eq(outcome.results.items[1], expected));
                    if (!dialect)
                        assert(atom_is_error(outcome.results.items[2]));
                }
                assert(!outcome.petta_raised_error);
            } else if (mode == 3u) {
                assert(outcome.completion == CETTA_EVAL_COMPLETE);
                assert(outcome.results.len == 0u);
            } else if (mode == 4u) {
                assert(outcome.completion == CETTA_EVAL_INCOMPLETE_CANCELLED);
            } else if (mode == 5u || mode >= 10u) {
                assert(outcome.completion == CETTA_EVAL_INCOMPLETE_HOST_FAILURE);
            } else if (mode == 6u) {
                assert(outcome.completion == CETTA_EVAL_COMPLETE);
                assert(outcome.results.len == 1u);
                assert(atom_eq(outcome.results.items[0], atom_int(&arena, 9)));
            } else if (mode == 7u) {
                assert(outcome.completion == CETTA_EVAL_INCOMPLETE_CAPACITY);
            } else {
                assert(outcome.completion == CETTA_EVAL_COMPLETE);
                assert(outcome.results.len == 1u);
                assert(atom_is_error(outcome.results.items[0]));
                assert(mode == 9u ? !outcome.petta_raised_error
                                 : dialect || outcome.petta_raised_error);
            }
            eval_outcome_free(&outcome);
        }
        native_mode = 0u;
        registry_free(&registry);
        space_free(&space);
        term_universe_free(&universe);
        cetta_library_context_free(context);
        eval_set_library_context(NULL);
        free(context);
        arena_free(&arena);
    }
    puts("PASS: 23 real evaluator boundaries preserve entered-call effects, tagged data, errors, constraints and interruption without replay");
}

static void test_invocation_operands(void) {
    Arena source, result;
    arena_init_detached(&source);
    arena_init_detached(&result);
    Atom *head = atom_symbol(&source, "invoke");
    Atom *atoms[16];
    BindingValue values[16];
    Atom *code = parse(&source, "(function (return (readln!)))");
    for (unsigned i = 0u; i < 16u; i++) {
        atoms[i] = i % 3u ? atom_int(&source, i) : code;
        values[i] = binding_value_from_atom(atoms[i]);
    }
    unsigned observations = 0u;
    for (unsigned first = 0u; first <= 8u; first++) {
        for (unsigned second = 0u; second <= 8u; second++) {
            for (unsigned contextual = 0u; contextual < 2u; contextual++) {
                CettaInvocation call = {
                    .head = binding_value_from_atom(head),
                    .captured = {.atoms = atoms, .count = first},
                    .supplied = {.atoms = contextual ? NULL : atoms + first,
                        .values = contextual ? values + first : NULL,
                        .count = second},
                };
                CettaExprLen arity = UINT64_MAX;
                assert(cetta_invocation_arity(&call, &arity));
                assert(arity == first + second);
                for (CettaExprIndex i = 0u; i < arity; i++)
                    assert(cetta_invocation_argument(&call, i).skeleton == atoms[i]);
                Atom *syntax = cetta_invocation_materialize(&result, &call);
                assert(syntax && syntax->kind == ATOM_EXPR &&
                    syntax->expr.len == arity + 1u && syntax->expr.elems[0] == head);
                for (CettaExprIndex i = 0u; i < arity; i++)
                    assert(syntax->expr.elems[i + 1u] == atoms[i]);
                if (!contextual && first == 0u)
                    assert(cetta_invocation_borrow_arguments(&call) == atoms);
                else if (!contextual && second == 0u)
                    assert(cetta_invocation_borrow_arguments(&call) == atoms);
                else if (first && second)
                    assert(!cetta_invocation_borrow_arguments(&call));
                Atom **native = cetta_invocation_materialize_arguments(&result, &call);
                assert(native);
                for (CettaExprIndex i = 0u; i < arity; i++)
                    assert(native[i] == atoms[i]);
                if (cetta_invocation_borrow_arguments(&call))
                    assert(native == cetta_invocation_borrow_arguments(&call));
                observations++;
            }
        }
    }
    CETTA_FRAME_IDENTITY_SCOPE(identities);
    CettaFrameIdentity left = cetta_frame_identity_scope_fresh(&identities);
    CettaFrameIdentity right = cetta_frame_identity_scope_fresh(&identities);
    VarId id = var_id_from_base(101u);
    Atom *x = atom_var_with_id(&source, "same", id);
    BindingValue scoped[] = {binding_value_from_context(x, left),
        binding_value_from_context(x, right), binding_value_from_context(x, left)};
    CettaInvocation call = {.head = binding_value_from_atom(head),
        .supplied = {.values = scoped, .count = 3u}};
    Atom *syntax = cetta_invocation_materialize(&result, &call);
    assert(syntax && !cetta_invocation_borrow_arguments(&call));
    assert(var_id_eq(syntax->expr.elems[1]->var_id, var_epoch_id(id, left)));
    assert(var_id_eq(syntax->expr.elems[2]->var_id, var_epoch_id(id, right)));
    assert(var_id_eq(syntax->expr.elems[1]->var_id, syntax->expr.elems[3]->var_id));
    BindingsBuilder builder;
    assert(bindings_builder_init(&builder, NULL));
    assert(bindings_builder_register_contextual_frame(&builder, &id, 1u, left));
    assert(bindings_builder_register_contextual_frame(&builder, &id, 1u, right));
    uint32_t mark = bindings_builder_save(&builder);
    CettaInvocation unresolved = {.head = binding_value_from_atom(code)};
    assert(!cetta_invocation_resolve_arguments(
        &result, &builder.current, &call, true, &unresolved));
    assert(unresolved.head.skeleton == code);
    for (unsigned round = 0u; round < 2u; round++) {
        assert(bindings_builder_add_id_fresh(&builder, var_epoch_id(id, left),
            SYMBOL_ID_NONE, atom_int(&source, 11 + round)));
        assert(bindings_builder_add_id_fresh(&builder, var_epoch_id(id, right),
            SYMBOL_ID_NONE, atom_int(&source, 23 + round)));
        Atom *observed = bindings_apply(&builder.current, &result, syntax);
        assert(observed->expr.elems[1]->ground.ival == 11 + round);
        assert(observed->expr.elems[2]->ground.ival == 23 + round);
        assert(observed->expr.elems[3]->ground.ival == 11 + round);
        BindingValue argument = cetta_invocation_argument(&call, 1u);
        Atom *ready = bindings_apply_value(&builder.current, &result, argument);
        assert(ready && ready->ground.ival == 23 + round);
        CettaInvocation resolved;
        assert(cetta_invocation_resolve_arguments(
            &result, &builder.current, &call, true, &resolved));
        Atom **arguments = cetta_invocation_materialize_arguments(&result, &resolved);
        assert(arguments && arguments[0]->ground.ival == 11 + round &&
            arguments[1]->ground.ival == 23 + round &&
            arguments[2]->ground.ival == 11 + round);
        bindings_builder_rollback(&builder, mark);
    }
    CettaInvocation corrupt = {.head = binding_value_from_atom(head),
        .captured = {.atoms = (Atom *const *)(uintptr_t)1u, .count = UINT64_MAX},
        .supplied = {.atoms = atoms, .count = 1u}};
    size_t before = arena_accounted_live_bytes(&result);
    CettaExprLen untouched = 77u;
    assert(!cetta_invocation_arity(&corrupt, &untouched) && untouched == 77u);
    assert(!cetta_invocation_materialize(&result, &corrupt));
    assert(arena_accounted_live_bytes(&result) == before);
    corrupt = (CettaInvocation){.head = binding_value_from_atom(head),
        .supplied = {.atoms = atoms, .values = values, .count = 1u}};
    assert(!cetta_invocation_arity(&corrupt, &untouched));
    assert(!cetta_invocation_materialize(&result, &corrupt));
    assert(arena_accounted_live_bytes(&result) == before);
    bindings_builder_free(&builder);
    arena_free(&result);
    arena_free(&source);
    printf("PASS: %u split invocation observations retain aliases and code-shaped data; lexical worlds, rollback and arity-before-read controls pass\n", observations);
}

static void test_credit_allocation_refusal(void) {
    fail_calloc=1u;
    assert(!cetta_work_credit_new(10u) && !fail_calloc);
    CettaWorkCredit *root=cetta_work_credit_new(10u);assert(root);
    fail_calloc=1u;
    assert(!cetta_work_credit_reserve(root,7u) && !fail_calloc);
    CettaWorkCreditView view;
    assert(cetta_work_credit_inspect(root,&view) && view.balance==10u &&
        !view.children && !view.root_spent && view.root_remaining==10u);
    CettaWorkCredit *child=cetta_work_credit_reserve(root,7u);assert(child);
    assert(cetta_work_credit_inspect(root,&view) && view.balance==3u && view.children==1u);
    cetta_work_credit_release(root);
    assert(cetta_work_credit_spend(child,4u));
    assert(cetta_work_credit_inspect(child,&view) && view.balance==3u &&
        view.root_spent==4u && view.root_remaining==6u);
    cetta_work_credit_release(child);
    puts("PASS: failed credit allocation preserves the parent purse; a live lease owns its parent lifetime");
}

static void test_resident_capture(void) {
    Account account = {.compatible = true};
    CettaOwnedExecution execution = new_execution(&account, 1u);
    Computation *resident = execution.computation;
    CettaOwnedReturn borrowed;
    CettaEvalCompletion completion = CETTA_EVAL_COMPLETE;
    assert(next_return(resident, &borrowed, &completion) == CETTA_OWNED_EXECUTION_RETURN);
    CettaOccurrenceLog *log = cetta_occurrence_log_new(2u, 1u);
    assert(log);
    CettaOwnedReturn original = borrowed;
    account.refuse_capture = true;
    assert(cetta_shared_return_capture_append(log, &borrowed, capture,
        compatible, &account) == CETTA_SHARED_RETURN_CAPTURE_REFUSED);
    account.refuse_capture = false;
    account.change_during_capture = true;
    assert(cetta_shared_return_capture_append(log, &borrowed, capture,
        compatible, &account) == CETTA_SHARED_RETURN_CONTEXT_REFUSED);
    assert(account.freed == 1u);
    account.change_during_capture = false;
    account.compatible = true;
    account.fail_log_reserve = true;
    assert(cetta_shared_return_capture_append(log, &borrowed, capture,
        compatible, &account) == CETTA_SHARED_RETURN_CAPACITY);
    assert(account.freed == 2u && !fail_malloc);
    assert(cetta_shared_return_capture_append(log, &borrowed, capture,
        compatible, &account) == CETTA_SHARED_RETURN_APPENDED);
    assert(account.effects == 1u && account.next_calls == 1u && !account.released);
    assert(borrowed.outcome.term == original.outcome.term &&
        borrowed.environment == original.environment &&
        borrowed.outcome.delayed == original.outcome.delayed);
    unsigned captures = account.captures;
    assert(cetta_shared_return_capture_append(log, &borrowed, capture,
        compatible, &account) == CETTA_SHARED_RETURN_BACKPRESSURE);
    assert(account.captures == captures);
    CettaOccurrenceReply a = cetta_occurrence_log_peek(log, 0u);
    CettaOccurrenceReply b = cetta_occurrence_log_peek(log, 1u);
    assert(a.kind == CETTA_OCCURRENCE_ANSWER && b.kind == CETTA_OCCURRENCE_ANSWER &&
        a.position == 0u && b.position == 0u && a.value == b.value);
    Captured *saved = (Captured *)a.value;
    ++saved->shares;
    cetta_occurrence_log_complete(log);
    assert(cetta_shared_return_capture_append(log, &borrowed, capture,
        compatible, &account) == CETTA_SHARED_RETURN_COMPLETE);
    assert(account.captures == captures);
    cetta_occurrence_log_free(log);
    assert(account.freed == 2u && !account.released);
    cetta_owned_execution_cancel(&execution);
    assert(account.released == 1u);
    assert(saved->returned.outcome.kind == CETTA_CALL_SUSPENDED &&
        saved->returned.outcome.delayed && saved->returned.environment);
    assert(strcmp(atom_to_string(&saved->arena,
        bindings_apply(&saved->bindings, &saved->arena,
            saved->returned.outcome.term)), "(closure 7 7 7)") == 0);
    release_capture(saved);
    assert(account.freed == 3u);
    puts("PASS: resident capture retries one earned occurrence without polling, moving or releasing its producer");
}

int main(void) {
    assert(setenv("CETTA_OPEN_EQUATIONS_REFERENCE", "1", 1) == 0);
    assert(setenv("CETTA_PREPARED_PURE_REFERENCE", "1", 1) == 0);
    test_credit_allocation_refusal();
    SymbolTable symbols;
    VarInternTable variables;
    symbol_table_init(&symbols);
    symbol_table_init_builtins(&symbols, &g_builtin_syms);
    g_symbols = &symbols;
    var_intern_init(&variables);
    g_var_intern = &variables;
    test_refusal_and_owned_graph();
    test_authority_and_detachment();
    test_construction_and_terminal_states();
    test_pending_return_detachment();
    test_resident_capture();
    test_real_prepared_producer(false);
    test_real_prepared_producer(true);
    test_native_return_cursor();
    test_native_evaluator_boundary();
    test_invocation_operands();
    symbol_table_free(&symbols);
    var_intern_free(&variables);
    g_symbols = NULL;
    g_var_intern = NULL;
    return 0;
}
