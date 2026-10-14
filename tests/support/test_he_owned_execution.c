#include "eval.h"
#include "grounded.h"
#include "library.h"
#include "parser.h"
#include "symbol.h"
#include <inttypes.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

typedef struct {
    Arena source, scratch;
    Space space;
    Registry registry;
    CettaLibraryContext *library;
} Fixture;

static unsigned checks;
static void fixture_eval(Fixture *f, const char *text);
static void check(bool ok, const char *name) {
    if (!ok) {
        fprintf(stderr, "FAIL: %s\n", name);
        exit(1);
    }
    checks++;
}

static Atom *parse(Arena *arena, const char *text) {
    size_t position = 0u;
    Atom *result = parse_sexpr(arena, text, &position);
    check(result != NULL, "parse source");
    return result;
}

static void fixture_begin(Fixture *f, const char *source) {
    memset(f, 0, sizeof(*f));
    arena_init(&f->source);
    arena_set_runtime_kind(&f->source, CETTA_ARENA_RUNTIME_KIND_PERSISTENT);
    arena_init(&f->scratch);
    arena_set_runtime_kind(&f->scratch, CETTA_ARENA_RUNTIME_KIND_EVAL);
    space_init(&f->space);
    registry_init(&f->registry);
    registry_bind_id(&f->registry, g_builtin_syms.self,
        atom_space(&f->source, &f->space));
    f->library = calloc(1u, sizeof(*f->library));
    check(f->library != NULL, "library allocation");
    cetta_eval_session_init(&f->library->session, CETTA_LANGUAGE_HE,
        cetta_profile_he_extended());
    eval_set_library_context(f->library);
    size_t position = 0u;
    Atom *form;
    while ((form = parse_sexpr(&f->source, source, &position)))
        space_add(&f->space, form);
}

static void fixture_end(Fixture *f) {
    eval_release_temporary_spaces();
    eval_reset_form_gc_survivor();
    eval_cleanup_owned_new_spaces(&f->registry, &f->space);
    eval_set_library_context(NULL);
    cetta_library_context_free(f->library);
    registry_free(&f->registry);
    space_free(&f->space);
    arena_free(&f->scratch);
    arena_free(&f->source);
    free(f->library);
}

static CettaHeExecution *begin(Fixture *f, const char *query, CettaHeEntry entry) {
    Atom *input = parse(&f->scratch, query);
    CettaHeExecution *owner = eval_he_owned_begin(&f->space, &f->registry,
        entry, input, NULL, eval_get_default_fuel(), NULL, true, false);
    check(owner != NULL, "begin owned execution");
    return owner;
}

static CettaHeExecutionView inspect(CettaHeExecution *owner) {
    CettaHeExecutionView view;
    check(eval_he_owned_inspect(owner, &view), "inspect execution");
    return view;
}

static void advance(CettaHeExecution *owner) {
    CettaHeExecutionView before = inspect(owner);
    CettaHeAdvance status = eval_he_owned_advance(owner);
    check(status == CETTA_HE_ADVANCED || status == CETTA_HE_CLOSED,
        "ordinary advance remains complete");
    CettaHeExecutionView after = inspect(owner);
    check(after.advances == before.advances + (status == CETTA_HE_ADVANCED),
        "one operation increments one control account");
}

static void drain(CettaHeExecution *owner) {
    for (size_t i = 0u; i < 1000000u; i++) {
        CettaHeExecutionView view = inspect(owner);
        if (view.closed) {
            check(eval_he_owned_advance(owner) == CETTA_HE_CLOSED,
                "closed execution does not advance");
            return;
        }
        advance(owner);
    }
    check(false, "test's operation watchdog");
}

