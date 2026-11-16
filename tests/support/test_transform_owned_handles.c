#define _POSIX_C_SOURCE 200809L
#ifdef NDEBUG
#undef NDEBUG
#endif
#include "runtime_session.h"
#include "runtime_names.h"
#include "eval_query.h"
#include "native_handle.h"
#include "parser.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>

typedef struct {
    CettaRuntimeSession runtime;
    Arena persistent, answers;
} Fixture;

static unsigned disposed;
static void dispose_resource(void *resource) { ++disposed; free(resource); }

static Atom *parse(Fixture *fixture, const char *text) {
    Atom **forms = NULL;
    int count = parse_metta_text(text, &fixture->persistent, &forms);
    assert(count == 1);
    Atom *form = forms[0];
    free(forms);
    return form;
}

static Atom *ask(Fixture *fixture, const char *text) {
    CettaEvalQuery *query = eval_query_open(&fixture->runtime.space,
        &fixture->persistent, &fixture->runtime.registry, parse(fixture, text), -1);
    assert(query);
    Atom *result = NULL;
    bool ended = false;
    for (unsigned turn = 0u; turn < 10000u; ++turn) {
        EvalOutcome outcome;
        eval_outcome_init(&outcome);
        CettaEvalQueryAnswer packet = {0};
        CettaEvalQueryStep step = eval_query_step(query, &fixture->answers,
            1000u, &outcome, &packet, NULL);
        assert(outcome.completion == CETTA_EVAL_COMPLETE && !outcome.petta_raised_error);
        if (step == CETTA_QUERY_ANSWER) {
            assert(!result && outcome.results.len == 1u);
            result = atom_deep_copy(&fixture->answers, outcome.results.items[0]);
            assert(result);
        } else {
            assert(step == CETTA_QUERY_PAUSED || step == CETTA_QUERY_EXHAUSTED);
        }
        ended = step == CETTA_QUERY_EXHAUSTED;
        eval_outcome_free(&outcome);
        if (ended) break;
    }
    assert(ended);
    eval_query_close(query);
    return result;
}

static bool ignore_document(void *context, const EvalOutcome *outcome) {
    (void)context;
    return outcome->completion == CETTA_EVAL_COMPLETE && !outcome->petta_raised_error;
}

static void load(Fixture *fixture, const char *text) {
    Arena scratch;
    arena_init_detached(&scratch);
    CettaDocumentRunReport report;
    assert(cetta_library_run_text(&fixture->runtime.libraries, &fixture->runtime.space,
        &fixture->runtime.registry, text, &scratch, &fixture->persistent,
        ignore_document, NULL, &report));
    arena_free(&scratch);
}

/* Observe the stored value after firing: the public receipt is not its
 * payload. Numeric equality alone cannot establish retained ownership. */
static void check(Fixture *fixture, const char *name, Atom *value, void *resource) {
    assert(disposed == 0u);
    Atom *row = atom_expr2(&fixture->persistent,
        atom_symbol(&fixture->persistent, "HandleProbeRow"), value);
    space_add(&fixture->runtime.space, row);
    assert(ask(fixture, "(match &self (HandleProbeRow $x) found)"));
    Atom *fired = ask(fixture,
        "(transform &self (take (HandleProbeRow $x)) (HandleProbeNext $x) &self)");
    if (!fired) fprintf(stderr, "transform failed at %s\n", name);
    assert(fired);
    Atom *made = ask(fixture,
        "(match &self (HandleProbeNext $x) (HandleProbeNext $x))");
    assert(made && made->kind == ATOM_EXPR && made->expr.len == 2u);
    assert(atom_eq(made->expr.elems[1], value));
    Atom *payload = made->expr.elems[1];
    if (payload->kind == ATOM_EXPR && atom_is_symbol(payload->expr.elems[0], "Body"))
        payload = payload->expr.elems[1];
    if (payload->kind == ATOM_EXPR && atom_is_symbol(payload->expr.elems[0], "NativeHandle"))
        assert(cetta_native_handle_get_owned(&fixture->runtime.libraries, payload,
            "probe.handle") == resource);
    assert(!ask(fixture, "(match &self (HandleProbeRow $x) remained)"));
    assert(ask(fixture, "(remove-atom &self (HandleProbeNext $x))"));
}

int main(int argc, char **argv) {
    (void)argc;
    CettaNameDomain names;
    cetta_name_domain_init(&names);
    CettaNameContext previous_names = cetta_name_context_current();
    CettaNameContext context = cetta_name_domain_context(&names);
    cetta_name_context_install(&context);
    Fixture fixture = {0};
    arena_init_detached(&fixture.persistent);
    arena_init_detached(&fixture.answers);
    assert(cetta_runtime_session_init(&fixture.runtime, &fixture.persistent,
        CETTA_LANGUAGE_PETTA, NULL));
    assert(cetta_runtime_session_open_root(&fixture.runtime, &fixture.persistent));
    CettaLibraryContext *library = eval_exchange_library_context(&fixture.runtime.libraries);
    CettaEvalResources *resources = eval_resources_exchange(fixture.runtime.resources);
    CettaEvalGc *collector = eval_collector_exchange(eval_resources_collector(fixture.runtime.resources));
    cetta_library_context_set_exec_path(&fixture.runtime.libraries, argv[0]);
    load(&fixture, "!(import! &self lib_transform)\n"
        "(: probe:fire (-> Atom Atom %Undefined%))\n"
        "(= (probe:fire $sources $template) (transform &self $sources $template &self))\n");
    void *resource = malloc(1u);
    assert(resource);
    uint64_t id;
    assert(cetta_native_handle_alloc(&fixture.runtime.libraries, "probe.handle",
        resource, dispose_resource, &id));
    Atom *owned = cetta_native_handle_owned_atom(&fixture.runtime.libraries,
        &fixture.persistent, "probe.handle", id);
    Atom *printed = cetta_native_handle_atom(&fixture.persistent, "probe.handle", id);
    assert(owned && printed);
    assert(cetta_native_handle_get_owned(&fixture.runtime.libraries, owned, "probe.handle") == resource);
    assert(!cetta_native_handle_get_owned(&fixture.runtime.libraries, printed, "probe.handle"));
    check(&fixture, "integer", atom_int(&fixture.persistent, 7), NULL);
    check(&fixture, "printed handle", printed, NULL);
    check(&fixture, "owning handle after its printed form", owned, resource);
    check(&fixture, "nested integer", atom_expr2(&fixture.persistent,
        atom_symbol(&fixture.persistent, "Body"), atom_int(&fixture.persistent, 7)), NULL);
    check(&fixture, "nested printed handle", atom_expr2(&fixture.persistent,
        atom_symbol(&fixture.persistent, "Body"), printed), NULL);
    check(&fixture, "nested owning handle", atom_expr2(&fixture.persistent,
        atom_symbol(&fixture.persistent, "Body"), owned), resource);
    assert(disposed == 0u);
    arena_free(&fixture.answers);
    cetta_runtime_session_dispose(&fixture.runtime);
    arena_free(&fixture.persistent);
    eval_collector_exchange(collector);
    eval_resources_exchange(resources);
    eval_exchange_library_context(library);
    cetta_name_context_install(&previous_names);
    cetta_name_domain_free(&names);
    assert(disposed == 1u);
    puts("PASS: transforms preserve owning handles across visible identifier aliases");
    return 0;
}
