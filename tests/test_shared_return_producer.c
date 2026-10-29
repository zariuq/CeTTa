#define _POSIX_C_SOURCE 200809L
#include "shared_return_producer.h"
#include "owned_return_image.h"
#include "grounded.h"
#include "foreign.h"
#include "library.h"
#include "parser.h"
#include "prepared_pure_machine.h"
#include "petta_program.h"
#include "stats.h"
#include "binding/frame_identity.h"
#include "binding/term_cursor.h"

#ifdef NDEBUG
#undef NDEBUG
#endif
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static unsigned fail_malloc, fail_calloc;
static unsigned native_mode, native_calls;
static SymbolId projection_head = SYMBOL_ID_NONE;
static unsigned projection_calls;
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
static bool oem_native_fixture;
static bool oem_native_enabled;

static void native_fixture_outcome(Arena *arena, CettaCallOutcome *outcome) {
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
    case 13: *outcome = cetta_call_completed_frontier(atom_unit(arena)); break;
    case 14: *outcome = cetta_call_completed_value(atom_symbol(arena, "Empty")); break;
    case 15:
    case 16: {
        Atom *quoted = atom_expr2(arena, atom_symbol_id(arena, g_builtin_syms.quote), code);
        Atom *rows = atom_expr2(arena, atom_symbol_id(arena, g_builtin_syms.quote),
                               atom_expr2(arena, quoted, quoted));
        *outcome = native_mode == 15u ? cetta_call_completed_value(rows)
            : cetta_call_completed_frontier(atom_expr2(arena, rows, rows));
        break;
    }
    default: abort();
    }
}

bool __real_grounded_call(Arena *arena, Atom *head, Atom **args,
                          uint32_t arity, CettaCallOutcome *outcome);
bool __wrap_grounded_call(Arena *arena, Atom *head, Atom **args,
                         uint32_t arity, CettaCallOutcome *outcome) {
    if (!native_mode ||
        (!atom_is_symbol_id(head, g_builtin_syms.readln_bang) &&
         !atom_is_symbol_id(head, projection_head)))
        return __real_grounded_call(arena, head, args, arity, outcome);
    ++native_calls;
    if (atom_is_symbol_id(head, projection_head))
        projection_calls++;
    /* A broken boundary must fail the count assertion rather than loop. */
    if (native_calls > 3u) {
        *outcome = cetta_call_interrupted();
        return true;
    }
    native_fixture_outcome(arena, outcome);
    return true;
}
bool __real_cetta_library_call_native(CettaLibraryContext *context, Space *space,
    Arena *arena, Atom *head, Atom **arguments, uint32_t arity,
    CettaCallOutcome *outcome);
