/* Inspect the real retained driver, including live metadata and collection.
 * This consumer replaces eval.o; it does not expose diagnostics in the ABI. */
#define _GNU_SOURCE
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <stdlib.h>
static void *retention_test_malloc(size_t);
static void *retention_test_cetta_malloc(size_t);
#include "atom.h"
#define malloc retention_test_malloc
#define cetta_malloc retention_test_cetta_malloc
#include "../../src/eval.c"
#undef malloc
#undef cetta_malloc
#include "parser.h"
#include "runtime_names.h"
#include "runtime_session.h"
#include "runtime_prelude.h"

static unsigned refused_boundary, boundary_refusals;
static bool retention_refuse_allocation(size_t bytes) {
    HeCanonicalDriver *driver = g_he_canonical_driver;
    HeCanonicalFrame *frame = driver ? (HeCanonicalFrame *)driver->stack.top : NULL;
    if (!driver || !driver->retained || !refused_boundary) return false;
    bool refuse = (refused_boundary == 1u && bytes == sizeof(HeCanonicalFrame)) ||
        (refused_boundary == 2u && bytes == sizeof(HeInstructionFrame)) ||
        (refused_boundary == 3u && frame && frame->kind == HE_CANONICAL_RETURN_CONTEXT &&
            bytes == sizeof(HeCanonicalDynamic)) ||
        (refused_boundary == 4u && frame && frame->kind == HE_CANONICAL_PROVIDER &&
            bytes == sizeof(CettaOwnedExecution)) ||
        (refused_boundary == 5u && frame && frame->kind == HE_CANONICAL_CAPTURE &&
            bytes == sizeof(CettaEvaluatorOptions));
    if (refuse) { refused_boundary = 0u; boundary_refusals++; }
    return refuse;
}
static void *retention_test_malloc(size_t bytes) {
    return retention_refuse_allocation(bytes) ? NULL : malloc(bytes);
}
static void *retention_test_cetta_malloc(size_t bytes) {
    return retention_refuse_allocation(bytes) ? NULL : cetta_malloc(bytes);
}

typedef struct {
    CettaNameDomain names;
    CettaNameContext previous_names;
    CettaRuntimeSession runtime;
    CettaLibraryContext *previous_library;
    CettaEvalResources *previous_resources;
    CettaEvalGc *previous_collector;
    CettaHeTypeTables *types, *previous_types;
    Arena persistent;
} RetainedFixture;

static RetainedFixture *retained_fixture_new(void) {
    RetainedFixture *fixture = calloc(1u, sizeof(*fixture));
    assert(fixture);
    cetta_name_domain_init(&fixture->names);
    fixture->previous_names = cetta_name_context_current();
    CettaNameContext names = cetta_name_domain_context(&fixture->names);
    cetta_name_context_install(&names);
    fixture->types = he_type_tables_new();
    assert(fixture->types);
    fixture->previous_types = he_type_tables_exchange(fixture->types);
    arena_init_detached(&fixture->persistent);
    assert(cetta_runtime_session_init(&fixture->runtime, &fixture->persistent, CETTA_LANGUAGE_HE, NULL));
    assert(cetta_runtime_session_open_root(&fixture->runtime, &fixture->persistent));
    fixture->previous_library = eval_exchange_library_context(&fixture->runtime.libraries);
    fixture->previous_resources = eval_resources_exchange(fixture->runtime.resources);
    fixture->previous_collector = eval_collector_exchange(eval_resources_collector(fixture->runtime.resources));
    assert(cetta_runtime_load_prelude(&fixture->runtime.space, &fixture->persistent, CETTA_LANGUAGE_HE, NULL));
    return fixture;
}

static void retained_fixture_free(RetainedFixture *fixture) {
    cetta_runtime_session_dispose(&fixture->runtime);
    arena_free(&fixture->persistent);
    eval_exchange_library_context(fixture->previous_library);
    eval_resources_exchange(fixture->previous_resources);
    eval_collector_exchange(fixture->previous_collector);
    he_type_tables_exchange(fixture->previous_types);
    he_type_tables_free(fixture->types);
    cetta_name_context_install(&fixture->previous_names);
    cetta_name_domain_free(&fixture->names);
    free(fixture);
}