static void render(const OutcomeSet *outcomes, Arena *arena,
                   char *output, size_t capacity) {
    size_t used = 0u;
    output[0] = '\0';
    for (CettaCount i = 0u; i < outcomes->len; i++) {
        const Outcome *out = &outcomes->items[i];
        check(out->kind == CETTA_OUTCOME_INLINE,
            "evaluate entry publishes structural outcomes");
        Atom *value = out->materialized_atom ? out->materialized_atom : out->atom;
        check(value != NULL, "outcome has structural atom");
        const char *text = atom_to_string(arena, value);
        int appended = snprintf(output + used, capacity - used, "%s%s",
            i ? "|" : "", text);
        check(appended >= 0 && (size_t)appended < capacity - used,
            "render capacity");
        used += (size_t)appended;
    }
}

static void expect(Fixture *f, CettaHeExecution *owner, const char *expected) {
    CettaHeExecutionView view = inspect(owner);
    char output[32768];
    render(view.outcomes, &f->scratch, output, sizeof(output));
    if (strcmp(output, expected)) {
        fprintf(stderr, "expected: %s\nactual: %s\n", expected, output);
        check(false, "exact ordered observations");
    }
    check(true, "exact ordered observations");
}

static void intervening_evaluation(Fixture *f) {
    ResultSet values;
    result_set_init(&values);
    eval_top_with_registry(&f->space, &f->scratch, &f->source, &f->registry,
        parse(&f->scratch, "(+ 40 2)"), &values);
    check(values.len == 1u && atom_eq(values.items[0], atom_int(&f->scratch, 42)),
        "intervening synchronous evaluation");
    result_set_free(&values);
    eval_release_temporary_spaces();
    eval_reset_form_gc_survivor();
}

typedef struct { const char *name, *source, *query, *expected; } Case;
static const Case cases[] = {
    {"correlated repeated choices", "(= (pick) 1) (= (pick) 1) (= (pick) 2) "
        "(= (same $x) ($x $x))", "(same (pick))", "(1 1)|(1 1)|(2 2)"},
    {"independent choices", "(= (pick) 1) (= (pick) 2)",
        "((pick) (pick))", "(1 1)|(1 2)|(2 1)|(2 2)"},
    {"raw typed argument", "(: hold (-> Atom Atom)) (= (hold $x) $x)",
        "(hold (println! should-never-print))", "(println! should-never-print)"},
    {"quotation", "(: quote (-> Atom Atom))", "(quote (+ 1 2))", "(quote (+ 1 2))"},
    {"reified errors are data", "", "(Error raw payload)", "(Error raw payload)"},
    {"empty raw symbol", "", "Empty", "Empty"},
    {"entered empty result", "(= (none) (empty))", "(none)", ""},
    {"minimal chain result", "(= (run) (function (chain (eval (+ 1 2)) $x (return $x))))",
        "(run)", "3"},
    {"caller binding", "(= (same $x $x) ok)",
        "(let $v (same $a 2) ($a $v))", "(2 ok)"},
    {"typed constructor demand", "(: Box (-> Number Atom)) (= (step) (+ 1 2))",
        "(Box (step))", "(Box 3)"},
    {"typed child error", "", "(let $x (+ 1 \"S\") (println! should-never-print))",
        "(Error (+ 1 \"S\") (BadArgType 2 Number String))"},
    {"executed native child fault", "", "(let $x (/ 1 0) (println! should-never-print))",
        "(Error (/ 1 0) DivisionByZero)"},
    {"nested duplicate match", "(row 1 a) (row 1 a) (row 2 b)",
        "(let $out (match &self (row $k $v) ($k $v)) ($out $out))",
        "((1 a) (1 a))|((1 a) (1 a))|((2 b) (2 b))"},
    {"singleton tuple retains its wrapper", "", "(a)", "(a)"},
    {"rightmost constructed error suffix", "", "(a Error b Error c)", "(Error c)"},
    {"late binding constructs error suffix", "(= (bind Error) tail)",
        "(a $x (bind $x))", "(Error tail)"},
};