bool __wrap_cetta_library_call_native(CettaLibraryContext *context, Space *space,
    Arena *arena, Atom *head, Atom **arguments, uint32_t arity,
    CettaCallOutcome *outcome) {
    if (!oem_native_fixture || !native_mode ||
        !atom_is_symbol_id(head, g_builtin_syms.lib_str))
        return __real_cetta_library_call_native(context, space, arena, head,
                                                 arguments, arity, outcome);
    ++native_calls;
    if (native_calls > 3u)
        *outcome = cetta_call_interrupted();
    else
        native_fixture_outcome(arena, outcome);
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

static bool copy_owned_buffer_row(void *context, uint64_t position,
    const CettaOccurrenceRow *source, CettaOccurrenceRow *out) {
    (void)position;
    CettaSharedReturnRow image = {0};
    if (!cetta_shared_return_capture_image(context, source->value, &image))
        return false;
    *out = (CettaOccurrenceRow){.value = image.returned,
        .owner = image.owner, .release = image.release};
    return true;
}

typedef struct {
    CettaOwnedReturnImage *rows[2];
    size_t length;
} CopiedBufferRecipient;

static bool accept_copied_buffer(void *context, uint64_t position, const void *value) {
    CopiedBufferRecipient *recipient = context;
    assert(recipient->length < 2u && position == recipient->length + 1u);
    CettaOwnedReturnImage *image = cetta_owned_return_image_from_view(value);
    if (!image || !cetta_owned_return_image_retain(image))
        return false;
    recipient->rows[recipient->length++] = image;
    return true;
}

static void test_copied_return_buffer(void) {
    Account account = {.compatible = true};
    CettaOwnedExecution execution = new_execution(&account, 4u);
    Computation *computation = execution.computation;
    Atom *term = computation->packet.outcome.term;
    Atom *parts[] = {term->expr.elems[0], term->expr.elems[1],
        term->expr.elems[2], term->expr.elems[2]};
    computation->packet.outcome.term = atom_expr(&computation->arena, parts, 4u);
    computation->packet.outcome.delayed = atom_expr2(&computation->arena,
        computation->packet.outcome.delayed->expr.elems[0], parts[1]);
    CettaSharedReturnProducer *producer = cetta_shared_return_producer_new(
        &execution, 2u, 3u, capture, compatible, &account);
    assert(producer);
    for (unsigned i = 0u; i < 3u; i++)
        assert(cetta_shared_return_producer_advance(producer, 1u) == CETTA_SHARED_RETURN_PAUSED);
    assert(account.effects == 3u && account.next_calls == 3u && account.captures == 3u);
    CettaSharedReturnCheckpoint *bookmark = cetta_shared_return_checkpoint_new_scoped(producer, 0u);
    Recipient recipient = {.producer = producer, .consumer = 0u};
    assert(bookmark && take_answer(producer, 0u, &recipient, 0u).kind == CETTA_SHARED_DELIVERY_ANSWER);
    release_recipient(&recipient);
    assert(cetta_shared_return_producer_cancel(producer, 1u) &&
        cetta_shared_return_producer_trim(producer) == 0u);
    const CettaOccurrenceLog *source = cetta_shared_return_producer_log(producer);
    CettaOccurrenceRow original;
    assert(cetta_occurrence_log_row(source, 1u, &original));
    const CettaOwnedReturn *borrowed = original.value;
    assert(borrowed->outcome.term->expr.elems[1] == borrowed->outcome.delayed->expr.elems[1] &&
        borrowed->outcome.term->expr.elems[2] == borrowed->outcome.term->expr.elems[3]);
    CettaOccurrenceLog *image = cetta_occurrence_log_copy(source, copy_owned_buffer_row, NULL);
    assert(image && cetta_occurrence_log_valid(image));
    assert(account.effects == 3u && account.next_calls == 3u && account.captures == 3u);
    CettaOccurrenceLogInfo info;
    assert(cetta_occurrence_log_info(image, &info) && info.rows == 3u &&
        info.consumers == 3u && !info.completed);
    assert(cetta_occurrence_log_peek(image, 0u).position == 1u &&
        cetta_occurrence_log_peek(image, 1u).kind == CETTA_OCCURRENCE_CANCELLED &&
        cetta_occurrence_log_peek(image, 2u).kind == CETTA_OCCURRENCE_INVALID);
    cetta_shared_return_producer_free(producer);
    assert(account.released == 1u && account.freed == 3u && account.effects == 3u);
    assert(cetta_shared_return_checkpoint_peek(bookmark).kind == CETTA_SHARED_DELIVERY_INVALID);
    cetta_shared_return_checkpoint_release(bookmark);

    Arena observed;
    arena_init_detached(&observed);
    CopiedBufferRecipient accepted = {0};
    for (uint64_t position = 1u; position < 3u; position++) {
        CettaOccurrenceReply reply = cetta_occurrence_log_peek(image, 0u);
        const CettaOwnedReturn *returned = reply.value;
        assert(reply.kind == CETTA_OCCURRENCE_ANSWER && reply.position == position &&
            returned && returned->outcome.kind == CETTA_CALL_SUSPENDED && returned->environment);
        Atom *term = returned->outcome.term, *delayed = returned->outcome.delayed;
        assert(term->expr.elems[1] == delayed->expr.elems[1] &&
            term->expr.elems[2] == term->expr.elems[3]);
        Atom *value = bindings_apply((Bindings *)returned->environment, &observed, term);
        Atom *goals = bindings_apply((Bindings *)returned->environment, &observed, delayed);
        assert(strcmp(atom_to_string(&observed, value), "(closure 7 7 7)") == 0 &&
            strcmp(atom_to_string(&observed, goals), "(check 7)") == 0);
        assert(cetta_occurrence_log_take(image, 0u, accept_copied_buffer, &accepted).position == position);
    }
    /* Copying a buffered prefix neither runs the fourth effect nor proves
     * the unfinished source complete when that prefix has been consumed. */
    assert(cetta_occurrence_log_peek(image, 0u).kind == CETTA_OCCURRENCE_PAUSED &&
        account.effects == 3u && account.next_calls == 3u);
    assert(cetta_occurrence_log_pin_release(image, 2u) && cetta_occurrence_log_trim(image) == 3u);
    cetta_occurrence_log_free(image);
    assert(accepted.length == 2u);
    for (size_t i = 0u; i < accepted.length; i++) {
        const CettaOwnedReturn *returned = cetta_owned_return_image_view(accepted.rows[i]);
        assert(strcmp(atom_to_string(&observed, bindings_apply(
            (Bindings *)returned->environment, &observed, returned->outcome.delayed)), "(check 7)") == 0);
        cetta_owned_return_image_release(accepted.rows[i]);
    }
    arena_free(&observed);
    puts("PASS: copied return buffers preserve reader positions, aliases and delayed bindings after source retirement without replay or false exhaustion");
}

typedef struct {
    CettaSharedReturnProducer *source, *image;
    const CettaSharedReturnCheckpoint *source_positions[3];
    CettaSharedReturnCheckpoint *positions[3];
    Account account;
    size_t mapped, row_calls, execution_calls, compatibility_checks;
    unsigned refusal;
} SharedCopyFixture;

static bool map_shared_producer(void *raw, const CettaSharedReturnProducer *source,
    CettaSharedReturnProducer *image) {
    SharedCopyFixture *copy = raw;
    assert(source == copy->source && source != image);
    copy->image = image;
    assert(cetta_shared_return_producer_advance(image, 1u) == CETTA_SHARED_RETURN_INVALID &&
        cetta_shared_return_producer_peek(image, 0u).kind == CETTA_SHARED_DELIVERY_INVALID &&
        !cetta_shared_return_producer_execution(image) && !cetta_shared_return_producer_log(image) &&
        !cetta_shared_return_producer_cancel(image, 0u) && !cetta_shared_return_producer_trim(image) &&
        !cetta_shared_return_checkpoint_new(image, 0u));
    assert(!copy->compatibility_checks);
    return copy->refusal != 1u;
}

static bool map_shared_checkpoint(void *raw, const CettaSharedReturnCheckpoint *source,
    CettaSharedReturnCheckpoint *image) {
    SharedCopyFixture *copy = raw;
    uint64_t before, after;
    assert(source != image && cetta_shared_return_checkpoint_position(source, &before) &&
        cetta_shared_return_checkpoint_position(image, &after) && before == after);
    assert(cetta_shared_return_checkpoint_peek(image).kind == CETTA_SHARED_DELIVERY_INVALID &&
        cetta_shared_return_checkpoint_advance(image, 1u) == CETTA_SHARED_RETURN_INVALID);
    assert(!copy->compatibility_checks);
    if (copy->refusal == 2u && copy->mapped == 1u)
        return false;
    if (copy->refusal == 6u)
        return true; /* A map without a receiving owner must be refused. */
    assert(copy->mapped < 3u && cetta_shared_return_checkpoint_retain(image));
    copy->source_positions[copy->mapped] = source;
    copy->positions[copy->mapped++] = image;
    return true;
}

static bool copy_shared_row(void *raw, uint64_t position,
    const CettaOccurrenceRow *source, CettaOccurrenceRow *out) {
    SharedCopyFixture *copy = raw;
    (void)position;
    if (++copy->row_calls == 2u && copy->refusal == 3u)
        return false;
    CettaSharedReturnRow image = {0};
    if (!capture(&copy->account, source->value, &image))
        return false;
    *out = (CettaOccurrenceRow){.value = image.returned, .owner = image.owner,
        .release = image.release};
    return true;
}

static void hold_copied_packet(void *raw) {
    bool held = cetta_owned_return_image_retain(raw);
    assert(held);
}

static bool copy_shared_execution(void *raw, const CettaOwnedExecution *source,
    CettaOwnedExecution *out) {
    SharedCopyFixture *copy = raw;
    copy->execution_calls++;
    if (copy->refusal == 4u)
        return false;
    if (copy->refusal == 7u) {
        *out = *source; /* Borrowing the actual source is not copying it. */
        return true;
    }
    const Computation *original = source->computation;
    Computation *image = calloc(1u, sizeof(*image));
    if (!image)
        return false;
    image->account = &copy->account;
    image->count = original->count;
    image->pause_once = original->pause_once;
    image->incomplete = original->incomplete;
    image->handoff = original->handoff;
    image->change_during_next = original->change_during_next;
    arena_init_detached(&image->arena);
    bindings_init(&image->bindings);
    CettaOwnedReturnImage *packet = cetta_owned_return_image_capture(&original->packet, NULL);
    if (!packet) {
        release_computation(image);
        return false;
    }
    if (!arena_retain_owner(&image->arena, packet, hold_copied_packet,
            cetta_owned_return_image_release)) {
        cetta_owned_return_image_release(packet);
        release_computation(image);
        return false;
    }
    image->packet = *cetta_owned_return_image_view(packet);
    cetta_owned_return_image_release(packet);
    copy->account.emitted = original->account->emitted;
    *out = *source;
    out->computation = image;
    out->pending = source->has_pending ? image->packet : (CettaOwnedReturn){0};
    if (source->has_pending)
        assert(source->pending.outcome.term == original->packet.outcome.term &&
            source->pending.outcome.delayed == original->packet.outcome.delayed &&
            source->pending.environment == original->packet.environment);
    if (copy->refusal == 5u)
        out->accepted++;
    return true;
}

static bool copied_producer_capture(void *raw, const CettaOwnedReturn *returned,
    CettaSharedReturnRow *out) {
    SharedCopyFixture *copy = raw;
    return capture(&copy->account, returned, out);
}

static bool copied_producer_compatible(void *raw) {
    SharedCopyFixture *copy = raw;
    copy->compatibility_checks++;
    return compatible(&copy->account);
}

static CettaSharedReturnCopy shared_copy_policy(SharedCopyFixture *copy) {
    return (CettaSharedReturnCopy){.context = copy,
        .producer = map_shared_producer, .checkpoint = map_shared_checkpoint,
        .execution = copy_shared_execution, .row = copy_shared_row,
        .capture = copied_producer_capture, .compatible = copied_producer_compatible,
        .image_context = copy};
}

static void release_copied_checkpoints(SharedCopyFixture *copy, bool refused) {
    for (size_t i = 0u; i < copy->mapped; i++) {
        if (refused)
            assert(cetta_shared_return_checkpoint_peek(copy->positions[i]).kind ==
                CETTA_SHARED_DELIVERY_INVALID);
        cetta_shared_return_checkpoint_release(copy->positions[i]);
        copy->positions[i] = NULL;
    }
    copy->mapped = 0u;
}

static bool accept_checkpoint(void *context, uint64_t position,
    const CettaOwnedReturn *returned, CettaSharedReturnCheckpoint *next) {
    (void)context; (void)position; (void)returned; (void)next;
    return true;
}

static void test_producer_copy(void) {
    Account source_account = {.compatible = true};
    CettaOwnedExecution execution = new_execution(&source_account, 4u);
    CettaSharedReturnProducer *source = cetta_shared_return_producer_new(
        &execution, 2u, 3u, capture, compatible, &source_account);
    assert(source);
    CettaSharedReturnCheckpoint *first = cetta_shared_return_checkpoint_new(source, 0u);
    CettaSharedReturnCheckpoint *scoped = cetta_shared_return_checkpoint_new_scoped(source, 0u);
    for (unsigned i = 0u; i < 3u; i++)
        assert(cetta_shared_return_producer_advance(source, 1u) == CETTA_SHARED_RETURN_PAUSED);
    CettaSharedReturnCheckpoint *later = NULL;
    assert(first && scoped && cetta_shared_return_checkpoint_take(first,
        accept_checkpoint, NULL, &later).kind == CETTA_SHARED_DELIVERY_ANSWER);
    Recipient recipient = {.producer = source, .consumer = 0u};
    for (uint64_t i = 0u; i < 3u; i++)
        take_answer(source, 0u, &recipient, i);
    release_recipient(&recipient);
    assert(cetta_shared_return_producer_cancel(source, 1u));

    for (unsigned refusal = 1u; refusal < 8u; refusal++) {
        SharedCopyFixture failed = {.source = source, .account = {.compatible = true},
            .refusal = refusal};
        CettaSharedReturnCopy policy = shared_copy_policy(&failed);
        assert(!cetta_shared_return_producer_copy(source, &policy));
        release_copied_checkpoints(&failed, true);
        assert(!failed.compatibility_checks);
        assert(source_account.effects == 3u && source_account.next_calls == 3u &&
            !source_account.released && !source_account.freed);
        assert(cetta_shared_return_checkpoint_peek(first).position == 0u &&
            cetta_shared_return_checkpoint_peek(later).position == 1u &&
            log_info(source).rows == 3u &&
            cetta_shared_return_producer_execution(source)->accepted == 3u);
        assert(failed.account.freed == failed.account.captures);
    }
    for (unsigned failure = 0u; failure < 5u; failure++) {
        SharedCopyFixture failed = {.source = source, .account = {.compatible = true}};
        CettaSharedReturnCopy policy = shared_copy_policy(&failed);
        if (failure < 2u) fail_calloc = failure + 1u;
        else fail_malloc = failure - 1u;
        assert(!cetta_shared_return_producer_copy(source, &policy) && !fail_calloc && !fail_malloc);
        release_copied_checkpoints(&failed, true);
        assert(source_account.effects == 3u && !source_account.released && log_info(source).rows == 3u);
    }

    SharedCopyFixture copied = {.source = source, .account = {.compatible = true}};
    CettaSharedReturnCopy policy = shared_copy_policy(&copied);
    CettaSharedReturnProducer *image = cetta_shared_return_producer_copy(source, &policy);
    assert(image && image == copied.image && copied.mapped == 3u && copied.execution_calls == 1u &&
        !copied.compatibility_checks &&
        source_account.effects == 3u && source_account.next_calls == 3u && !copied.account.effects &&
        !copied.account.next_calls && copied.account.emitted == 3u);
    assert(cetta_shared_return_producer_execution(image)->accepted == 3u &&
        cetta_shared_return_producer_peek(image, 0u).kind == CETTA_SHARED_DELIVERY_PAUSED &&
        cetta_shared_return_producer_advance(image, 1u) == CETTA_SHARED_RETURN_BACKPRESSURE);
    cetta_shared_return_producer_free(source);
    cetta_shared_return_checkpoint_release(first);
    cetta_shared_return_checkpoint_release(scoped);
    cetta_shared_return_checkpoint_release(later);
    assert(source_account.released == 1u && source_account.freed == 3u);
    for (size_t i = 0u; i < copied.mapped; i++) {
        uint64_t position;
        assert(cetta_shared_return_checkpoint_position(copied.positions[i], &position));
        if (!position) {
            cetta_shared_return_checkpoint_release(copied.positions[i]);
            copied.positions[i] = NULL;
        }
    }
    CettaSharedReturnCheckpoint *retained = NULL;
    for (size_t i = 0u; i < copied.mapped; i++)
        if (copied.positions[i]) retained = copied.positions[i];
    assert(retained);
    cetta_shared_return_producer_free(image);
    assert(!copied.account.released &&
        cetta_shared_return_checkpoint_advance(retained, 1u) == CETTA_SHARED_RETURN_PAUSED &&
        copied.account.effects == 1u && copied.account.next_calls == 1u);
    Recipient image_recipient = {.producer = image, .consumer = 0u};
    take_answer(image, 0u, &image_recipient, 3u);
    release_recipient(&image_recipient);
    CettaSharedReturnCheckpoint *at_end = cetta_shared_return_checkpoint_new(image, 0u);
    assert(at_end);
    release_copied_checkpoints(&copied, false);
    assert(cetta_shared_return_producer_trim(image) == 3u &&
        cetta_shared_return_checkpoint_advance(at_end, 1u) == CETTA_SHARED_RETURN_COMPLETE &&
        copied.account.effects == 1u && copied.account.next_calls == 2u &&
        cetta_shared_return_producer_execution(image)->accepted == 4u);
    cetta_shared_return_checkpoint_release(at_end);
    assert(copied.account.released == 1u && copied.account.freed == 4u);
    puts("PASS: shared producer copies preserve buffered roots, checkpoint ownership, backpressure and paid continuation without replay; partial or borrowed images refuse");
}

static void test_pending_producer_copy(void) {
    Account account = {.compatible = true, .refuse_capture = true};
    CettaOwnedExecution execution = new_execution(&account, 2u);
    CettaSharedReturnProducer *source = cetta_shared_return_producer_new(
        &execution, 1u, 2u, capture, compatible, &account);
    assert(source && cetta_shared_return_producer_advance(source, 1u) == CETTA_SHARED_RETURN_CAPTURE_REFUSED);
    SharedCopyFixture copied = {.source = source, .account = {.compatible = true}};
    CettaSharedReturnCopy policy = shared_copy_policy(&copied);
    CettaSharedReturnProducer *image = cetta_shared_return_producer_copy(source, &policy);
    assert(image && cetta_shared_return_producer_execution(image)->has_pending &&
        !cetta_shared_return_producer_execution(image)->accepted &&
        cetta_shared_return_producer_execution(source)->has_pending);
    assert(cetta_shared_return_producer_advance(image, 1u) == CETTA_SHARED_RETURN_PAUSED &&
        !cetta_shared_return_producer_execution(image)->has_pending &&
        cetta_shared_return_producer_execution(image)->accepted == 1u &&
        !copied.account.next_calls && !copied.account.effects &&
        account.next_calls == 1u && account.effects == 1u);
    cetta_shared_return_producer_free(source);
    cetta_shared_return_producer_free(image);
    assert(account.released == 1u && copied.account.released == 1u && copied.account.freed == 1u);
    puts("PASS: a copied pending return is accepted without polling or repeating its already-paid effect");
}

static void test_terminal_producer_copy(bool detached) {
    Account account = {.compatible = true};
    CettaOwnedExecution execution = new_execution(&account, 1u), tail = {0};
    CettaSharedReturnProducer *source = cetta_shared_return_producer_new(
        &execution, 1u, 2u, capture, compatible, &account);
    assert(source && cetta_shared_return_producer_advance(source, 1u) == CETTA_SHARED_RETURN_PAUSED);
    if (detached)
        assert(cetta_shared_return_producer_detach_execution(source, &tail));
    else
        assert(cetta_shared_return_producer_advance(source, 1u) == CETTA_SHARED_RETURN_COMPLETE);
    SharedCopyFixture copied = {.source = source, .account = {.compatible = true}};
    CettaSharedReturnCopy policy = shared_copy_policy(&copied);
    if (detached) policy.execution = NULL;
    CettaSharedReturnProducer *image = cetta_shared_return_producer_copy(source, &policy);
    assert(image && !copied.mapped &&
        copied.execution_calls == (detached ? 0u : 1u) &&
        cetta_shared_return_producer_advance(image, 1u) ==
            (detached ? CETTA_SHARED_RETURN_DETACHED : CETTA_SHARED_RETURN_COMPLETE) &&
        !copied.account.next_calls && !copied.account.effects);
    assert(detached ? !cetta_shared_return_producer_execution(image) :
        cetta_shared_return_producer_execution(image)->accepted == 1u);
    cetta_shared_return_producer_free(source);
    cetta_owned_execution_cancel(&tail);
    assert(account.released == 1u && account.freed == 1u);
    Recipient recipient = {.producer = image, .consumer = 0u};
    take_answer(image, 0u, &recipient, 0u);
    release_recipient(&recipient);
    assert(cetta_shared_return_producer_peek(image, 0u).kind ==
        (detached ? CETTA_SHARED_DELIVERY_PAUSED : CETTA_SHARED_DELIVERY_COMPLETED));
    assert(cetta_shared_return_producer_trim(image) == 1u);
    cetta_shared_return_producer_free(image);
    assert(copied.account.released == (detached ? 0u : 1u) && copied.account.freed == 1u);
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

typedef struct {
    Arena *arena;
    CettaCallReturnCursor body;
    unsigned entries, answers;
    bool refuse;
} PreparedBind;

static bool prepared_bind_body(void *context, const CettaOwnedReturn *returned,
        CettaOwnedExecution *execution) {
    PreparedBind *bind = context;
    bind->entries++;
    bind->body = (CettaCallReturnCursor){
        .outcome = cetta_call_completed_frontier(atom_expr2(
            bind->arena, returned->outcome.term, returned->outcome.term)),
        .environment = returned->environment,
    };
    cetta_owned_execution_init(execution, &bind->body, cetta_call_return_next, NULL);
    return true;
}

static bool prepared_bind_publish(void *context, const CettaOwnedReturn *returned) {
    PreparedBind *bind = context;
    if (bind->refuse)
        return false;
    const char *expected[] = {"(pair 7 7)", "(pair 7 7)",
        "(pair 7 7)", "(pair 7 7)", "(pair 9 9)", "(pair 9 9)"};
    assert(bind->answers < 6u && returned->outcome.kind == CETTA_CALL_VALUE);
    assert(strcmp(atom_to_string(bind->arena, returned->outcome.term),
                  expected[bind->answers++]) == 0);
    return true;
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
    CettaPreparedPureAnswerCursor *bound_cursor =
        cetta_prepared_pure_answer_cursor_open(program, &options);
    assert(bound_cursor);
    CettaOwnedExecution source_execution = {0}, bound_execution = {0};
    assert(cetta_prepared_pure_answer_cursor_own_execution(&source_execution, bound_cursor));
    CettaOwnedBind sequence;
    PreparedBind bound = {.arena = &source, .refuse = true};
    assert(cetta_owned_bind_open(&bound_execution, &sequence, &source_execution,
                                 prepared_bind_body, &bound));
    assert(cetta_owned_execution_sequence(&bound_execution, 1u,
        prepared_bind_publish, &bound) == CETTA_OWNED_EXECUTION_PAUSED);
    assert(bound.entries == 1u && !bound.answers);
    bound.refuse = false;
    CettaOwnedExecutionStep bound_step = CETTA_OWNED_EXECUTION_PAUSED;
    for (unsigned i = 0u; bound_step == CETTA_OWNED_EXECUTION_PAUSED && i < 16u; i++)
        bound_step = cetta_owned_execution_sequence(&bound_execution, 1u,
            prepared_bind_publish, &bound);
    assert(bound_step == CETTA_OWNED_EXECUTION_COMPLETE && bound.entries == 3u &&
           bound.answers == 6u);
    cetta_owned_execution_cancel(&bound_execution);
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
        bool select_expressions;
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
        {cetta_call_expression_frontier(frontier), values, 5u, CETTA_CALL_VALUE,
         CETTA_CALL_RESULT_EXPRESSION, CETTA_EVAL_COMPLETE, NULL, true},
        {cetta_call_expression_frontier(frontier), &frontier, 1u, CETTA_CALL_VALUE,
         CETTA_CALL_RESULT_EXPRESSION_FRONTIER, CETTA_EVAL_COMPLETE, NULL},
        {cetta_call_completed_value(frontier), &frontier, 1u, CETTA_CALL_VALUE,
         CETTA_CALL_RESULT_COMPLETED_VALUE, CETTA_EVAL_COMPLETE, NULL},
        {cetta_call_completed_frontier(empty_frontier), NULL, 0u, CETTA_CALL_VALUE,
         CETTA_CALL_RESULT_COMPLETED_VALUE, CETTA_EVAL_COMPLETE, NULL},
        {cetta_call_expression_frontier(empty_frontier), NULL, 0u, CETTA_CALL_VALUE,
         CETTA_CALL_RESULT_EXPRESSION, CETTA_EVAL_COMPLETE, NULL, true},
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
        {cetta_call_expression_frontier(broken_frontier), values, 1u,
         CETTA_CALL_VALUE, CETTA_CALL_RESULT_EXPRESSION,
         CETTA_EVAL_INCOMPLETE_HOST_FAILURE, NULL, true},
    };
    size_t checks = 0u;
    for (size_t i = 0u; i < sizeof(cases) / sizeof(cases[0]); ++i) {
        CettaCallReturnCursor cursor = {
            .outcome = cases[i].call, .environment = &environment,
            .select_expressions = cases[i].select_expressions,
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

static void test_native_evaluator_boundary(bool oem) {
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
        if (oem) {
            Atom *import_error = NULL;
            assert(cetta_library_import(context, "str", &space, &arena, &arena,
                &registry, -1, &import_error));
            assert(!import_error);
            const char *equations[] = {
                "(= (native-packet $x) (case $x ((go "
                "(__cetta_lib_str byte-slice \"abc\" 0 1)))))",
                "(= (native-follow $x) (case $x ((go (let $unused "
                "(__cetta_lib_str byte-slice \"abc\" 0 1) (readln!))))))",
            };
            for (unsigned index = 0u; index < 2u; index++) {
                Atom *equation = parse(&arena, equations[index]);
                if (!dialect) {
                    assert(context->petta_program);
                    const PettaPlanNode *plan = petta_program_plan_dynamic_add(
                        context->petta_program, equation);
                    assert(plan);
                    CettaCount before = space_length64(&space);
                    space_add(&space, equation);
                    Atom *stored = space_get_at64(&space, before);
                    assert(stored && petta_program_note_add(
                        context->petta_program, &space, stored, plan));
                } else {
                    assert(space_admit_atom(&space, &arena, equation));
                }
            }
        }
        for (unsigned mode = 1u; mode <= 14u; mode++) {
            if (dialect && (mode == 7u || mode == 14u))
                continue;
            native_mode = mode;
            native_calls = 0u;
            bool observes_zero = (mode >= 3u && mode <= 5u) ||
                (mode >= 10u && mode <= 13u);
            Atom *source = parse(&arena, oem
                ? observes_zero ? "(native-follow go)" : "(native-packet go)"
                : observes_zero ? "(let $ignored (readln!) (readln!))" : "(readln!)");
            EvalOutcome outcome;
            eval_outcome_init(&outcome);
#if CETTA_BUILD_WITH_RUNTIME_STATS
            cetta_runtime_stats_reset();
            cetta_runtime_stats_enable();
#endif
            eval_top_with_registry_outcome(&space, &arena, &arena, &registry,
                                          source, &outcome, NULL, NULL);
            if (mode == 6u && outcome.completion != CETTA_EVAL_COMPLETE)
                fprintf(stderr, "suspended boundary dialect=%u completion=%u rows=%llu\n",
                        dialect, (unsigned)outcome.completion,
                        (unsigned long long)outcome.results.len);
#if CETTA_BUILD_WITH_RUNTIME_STATS
            CettaRuntimeStats stats;
            cetta_runtime_stats_snapshot(&stats);
            uint64_t entered = stats.counters[
                CETTA_RUNTIME_COUNTER_OPEN_EQUATION_NATIVE_CALL];
            uint64_t expected_entry = oem_native_enabled && !dialect ? 1u : 0u;
            if (entered != expected_entry)
                fprintf(stderr, "native route dialect=%u mode=%u entered=%llu expected=%llu\n",
                    dialect, mode, (unsigned long long)entered,
                    (unsigned long long)expected_entry);
            assert(entered == expected_entry);
#endif
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
            } else if (mode == 14u) {
                assert(outcome.completion == CETTA_EVAL_COMPLETE);
                assert(outcome.results.len == 1u);
                assert(atom_is_symbol_id(outcome.results.items[0], g_builtin_syms.empty));
            } else if (mode == 3u || mode == 13u) {
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
                if (outcome.completion != CETTA_EVAL_COMPLETE)
                    fprintf(stderr, "native boundary dialect=%u mode=%u completion=%u rows=%llu\n",
                            dialect, mode, (unsigned)outcome.completion,
                            (unsigned long long)outcome.results.len);
                assert(outcome.completion == CETTA_EVAL_COMPLETE);
                assert(outcome.results.len == 1u);
                assert(atom_is_error(outcome.results.items[0]));
                assert(mode == 9u ? !outcome.petta_raised_error
                                 : dialect || outcome.petta_raised_error);
            }
            eval_outcome_free(&outcome);
        }
        if (oem && !dialect) {
            const char *observers[] = {
                "(once (native-packet go))",
                "(collapse (native-packet go))",
                "(let $callee native-packet ($callee go))",
                "(let $callee native-packet ($callee go))",
            };
            const unsigned modes[] = {2u, 2u, 8u, 9u};
            for (unsigned observer = 0u; observer < 4u; observer++) {
                native_mode = modes[observer];
                native_calls = 0u;
                EvalOutcome outcome;
                eval_outcome_init(&outcome);
                eval_top_with_registry_outcome(&space, &arena, &arena, &registry,
                    parse(&arena, observers[observer]), &outcome, NULL, NULL);
                assert(native_calls == 1u);
                assert(outcome.completion == CETTA_EVAL_COMPLETE);
                assert(!outcome.petta_raised_error);
                assert(outcome.results.len == (observer == 2u ? 0u : 1u));
                if (observer == 1u) {
                    Atom *frontier = outcome.results.items[0];
                    assert(frontier->kind == ATOM_EXPR && frontier->expr.len == 3u);
                    assert(atom_eq(frontier->expr.elems[0], frontier->expr.elems[1]));
                    assert(atom_is_error(frontier->expr.elems[2]));
                } else if (observer == 3u) {
                    assert(atom_is_error(outcome.results.items[0]));
                } else if (observer == 0u) {
                    assert(atom_eq(outcome.results.items[0], parse(&arena,
                        "(function (return (readln!)))")));
                }
                eval_outcome_free(&outcome);
            }
            puts("PASS: entered native once, collection and dynamic recovery observers preserve prefixes and error data");
        }
        native_mode = 0u;
#if CETTA_BUILD_WITH_PYTHON
        if (!dialect) {
            /* PeTTa py-call accepts a call expression alone. No matching
             * clause is an entered zero-answer call, not the datum Empty. */
            Atom *argument = atom_symbol(&arena, "not-a-call-expression");
            CettaCallOutcome no_answer;
            assert(cetta_foreign_call_native(context->foreign_runtime, &space,
                &arena, atom_symbol_id(&arena, g_builtin_syms.py_call),
                &argument, 1u, &no_answer));
            assert(no_answer.kind == CETTA_CALL_FAILURE);
            puts("PASS: entered foreign zero is tagged failure independently of Empty data");
        }
#endif
        registry_free(&registry);
        space_free(&space);
        term_universe_free(&universe);
        cetta_library_context_free(context);
        eval_set_library_context(NULL);
        free(context);
        arena_free(&arena);
    }
    printf("PASS: 26 real evaluator boundaries preserve entered-call effects, tagged data, errors, constraints and interruption without replay (%s)\n",
        oem ? "library entry and dialect fallback" : "ordinary entry");
}

static void test_native_projection_consumers(void) {
    unsigned checks = 0u;
    const char *setting = getenv("CETTA_PETTA_SEARCH_MACHINE");
    char *previous = setting ? strdup(setting) : NULL;
    assert(!setting || previous);
    for (unsigned dialect = 0u; dialect < 2u; dialect++) {
        CettaLibraryContext *context = calloc(1u, sizeof(*context));
        assert(context);
        cetta_library_context_init_for_language_profile(context,
            dialect ? CETTA_LANGUAGE_HE : CETTA_LANGUAGE_PETTA, NULL);
        eval_set_library_context(context);
        Arena arena;
        arena_init(&arena);
        TermUniverse universe;
        term_universe_init(&universe);
        term_universe_set_persistent_arena(&universe, &arena);
        Space space;
        space_init_with_universe(&space, &universe);
        Registry registry;
        registry_init(&registry);
        registry_bind_id(&registry, g_builtin_syms.self, atom_space(&arena, &space));
        const SymbolId heads[] = {g_builtin_syms.lib_mork_space_atoms,
            g_builtin_syms.lib_mork_space_add_atoms, g_builtin_syms.lib_list_range};
        const char *sources[] = {"(mork:get-atoms opaque)",
            "(__cetta_lib_mork_space_add_atoms opaque ())", "(__cetta_lib_list_range (readln!) 1 2)"};
        const unsigned modes[] = {1u, 2u, 3u, 4u, 5u, 6u,
            8u, 9u, 10u, 11u, 13u, 14u, 15u, 16u};
        for (unsigned client = 0u; client < 3u; client++) {
            /* PeTTa's eager/over-application protocol can evaluate arguments
             * before this native arity consumer. HE owns the preflight. */
            if (client == 2u && !dialect)
                continue;
            /* Arity preflight is a canonical source consumer. The search
             * machine also owns partial/over-application, a different call
             * protocol from this already rejected native entrance. */
            assert(setenv("CETTA_PETTA_SEARCH_MACHINE", client == 2u ? "0" : "1", 1) == 0);
            eval_set_library_context(context);
            projection_head = heads[client];
            for (unsigned index = 0u; index < sizeof(modes) / sizeof(*modes); index++) {
                native_mode = modes[index];
                if ((client == 0u && (native_mode == 1u || native_mode == 2u)) ||
                    (client != 0u && native_mode >= 15u))
                    continue;
                native_calls = 0u;
                projection_calls = 0u;
                EvalOutcome outcome;
                eval_outcome_init(&outcome);
                eval_top_with_registry_outcome(&space, &arena, &arena, &registry,
                    parse(&arena, sources[client]), &outcome, NULL, NULL);
                if (native_calls != 1u || projection_calls != 1u)
                    fprintf(stderr, "native projection dialect=%u client=%u mode=%u calls=%u entries=%u\n",
                        dialect, client, native_mode, native_calls, projection_calls);
                assert(native_calls == 1u && projection_calls == 1u);
                bool invalid = native_mode == 5u ||
                    native_mode == 10u || native_mode == 11u;
                CettaEvalCompletion expected_completion = native_mode == 4u
                    ? CETTA_EVAL_INCOMPLETE_CANCELLED : invalid
                    ? CETTA_EVAL_INCOMPLETE_HOST_FAILURE : CETTA_EVAL_COMPLETE;
                if (outcome.completion != expected_completion)
                    fprintf(stderr, "native projection dialect=%u client=%u mode=%u completion=%u expected=%u\n",
                        dialect, client, native_mode, outcome.completion, expected_completion);
                assert(outcome.completion == expected_completion);
                if (native_mode == 8u) {
                    assert(dialect || outcome.petta_raised_error);
                } else {
                    assert(!outcome.petta_raised_error);
                    bool zero = native_mode == 3u || native_mode == 13u;
                    CettaCount count = zero ?
                        (dialect && !(client == 0u && native_mode == 13u) ? 1u : 0u)
                        : native_mode == 4u || invalid ? 0u
                        : native_mode == 2u ? (dialect ? 2u : 3u)
                        : native_mode == 15u ? 2u : native_mode == 16u ? 4u : 1u;
                    if (outcome.results.len != count) {
                        fprintf(stderr, "native projection dialect=%u client=%u mode=%u count=%llu expected=%llu\n",
                            dialect, client, native_mode,
                            (unsigned long long)outcome.results.len, (unsigned long long)count);
                        for (CettaCount i = 0u; i < outcome.results.len; i++) {
                            atom_print(outcome.results.items[i], stderr);
                            fputc('\n', stderr);
                        }
                    }
                    assert(outcome.results.len == count);
                    for (CettaCount i = 0u; i < count; i++) {
                        if (zero) {
                            assert(atom_is_empty(outcome.results.items[i]));
                            continue;
                        }
                        Atom *expected = native_mode == 9u || (native_mode == 2u && i == 2u)
                            ? atom_error(&arena, atom_symbol(&arena, "native"), atom_symbol(&arena, "datum"))
                            : native_mode == 14u ? atom_symbol(&arena, "Empty")
                            : native_mode == 6u ? atom_int(&arena, 9)
                            : parse(&arena, "(function (return (readln!)))");
                        if (!atom_eq(outcome.results.items[i], expected)) {
                            fprintf(stderr, "native projection dialect=%u client=%u mode=%u item=%llu\n",
                                dialect, client, native_mode, (unsigned long long)i);
                            atom_print(outcome.results.items[i], stderr);
                            fputs(" expected ", stderr);
                            atom_print(expected, stderr);
                            fputc('\n', stderr);
                        }
                        assert(atom_eq(outcome.results.items[i], expected));
                    }
                }
                eval_outcome_free(&outcome);
                checks++;
            }
        }
        native_mode = 0u;
        projection_head = SYMBOL_ID_NONE;
        registry_free(&registry);
        space_free(&space);
        term_universe_free(&universe);
        cetta_library_context_free(context);
        eval_set_library_context(NULL);
        free(context);
        arena_free(&arena);
    }
    if (previous)
        assert(setenv("CETTA_PETTA_SEARCH_MACHINE", previous, 1) == 0);
    else
        assert(unsetenv("CETTA_PETTA_SEARCH_MACHINE") == 0);
    free(previous);
    printf("PASS: %u native row, mutation and arity consumers preserve tagged returns without operand demand or replay\n", checks);
}

static void test_prime_native_return_boundary(void) {
    CettaLibraryContext *context = calloc(1u, sizeof(*context));
    assert(context);
    cetta_library_context_init_for_language_profile(
        context, CETTA_LANGUAGE_PRIME, NULL);
    eval_set_library_context(context);
    Arena arena;
    arena_init(&arena);
    TermUniverse universe;
    term_universe_init(&universe);
    term_universe_set_persistent_arena(&universe, &arena);
    Space space;
    space_init_with_universe(&space, &universe);
    Registry registry;
    registry_init(&registry);
    registry_bind_id(&registry, g_builtin_syms.self, atom_space(&arena, &space));
    Atom *import_error = NULL;
    assert(cetta_library_import(context, "str", &space, &arena, &arena,
        &registry, -1, &import_error));
    assert(!import_error);
    bool previous_fixture = oem_native_fixture;
    oem_native_fixture = true;
    const unsigned modes[] = {1u, 2u, 3u, 4u, 9u};
    for (unsigned index = 0u; index < sizeof(modes) / sizeof(*modes); index++) {
        native_mode = modes[index];
        native_calls = 0u;
        EvalOutcome outcome;
        eval_outcome_init(&outcome);
        eval_top_with_registry_outcome(&space, &arena, &arena, &registry,
            parse(&arena, "(__cetta_lib_str byte-slice \"abc\" 0 1)"),
            &outcome, NULL, NULL);
        if (native_calls != 1u)
            fprintf(stderr, "Prime native mode=%u calls=%u results=%llu completion=%u\n",
                native_mode, native_calls, (unsigned long long)outcome.results.len,
                outcome.completion);
        assert(native_calls == 1u);
        assert(outcome.completion == (native_mode == 4u
            ? CETTA_EVAL_INCOMPLETE_CANCELLED : CETTA_EVAL_COMPLETE));
        assert(!outcome.petta_raised_error);
        if (native_mode == 3u || native_mode == 4u) {
            assert(outcome.results.len == 0u);
        } else if (native_mode == 2u) {
            assert(outcome.results.len == 3u);
            assert(atom_eq(outcome.results.items[0], outcome.results.items[1]));
            assert(atom_eq(outcome.results.items[0],
                parse(&arena, "(function (return (readln!)))")));
            assert(atom_is_error(outcome.results.items[2]));
        } else {
            assert(outcome.results.len == 1u);
            if (native_mode == 9u)
                assert(atom_is_error(outcome.results.items[0]));
            else
                assert(atom_eq(outcome.results.items[0],
                    parse(&arena, "(function (return (readln!)))")));
        }
        eval_outcome_free(&outcome);
    }
    native_mode = 0u;
    oem_native_fixture = previous_fixture;
    registry_free(&registry);
    space_free(&space);
    term_universe_free(&universe);
    cetta_library_context_free(context);
    eval_set_library_context(NULL);
    free(context);
    arena_free(&arena);
    puts("PASS: Prime native data, duplicate frontiers, failure, cancellation and Error data retain their control roles");
}

static void test_native_provider_ownership(void) {
    Arena arena;
    arena_init(&arena);
    const char *library_heads[] = {
        "__cetta_lib_str", "__cetta_lib_json_parse_v1",
        "__cetta_lib_future_operation", "__cetta_lib_tensor_future_operation", "tensor:sum",
    };
    for (size_t index = 0u; index < sizeof(library_heads) / sizeof(*library_heads);
         index++) {
        Atom *head = atom_symbol(&arena, library_heads[index]);
        assert(grounded_op_is_library_native(atom_symbol_current_id(head)));
        CettaCallOutcome outcome;
        assert(!grounded_call(&arena, head, NULL, 0u, &outcome));
    }
    const char *other_heads[] = {
        "+", "not", "Empty", "__cetta_library_future", "__cetta_lib",
    };
    for (size_t index = 0u; index < sizeof(other_heads) / sizeof(*other_heads);
         index++)
        assert(!grounded_op_is_library_native(
            atom_symbol_current_id(atom_symbol(&arena, other_heads[index]))));
    Atom *arguments[] = {atom_int(&arena, 2), atom_int(&arena, 3)};
    CettaCallOutcome sum;
    assert(grounded_call(&arena, atom_symbol(&arena, "+"), arguments, 2u, &sum));
    assert(sum.kind == CETTA_CALL_VALUE && atom_eq(sum.term, atom_int(&arena, 5)));
    /* Library spelling does not imply exclusive library ownership. */
    const char *core_library_heads[] = {
        "__cetta_lib_list_range", "__cetta_lib_list_repeat",
        "__cetta_lib_list_from_expression", "__cetta_lib_list_to_expression",
    };
    Atom *list = atom_list(&arena, arguments, 2u);
    Atom *expression = atom_expr(&arena, arguments, 2u);
    for (unsigned index = 0u; index < 4u; index++) {
        Atom *head = atom_symbol(&arena, core_library_heads[index]);
        Atom *operands[] = {index == 2u ? expression : list, atom_int(&arena, 2)};
        if (index < 2u) {
            operands[0] = atom_int(&arena, index ? 2 : 0);
            if (index)
                operands[1] = atom_symbol(&arena, "x");
        }
        CettaCallOutcome returned;
        assert(grounded_call(&arena, head, operands, index < 2u ? 2u : 1u,
                             &returned));
        assert(returned.kind == CETTA_CALL_VALUE && returned.term);
        if (index < 2u) {
            Atom *expected = parse(&arena, index ? "(x x)" : "(0 1)");
            assert(atom_eq(returned.term, expected));
        } else {
            assert(atom_eq(returned.term, index == 2u ? list : expression));
        }
    }
    arena_free(&arena);
    puts("PASS: library provider exclusion preserves core execution and lexical boundaries");
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
    BindingsTermCursorContextV1 cursor_context = {&builder.current, NULL};
    CettaGsltTermCursorV1 cursors[3];
    for (unsigned i = 0u; i < 3u; i++)
        assert(bindings_value_term_cursor_v1(&builder.current, scoped[i],
            &cursors[i]) == CETTA_GSLT_TERM_VIEW_OK_V1);
    assert(cursors[0].scope && cursors[0].scope == cursors[2].scope &&
        cursors[0].scope != cursors[1].scope);
    CettaGsltTermCursorV1 refused = {code, NULL};
    CettaFrameIdentity absent = cetta_frame_identity_scope_fresh(&identities);
    assert(bindings_value_term_cursor_v1(&builder.current,
        binding_value_from_context(x, absent), &refused) ==
        CETTA_GSLT_TERM_VIEW_DEFER_V1);
    assert(!refused.source && !refused.scope);
    assert(bindings_value_term_cursor_v1(&builder.current,
        binding_value_from_atom(code), &refused) == CETTA_GSLT_TERM_VIEW_OK_V1);
    assert(refused.source == code && !refused.scope);
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
        for (unsigned i = 0u; i < 3u; i++) {
            CettaGsltTermCursorV1 current;
            /* Borrow again after writes: directory entries are read loans,
             * not evidence that can be retained through mutation. */
            assert(bindings_value_term_cursor_v1(&builder.current, scoped[i],
                &cursors[i]) == CETTA_GSLT_TERM_VIEW_OK_V1);
            assert(bindings_resolve_scoped_term_cursor_v1(&cursor_context,
                cursors[i], &current) == CETTA_GSLT_TERM_VIEW_OK_V1);
            assert(atom_eq(current.source, observed->expr.elems[i + 1u]));
            assert(!current.scope);
        }
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
        for (unsigned i = 0u; i < 3u; i++) {
            CettaGsltTermCursorV1 current;
            assert(bindings_value_term_cursor_v1(&builder.current, scoped[i],
                &cursors[i]) == CETTA_GSLT_TERM_VIEW_OK_V1);
            assert(bindings_resolve_scoped_term_cursor_v1(&cursor_context,
                cursors[i], &current) == CETTA_GSLT_TERM_VIEW_OK_V1);
            assert(current.source == x && current.scope == cursors[i].scope);
        }
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

static bool publish_checkpoint(void *raw, uint64_t position,
    const CettaOwnedReturn *returned, CettaSharedReturnCheckpoint *next) {
    Recipient *recipient = raw;
    uint64_t after = 0u;
    assert(cetta_shared_return_checkpoint_position(next, &after) && after == position + 1u);
    if (recipient->refuse) return false;
    assert(recipient->count < 8u);
    Captured *captured = (Captured *)returned;
    ++captured->shares;
    recipient->rows[recipient->count] = returned;
    recipient->positions[recipient->count++] = position;
    return true;
}

static void test_reader_checkpoint_lifetime(void) {
    Account account = {.compatible=true};
    CettaOwnedExecution execution = new_execution(&account, 3u);
    CettaSharedReturnProducer *producer = cetta_shared_return_producer_new(
        &execution, 1u, 1u, capture, compatible, &account);
    assert(producer);
    size_t consumers = log_info(producer).consumers;
    /* Checkpoint preparation is fallible before it owns any publication. */
    fail_malloc=1u;
    assert(!cetta_shared_return_checkpoint_new(producer, 0u) && !fail_malloc &&
        log_info(producer).consumers==consumers);
    fail_malloc=2u;
    assert(!cetta_shared_return_checkpoint_new(producer, 0u) && !fail_malloc &&
        log_info(producer).consumers==consumers);
    CettaSharedReturnCheckpoint *root=cetta_shared_return_checkpoint_new(producer, 0u);
    assert(root && cetta_shared_return_checkpoint_retain(root));
    CettaSharedReturnCheckpoint *other=root;
    assert(log_info(producer).consumers==consumers+1u);
    assert(!cetta_shared_return_producer_cancel(producer, consumers));
    assert(cetta_shared_return_producer_take(producer, consumers, NULL, NULL).kind==
        CETTA_SHARED_DELIVERY_INVALID);
    CettaOwnedExecution stolen={0};
    assert(!cetta_shared_return_producer_detach_execution(producer, &stolen) && !stolen.computation);
    assert(cetta_shared_return_producer_cancel(producer, 0u));
    cetta_shared_return_producer_free(producer);
    assert(!account.released && cetta_shared_return_checkpoint_peek(root).kind==CETTA_SHARED_DELIVERY_PAUSED);
    assert(cetta_shared_return_checkpoint_advance(root, 1u)==CETTA_SHARED_RETURN_PAUSED &&
        account.effects==1u && account.next_calls==1u);
    CettaSharedDelivery row=cetta_shared_return_checkpoint_peek(root);
    assert(row.kind==CETTA_SHARED_DELIVERY_ANSWER && row.position==0u);
    Recipient first={.refuse=true}, second={0};
    CettaSharedReturnCheckpoint *next=NULL;
    assert(cetta_shared_return_checkpoint_take(root, publish_checkpoint, &first, &next).kind==
        CETTA_SHARED_DELIVERY_PUBLICATION_REFUSED && !next && !first.count);
    first.refuse=false;
    fail_malloc=1u;
    assert(cetta_shared_return_checkpoint_take(root, publish_checkpoint, &first, &next).kind==
        CETTA_SHARED_DELIVERY_PUBLICATION_REFUSED && !fail_malloc && !next && !first.count);
    assert(account.effects==1u && account.next_calls==1u && !cetta_shared_return_producer_trim(producer));
    assert(cetta_shared_return_checkpoint_take(root, publish_checkpoint, &first, &next).kind==
        CETTA_SHARED_DELIVERY_ANSWER && next && first.count==1u);
    uint64_t position=UINT64_MAX;
    assert(cetta_shared_return_checkpoint_position(root,&position) && position==0u &&
        cetta_shared_return_checkpoint_position(next,&position) && position==1u);
    /* The first reader stops. Its independent sibling still owns the real
     * residual and receives the exact same already-earned occurrence. */
    cetta_shared_return_checkpoint_release(root); root=NULL;
    cetta_shared_return_checkpoint_release(next); next=NULL;
    assert(!account.released && cetta_shared_return_checkpoint_peek(other).returned==row.returned);
    assert(cetta_shared_return_checkpoint_take(other,publish_checkpoint,&second,&next).kind==
        CETTA_SHARED_DELIVERY_ANSWER && second.rows[0]==first.rows[0] && account.effects==1u);
    cetta_shared_return_checkpoint_release(other); other=next; next=NULL;
    for (uint64_t i=1u;i<3u;i++) {
        assert(cetta_shared_return_checkpoint_advance(other,1u)==CETTA_SHARED_RETURN_PAUSED);
        assert(cetta_shared_return_checkpoint_take(other,publish_checkpoint,&second,&next).kind==
            CETTA_SHARED_DELIVERY_ANSWER && second.positions[i]==i);
        cetta_shared_return_checkpoint_release(other); other=next; next=NULL;
    }
    assert(cetta_shared_return_checkpoint_advance(other,1u)==CETTA_SHARED_RETURN_COMPLETE &&
        cetta_shared_return_checkpoint_peek(other).kind==CETTA_SHARED_DELIVERY_COMPLETED &&
        account.effects==3u && account.next_calls==4u && !account.released);
    cetta_shared_return_checkpoint_release(other);
    assert(account.released==1u && !account.freed);
    release_recipient(&first); release_recipient(&second);
    assert(account.freed==3u);

    /* A bounded log retains backpressure while a checkpoint can still read
     * its prefix; source retirement alone does not authorize trimming it. */
    Account bounded={.compatible=true};
    execution=new_execution(&bounded,2u);
    producer=cetta_shared_return_producer_new(&execution,2u,1u,capture,compatible,&bounded);
    assert(producer && cetta_shared_return_producer_advance(producer,1u)==CETTA_SHARED_RETURN_PAUSED);
    Recipient a={.producer=producer,.consumer=0u}, b={.producer=producer,.consumer=1u};
    take_answer(producer,0u,&a,0u);
    CettaSharedReturnCheckpoint *later=cetta_shared_return_checkpoint_new(producer,0u);
    CettaSharedReturnCheckpoint *earlier=cetta_shared_return_checkpoint_new(producer,1u);
    assert(later && earlier && cetta_shared_return_checkpoint_position(later,&position) && position==1u &&
        cetta_shared_return_checkpoint_position(earlier,&position) && position==0u);
    take_answer(producer,1u,&b,0u);
    assert(!cetta_shared_return_producer_trim(producer) &&
        cetta_shared_return_checkpoint_advance(later,1u)==CETTA_SHARED_RETURN_BACKPRESSURE &&
        bounded.effects==1u && !log_info(producer).completed);
    bounded.compatible=false;
    assert(cetta_shared_return_checkpoint_peek(earlier).kind==CETTA_SHARED_DELIVERY_CONTEXT_REFUSED &&
        cetta_shared_return_checkpoint_advance(later,1u)==CETTA_SHARED_RETURN_CONTEXT_REFUSED);
    bounded.compatible=true;
    cetta_shared_return_checkpoint_release(later); cetta_shared_return_checkpoint_release(earlier);
    assert(cetta_shared_return_producer_trim(producer)==1u);
    cetta_shared_return_producer_free(producer);
    release_recipient(&a); release_recipient(&b);
    assert(bounded.released==1u && bounded.freed==1u);
    puts("PASS: immutable reader checkpoints retain one unfinished producer after caller and first-reader retirement");
}

static unsigned scoped_checkpoint_finalizations;
static void scoped_checkpoint_hold(void *raw) {
    assert(cetta_shared_return_checkpoint_retain(raw));
}
static void scoped_checkpoint_retire(void *raw) {
    CettaSharedReturnCheckpoint *checkpoint=raw,*next=NULL;
    assert(cetta_shared_return_checkpoint_peek(checkpoint).kind==CETTA_SHARED_DELIVERY_INVALID &&
        cetta_shared_return_checkpoint_advance(checkpoint,1u)==CETTA_SHARED_RETURN_INVALID &&
        cetta_shared_return_checkpoint_take(checkpoint,NULL,NULL,&next).kind==
            CETTA_SHARED_DELIVERY_INVALID && !next);
    scoped_checkpoint_finalizations++;
    cetta_shared_return_checkpoint_release(checkpoint);
}

static void test_scoped_reader_checkpoints(void) {
    Account account={.compatible=true};
    CettaOwnedExecution execution=new_execution(&account,2u);
    Computation *resident=execution.computation;
    CettaSharedReturnProducer *producer=cetta_shared_return_producer_new(
        &execution,1u,1u,capture,compatible,&account);
    assert(producer);
    fail_malloc=1u;
    assert(!cetta_shared_return_checkpoint_new_scoped(producer,0u) && !fail_malloc);
    CettaSharedReturnCheckpoint *root=cetta_shared_return_checkpoint_new_scoped(producer,0u);
    CettaSharedReturnCheckpoint *strong=cetta_shared_return_checkpoint_new(producer,0u);
    assert(root && strong && cetta_shared_return_producer_cancel(producer,0u));
    assert(cetta_shared_return_checkpoint_advance(root,1u)==CETTA_SHARED_RETURN_PAUSED);
    Recipient receiver={.refuse=true};CettaSharedReturnCheckpoint *next=NULL;
    assert(cetta_shared_return_checkpoint_take(root,publish_checkpoint,&receiver,&next).kind==
        CETTA_SHARED_DELIVERY_PUBLICATION_REFUSED && !next && !receiver.count);
    receiver.refuse=false;
    assert(cetta_shared_return_checkpoint_take(root,publish_checkpoint,&receiver,&next).kind==
        CETTA_SHARED_DELIVERY_ANSWER && next && receiver.count==1u);
    assert(cetta_shared_return_checkpoint_advance(next,1u)==CETTA_SHARED_RETURN_BACKPRESSURE &&
        account.effects==1u && !log_info(producer).completed);
    /* Both the actual continuation and a retained row contain reader
     * positions. Neither may keep the producer alive after its owner exits.
     * All siblings must be invalid BEFORE either finalizer runs. */
    Captured *row=(Captured *)receiver.rows[0];
    assert(arena_retain_owner(&resident->arena,root,scoped_checkpoint_hold,scoped_checkpoint_retire));
    assert(arena_retain_owner(&row->arena,next,scoped_checkpoint_hold,scoped_checkpoint_retire));
    release_recipient(&receiver);
    scoped_checkpoint_finalizations=0u;
    cetta_shared_return_producer_free(producer);
    assert(!account.released && cetta_shared_return_checkpoint_peek(root).kind==CETTA_SHARED_DELIVERY_ANSWER);
    cetta_shared_return_checkpoint_release(strong);
    assert(account.released==1u && account.freed==1u && scoped_checkpoint_finalizations==2u);
    assert(cetta_shared_return_checkpoint_peek(root).kind==CETTA_SHARED_DELIVERY_INVALID &&
        cetta_shared_return_checkpoint_advance(next,1u)==CETTA_SHARED_RETURN_INVALID);
    uint64_t position=0u;
    assert(cetta_shared_return_checkpoint_position(next,&position) && position==1u);
    cetta_shared_return_checkpoint_release(root);cetta_shared_return_checkpoint_release(next);

    Account bounded={.compatible=true};execution=new_execution(&bounded,2u);
    producer=cetta_shared_return_producer_new(&execution,1u,1u,capture,compatible,&bounded);
    root=cetta_shared_return_checkpoint_new_scoped(producer,0u);
    assert(root && cetta_shared_return_producer_cancel(producer,0u) &&
        cetta_shared_return_checkpoint_advance(root,1u)==CETTA_SHARED_RETURN_PAUSED);
    receiver=(Recipient){0};next=NULL;
    assert(cetta_shared_return_checkpoint_take(root,publish_checkpoint,&receiver,&next).kind==
        CETTA_SHARED_DELIVERY_ANSWER);
    assert(!cetta_shared_return_producer_trim(producer));
    cetta_shared_return_checkpoint_release(root);root=next;next=NULL;
    assert(cetta_shared_return_checkpoint_advance(root,1u)==CETTA_SHARED_RETURN_PAUSED && bounded.effects==2u);
    assert(cetta_shared_return_checkpoint_take(root,publish_checkpoint,&receiver,&next).kind==
        CETTA_SHARED_DELIVERY_ANSWER);
    cetta_shared_return_checkpoint_release(root);root=next;
    cetta_shared_return_producer_free(producer);
    assert(bounded.released==1u && cetta_shared_return_checkpoint_peek(root).kind==CETTA_SHARED_DELIVERY_INVALID);
    cetta_shared_return_checkpoint_release(root);release_recipient(&receiver);
    assert(bounded.freed==2u);
    puts("PASS: owner-scoped checkpoints pin one shared frontier without producer cycles; retirement precedes continuation and row finalizers");
}

typedef struct {
    Arena arena;
    Bindings environments[3];
    CettaCallOutcome packets[3];
    unsigned count, source_position, source_pulls, effects, factories;
    unsigned body_position, body_count, body_pulls;
    unsigned release_order[8], releases;
    const CettaOwnedReturn *borrowed;
    bool body_live, decline, malformed_body, body_pause, body_handoff;
    bool compatible, source_released;
} BindFixture;

static CettaOwnedExecutionStep bind_source_next(void *context,
        CettaOwnedReturn *returned, CettaEvalCompletion *completion) {
    BindFixture *fixture = context;
    fixture->source_pulls++;
    assert(!fixture->body_live);
    if (!fixture->compatible) {
        *completion = CETTA_EVAL_INCOMPLETE_INVALIDATED;
        return CETTA_OWNED_EXECUTION_HANDOFF;
    }
    if (fixture->source_position == fixture->count)
        return CETTA_OWNED_EXECUTION_COMPLETE;
    unsigned position = fixture->source_position++;
    fixture->effects++;
    *returned = (CettaOwnedReturn){.outcome = fixture->packets[position],
        .environment = &fixture->environments[position],
        .final = fixture->source_position == fixture->count};
    return CETTA_OWNED_EXECUTION_RETURN;
}

static bool bind_source_current(void *context) {
    return ((BindFixture *)context)->compatible;
}

static void bind_source_release(void *context) {
    BindFixture *fixture = context;
    assert(!fixture->body_live && !fixture->source_released);
    fixture->source_released = true;
    fixture->release_order[fixture->releases++] = 2u;
    for (unsigned i = 0u; i < 3u; i++)
        bindings_free(&fixture->environments[i]);
    arena_free(&fixture->arena);
}

static CettaOwnedExecutionStep bind_body_next(void *context,
        CettaOwnedReturn *returned, CettaEvalCompletion *completion) {
    BindFixture *fixture = context;
    (void)completion;
    fixture->body_pulls++;
    assert(fixture->body_live && fixture->borrowed &&
           fixture->borrowed->outcome.term && !fixture->source_released);
    assert(fixture->borrowed->environment ==
           &fixture->environments[fixture->source_position - 1u]);
    if (fixture->body_pause) {
        fixture->body_pause = false;
        return CETTA_OWNED_EXECUTION_PAUSED;
    }
    if (fixture->body_handoff)
        return CETTA_OWNED_EXECUTION_HANDOFF;
    if (fixture->body_position == fixture->body_count)
        return CETTA_OWNED_EXECUTION_COMPLETE;
    fixture->body_position++;
    *returned = *fixture->borrowed;
    returned->final = fixture->body_position == fixture->body_count;
    return CETTA_OWNED_EXECUTION_RETURN;
}

static void bind_body_release(void *context) {
    BindFixture *fixture = context;
    assert(fixture->body_live && !fixture->source_released &&
           fixture->borrowed->outcome.term);
    fixture->body_live = false;
    fixture->release_order[fixture->releases++] = 1u;
}

static void bind_make_body(BindFixture *fixture,
        const CettaOwnedReturn *returned, CettaOwnedExecution *body) {
    fixture->borrowed = returned;
    fixture->body_position = 0u;
    fixture->body_live = true;
    cetta_owned_execution_init(body, fixture, bind_body_next, bind_body_release);
}

static bool bind_continue(void *context, const CettaOwnedReturn *returned,
        CettaOwnedExecution *body) {
    BindFixture *fixture = context;
    fixture->factories++;
    assert(returned->outcome.kind == CETTA_CALL_VALUE ||
           returned->outcome.kind == CETTA_CALL_SUSPENDED);
    if (fixture->decline)
        return false;
    if (fixture->malformed_body) {
        *body = (CettaOwnedExecution){0};
        return true;
    }
    bind_make_body(fixture, returned, body);
    return true;
}

static CettaOwnedExecution bind_fixture(BindFixture *fixture,
        CettaOwnedBind *bind, unsigned count) {
    *fixture = (BindFixture){.count = count, .body_count = 2u, .compatible = true};
    arena_init(&fixture->arena);
    for (unsigned i = 0u; i < 3u; i++) {
        bindings_init(&fixture->environments[i]);
        fixture->packets[i] = cetta_call_suspended(atom_int(&fixture->arena, i + 1u),
            atom_symbol(&fixture->arena, "pending-check"));
    }
    CettaOwnedExecution source, execution;
    cetta_owned_execution_init(&source, fixture, bind_source_next, bind_source_release);
    source.final_is_current = bind_source_current;
    assert(cetta_owned_bind_open(&execution, bind, &source, bind_continue, fixture));
    assert(!source.computation);
    return execution;
}

typedef struct {
    BindFixture *fixture;
    unsigned count;
    int64_t values[8];
    CettaCallOutcomeKind kinds[8];
    bool refuse, invalidate;
} BindRecipient;

static bool bind_collect(void *context, const CettaOwnedReturn *returned) {
    BindRecipient *recipient = context;
    BindFixture *fixture = recipient->fixture;
    assert(returned->environment == &fixture->environments[fixture->source_position - 1u]);
    if (recipient->refuse)
        return false;
    assert(recipient->count < 8u);
    recipient->kinds[recipient->count] = returned->outcome.kind;
    Atom *value = returned->outcome.term;
    recipient->values[recipient->count++] = value->kind == ATOM_GROUNDED &&
        value->ground.gkind == GV_INT ? value->ground.ival : INT64_MIN;
    if (returned->outcome.kind == CETTA_CALL_SUSPENDED)
        assert(atom_is_symbol(returned->outcome.delayed, "pending-check"));
    if (recipient->invalidate)
        fixture->compatible = false;
    return true;
}

static CettaOwnedExecutionStep bind_drain(CettaOwnedExecution *execution,
        BindRecipient *recipient, uint64_t allowance) {
    for (unsigned attempts = 0u; attempts < 32u; attempts++) {
        CettaOwnedExecutionStep step = cetta_owned_execution_sequence(
            execution, allowance, bind_collect, recipient);
        if (step != CETTA_OWNED_EXECUTION_PAUSED)
            return step;
    }
    assert(false);
    return CETTA_OWNED_EXECUTION_INTERRUPTED;
}

static void test_owned_bind(void) {
    for (uint64_t allowance = 1u; allowance <= 8u; allowance++) {
        BindFixture fixture;
        CettaOwnedBind bind;
        CettaOwnedExecution execution = bind_fixture(&fixture, &bind, 3u);
        BindRecipient recipient = {.fixture = &fixture, .refuse = true};
        assert(cetta_owned_execution_sequence(&execution, 0u, bind_collect, &recipient) ==
               CETTA_OWNED_EXECUTION_PAUSED);
        assert(!fixture.effects && !fixture.factories);
        assert(cetta_owned_execution_sequence(&execution, 1u, bind_collect, &recipient) ==
               CETTA_OWNED_EXECUTION_PAUSED);
        assert(fixture.effects == 1u && fixture.factories == 1u && fixture.body_pulls == 1u);
        assert(bind.producer.has_pending && bind.body.has_pending);
        recipient.refuse = false;
        assert(bind_drain(&execution, &recipient, allowance) == CETTA_OWNED_EXECUTION_COMPLETE);
        assert(recipient.count == 6u && fixture.effects == 3u && fixture.factories == 3u);
        for (unsigned i = 0u; i < 6u; i++)
            assert(recipient.values[i] == (int64_t)(i / 2u + 1u));
        cetta_owned_execution_cancel(&execution);
        cetta_owned_execution_cancel(&execution);
        assert(fixture.releases == 4u && fixture.release_order[3] == 2u);
    }
    for (unsigned mode = 0u; mode < 9u; mode++) {
        BindFixture fixture;
        CettaOwnedBind bind;
        CettaOwnedExecution execution = bind_fixture(&fixture, &bind, 1u);
        BindRecipient recipient = {.fixture = &fixture};
        if (mode == 0u) fixture.body_count = 0u;
        if (mode == 1u) fixture.body_pause = true;
        if (mode == 2u) fixture.packets[0] = cetta_call_failure();
        if (mode == 3u) fixture.packets[0] = cetta_call_raised(atom_int(&fixture.arena, 7));
        if (mode == 4u) fixture.packets[0] = cetta_call_interrupted();
        if (mode == 5u) fixture.malformed_body = true;
        if (mode == 6u) fixture.packets[0] = cetta_call_completed_value(
            atom_error(&fixture.arena, atom_int(&fixture.arena, 1), atom_int(&fixture.arena, 2)));
        if (mode == 7u) fixture.packets[0] = cetta_call_completed_value(
            parse(&fixture.arena, "(function (return (readln!)))"));
        if (mode == 8u) fixture.packets[0] = cetta_call_value(NULL);
        CettaOwnedExecutionStep step = bind_drain(&execution, &recipient, 1u);
        if (mode < 4u || mode == 6u || mode == 7u)
            assert(step == CETTA_OWNED_EXECUTION_COMPLETE);
        else assert(step == CETTA_OWNED_EXECUTION_INTERRUPTED);
        if (mode == 0u || mode == 2u) assert(!recipient.count);
        if (mode == 1u) assert(recipient.count == 2u && fixture.factories == 1u);
        if (mode == 3u) assert(recipient.count == 1u && !fixture.factories &&
                               recipient.kinds[0] == CETTA_CALL_RAISED);
        if (mode == 4u) assert(execution.completion == CETTA_EVAL_INCOMPLETE_CANCELLED);
        if (mode == 5u || mode == 8u)
            assert(execution.completion == CETTA_EVAL_INCOMPLETE_HOST_FAILURE);
        if (mode == 6u || mode == 7u)
            assert(recipient.count == 2u && fixture.factories == 1u &&
                   recipient.kinds[0] == CETTA_CALL_VALUE && recipient.values[0] == INT64_MIN);
        cetta_owned_execution_cancel(&execution);
    }
    BindFixture fixture;
    CettaOwnedBind bind;
    CettaOwnedExecution execution = bind_fixture(&fixture, &bind, 1u);
    BindRecipient recipient = {.fixture = &fixture};
    fixture.decline = true;
    assert(bind_drain(&execution, &recipient, 1u) == CETTA_OWNED_EXECUTION_HANDOFF);
    const CettaOwnedReturn *pending = cetta_owned_bind_pending(&bind);
    assert(pending && fixture.effects == 1u && fixture.factories == 1u);
    assert(bind_drain(&execution, &recipient, 1u) == CETTA_OWNED_EXECUTION_HANDOFF);
    CettaOwnedExecution ordinary;
    bind_make_body(&fixture, pending, &ordinary);
    assert(cetta_owned_bind_resume(&execution, &bind, &ordinary));
    assert(bind_drain(&execution, &recipient, 1u) == CETTA_OWNED_EXECUTION_COMPLETE);
    assert(recipient.count == 2u && fixture.effects == 1u && fixture.factories == 1u);
    cetta_owned_execution_cancel(&execution);

    execution = bind_fixture(&fixture, &bind, 1u);
    recipient = (BindRecipient){.fixture = &fixture, .invalidate = true};
    assert(bind_drain(&execution, &recipient, 1u) == CETTA_OWNED_EXECUTION_HANDOFF);
    assert(execution.completion == CETTA_EVAL_INCOMPLETE_INVALIDATED &&
           fixture.effects == 1u && fixture.factories == 1u && recipient.count == 2u);
    cetta_owned_execution_cancel(&execution);

    execution = bind_fixture(&fixture, &bind, 2u);
    recipient = (BindRecipient){.fixture = &fixture};
    fixture.body_handoff = true;
    assert(bind_drain(&execution, &recipient, 1u) == CETTA_OWNED_EXECUTION_HANDOFF);
    assert(bind.has_body && bind.producer.has_pending && !cetta_owned_bind_pending(&bind));
    assert(!cetta_owned_bind_resume(&execution, &bind, &bind.body));
    cetta_owned_execution_cancel(&execution);
    assert(fixture.releases == 2u && fixture.release_order[0] == 1u &&
           fixture.release_order[1] == 2u && fixture.effects == 1u);
    puts("PASS: shared bind retains packet context, ordered multiplicity, zero, refusal, split execution, handoff, finality and release order");
}

static unsigned prepared_native_attempts;
static bool prepared_native_call(void *context, Space *space, Arena *arena,
        const CettaInvocation *invocation, CettaCallOutcome *outcome) {
    (void)context; (void)space;
    CettaExprLen arity;
    assert(cetta_invocation_arity(invocation, &arity) && arity == 1u);
    prepared_native_attempts++;
    if (native_mode == 18u)
        return false;
    native_calls++;
    Atom *const *arguments = cetta_invocation_borrow_arguments(invocation);
    if (native_mode == 20u && arguments[0]->kind == ATOM_EXPR &&
        arguments[0]->expr.len == 2u && atom_is_symbol(arguments[0]->expr.elems[0], "Save"))
        *outcome = cetta_call_completed_value(atom_expr2(arena,
            atom_symbol(arena, "Saved"), arguments[0]->expr.elems[1]));
    else if (!native_mode || native_mode == 20u || (native_mode == 19u && native_calls <= 20000u))
        *outcome = cetta_call_completed_value(atom_int(arena, 7));
    else if (native_mode == 19u) {
        Atom *rows[] = {atom_int(arena, 7), atom_int(arena, 7), atom_int(arena, 9)};
        *outcome = cetta_call_completed_frontier(atom_expr(arena, rows, 3u));
    }
    else if (native_mode == 17u)
        *outcome = cetta_call_value(parse(arena, "(+ 4 5)"));
    else
        native_fixture_outcome(arena, outcome);
    return true;
}

typedef struct {
    Arena *arena;
    unsigned count;
    bool refuse;
    bool raised;
} PreparedNativeRecipient;

static bool prepared_native_publish(void *context, const CettaOwnedReturn *returned) {
    PreparedNativeRecipient *recipient = context;
    if (recipient->refuse)
        return false;
    recipient->count++;
    if (native_mode == 8u) {
        assert(returned->outcome.kind == CETTA_CALL_RAISED);
        recipient->raised = true;
        return true;
    }
    assert(returned->outcome.kind == CETTA_CALL_VALUE);
    Atom *value = returned->outcome.term;
    assert(value && value->kind == ATOM_EXPR && value->expr.len == 3u);
    assert(atom_is_symbol_id(value->expr.elems[0], symbol_intern_cstr(g_symbols, "Box")));
    assert(atom_eq(value->expr.elems[1], value->expr.elems[2]));
    if (!native_mode)
        assert(atom_eq(value->expr.elems[1], atom_int(recipient->arena, 7)));
    if (native_mode == 17u || native_mode == 18u || native_mode == 6u)
        assert(atom_eq(value->expr.elems[1], atom_int(recipient->arena, 9)));
    return true;
}

static bool prepared_native_interrupt_poll(void *context) {
    unsigned *after = context;
    return native_calls >= *after;
}

typedef struct { unsigned count; int64_t expected; } PreparedNativeLoopRecipient;
static bool prepared_native_loop_publish(void *context, const CettaOwnedReturn *returned) {
    PreparedNativeLoopRecipient *recipient = context;
    assert(returned->outcome.kind == CETTA_CALL_VALUE &&
        returned->outcome.term->kind == ATOM_GROUNDED &&
        returned->outcome.term->ground.gkind == GV_INT &&
        returned->outcome.term->ground.ival == recipient->expected);
    recipient->count++;
    return true;
}

typedef struct { unsigned count; bool refuse; } PreparedNativeLiveRecipient;
static bool prepared_native_live_publish(void *context, const CettaOwnedReturn *returned) {
    PreparedNativeLiveRecipient *recipient = context;
    Atom *term = returned->outcome.term;
    assert(returned->outcome.kind == CETTA_CALL_VALUE && term && term->kind == ATOM_EXPR &&
        term->expr.len == 3u && atom_is_symbol(term->expr.elems[0], "Box"));
    assert(term->expr.elems[1]->ground.gkind == GV_INT && term->expr.elems[1]->ground.ival == 140000);
    assert(term->expr.elems[2]->ground.gkind == GV_INT &&
        term->expr.elems[2]->ground.ival == (recipient->count == 2u ? 9 : 7));
    if (recipient->refuse)
        return false;
    recipient->count++;
    return true;
}

typedef struct { int64_t tag; unsigned count; bool refuse; } PreparedNativeSavedRecipient;
static bool prepared_native_saved_publish(void *context, const CettaOwnedReturn *returned) {
    PreparedNativeSavedRecipient *recipient = context;
    Atom *value = returned->outcome.term;
    assert(returned->outcome.kind == CETTA_CALL_VALUE && value &&
        value->kind == ATOM_EXPR && value->expr.len == 2u &&
        atom_is_symbol(value->expr.elems[0], "Saved") &&
        value->expr.elems[1]->kind == ATOM_GROUNDED &&
        value->expr.elems[1]->ground.gkind == GV_INT &&
        value->expr.elems[1]->ground.ival == recipient->tag);
    if (recipient->refuse)
        return false;
    recipient->count++;
    return true;
}

static void test_prepared_native_continuation(void) {
    Arena source;
    arena_init(&source);
    TermUniverse universe;
    term_universe_init(&universe);
    term_universe_set_persistent_arena(&universe, &source);
    Space space;
    space_init_with_universe(&space, &universe);
    assert(space_admit_atom(&space, &source, parse(&source,
        "(= (native-box $x) (let $v (__cetta_lib_json_parse_v1 $x) (native-after $v)))")));
    assert(space_admit_atom(&space, &source, parse(&source,
        "(= (native-after $v) (Box $v $v))")));
    CettaPreparedPureSourceView view = {.native_call = prepared_native_call};
    CettaPreparedPureProgram *program = cetta_prepared_pure_program_compile_closed_answers(
        &space, parse(&source, "(native-box 1)"), CETTA_GSLT_PURE_CALL_EAGER,
        atom_bool, atom_expr, cetta_prepared_pure_constructs_expression_always,
        NULL, NULL, NULL, NULL, &view, true, true,
        (CettaMatchDecisionSemanticIdentity){
            .compiler_identity = cetta_match_decision_compiler_identity()});
    assert(program);
    CettaPreparedPureAnswerCursorOptions options = {
        .limits = {.max_transitions = 1024u, .max_scratch_bytes = 1024u * 1024u,
                   .construct_allocation_bound = atom_expr_allocation_bound},
        .allow_continuations = true,
    };
    unsigned modes[] = {0u, 1u, 2u, 3u, 4u, 6u, 8u, 9u, 13u, 14u, 17u, 18u};
    for (size_t i = 0u; i < sizeof(modes) / sizeof(modes[0]); i++) {
        native_mode = modes[i]; native_calls = prepared_native_attempts = 0u;
        CettaPreparedPureAnswerCursor *cursor =
            cetta_prepared_pure_answer_cursor_open(program, &options);
        assert(cursor);
        CettaOwnedExecution execution = {0};
        assert(cetta_prepared_pure_answer_cursor_own_execution(&execution, cursor));
        PreparedNativeRecipient recipient = {.arena = &source};
        assert(cetta_owned_execution_sequence(&execution, 0u,
            prepared_native_publish, &recipient) == CETTA_OWNED_EXECUTION_PAUSED);
        assert(prepared_native_attempts == 0u);
        CettaOwnedExecutionStep stop = cetta_owned_execution_sequence(&execution,
            UINT64_MAX, prepared_native_publish, &recipient);
        CettaCallReturnCursor completion = {0};
        if (native_mode == 17u || native_mode == 18u || native_mode == 6u) {
            assert(stop == CETTA_OWNED_EXECUTION_HANDOFF && recipient.count == 0u);
            CettaInvocation call;
            bool entered;
            CettaOwnedReturn snapshot;
            assert(cetta_prepared_pure_answer_cursor_native_boundary(cursor, &call, &entered, &snapshot));
            const CettaOwnedReturn *pending = entered ? &snapshot : NULL;
            assert(entered == (native_mode != 18u));
            if (native_mode == 6u)
                assert(pending && pending->outcome.kind == CETTA_CALL_SUSPENDED && pending->outcome.delayed);
            if (native_mode == 17u)
                assert(pending && pending->outcome.result_form == CETTA_CALL_RESULT_EXPRESSION);
            completion = (CettaCallReturnCursor){.outcome = cetta_call_completed_value(atom_int(&source, 9))};
            CettaOwnedExecution replacement;
            cetta_owned_execution_init(&replacement, &completion, cetta_call_return_next, NULL);
            assert(cetta_prepared_pure_answer_cursor_resume_native(&execution, &replacement));
            assert(!replacement.computation);
            stop = cetta_owned_execution_sequence(&execution, UINT64_MAX,
                prepared_native_publish, &recipient);
        }
        if (native_mode == 4u)
            assert(stop == CETTA_OWNED_EXECUTION_INTERRUPTED && recipient.count == 0u);
        else {
            assert(stop == CETTA_OWNED_EXECUTION_COMPLETE);
            unsigned expected = native_mode == 2u ? 3u :
                native_mode == 3u || native_mode == 13u ? 0u : 1u;
            assert(recipient.count == expected);
        }
        assert(prepared_native_attempts == 1u && native_calls == (native_mode == 18u ? 0u : 1u));
        cetta_owned_execution_cancel(&execution);
    }
    native_mode = 0u;
    assert(space_admit_atom(&space, &source, parse(&source,
        "(= (native-loop $n $sum) (if (> $n 0) (native-loop (- $n 1) "
        "(+ $sum (__cetta_lib_json_parse_v1 $n))) $sum))")));
    for (unsigned series = 0u; series < 2u; series++) {
        unsigned iterations = series ? 257u : 17u;
        char call[64];
        snprintf(call, sizeof(call), "(native-loop %u 0)", iterations);
        CettaPreparedPureProgram *loop = cetta_prepared_pure_program_compile_closed_answers(
            &space, parse(&source, call), CETTA_GSLT_PURE_CALL_EAGER,
            atom_bool, atom_expr, cetta_prepared_pure_constructs_expression_always,
            NULL, NULL, NULL, NULL, &view, true, true,
            (CettaMatchDecisionSemanticIdentity){
                .compiler_identity = cetta_match_decision_compiler_identity()});
        assert(loop);
        options.limits.max_transitions = series ? 65u : 3u;
        CettaPreparedPureAnswerCursor *bounded = cetta_prepared_pure_answer_cursor_open(loop, &options);
        assert(bounded);
        CettaOwnedExecution execution = {0};
        assert(cetta_prepared_pure_answer_cursor_own_execution(&execution, bounded));
        native_calls = prepared_native_attempts = 0u;
        PreparedNativeLoopRecipient answers = {.expected = (int64_t)iterations * 7};
        unsigned refills = 0u;
        for (;;) {
            CettaOwnedExecutionStep stop = cetta_owned_execution_sequence(&execution,
                UINT64_MAX, prepared_native_loop_publish, &answers);
            if (stop == CETTA_OWNED_EXECUTION_COMPLETE)
                break;
            assert(stop == CETTA_OWNED_EXECUTION_HANDOFF &&
                execution.completion == CETTA_EVAL_COMPLETE);
            assert(cetta_prepared_pure_answer_cursor_frame_count(bounded) == 1u);
            assert(!cetta_prepared_pure_answer_cursor_resume_transitions(&execution, 0u));
            assert(cetta_prepared_pure_answer_cursor_resume_transitions(&execution,
                options.limits.max_transitions));
            refills++;
        }
        assert(answers.count == 1u && refills > 1u && native_calls == iterations &&
            prepared_native_attempts == iterations);
        cetta_owned_execution_cancel(&execution);
        cetta_prepared_pure_program_free(loop);
    }
    for (unsigned after = 0u; after < 2u; after++) {
        CettaPreparedPureProgram *loop = cetta_prepared_pure_program_compile_closed_answers(
            &space, parse(&source, "(native-loop 20000 0)"), CETTA_GSLT_PURE_CALL_EAGER,
            atom_bool, atom_expr, cetta_prepared_pure_constructs_expression_always,
            NULL, NULL, NULL, NULL, &view, true, true,
            (CettaMatchDecisionSemanticIdentity){
                .compiler_identity = cetta_match_decision_compiler_identity()});
        assert(loop);
        options.limits.max_transitions = 100000u;
        options.interrupt_poll = prepared_native_interrupt_poll;
        options.interrupt_context = &after;
        options.interrupt_poll_interval = 1u;
        native_calls = prepared_native_attempts = 0u;
        CettaPreparedPureAnswerCursor *cursor = cetta_prepared_pure_answer_cursor_open(loop, &options);
        assert(cursor);
        CettaOwnedExecution interrupted = {0};
        assert(cetta_prepared_pure_answer_cursor_own_execution(&interrupted, cursor));
        PreparedNativeLoopRecipient recipient = {.expected = 140000};
        assert(cetta_owned_execution_sequence(&interrupted, UINT64_MAX,
            prepared_native_loop_publish, &recipient) == CETTA_OWNED_EXECUTION_INTERRUPTED);
        assert(interrupted.completion == CETTA_EVAL_INCOMPLETE_CANCELLED &&
            recipient.count == 0u && native_calls == after && prepared_native_attempts == after);
        cetta_owned_execution_cancel(&interrupted);
        cetta_prepared_pure_program_free(loop);
    }
    options.interrupt_poll = NULL;
    options.interrupt_context = NULL;
    const char *retained_equations[] = {
        "(= (native-inner $x) (__cetta_lib_json_parse_v1 $x))",
        "(= (native-outer $x) (let $v (native-inner $x) (Box $x $v)))",
        "(= (native-retain $n) (native-outer (native-loop $n 0)))",
    };
    for (size_t i = 0u; i < sizeof(retained_equations) / sizeof(retained_equations[0]); i++)
        assert(space_admit_atom(&space, &source, parse(&source, retained_equations[i])));
    CettaPreparedPureProgram *retained = cetta_prepared_pure_program_compile_closed_answers(
        &space, parse(&source, "(native-retain 20000)"), CETTA_GSLT_PURE_CALL_EAGER,
        atom_bool, atom_expr, cetta_prepared_pure_constructs_expression_always,
        NULL, NULL, NULL, NULL, &view, true, true,
        (CettaMatchDecisionSemanticIdentity){
            .compiler_identity = cetta_match_decision_compiler_identity()});
    assert(retained);
    options.limits.max_transitions = 100000u;
    CettaPreparedPureAnswerCursor *first = cetta_prepared_pure_answer_cursor_open(retained, &options);
    assert(first);
    CettaOwnedExecution held = {0};
    assert(cetta_prepared_pure_answer_cursor_own_execution(&held, first));
    native_mode = 19u; native_calls = prepared_native_attempts = 0u;
    PreparedNativeLiveRecipient recipient = {.refuse = true};
    assert(cetta_owned_execution_sequence(&held, UINT64_MAX, prepared_native_live_publish,
        &recipient) == CETTA_OWNED_EXECUTION_PAUSED);
    assert(held.has_pending && recipient.count == 0u && native_calls == 20001u);
    CettaPreparedPureAnswerCursor *second = cetta_prepared_pure_answer_cursor_open(retained, &options);
    assert(second);
    CettaOwnedExecution reentered = {0};
    assert(cetta_prepared_pure_answer_cursor_own_execution(&reentered, second));
    native_mode = 0u;
    PreparedNativeLiveRecipient nested = {0};
    assert(cetta_owned_execution_sequence(&reentered, UINT64_MAX, prepared_native_live_publish,
        &nested) == CETTA_OWNED_EXECUTION_COMPLETE);
    assert(nested.count == 1u && native_calls == 40002u);
    cetta_owned_execution_cancel(&reentered);
    recipient.refuse = false;
    assert(cetta_owned_execution_sequence(&held, UINT64_MAX, prepared_native_live_publish,
        &recipient) == CETTA_OWNED_EXECUTION_COMPLETE);
    assert(recipient.count == 3u && native_calls == 40002u && prepared_native_attempts == 40002u);
    cetta_owned_execution_cancel(&held);
    cetta_prepared_pure_program_free(retained);
    assert(space_admit_atom(&space, &source, parse(&source,
        "(= (native-save $n $tag) (let $saved (__cetta_lib_json_parse_v1 (Save $tag)) "
        "(let $ignored (native-loop $n 0) $saved)))")));
    for (unsigned series = 0u; series < 2u; series++) {
        unsigned iterations = series ? 65536u : 2u;
        char call[80];
        snprintf(call, sizeof(call), "(native-save %u 41)", iterations);
        CettaPreparedPureProgram *saved = cetta_prepared_pure_program_compile_closed_answers(
            &space, parse(&source, call), CETTA_GSLT_PURE_CALL_EAGER,
            atom_bool, atom_expr, cetta_prepared_pure_constructs_expression_always,
            NULL, NULL, NULL, NULL, &view, true, true,
            (CettaMatchDecisionSemanticIdentity){
                .compiler_identity = cetta_match_decision_compiler_identity()});
        assert(saved);
        native_mode = 20u; native_calls = prepared_native_attempts = 0u;
        CettaPreparedPureAnswerCursor *first = cetta_prepared_pure_answer_cursor_open(saved, &options);
        assert(first);
        CettaOwnedExecution held = {0};
        assert(cetta_prepared_pure_answer_cursor_own_execution(&held, first));
        PreparedNativeSavedRecipient recipient = {.tag = 41, .refuse = true};
        assert(cetta_owned_execution_sequence(&held, UINT64_MAX,
            prepared_native_saved_publish, &recipient) == CETTA_OWNED_EXECUTION_PAUSED);
        assert(held.has_pending && native_calls == iterations + 1u);
        assert((cetta_prepared_pure_program_closed_survivor_arena(saved) != NULL) == (series != 0u));
        snprintf(call, sizeof(call), "(native-save %u 99)", iterations);
        assert(cetta_prepared_pure_program_rebind_closed_entry_call(saved, parse(&source, call)));
        CettaPreparedPureAnswerCursor *second = cetta_prepared_pure_answer_cursor_open(saved, &options);
        assert(second);
        CettaOwnedExecution reentered = {0};
        assert(cetta_prepared_pure_answer_cursor_own_execution(&reentered, second));
        PreparedNativeSavedRecipient nested = {.tag = 99};
        assert(cetta_owned_execution_sequence(&reentered, UINT64_MAX,
            prepared_native_saved_publish, &nested) == CETTA_OWNED_EXECUTION_COMPLETE);
        assert(nested.count == 1u && native_calls == 2u * (iterations + 1u));
        cetta_owned_execution_cancel(&reentered);
        recipient.refuse = false;
        assert(cetta_owned_execution_sequence(&held, UINT64_MAX,
            prepared_native_saved_publish, &recipient) == CETTA_OWNED_EXECUTION_COMPLETE);
        assert(recipient.count == 1u && native_calls == 2u * (iterations + 1u));
        cetta_owned_execution_cancel(&held);
        cetta_prepared_pure_program_free(saved);
    }
    native_mode = 0u;
    cetta_prepared_pure_program_free(program);
    space_free(&space);
    term_universe_free(&universe);
    arena_free(&source);
    puts("PASS: prepared native continuations preserve tagged data, frontiers, zero, control and replacement without replay");
}

int main(int argc, char **argv) {
    bool reference_native = argc == 2 && strcmp(argv[1], "--native-reference") == 0;
    bool prime_native = argc == 2 && strcmp(argv[1], "--prime-native") == 0;
    assert(argc == 1 || reference_native || prime_native ||
           (argc == 2 && strcmp(argv[1], "--oem-native") == 0));
    oem_native_fixture = argc == 2 && !prime_native;
    oem_native_enabled = oem_native_fixture && !reference_native;
    assert(setenv("CETTA_OPEN_EQUATIONS_REFERENCE",
                  oem_native_fixture && !reference_native ? "0" : "1", 1) == 0);
    assert(setenv("CETTA_PREPARED_PURE_REFERENCE", "1", 1) == 0);
    test_credit_allocation_refusal();
    SymbolTable symbols;
    VarInternTable variables;
    symbol_table_init(&symbols);
    symbol_table_init_builtins(&symbols, &g_builtin_syms);
    g_symbols = &symbols;
    var_intern_init(&variables);
    g_var_intern = &variables;
    if (prime_native) {
        test_prime_native_return_boundary();
    } else if (!oem_native_fixture) {
        test_refusal_and_owned_graph();
        test_copied_return_buffer();
        test_producer_copy();
        test_pending_producer_copy();
        test_terminal_producer_copy(false);
        test_terminal_producer_copy(true);
        test_authority_and_detachment();
        test_construction_and_terminal_states();
        test_pending_return_detachment();
        test_resident_capture();
        test_reader_checkpoint_lifetime();
        test_scoped_reader_checkpoints();
        test_real_prepared_producer(false);
        test_real_prepared_producer(true);
        test_native_return_cursor();
        test_owned_bind();
        test_prepared_native_continuation();
        test_native_provider_ownership();
        test_invocation_operands();
        test_native_projection_consumers();
    }
    if (!prime_native)
        test_native_evaluator_boundary(oem_native_fixture);
    symbol_table_free(&symbols);
    var_intern_free(&variables);
    g_symbols = NULL;
    g_var_intern = NULL;
    return 0;
}