typedef struct {
    unsigned references, opens, closes, made, live, freed;
    bool wrap_return, variables;
} RetentionProvider;
typedef struct {
    CettaForeignHold hold;
    RetentionProvider *provider;
    unsigned references, number;
} RetentionPayload;
typedef struct {
    RetentionProvider *provider;
    Arena storage;
    unsigned next;
} RetentionCursor;

static void retention_provider_retain(void *pointer) { ((RetentionProvider *)pointer)->references++; }
static void retention_provider_release(void *pointer) {
    RetentionProvider *provider = pointer;
    assert(provider->references);
    if (!--provider->references) free(provider);
}
static void retention_payload_retain(void *pointer) { ((RetentionPayload *)pointer)->references++; }
static void retention_payload_release(void *pointer) {
    RetentionPayload *payload = pointer;
    assert(payload->references);
    if (!--payload->references) {
        RetentionProvider *provider = payload->provider;
        provider->live--; provider->freed++;
        free(payload);
        retention_provider_release(provider);
    }
}
static void retention_payload_print(const void *pointer, FILE *file) {
    fprintf(file, "(retained-payload %u)", ((const RetentionPayload *)pointer)->number);
}
static void retention_cursor_free(void *pointer) {
    RetentionCursor *cursor = pointer;
    cursor->provider->closes++;
    arena_free(&cursor->storage);
    retention_provider_release(cursor->provider);
    free(cursor);
}
static CettaOwnedExecutionStep retention_cursor_next(void *pointer,
        CettaOwnedReturn *returned, CettaEvalCompletion *completion) {
    (void)completion;
    RetentionCursor *cursor = pointer;
    arena_free(&cursor->storage);
    arena_init_detached(&cursor->storage);
    RetentionPayload *payload = calloc(1u, sizeof(*payload));
    assert(payload);
    *payload = (RetentionPayload){.hold = {retention_payload_retain,
        retention_payload_release, retention_payload_print}, .provider = cursor->provider,
        .references = 1u, .number = cursor->next++};
    retention_provider_retain(cursor->provider);
    cursor->provider->made++; cursor->provider->live++;
    Atom *value = atom_foreign(&cursor->storage, (CettaForeignValue *)payload);
    retention_payload_release(payload);
    if (cursor->provider->variables) {
        Atom *variable = atom_var_with_id(&cursor->storage, "same", fresh_var_id());
        Atom *parts[] = {atom_symbol(&cursor->storage, "row"), value, variable, variable};
        value = atom_expr(&cursor->storage, parts, 4u);
    }
    if (cursor->provider->wrap_return)
        value = atom_expr2(&cursor->storage, atom_symbol_id(&cursor->storage, g_builtin_syms.return_text), value);
    *returned = (CettaOwnedReturn){.outcome = cetta_call_completed_value(value)};
    return CETTA_OWNED_EXECUTION_RETURN;
}
static bool retention_cursor_open(void *context, const CettaInvocation *invocation,
        CettaOwnedExecution *execution, CettaEvalCompletion *completion) {
    (void)invocation; (void)completion;
    RetentionCursor *cursor = calloc(1u, sizeof(*cursor));
    assert(cursor);
    cursor->provider = context;
    retention_provider_retain(context);
    cursor->provider->opens++;
    arena_init_detached(&cursor->storage);
    cetta_owned_execution_init(execution, cursor, retention_cursor_next, retention_cursor_free);
    return true;
}

static size_t retained_outcome_slots(const HeRetainedExecution *work) {
    size_t count = work->output.len;
    for (CettaOwnedFrameLink *link = work->driver.stack.top; link; link = link->next) {
        HeCanonicalFrame *frame = (HeCanonicalFrame *)link;
        count += frame->child.len + frame->work.len + frame->paths.len;
        for (CettaOwnedFrameLink *instruction = frame->instructions.top; instruction; instruction = instruction->next)
            count += ((HeInstructionFrame *)instruction)->inner.len;
    }
    return count;
}