static void prefix_and_interleave(void) {
    for (size_t c = 0u; c < sizeof(cases) / sizeof(cases[0]); c++) {
        const Case *test = &cases[c];
        Fixture f;
        fixture_begin(&f, test->source);
        CettaHeExecution *initial = begin(&f, test->query, CETTA_HE_ENTRY_EVALUATE);
        drain(initial);
        expect(&f, initial, test->expected);
        uint64_t count = inspect(initial).advances;
        check(eval_he_owned_destroy(initial), "destroy complete execution");
        for (uint64_t prefix = 0u; prefix <= count; prefix++) {
            CettaHeExecution *left = begin(&f, test->query, CETTA_HE_ENTRY_EVALUATE);
            for (uint64_t i = 0u; i < prefix; i++)
                advance(left);
            CettaHeExecutionView first = inspect(left);
            CettaHeExecutionView second = inspect(left);
            check(first.advances == second.advances &&
                first.frames == second.frames && first.outcomes == second.outcomes,
                "inspection is inert");
            intervening_evaluation(&f);
            CettaHeExecution *right = begin(&f, test->query, CETTA_HE_ENTRY_EVALUATE);
            while (!inspect(left).closed || !inspect(right).closed) {
                if (!inspect(left).closed)
                    advance(left);
                if (!inspect(right).closed)
                    advance(right);
            }
            expect(&f, left, test->expected);
            expect(&f, right, test->expected);
            check(eval_he_owned_destroy(left), "destroy first interleaved execution");
            check(eval_he_owned_destroy(right), "destroy second interleaved execution");
        }
        printf("PASS: %s at all %" PRIu64 " control prefixes\n", test->name, count + 1u);
        fixture_end(&f);
    }
}

static void collected_bindings(void) {
    Fixture f;
    fixture_begin(&f, "(= (down $n) (if (== $n 0) 0 "
        "(let $u (change-state! &pressure $n) (+ 1 (down (- $n 1))))))");
    fixture_eval(&f, "(bind! &pressure (new-state 0))");
    Atom *held = parse(&f.scratch, "(kept a b c)");
    Atom *body = parse(&f.scratch, "(let $answer (collapse (down 3000)) (Held $held $held))");
    Atom *query = atom_expr2(&f.scratch, atom_symbol(&f.scratch, "collapse"), body);
    Atom *variable = body->expr.elems[3]->expr.elems[1];
    check(variable->kind == ATOM_VAR && variable->var_id ==
        body->expr.elems[3]->expr.elems[2]->var_id, "query uses one caller variable");
    Bindings bindings;
    bindings_init(&bindings);
    check(bindings_add_var(&bindings, variable, held), "input logical binding");
    CettaHeExecution *owner = eval_he_owned_begin(&f.space, &f.registry,
        CETTA_HE_ENTRY_BIND, query, NULL, eval_get_default_fuel(),
        &bindings, true, true);
    check(owner != NULL, "begin bound recursive execution");
    bindings_free(&bindings);
    arena_free(&f.scratch);
    arena_init(&f.scratch);
    CettaHeExecution *other = begin(&f, "(collapse (down 2000))", CETTA_HE_ENTRY_EVALUATE);
    for (size_t step = 0u; !inspect(owner).closed || !inspect(other).closed; step++) {
        check(step < 1000000u, "collection test watchdog");
        if (!inspect(owner).closed)
            advance(owner);
        if (!inspect(other).closed)
            advance(other);
        if (step % 97u == 0u)
            intervening_evaluation(&f);
    }
    expect(&f, owner, "((Held (kept a b c) (kept a b c)))");
    expect(&f, other, "(2000)");
    uint64_t collections = inspect(owner).collections + inspect(other).collections;
    printf("collection probe: %" PRIu64 " advances, %" PRIu64 " collections\n",
        inspect(owner).advances + inspect(other).advances, collections);
    check(collections > 0u, "suspended owners actually collect");
    check(eval_he_owned_destroy(other) && eval_he_owned_destroy(owner),
        "release collected owners");
    printf("PASS: copied borrowed bindings across %" PRIu64 " actual collections\n", collections);
    fixture_end(&f);
}

