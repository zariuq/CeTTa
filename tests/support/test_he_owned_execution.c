#include "eval.h"
#include "grounded.h"
#include "he_type_policy.h"
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
static const char *checking_case = "owned execution";
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

static void fixture_add_source(Fixture *f, const char *source) {
    size_t position = 0u;
    Atom *form;
    while ((form = parse_sexpr(&f->source, source, &position))) {
        form = eval_read_registry_words(&f->registry, &f->source, form);
        check(form != NULL, "capture source words at read time");
        space_add(&f->space, form);
    }
}

static void fixture_begin_profile(Fixture *f, const char *source,
                                  const CettaProfile *profile) {
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
    cetta_eval_session_init(&f->library->session, CETTA_LANGUAGE_HE, profile);
    eval_set_library_context(f->library);
    fixture_add_source(f, source);
}

static void fixture_begin(Fixture *f, const char *source) {
    fixture_begin_profile(f, source, cetta_profile_he_extended());
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
    Atom *input = eval_read_registry_words(
        &f->registry, &f->scratch, parse(&f->scratch, query));
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
        if (appended < 0 || (size_t)appended >= capacity - used) {
            fprintf(stderr, "render overflow case %s after [%s] next [%s]\n",
                checking_case, output, text ? text : "");
            check(false, "render capacity");
        }
        used += (size_t)appended;
    }
}