static RetentionPayload *retained_payload(Atom *value, bool variables) {
    if (variables) {
        assert(value->kind == ATOM_EXPR && value->expr.len == 4u);
        assert(value->expr.elems[2]->kind == ATOM_VAR && value->expr.elems[3]->kind == ATOM_VAR);
        assert(var_id_eq(value->expr.elems[2]->var_id, value->expr.elems[3]->var_id));
        value = value->expr.elems[1];
    }
    assert(value->kind == ATOM_GROUNDED && value->ground.gkind == GV_FOREIGN);
    return value->ground.ptr;
}

static void retained_stream_ownership(const char *source, bool wrap_return, unsigned refusal, bool variables) {
    RetainedFixture *fixture = retained_fixture_new();
    RetentionProvider *provider = calloc(1u, sizeof(*provider));
    assert(provider);
    *provider = (RetentionProvider){.references = 1u, .wrap_return = wrap_return, .variables = variables};
    CettaForeignProviderSpec spec = {.context = provider, .owner_thread = true,
        .retain = retention_provider_retain, .release = retention_provider_release, .open = retention_cursor_open};
    Atom *head = cetta_foreign_provider_atom(&fixture->persistent, &spec);
    assert(head);
    registry_bind(&fixture->runtime.registry, "&producer", head);
    size_t position = 0u;
    Atom *expression = parse_sexpr(&fixture->persistent, source, &position);
    assert(expression);
    CettaEvalQuery *query = eval_query_open(&fixture->runtime.space, &fixture->persistent,
        &fixture->runtime.registry, expression, -1);
    assert(query);
    EvalWork *entered = eval_work_lookup(query->handle);
    assert(entered && entered->canonical);
    HeRetainedExecution *work = entered->canonical;
    HeRetainedScope scope;
    he_retained_enter(work, &scope);
    eval_gc_init_once();
    work->collector.budget_bytes = 8192u;
    he_retained_leave(work, &scope);
    if (refusal) {
        Arena result;
        arena_init_detached(&result);
        EvalOutcome outcome;
        eval_outcome_init(&outcome);
        Atom *published = NULL;
        if (refusal == 6u) {
            CettaEvalQueryStep first;
            do {
                first = eval_query_step(query, &result, 1u, &outcome, NULL, NULL);
                assert(first == CETTA_QUERY_PAUSED || first == CETTA_QUERY_ANSWER);
            } while (first == CETTA_QUERY_PAUSED);
            assert(outcome.results.len == 1u && provider->made == 1u);
            published = outcome.results.items[0];
            eval_outcome_free(&outcome);
            eval_outcome_init(&outcome);
        }
        refused_boundary = refusal == 6u ? 3u : refusal;
        unsigned failures = boundary_refusals;
        CettaEvalQueryStep step = eval_query_step(query, &result, 64u, &outcome, NULL, NULL);
        assert(boundary_refusals == failures + 1u && !refused_boundary);
        assert(step == CETTA_QUERY_STOPPED && outcome.completion == CETTA_EVAL_INCOMPLETE_CAPACITY);
        assert(!outcome.results.len);
        assert(provider->opens == (refusal == 3u || refusal == 6u ? 1u : 0u));
        assert(provider->made == (refusal == 6u ? 2u : refusal == 3u ? 1u : 0u));
        assert(provider->closes == provider->opens && provider->live == (refusal == 6u ? 1u : 0u));
        eval_outcome_free(&outcome);
        eval_query_close(query);
        position = 0u;
        Atom *sum = parse_sexpr(&fixture->persistent, "(+ 20 22)", &position);
        CettaEvalQuery *recovery = eval_query_open(&fixture->runtime.space, &fixture->persistent,
            &fixture->runtime.registry, sum, -1);
        assert(recovery);
        eval_outcome_init(&outcome);
        do {
            step = eval_query_step(recovery, &result, 1u, &outcome, NULL, NULL);
            assert(step == CETTA_QUERY_PAUSED || step == CETTA_QUERY_ANSWER);
            assert(outcome.completion == CETTA_EVAL_COMPLETE);
        } while (step == CETTA_QUERY_PAUSED);
        assert(outcome.results.len == 1u && outcome.results.items[0]->kind == ATOM_GROUNDED &&
            outcome.results.items[0]->ground.ival == 42);
        eval_outcome_free(&outcome);
        eval_query_close(recovery);
        retained_fixture_free(fixture);
        if (published) assert(((RetentionPayload *)published->ground.ptr)->number == 0u);
        arena_free(&result);
        assert(provider->references == 1u && provider->made == provider->freed);
        retention_provider_release(provider);
        printf("HE owned allocation refusal: boundary %u, no replay or surviving payload\n", refusal);
        return;
    }
    Arena retained;
    arena_init_detached(&retained);
    Atom *first = NULL;
    size_t peak_slots = 0u;
    for (unsigned number = 0u; number < 1024u; ++number) {
        Arena answer;
        arena_init_detached(&answer);
        EvalOutcome outcome;
        eval_outcome_init(&outcome);
        CettaEvalQueryStep step;
        do {
            step = eval_query_step(query, &answer, 64u, &outcome, NULL, NULL);
            assert(step == CETTA_QUERY_PAUSED || step == CETTA_QUERY_ANSWER);
            assert(outcome.completion == CETTA_EVAL_COMPLETE);
        } while (step == CETTA_QUERY_PAUSED);
        assert(outcome.results.len == 1u);
        Atom *value = outcome.results.items[0];
        assert(retained_payload(value, variables)->number == number);
        if (!number) first = atom_deep_copy(&retained, value);
        else if (variables)
            assert(!var_id_eq(first->expr.elems[2]->var_id, value->expr.elems[2]->var_id));
        size_t slots = retained_outcome_slots(work);
        if (slots > peak_slots) peak_slots = slots;
        if (slots > 64u) fprintf(stderr, "consumed HE slots: %zu at answer %u\n", slots, number);
        assert(slots <= 64u);
        eval_outcome_free(&outcome);
        arena_free(&answer);
    }
    assert(work->collector.collections > 0u);
    unsigned collections = (unsigned)work->collector.collections;
    assert(provider->opens == 1u && provider->made == 1024u && provider->closes == 0u);
    assert(provider->live < 128u);
    eval_query_close(query);
    assert(provider->closes == 1u && provider->live == 1u);
    cetta_runtime_session_dispose(&fixture->runtime);
    assert(retained_payload(first, variables)->number == 0u);
    arena_free(&retained);
    retained_fixture_free(fixture);
    assert(!provider->live && provider->made == provider->freed && provider->references == 1u);
    printf("HE retained stream: %s%s, peak %zu outcome slots, %u collections, 1024 payloads released\n",
        source, variables ? " with shared/distinct variables" : "", peak_slots, collections);
    retention_provider_release(provider);
}

int main(int argc, char **argv) {
    bool faults = argc > 1 && !strcmp(argv[1], "--faults");
    if (!faults) {
        retained_stream_ownership("(&producer)", false, 0u, false);
        retained_stream_ownership("(function (&producer))", true, 0u, false);
        retained_stream_ownership("(let $f capture ($f (function (&producer))))", true, 0u, false);
        retained_stream_ownership("(&producer)", false, 0u, true);
        retained_stream_ownership("(function (&producer))", true, 0u, true);
    }
    for (unsigned refusal = 1u; refusal <= 6u; ++refusal)
        retained_stream_ownership(refusal == 5u
            ? "(let $f capture ($f (&producer)))"
            : refusal == 2u || refusal == 3u || refusal == 6u ? "(function (&producer))" : "(&producer)",
            refusal == 2u || refusal == 3u || refusal == 6u, refusal, false);
    return 0;
}