static void fixture_eval(Fixture *f, const char *text) {
    ResultSet out;
    result_set_init(&out);
    eval_top_with_registry(&f->space, &f->scratch, &f->source, &f->registry,
        parse(&f->scratch, text), &out);
    result_set_free(&out);
}

static void effect_prefixes(void) {
    const char *query = "(let $u (println! owned-effect) "
        "(let $v (change-state! &cell (+ (get-state &cell) 1)) (get-state &cell)))";
    Fixture first;
    fixture_begin(&first, "");
    fixture_eval(&first, "(bind! &cell (new-state 0))");
    CettaHeExecution *initial = begin(&first, query, CETTA_HE_ENTRY_EVALUATE);
    FILE *capture = tmpfile();
    check(capture != NULL, "effect capture");
    fflush(stdout);
    int saved_stdout = dup(STDOUT_FILENO);
    check(saved_stdout >= 0 && dup2(fileno(capture), STDOUT_FILENO) >= 0,
        "redirect effect output");
    drain(initial);
    fflush(stdout);
    check(dup2(saved_stdout, STDOUT_FILENO) >= 0, "restore output");
    close(saved_stdout);
    fclose(capture);
    uint64_t count = inspect(initial).advances;
    expect(&first, initial, "1");
    check(eval_he_owned_destroy(initial), "destroy initial effect run");
    fixture_end(&first);
    for (uint64_t prefix = 0u; prefix <= count; prefix++) {
        Fixture f;
        fixture_begin(&f, "");
        fixture_eval(&f, "(bind! &cell (new-state 0))");
        CettaHeExecution *owner = begin(&f, query, CETTA_HE_ENTRY_EVALUATE);
        capture = tmpfile();
        check(capture != NULL, "prefix effect capture");
        fflush(stdout);
        saved_stdout = dup(STDOUT_FILENO);
        check(saved_stdout >= 0 && dup2(fileno(capture), STDOUT_FILENO) >= 0,
            "redirect prefix effect output");
        for (uint64_t i = 0u; i < prefix; i++)
            advance(owner);
        CettaHeExecutionView before = inspect(owner);
        for (unsigned i = 0u; i < 50u; i++) {
            CettaHeExecutionView view = inspect(owner);
            check(view.advances == before.advances && view.outcomes == before.outcomes,
                "inspection cannot commit the pending effect");
        }
        intervening_evaluation(&f);
        drain(owner);
        fflush(stdout);
        check(dup2(saved_stdout, STDOUT_FILENO) >= 0, "restore prefix output");
        close(saved_stdout);
        rewind(capture);
        char output[128] = {0};
        check(fread(output, 1u, sizeof(output) - 1u, capture) == strlen("owned-effect\n") &&
            !strcmp(output, "owned-effect\n"), "effect occurs exactly once after pause");
        fclose(capture);
        expect(&f, owner, "1");
        check(eval_he_owned_destroy(owner), "destroy effect execution");
        fixture_end(&f);
    }
    printf("PASS: println!/change-state! at all %" PRIu64 " control prefixes\n", count + 1u);
}