static void expect(Fixture *f, CettaHeExecution *owner, const char *expected) {
    CettaHeExecutionView view = inspect(owner);
    char output[32768];
    render(view.outcomes, &f->scratch, output, sizeof(output));
    if (strcmp(output, expected)) {
        fprintf(stderr, "case: %s\nexpected: %s\nactual: %s\n",
            checking_case, expected, output);
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
    {"minimal chain local binder", "(: function (-> Atom Atom))",
        "(function (chain (unify $x A B refused) $x (return $x)))", "B"},
    {"minimal chain renamed binder", "(: function (-> Atom Atom))",
        "(function (chain (unify $x A B refused) $y (return $y)))", "B"},
    {"minimal chain prebound nested binder", "(: function (-> Atom Atom))",
        "(function (chain (unify $x A B refused) $r (chain C $x (return $x))))", "C"},
    {"minimal chain aliased binder", "(: function (-> Atom Atom))",
        "(function (chain (unify $x $z B refused) $x (return $x)))", "B"},
    {"minimal unify incoming conflict", "(: function (-> Atom Atom))",
        "(function (chain (unify $x A first refused) $r "
        "(chain (unify $x B accepted refused) $s (return $s))))", "refused"},
    {"minimal unify compatible incoming", "(: function (-> Atom Atom))",
        "(function (chain (unify $x A first refused) $r "
        "(chain (unify $x A accepted refused) $s (return (pair $x $s)))))", "(pair A accepted)"},
    {"raw refusal restores evaluated call", "(= (decline $x) NotReducible)",
        "(decline (+ 1 2))", "(decline 3)"},
    {"duplicate raw refusal occurrences", "(= (decline $x) NotReducible) "
        "(= (decline $x) NotReducible)",
        "(decline 7)", "(decline 7)|(decline 7)"},
    {"changed binding reaches refusal caller", "(= (decline A) NotReducible)",
        "(let $r (decline $x) (pair $r $x))", "(pair (decline A) A)"},
    {"function refusal retains selected call", "(= (decline $x) "
        "(function (chain (unify $x A ok bad) $r (return NotReducible))))",
        "(decline $x)", "(decline A)"},
    {"minimal refusal stays protocol data", "(: function (-> Atom Atom)) "
        "(= (decline $x) NotReducible)",
        "(function (chain (eval (decline 7)) $r (return (pair $r raw))))",
        "(pair NotReducible raw)"},
    {"case keeps refused source bindings", "(= (decline A) NotReducible)",
        "(case (decline $x) (((decline A) (pair $x yes))))", "(pair A yes)"},
    {"case keeps ordinary source bindings", "(= (bind-a A) (val))",
        "(case (bind-a $x) (((val) (pair $x yes))))", "(pair A yes)"},
    {"case rejects conflicting captured branch", "(= (bind-a A) B)",
        "(case (bind-a $x) (($x wrong) (B (pair $x right))))", "(pair A right)"},
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
        checking_case = test->name;
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
    checking_case = "owned execution";
}

static void collected_bindings(void) {
    Fixture f;
    fixture_begin(&f, "(= (before-binding) &pressure)");
    fixture_eval(&f, "(bind! &pressure (new-state 0))");
    fixture_add_source(&f, "(= (down $n) (if (== $n 0) 0 "
        "(let $u (change-state! &pressure $n) (+ 1 (down (- $n 1))))))");
    CettaHeExecution *earlier = begin(&f, "(before-binding)", CETTA_HE_ENTRY_EVALUATE);
    drain(earlier);
    expect(&f, earlier, "&pressure");
    check(eval_he_owned_destroy(earlier), "release source read before binding");
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
        eval_read_registry_words(&f->registry, &f->scratch,
            parse(&f->scratch, text)), &out);
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

static void nested_minimal_collapse_prefixes(void) {
    checking_case = "nested minimal collapse";
    const char *source = "(: function (-> Atom Atom)) "
        "(= (saved A) token) (= (saved A) token) (= (saved B) token) "
        "(= (saved ignored) Empty)";
    const char *query =
        "(function (chain (collapse-bind "
        "(chain (metta (change-state! &cell (+ (get-state &cell) 1)) "
        "%Undefined% &self) $effect (collapse-bind (eval (saved $x))))) $outer "
        "(chain (superpose-bind $outer) $inner "
        "(chain (superpose-bind $inner) $answer (return ($answer $x))))))";
    const char *expected = "(token A)|(token A)|(token B)";
    Fixture first;
    fixture_begin(&first, source);
    fixture_eval(&first, "(bind! &cell (new-state 0))");
    CettaHeExecution *initial = begin(&first, query, CETTA_HE_ENTRY_EVALUATE);
    drain(initial);
    expect(&first, initial, expected);
    uint64_t count = inspect(initial).advances;
    check(eval_he_owned_destroy(initial), "destroy completed nested collapse");
    fixture_end(&first);

    uint64_t paused_after_effect = 0u;
    for (uint64_t prefix = 0u; prefix <= count; prefix++) {
        Fixture f;
        fixture_begin(&f, source);
        fixture_eval(&f, "(bind! &cell (new-state 0))");
        Atom *cell_atom = registry_lookup_id(&f.registry,
            symbol_intern_cstr(g_symbols, "&cell"));
        check(cell_atom && cell_atom->kind == ATOM_GROUNDED &&
            cell_atom->ground.gkind == GV_STATE, "nested collapse effect cell");
        StateCell *cell = cell_atom->ground.ptr;
        CettaHeExecution *owner = begin(&f, query, CETTA_HE_ENTRY_EVALUATE);
        for (uint64_t i = 0u; i < prefix; i++) advance(owner);
        CettaHeExecutionView before = inspect(owner);
        bool effect_done = atom_eq(cell->value, atom_int(&f.scratch, 1));
        check(effect_done || atom_eq(cell->value, atom_int(&f.scratch, 0)),
            "nested effect has not been repeated at a prefix");
        if (effect_done && !before.closed && before.outcomes->len == 0u)
            paused_after_effect++;
        for (unsigned i = 0u; i < 3u; i++) {
            CettaHeExecutionView after = inspect(owner);
            check(after.advances == before.advances &&
                after.frames == before.frames && after.top_phase == before.top_phase &&
                after.outcomes == before.outcomes && after.closed == before.closed &&
                atom_eq(cell->value, atom_int(&f.scratch, effect_done ? 1 : 0)),
                "inspection retains nested child and collector without replay");
        }
        intervening_evaluation(&f);
        drain(owner);
        expect(&f, owner, expected);
        check(atom_eq(cell->value, atom_int(&f.scratch, 1)),
            "nested child effect occurs exactly once across every pause");
        check(eval_he_owned_destroy(owner), "release nested collapse prefix");
        fixture_end(&f);
    }
    check(paused_after_effect > 0u,
        "scheduler can pause after child effect before aggregate publication");

    Fixture cancelled;
    fixture_begin(&cancelled, source);
    fixture_eval(&cancelled, "(bind! &cell (new-state 0))");
    Atom *cell_atom = registry_lookup_id(&cancelled.registry,
        symbol_intern_cstr(g_symbols, "&cell"));
    check(cell_atom && cell_atom->kind == ATOM_GROUNDED &&
        cell_atom->ground.gkind == GV_STATE, "cancelled nested collapse cell");
    StateCell *cell = cell_atom->ground.ptr;
    CettaHeExecution *owner = begin(&cancelled, query, CETTA_HE_ENTRY_EVALUATE);
    for (uint64_t i = 0u; !atom_eq(cell->value, atom_int(&cancelled.scratch, 1)); i++) {
        check(i < count, "nested effect reached before completed run");
        advance(owner);
    }
    CettaHeExecutionView pending = inspect(owner);
    check(!pending.closed && pending.outcomes->len == 0u,
        "unfinished nested collector has no published partial aggregate");
    check(eval_he_owned_cancel(owner), "cancel nested child after committed effect");
    for (unsigned i = 0u; i < 3u; i++) {
        check(eval_he_owned_advance(owner) == CETTA_HE_INTERRUPTED,
            "cancelled child cannot resume or replay");
        CettaHeExecutionView stopped = inspect(owner);
        check(stopped.interrupted && !stopped.closed &&
            stopped.outcomes->len == 0u && stopped.advances == pending.advances &&
            atom_eq(cell->value, atom_int(&cancelled.scratch, 1)),
            "cancellation retains the effect and does not complete the aggregate");
    }
    check(eval_he_owned_destroy(owner), "release cancelled nested collapse");
    CettaHeExecution *restart = begin(&cancelled, query, CETTA_HE_ENTRY_EVALUATE);
    drain(restart);
    expect(&cancelled, restart, expected);
    check(atom_eq(cell->value, atom_int(&cancelled.scratch, 2)),
        "a new execution really repeats the effect, unlike retained resumption");
    check(eval_he_owned_destroy(restart), "release explicit restarted control");
    fixture_end(&cancelled);
    printf("PASS: nested minimal collapse at %" PRIu64
        " prefixes, captures, duplicate occurrences, one effect, cancellation\n", count + 1u);
    checking_case = "owned execution";
}

static void lazy_refusal_prefixes(void) {
    checking_case = "lazy raw refusal";
    const char *source =
        "(= (decline-once) (function (chain "
        "(metta (change-state! &cell (+ (get-state &cell) 1)) %Undefined% &self) "
        "$effect (return NotReducible)))) "
        "(= (decline-once) (function (chain "
        "(metta (change-state! &cell (+ (get-state &cell) 1)) %Undefined% &self) "
        "$effect (return NotReducible))))";
    const char *query = "(once (decline-once))";
    Fixture first;
    fixture_begin(&first, "");
    fixture_eval(&first, "(pragma! select-mode lazy)");
    fixture_eval(&first, "(bind! &cell (new-state 0))");
    fixture_add_source(&first, source);
    CettaHeExecution *initial = begin(&first, query, CETTA_HE_ENTRY_EVALUATE);
    drain(initial);
    expect(&first, initial, "(decline-once)");
    uint64_t count = inspect(initial).advances;
    check(eval_he_owned_destroy(initial), "destroy complete lazy refusal");
    fixture_end(&first);
    for (uint64_t prefix = 0u; prefix <= count; prefix++) {
        Fixture f;
        fixture_begin(&f, "");
        fixture_eval(&f, "(pragma! select-mode lazy)");
        fixture_eval(&f, "(bind! &cell (new-state 0))");
        fixture_add_source(&f, source);
        Atom *cell_atom = registry_lookup_id(&f.registry,
            symbol_intern_cstr(g_symbols, "&cell"));
        check(cell_atom && cell_atom->kind == ATOM_GROUNDED &&
            cell_atom->ground.gkind == GV_STATE, "lazy refusal effect cell");
        StateCell *cell = cell_atom->ground.ptr;
        CettaHeExecution *owner = begin(&f, query, CETTA_HE_ENTRY_EVALUATE);
        for (uint64_t i = 0u; i < prefix; i++) advance(owner);
        check(atom_eq(cell->value, atom_int(&f.scratch, 0)) ||
            atom_eq(cell->value, atom_int(&f.scratch, 1)),
            "an unselected refusal does not execute before a pause");
        CettaHeExecutionView paused = inspect(owner);
        check(inspect(owner).advances == paused.advances,
            "lazy refusal inspection does not advance work");
        intervening_evaluation(&f);
        drain(owner);
        expect(&f, owner, "(decline-once)");
        check(atom_eq(cell->value, atom_int(&f.scratch, 1)),
            "resumption keeps one committed effect and one restored call");
        check(eval_he_owned_advance(owner) == CETTA_HE_CLOSED &&
            atom_eq(cell->value, atom_int(&f.scratch, 1)),
            "completed bounded call cannot replay either occurrence");
        check(eval_he_owned_destroy(owner), "release lazy refusal prefix");
        fixture_end(&f);
    }
    printf("PASS: lazy raw refusal at all %" PRIu64
        " control prefixes, one selected occurrence and one effect\n", count + 1u);
    checking_case = "owned execution";
}

static StateCell *host_return_fixture(Fixture *f) {
    fixture_begin(f, "(: function (-> Atom Atom))");
    cetta_eval_session_init(&f->library->session, CETTA_LANGUAGE_HE,
        cetta_profile_he_compat());
    eval_set_library_context(f->library);
    fixture_eval(f, "(bind! &count (new-state 0))");
    /* Store a callable before its rule exists, so construction itself has
     * no effect. The raw read must enter its saved ordinary continuation. */
    fixture_eval(f, "(bind! &box (new-state (stored-tick)))");
    fixture_eval(f, "(bind! &alias &box)");
    fixture_add_source(f,
        "(= (stored-tick) (function (chain "
        "(metta (change-state! &count (+ (get-state &count) 1)) %Undefined% &self) "
        "$effect (return (delivered)))))");
    Atom *counter = registry_lookup_id(&f->registry,
        symbol_intern_cstr(g_symbols, "&count"));
    check(counter && counter->kind == ATOM_GROUNDED &&
        counter->ground.gkind == GV_STATE, "host-return effect cell");
    return counter->ground.ptr;
}

static void host_return_prefixes(void) {
    checking_case = "raw host producer retained return";
    const char *query = "(collapse (superpose ((get-state &box) (get-state &alias))))";
    const char *expected = "((delivered) (delivered))";
    Fixture first;
    StateCell *counter = host_return_fixture(&first);
    CettaHeExecution *initial = begin(&first, query, CETTA_HE_ENTRY_EVALUATE);
    drain(initial);
    expect(&first, initial, expected);
    check(atom_eq(counter->value, atom_int(&first.scratch, 2)),
        "two host read occurrences each execute their effect once");
    uint64_t count = inspect(initial).advances;
    check(eval_he_owned_destroy(initial), "destroy complete host return");
    fixture_end(&first);

    uint64_t pending_after_effect = 0u;
    for (uint64_t prefix = 0u; prefix <= count; prefix++) {
        Fixture f;
        counter = host_return_fixture(&f);
        CettaHeExecution *owner = begin(&f, query, CETTA_HE_ENTRY_EVALUATE);
        for (uint64_t i = 0u; i < prefix; i++) advance(owner);
        CettaHeExecutionView saved = inspect(owner);
        Atom *before = counter->value;
        bool first_effect = atom_eq(before, atom_int(&f.scratch, 1));
        check(first_effect || atom_eq(before, atom_int(&f.scratch, 0)) ||
            atom_eq(before, atom_int(&f.scratch, 2)),
            "host-return prefix has not replayed a producer occurrence");
        if (first_effect && !saved.closed && saved.outcomes->len == 0u)
            pending_after_effect++;
        check(inspect(owner).advances == saved.advances && counter->value == before,
            "host-return inspection preserves saved child and effect");
        intervening_evaluation(&f);
        drain(owner);
        expect(&f, owner, expected);
        check(atom_eq(counter->value, atom_int(&f.scratch, 2)),
            "resumption neither skips nor replays either stored call");
        check(eval_he_owned_advance(owner) == CETTA_HE_CLOSED &&
            atom_eq(counter->value, atom_int(&f.scratch, 2)),
            "completed host return cannot execute again");
        check(eval_he_owned_destroy(owner), "release host-return prefix");
        fixture_end(&f);
    }
    check(pending_after_effect > 0u,
        "host return can pause after an effect before aggregate publication");

    Fixture cancelled;
    counter = host_return_fixture(&cancelled);
    CettaHeExecution *owner = begin(&cancelled, query, CETTA_HE_ENTRY_EVALUATE);
    for (uint64_t i = 0u; !atom_eq(counter->value, atom_int(&cancelled.scratch, 1)); i++) {
        check(i < count, "reach the first stored-call effect");
        advance(owner);
    }
    CettaHeExecutionView saved = inspect(owner);
    check(!saved.closed && saved.outcomes->len == 0u,
        "unfinished host aggregate is not published");
    check(eval_he_owned_cancel(owner), "cancel stored-call continuation");
    check(eval_he_owned_advance(owner) == CETTA_HE_INTERRUPTED &&
        !inspect(owner).closed && inspect(owner).outcomes->len == 0u &&
        atom_eq(counter->value, atom_int(&cancelled.scratch, 1)),
        "cancellation preserves committed effect without completing sibling work");
    check(eval_he_owned_destroy(owner), "release cancelled host return");
    fixture_end(&cancelled);

    Fixture collected;
    counter = host_return_fixture(&collected);
    /* The deep let is the standard library equation on this he-compat space. */
    Atom *let_equation = he_library_equation_at(0u);
    check(let_equation != NULL, "standard let equation for host collection");
    if (!space_admit_atom(&collected.space, &collected.source, let_equation))
        space_add(&collected.space, let_equation);
    fixture_eval(&collected, "(bind! &pressure (new-state 0))");
    fixture_eval(&collected, "(bind! &long-box (new-state (stored-long)))");
    fixture_add_source(&collected,
        "(= (down $n) (if (== $n 0) 0 "
        "(let $u (change-state! &pressure $n) (+ 1 (down (- $n 1)))))) "
        "(= (stored-long) (let $u (change-state! &count 1) (down 3000)))");
    owner = begin(&collected, "(get-state &long-box)", CETTA_HE_ENTRY_EVALUATE);
    drain(owner);
    expect(&collected, owner, "3000");
    check(inspect(owner).collections > 0u,
        "raw host continuation actually performs moving collection");
    check(atom_eq(counter->value, atom_int(&collected.scratch, 1)),
        "raw host continuation survives collection with one producer effect");
    check(eval_he_owned_destroy(owner), "release collected host return");
    fixture_end(&collected);
    printf("PASS: host return at all %" PRIu64
        " control prefixes, alias occurrences, effects, cancellation, collection\n", count + 1u);
    checking_case = "owned execution";
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
    fixture_begin(&f, "(= (later) old)");
    fixture_eval(&f, "(bind! &cell (new-state 0))");
    fixture_add_source(&f, "(= (driver) (let $u (change-state! &cell 1) (later)))");
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

static void install_he_declarations(Fixture *f) {
    size_t count = he_library_declaration_count();
    check(count > 0u, "HE library declarations load");
    for (size_t i = 0u; i < count; i++) {
        Atom *declaration = he_library_declaration_at(i);
        check(declaration != NULL, "HE library declaration");
        if (!space_admit_atom(&f->space, &f->source, declaration))
            space_add(&f->space, declaration);
    }
}

static void install_standard_let(Fixture *f) {
    install_he_declarations(f);
    Atom *equation = he_library_equation_at(0u);
    check(he_library_equation_count() == 2u && equation != NULL &&
        equation->kind == ATOM_EXPR && equation->expr.len == 3u &&
        atom_head_symbol_id(equation->expr.elems[1]) == g_builtin_syms.let,
        "the standard library keeps its let equation first");
    if (!space_admit_atom(&f->space, &f->source, equation))
        space_add(&f->space, equation);
}

static StateCell *bound_zero_cell(Fixture *f) {
    fixture_eval(f, "(bind! &cell (new-state 0))");
    Atom *cell_atom = registry_lookup_id(&f->registry,
        symbol_intern_cstr(g_symbols, "&cell"));
    check(cell_atom && cell_atom->kind == ATOM_GROUNDED &&
        cell_atom->ground.gkind == GV_STATE, "let/function boundary cell");
    return cell_atom->ground.ptr;
}

static bool rendered_has_item(const char *rendered, const char *item) {
    size_t item_len = strlen(item);
    const char *cursor = rendered;
    while (*cursor) {
        const char *bar = strchr(cursor, '|');
        size_t len = bar ? (size_t)(bar - cursor) : strlen(cursor);
        if (len == item_len && !memcmp(cursor, item, len))
            return true;
        if (!bar)
            break;
        cursor = bar + 1u;
    }
    return false;
}

static void capture_open(FILE **capture, int *saved) {
    *capture = tmpfile();
    check(*capture != NULL, "let/function capture");
    fflush(stdout);
    *saved = dup(STDOUT_FILENO);
    check(*saved >= 0 && dup2(fileno(*capture), STDOUT_FILENO) >= 0,
        "redirect let/function output");
}

static void capture_close(FILE *capture, int saved, char *output, size_t capacity) {
    fflush(stdout);
    check(dup2(saved, STDOUT_FILENO) >= 0, "restore let/function output");
    close(saved);
    rewind(capture);
    size_t n = fread(output, 1u, capacity - 1u, capture);
    output[n] = '\0';
    check(n + 1u < capacity || feof(capture), "captured effect text fits");
    fclose(capture);
}

/* He-compat occurrences of the real library let, and of a function body,
 * across pause, resume, and cancellation. The extended println/change-state
 * sweep above is a different profile and does not cross this boundary. */
static void let_function_boundary(void) {
    checking_case = "he-compat let/function boundary";
    const char *raw_query =
        "(eval (let $a (println! owned-raw-let) (change-state! &cell 7)))";
    const char *full_query =
        "(let $u (println! owned-full-let) "
        "(let $v (change-state! &cell (+ (get-state &cell) 1)) (get-state &cell)))";
    const char *function_source =
        "(= (show) (function (println! owned-fn))) "
        "(= (touch) (function (change-state! &cell 9))) "
        "(= (give) (function (return (println! owned-return))))";
    struct {
        const char *name;
        const char *source;
        const char *query;
        const char *outcome;
        const char *printed;
        int cell;
        bool structural_raw;
    } cases[] = {
        {"raw let keeps the equation syntax", "", raw_query, NULL, "", 0, true},
        {"full let runs the library step once", "", full_query, "1",
            "owned-full-let\n", 1, false},
        {"named function body is not executed", function_source, "(show)",
            "(Error (show) NoReturn)", "", 0, false},
        {"named function state body is not executed", function_source, "(touch)",
            "(Error (touch) NoReturn)", "", 0, false},
        {"returned effect runs once", function_source, "(give)",
            "()", "owned-return\n", 0, false},
        {"returned bound effect runs once",
            "(= (give $x) (function (return (println! $x))))",
            "(give owned-bound)",
            "()", "owned-bound\n", 0, false},
    };

    for (size_t c = 0u; c < sizeof(cases) / sizeof(cases[0]); c++) {
        checking_case = cases[c].name;
        Fixture baseline;
        fixture_begin_profile(&baseline, cases[c].source, cetta_profile_he_compat());
        install_standard_let(&baseline);
        StateCell *base_cell = bound_zero_cell(&baseline);
        CettaHeExecution *initial = begin(&baseline, cases[c].query,
            CETTA_HE_ENTRY_EVALUATE);
        FILE *capture = NULL;
        int saved = -1;
        capture_open(&capture, &saved);
        drain(initial);
        char printed[256];
        capture_close(capture, saved, printed, sizeof(printed));
        char outcome[4096];
        render(inspect(initial).outcomes, &baseline.scratch, outcome, sizeof(outcome));
        if (cases[c].structural_raw) {
            bool raw_step = strstr(outcome, "(unify ") == outcome &&
                strstr(outcome, "(println! owned-raw-let)") &&
                strstr(outcome, "(change-state! (State 0) 7)") &&
                strstr(outcome, " Empty)") &&
                !strchr(outcome, '|');
            if (!raw_step)
                fprintf(stderr, "raw outcome: [%s]\nraw printed: [%s]\n",
                    outcome, printed);
            check(raw_step, "raw let returns the single unevaluated library step");
        } else {
            expect(&baseline, initial, cases[c].outcome);
        }
        check(!strcmp(printed, cases[c].printed), "baseline effect text");
        check(atom_eq(base_cell->value, atom_int(&baseline.scratch, cases[c].cell)),
            "baseline cell");
        uint64_t count = inspect(initial).advances;
        char raw_outcome[4096];
        snprintf(raw_outcome, sizeof(raw_outcome), "%s", outcome);
        check(eval_he_owned_destroy(initial), "destroy baseline boundary run");
        fixture_end(&baseline);
        bool saw_live_effect = false;

        for (uint64_t prefix = 0u; prefix <= count; prefix++) {
            Fixture f;
            fixture_begin_profile(&f, cases[c].source, cetta_profile_he_compat());
            install_standard_let(&f);
            StateCell *cell = bound_zero_cell(&f);
            CettaHeExecution *owner = begin(&f, cases[c].query,
                CETTA_HE_ENTRY_EVALUATE);
            capture_open(&capture, &saved);
            for (uint64_t i = 0u; i < prefix; i++)
                advance(owner);
            int paused_cell = -1;
            for (int value = 0; value <= 9; value++) {
                if (atom_eq(cell->value, atom_int(&f.scratch, value)))
                    paused_cell = value;
            }
            check(paused_cell == 0 || paused_cell == cases[c].cell,
                "a prefix has not repeated the cell effect");
            if (cases[c].cell != 0 && paused_cell == cases[c].cell &&
                !inspect(owner).closed)
                saw_live_effect = true;
            CettaHeExecutionView paused = inspect(owner);
            for (unsigned i = 0u; i < 3u; i++) {
                CettaHeExecutionView again = inspect(owner);
                check(again.advances == paused.advances &&
                    again.outcomes == paused.outcomes &&
                    again.frames == paused.frames &&
                    atom_eq(cell->value, atom_int(&f.scratch, paused_cell)),
                    "inspection does not commit or replay the boundary effect");
            }
            intervening_evaluation(&f);
            check(atom_eq(cell->value, atom_int(&f.scratch, paused_cell)),
                "an intervening evaluation does not replay the suspended effect");
            drain(owner);
            capture_close(capture, saved, printed, sizeof(printed));
            check(!strcmp(printed, cases[c].printed),
                "resume prints the effect exactly as the uninterrupted run");
            render(inspect(owner).outcomes, &f.scratch, outcome, sizeof(outcome));
            if (cases[c].structural_raw) {
                if (strcmp(outcome, raw_outcome)) {
                    fprintf(stderr, "baseline raw: %s\nresumed raw: %s\n",
                        raw_outcome, outcome);
                    check(false, "resume returns the same raw library step");
                }
                check(true, "resume returns the same raw library step");
            } else {
                expect(&f, owner, cases[c].outcome);
            }
            check(atom_eq(cell->value, atom_int(&f.scratch, cases[c].cell)),
                "resume leaves the cell at the single committed value");
            check(eval_he_owned_destroy(owner), "destroy resumed boundary run");
            fixture_end(&f);
        }
        if (cases[c].cell != 0)
            check(saw_live_effect,
                "the cell effect is visible while the let boundary is still open");

        Fixture cancelled;
        fixture_begin_profile(&cancelled, cases[c].source, cetta_profile_he_compat());
        install_standard_let(&cancelled);
        StateCell *cell = bound_zero_cell(&cancelled);
        CettaHeExecution *owner = begin(&cancelled, cases[c].query,
            CETTA_HE_ENTRY_EVALUATE);
        capture_open(&capture, &saved);
        check(eval_he_owned_cancel(owner), "cancel before the boundary effect");
        for (unsigned i = 0u; i < 3u; i++) {
            check(eval_he_owned_advance(owner) == CETTA_HE_INTERRUPTED,
                "a cancelled boundary does not resume");
            CettaHeExecutionView stopped = inspect(owner);
            check(stopped.interrupted && !stopped.closed &&
                stopped.outcomes->len == 0u &&
                atom_eq(cell->value, atom_int(&cancelled.scratch, 0)),
                "early cancellation publishes neither a result nor an effect");
        }
        capture_close(capture, saved, printed, sizeof(printed));
        check(printed[0] == '\0', "early cancellation prints nothing");
        check(eval_he_owned_destroy(owner), "destroy early-cancelled boundary");

        owner = begin(&cancelled, cases[c].query, CETTA_HE_ENTRY_EVALUATE);
        capture_open(&capture, &saved);
        uint64_t stepped = 0u;
        while (!inspect(owner).closed &&
               atom_eq(cell->value, atom_int(&cancelled.scratch, 0)) &&
               stepped < count) {
            advance(owner);
            stepped++;
        }
        if (inspect(owner).closed && stepped > 0u) {
            capture_close(capture, saved, printed, sizeof(printed));
            check(eval_he_owned_destroy(owner),
                "release a boundary that finished on its committing step");
            cell = bound_zero_cell(&cancelled);
            owner = begin(&cancelled, cases[c].query, CETTA_HE_ENTRY_EVALUATE);
            capture_open(&capture, &saved);
            for (uint64_t i = 1u; i < stepped; i++)
                advance(owner);
        }
        check(!inspect(owner).closed, "cancellation stops a live boundary");
        CettaHeExecutionView pending = inspect(owner);
        char partial[4096];
        render(pending.outcomes, &cancelled.scratch, partial, sizeof(partial));
        int held_cell = atom_eq(cell->value, atom_int(&cancelled.scratch, cases[c].cell))
            ? cases[c].cell : 0;
        check(eval_he_owned_cancel(owner), "cancel a partially advanced boundary");
        for (unsigned i = 0u; i < 3u; i++) {
            check(eval_he_owned_advance(owner) == CETTA_HE_INTERRUPTED,
                "partial cancellation does not continue");
            CettaHeExecutionView stopped = inspect(owner);
            char kept[4096];
            render(stopped.outcomes, &cancelled.scratch, kept, sizeof(kept));
            check(stopped.interrupted && stopped.advances == pending.advances &&
                !strcmp(kept, partial) &&
                atom_eq(cell->value, atom_int(&cancelled.scratch, held_cell)),
                "cancellation retains committed work and does not replay it");
        }
        capture_close(capture, saved, printed, sizeof(printed));
        check(strstr(printed, cases[c].printed) == printed ||
            printed[0] == '\0',
            "cancellation does not print a second copy of the effect");
        if (cases[c].printed[0] != '\0')
            check(printed[0] == '\0' || !strcmp(printed, cases[c].printed),
                "a committed print is not repeated by cancellation");
        check(eval_he_owned_destroy(owner), "destroy partially cancelled boundary");
        CettaHeExecution *restart = begin(&cancelled, cases[c].query,
            CETTA_HE_ENTRY_EVALUATE);
        capture_open(&capture, &saved);
        drain(restart);
        capture_close(capture, saved, printed, sizeof(printed));
        check(!strcmp(printed, cases[c].printed),
            "a new execution repeats the effect once");
        check(atom_eq(cell->value,
                atom_int(&cancelled.scratch, held_cell + cases[c].cell)),
            "a new execution is distinct from retained resumption");
        check(eval_he_owned_destroy(restart), "destroy restarted boundary");
        fixture_end(&cancelled);
        printf("PASS: %s at %" PRIu64 " pause points, with cancellation\n",
            cases[c].name, count + 1u);
    }

    checking_case = "duplicate let rows stay distinct";
    Fixture duplicates;
    fixture_begin_profile(&duplicates, "", cetta_profile_he_compat());
    install_standard_let(&duplicates);
    fixture_add_source(&duplicates,
        "(= (let $pattern $atom $template) (unify $atom $pattern $template Empty))");
    CettaHeExecution *owner = begin(&duplicates, "(let $a 7 $a)",
        CETTA_HE_ENTRY_EVALUATE);
    drain(owner);
    char outcome[4096];
    render(inspect(owner).outcomes, &duplicates.scratch, outcome, sizeof(outcome));
    const char *split = strchr(outcome, '|');
    bool same_row = split &&
        (size_t)(split - outcome) == strlen(split + 1u) &&
        !memcmp(outcome, split + 1u, (size_t)(split - outcome));
    if (!same_row || inspect(owner).outcomes->len != 2u)
        fprintf(stderr, "duplicate let outcomes: %s\n", outcome);
    check(inspect(owner).outcomes->len == 2u && same_row && split != outcome,
        "two identical let rows both reduce, and specialization stays off");
    check(eval_he_owned_destroy(owner), "destroy duplicate let rows");
    fixture_end(&duplicates);

    checking_case = "user let alternative is kept";
    Fixture alternatives;
    fixture_begin_profile(&alternatives,
        "(= (let $p $a $t) (user-let $a))", cetta_profile_he_compat());
    install_standard_let(&alternatives);
    owner = begin(&alternatives, "(let $x 1 $x)", CETTA_HE_ENTRY_EVALUATE);
    drain(owner);
    render(inspect(owner).outcomes, &alternatives.scratch, outcome, sizeof(outcome));
    if (inspect(owner).outcomes->len != 2u ||
        !rendered_has_item(outcome, "(user-let 1)") ||
        !rendered_has_item(outcome, "1"))
        fprintf(stderr, "user let outcomes: %s\n", outcome);
    check(inspect(owner).outcomes->len == 2u &&
        rendered_has_item(outcome, "(user-let 1)") &&
        rendered_has_item(outcome, "1"),
        "a different user let equation remains beside the library step");
    check(eval_he_owned_destroy(owner), "destroy user let alternative");
    fixture_end(&alternatives);

    checking_case = "changed let row is interpreted";
    Fixture changed;
    fixture_begin_profile(&changed,
        "(= (let $a $b $c) (other $b))", cetta_profile_he_compat());
    install_he_declarations(&changed);
    owner = begin(&changed, "(let $x 1 $x)", CETTA_HE_ENTRY_EVALUATE);
    drain(owner);
    expect(&changed, owner, "(other 1)");
    check(eval_he_owned_destroy(owner), "destroy changed let row");
    fixture_end(&changed);
    checking_case = "owned execution";
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
    nested_minimal_collapse_prefixes();
    lazy_refusal_prefixes();
    host_return_prefixes();
    cleanup_guards();
    current_world_and_thread();
    transactional_caller_conflict();
    native_fault_outcome_profiles();
    let_function_boundary();
    printf("PASS: %u retained execution checks\n", checks);
    var_intern_free(&variables);
    symbol_table_free(&symbols);
    return 0;
}