static void cleanup_guards(void) {
    Fixture f;
    fixture_begin(&f, "(= (choose) 1) (= (choose) 2)");
    CaptureClosure closure = {.space_ptr = &f.space,
        .options = f.library->session.options};
    closure.options.type_check_auto = true;
    closure.options.max_stack_depth = 17;
    Atom *capture = atom_capture(&f.source, &closure);
    Atom *argument = parse(&f.scratch, "(collapse (let $x (choose) ($x $x)))");
    Atom *application = atom_expr2(&f.scratch, capture, argument);
    CettaHeExecution *initial = eval_he_owned_begin(&f.space, &f.registry,
        CETTA_HE_ENTRY_EVALUATE, application, NULL, eval_get_default_fuel(), NULL, true, false);
    check(initial != NULL, "begin captured options");
    drain(initial);
    expect(&f, initial, "((1 1) (2 2))");
    uint64_t count = inspect(initial).advances;
    check(eval_he_owned_destroy(initial), "destroy complete captured run");
    CettaEvaluatorOptions before = f.library->session.options;
    for (uint64_t prefix = 0u; prefix <= count; prefix++) {
        CettaHeExecution *owner = eval_he_owned_begin(&f.space, &f.registry,
            CETTA_HE_ENTRY_EVALUATE, application, NULL, eval_get_default_fuel(), NULL, true, false);
        check(owner != NULL, "begin guarded prefix");
        for (uint64_t i = 0u; i < prefix; i++) {
            advance(owner);
            check(!memcmp(&before, &f.library->session.options, sizeof(before)),
                "captured options cannot leak into another execution");
        }
        check(eval_he_owned_destroy(owner), "destroy every options/demand frame phase");
        check(!memcmp(&before, &f.library->session.options, sizeof(before)),
            "destroy restores caller options");
        intervening_evaluation(&f);
    }
    CettaHeExecution *cancelled = begin(&f, "(collapse (choose))", CETTA_HE_ENTRY_EVALUATE);
    advance(cancelled);
    check(eval_he_owned_cancel(cancelled), "cancel retained work");
    CettaHeExecutionView cancellation = inspect(cancelled);
    check(cancellation.interrupted && !cancellation.closed &&
        cancellation.completion == CETTA_EVAL_INCOMPLETE_CANCELLED &&
        eval_he_owned_advance(cancelled) == CETTA_HE_INTERRUPTED,
        "cancelled frontier cannot masquerade as complete");
    check(eval_he_owned_destroy(cancelled), "destroy cancelled work");
    CettaHeExecution *invalidated = begin(&f, "(choose)", CETTA_HE_ENTRY_EVALUATE);
    f.library->session.profile = cetta_profile_he_compat();
    check(eval_he_owned_advance(invalidated) == CETTA_HE_INTERRUPTED &&
        inspect(invalidated).completion == CETTA_EVAL_INCOMPLETE_INVALIDATED,
        "profile change invalidates owner");
    f.library->session.profile = cetta_profile_he_extended();
    check(eval_he_owned_destroy(invalidated), "destroy invalidated work");
    printf("PASS: options/observation cleanup at %" PRIu64 " prefixes, cancellation, invalidation\n", count + 1u);
    fixture_end(&f);
}

typedef struct { CettaHeExecution *owner; bool ok; } ThreadProbe;
static void *wrong_thread(void *argument) {
    ThreadProbe *probe = argument;
    CettaHeExecutionView view;
    probe->ok = !eval_he_owned_inspect(probe->owner, &view) &&
        eval_he_owned_advance(probe->owner) == CETTA_HE_BUSY &&
        !eval_he_owned_cancel(probe->owner) && !eval_he_owned_destroy(probe->owner);
    return NULL;
}

static void current_world_and_thread(void) {
    Fixture f;
    fixture_begin(&f, "(= (later) old) "
        "(= (driver) (let $u (change-state! &cell 1) (later)))");
    fixture_eval(&f, "(bind! &cell (new-state 0))");
    Atom *cell_atom = registry_lookup_id(&f.registry, symbol_intern_cstr(g_symbols, "&cell"));
    check(cell_atom && cell_atom->kind == ATOM_GROUNDED && cell_atom->ground.gkind == GV_STATE,
        "borrowed state resource");
    StateCell *cell = cell_atom->ground.ptr;
    CettaHeExecution *owner = begin(&f, "(driver)", CETTA_HE_ENTRY_EVALUATE);
    for (size_t step = 0u; !atom_eq(cell->value, atom_int(&f.scratch, 1)); step++) {
        check(step < 1000u, "world-change control watchdog");
        advance(owner);
    }
    check(!inspect(owner).closed, "pause between committed effect and next source read");
    check(space_remove(&f.space, parse(&f.scratch, "(= (later) old)")),
        "remove old source rule");
    space_add(&f.space, parse(&f.scratch, "(= (later) new)"));
    ThreadProbe probe = {.owner = owner};
    pthread_t thread;
    check(!pthread_create(&thread, NULL, wrong_thread, &probe) &&
        !pthread_join(thread, NULL) && probe.ok, "other thread cannot access retained owner");
    drain(owner);
    expect(&f, owner, "new");
    check(eval_he_owned_destroy(owner), "release changed-world owner");
    CettaHeExecution *stop = begin(&f, "(later)", CETTA_HE_ENTRY_EVALUATE);
    advance(stop);
    cetta_eval_session_request_process_exit(&f.library->session, 7);
    CettaHeAdvance status = CETTA_HE_ADVANCED;
    for (size_t step = 0u; status == CETTA_HE_ADVANCED; step++) {
        check(step < 1000u, "exit control watchdog");
        status = eval_he_owned_advance(stop);
    }
    check(status == CETTA_HE_INTERRUPTED && inspect(stop).interrupted &&
        !inspect(stop).closed && f.library->session.process_control.exit_requested &&
        f.library->session.process_control.exit_code == 7,
        "host-requested exit is a retained operational stop");
    cetta_eval_session_clear_process_exit(&f.library->session);
    check(eval_he_owned_advance(stop) == CETTA_HE_INTERRUPTED,
        "external clearing cannot replay stopped work");
    check(eval_he_owned_destroy(stop), "release stopped execution");
    printf("PASS: current-revision read after effect, thread boundary, native operational stop\n");
    fixture_end(&f);
}

static void transactional_caller_conflict(void) {
    Fixture f;
    fixture_begin(&f, "");
    fixture_eval(&f, "(bind! &cell (new-state 0))");
    Atom *cell_atom = registry_lookup_id(&f.registry,
        symbol_intern_cstr(g_symbols, "&cell"));
    check(cell_atom && cell_atom->kind == ATOM_GROUNDED &&
        cell_atom->ground.gkind == GV_STATE, "transaction caller state");
    StateCell *cell = cell_atom->ground.ptr;
    const char *query = "(let $u (change-state! &cell 1) (get-state &cell))";
    CettaHeExecution *owner = begin(&f, query, CETTA_HE_ENTRY_EVALUATE);
    CettaHeExecutionView before = inspect(owner);
    CettaEvaluatorOptions options = f.library->session.options;
    check(eval_payload_redirects_begin(&f.source), "begin real caller redirects");
    check(eval_he_owned_advance(owner) == CETTA_HE_BUSY,
        "redirect setup is a temporary caller conflict");
    check(!eval_he_owned_begin(&f.space, &f.registry, CETTA_HE_ENTRY_EVALUATE,
        parse(&f.scratch, query), NULL, eval_get_default_fuel(), NULL, true, false),
        "cannot construct an owner inside payload setup");
    check(!eval_he_owned_destroy(owner), "destroy cannot enter payload setup");
    bool previous_transactional = eval_set_payload_transactional(true);
    check(!previous_transactional, "caller begins outside another payload");
    uint64_t epoch = eval_next_payload_owner_epoch();
    uint64_t previous_epoch = eval_set_payload_owner_epoch(epoch);
    fixture_eval(&f, "(change-state! &cell 7)");
    StateCell *original = NULL, *redirect = NULL;
    check(eval_payload_state_redirect_count() == 1u &&
        eval_payload_state_redirect_at(0u, &original, &redirect) &&
        original == cell && redirect != cell &&
        atom_eq(cell->value, atom_int(&f.scratch, 0)) &&
        atom_eq(redirect->value, atom_int(&f.scratch, 7)),
        "caller write remains a real speculative redirect");
    for (size_t i = 0u; i < 3u; i++) {
        check(eval_he_owned_advance(owner) == CETTA_HE_BUSY,
            "owner cannot enter caller publication region");
        CettaHeExecutionView after = inspect(owner);
        check(after.advances == before.advances && after.frames == before.frames &&
            after.top_phase == before.top_phase &&
            after.completion == CETTA_EVAL_COMPLETE && !after.interrupted &&
            after.outcomes == before.outcomes && after.outcomes->len == 0u,
            "temporary conflict does not invalidate or advance owner");
        StateCell *saved_original = NULL, *saved_redirect = NULL;
        check(eval_payload_transactional() && eval_payload_owner_epoch() == epoch &&
            eval_payload_state_redirect_count() == 1u &&
            eval_payload_state_redirect_at(0u, &saved_original, &saved_redirect) &&
            saved_original == original && saved_redirect == redirect &&
            atom_eq(redirect->value, atom_int(&f.scratch, 7)) &&
            atom_eq(cell->value, atom_int(&f.scratch, 0)) &&
            !memcmp(&options, &f.library->session.options, sizeof(options)),
            "refusal leaves caller epoch, maps, values and options unchanged");
    }
    eval_set_payload_owner_epoch(previous_epoch);
    eval_set_payload_transactional(previous_transactional);
    check(eval_he_owned_advance(owner) == CETTA_HE_BUSY &&
        !eval_he_owned_destroy(owner),
        "payload promotion still owns redirects with flag cleared");
    eval_payload_redirects_end();
    check(!eval_payload_transactional() &&
        eval_payload_owner_epoch() == previous_epoch &&
        eval_payload_state_redirect_count() == 0u,
        "caller finishes its own payload cleanup");
    drain(owner);
    expect(&f, owner, "1");
    check(atom_eq(cell->value, atom_int(&f.scratch, 1)),
        "original HE effect commits only after caller cleanup");
    check(eval_he_owned_destroy(owner), "destroy ordinary owner after conflict");
    printf("PASS: caller transactional setup/write/promotion/refusal/resume\n");
    fixture_end(&f);
}

static void native_fault_outcome_profiles(void) {
    Fixture f;
    fixture_begin(&f, "");
    Atom *head = atom_symbol_id(&f.scratch, g_builtin_syms.op_div);
    Atom *arguments[] = {atom_int(&f.scratch, 1), atom_int(&f.scratch, 0)};
    CettaCallOutcome outcome;
    check(grounded_call(&f.scratch, head, arguments, 2u, &outcome) &&
        outcome.kind == CETTA_CALL_VALUE && atom_is_error(outcome.term) &&
        atom_eq(outcome.term, parse(&f.scratch, "(Error (/ 1 0) DivisionByZero)")),
        "HE native operation fault remains an exact error value");
    cetta_eval_session_init(&f.library->session, CETTA_LANGUAGE_PETTA,
        cetta_profile_petta_extended());
    eval_set_library_context(f.library);
    check(grounded_call(&f.scratch, head, arguments, 2u, &outcome) &&
        outcome.kind == CETTA_CALL_RAISED && atom_is_error(outcome.term),
        "PeTTa native operation fault is explicitly raised");
    check(!eval_he_owned_begin(&f.space, &f.registry, CETTA_HE_ENTRY_EVALUATE,
        parse(&f.scratch, "(/ 1 0)"), NULL, eval_get_default_fuel(),
        NULL, true, false), "HE lifecycle does not admit a PeTTa profile");
    cetta_eval_session_init(&f.library->session, CETTA_LANGUAGE_HE,
        cetta_profile_he_extended());
    eval_set_library_context(f.library);
    printf("PASS: actual native operation faults retain VALUE/RAISED profile distinction\n");
    fixture_end(&f);
}

int main(void) {
    SymbolTable symbols;
    VarInternTable variables;
    symbol_table_init(&symbols);
    symbol_table_init_builtins(&symbols, &g_builtin_syms);
    var_intern_init(&variables);
    g_symbols = &symbols;
    g_var_intern = &variables;
    eval_set_library_context(NULL);
    prefix_and_interleave();
    collected_bindings();
    effect_prefixes();
    cleanup_guards();
    current_world_and_thread();
    transactional_caller_conflict();
    native_fault_outcome_profiles();
    printf("PASS: %u retained execution checks\n", checks);
    var_intern_free(&variables);
    symbol_table_free(&symbols);
    return 0;
}
