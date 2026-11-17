#define _POSIX_C_SOURCE 200809L
#ifdef NDEBUG
#undef NDEBUG
#endif
#include "tmetta_internal.h"
#include "runtime_session.h"
#include "runtime_names.h"
#include "parser.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>

typedef struct { CettaRuntimeSession runtime; Arena source, output, retained; } Fixture;

static bool refuse_answers, answers_refused;
static unsigned matches_until_refusal;

bool __real_match_atoms(Atom *left, Atom *right, Bindings *bindings, Arena *arena);
bool __real_match_atoms_builder(Atom *left, Atom *right, BindingsBuilder *bindings, Arena *arena);

static bool refuse_match(void) {
    if (!matches_until_refusal || --matches_until_refusal) return false;
    assert(cetta_native_memory_recover());
    return true;
}

bool __wrap_match_atoms(Atom *left, Atom *right, Bindings *bindings, Arena *arena) {
    return !refuse_match() && __real_match_atoms(left, right, bindings, arena);
}

bool __wrap_match_atoms_builder(Atom *left, Atom *right, BindingsBuilder *bindings, Arena *arena) {
    return !refuse_match() && __real_match_atoms_builder(left, right, bindings, arena);
}

bool __real_cetta_native_handle_alloc(CettaLibraryContext *, const char *,
    void *, CettaNativeHandleFreeFn, uint64_t *);

bool __wrap_cetta_native_handle_alloc(CettaLibraryContext *context, const char *kind,
    void *value, CettaNativeHandleFreeFn release, uint64_t *id) {
    if (refuse_answers && strcmp(kind, "tmetta-answers") == 0) {
        answers_refused = true;
        return false;
    }
    return __real_cetta_native_handle_alloc(context, kind, value, release, id);
}

static Atom *parse(Fixture *fixture, const char *source) {
    Atom **forms = NULL;
    int count = parse_metta_text(source, &fixture->source, &forms);
    assert(count == 1);
    Atom *result = forms[0];
    free(forms);
    return result;
}

static Atom *ask_atom(Fixture *fixture, Atom *source) {
    EvalOutcome result;
    eval_outcome_init(&result);
    eval_top_with_registry_outcome(&fixture->runtime.space, &fixture->output,
        &fixture->source, &fixture->runtime.registry, source, &result, NULL, NULL);
    assert(result.completion == CETTA_EVAL_COMPLETE && result.results.len == 1);
    Atom *answer = atom_deep_copy(&fixture->retained, result.results.items[0]);
    assert(answer);
    eval_outcome_free(&result);
    arena_free(&fixture->output);
    arena_init_detached(&fixture->output);
    return answer;
}

static Atom *ask(Fixture *fixture, const char *source) { return ask_atom(fixture, parse(fixture, source)); }

static void define(Fixture *fixture, const char *source) {
    Atom *row = parse(fixture, source);
    assert(space_admit_atom_from_source_arena(&fixture->runtime.space, NULL, &fixture->source, row));
}

static void number(Atom *value, int64_t expected) {
    assert(value && value->kind == ATOM_GROUNDED && value->ground.gkind == GV_INT && value->ground.ival == expected);
}

static void publication(Fixture *fixture) {
    Space left, right;
    TermUniverse *universe = cetta_library_context_universe(&fixture->runtime.libraries);
    assert(space_try_init_with_universe(&left, universe));
    assert(space_try_init_with_universe(&right, universe));
    Atom *coin = atom_symbol(&fixture->source, "coin");
    Atom *done = atom_symbol(&fixture->source, "done");
    assert(space_admit_atom(&left, NULL, coin) && space_admit_atom(&right, NULL, coin));
    Space *destinations[] = {&left, &right};
    Space *prepared[] = {space_heap_clone_shallow(&left), space_heap_clone_shallow(&right)};
    assert(prepared[0] && prepared[1]);
    SpaceReadToken versions[] = {space_read_token(&left), space_read_token(&right)};
    assert(space_remove(prepared[0], coin) && space_remove(prepared[1], coin));
    assert(space_admit_atom(prepared[0], NULL, done));
    assert(space_admit_atom(&left, NULL, done));
    /* A stale first participant cannot spend a current second participant. */
    assert(!space_publish_native_replacements(destinations, prepared, versions, 2));
    assert(space_contains_exact(&left, coin) && space_contains_exact(&right, coin));
    versions[0] = space_read_token(&left);
    assert(space_publish_native_replacements(destinations, prepared, versions, 2));
    assert(space_length64(&left) == 1 && space_contains_exact(&left, done));
    assert(space_length64(&right) == 0);
    /* Retired copies own the previous contents and can be reclaimed now. */
    for (size_t i = 0; i < 2; ++i) { space_free(prepared[i]); free(prepared[i]); }
    assert(space_contains_exact(&left, done));
    prepared[0] = space_heap_clone_shallow(&left);
    assert(prepared[0]);
    versions[0] = space_read_token(&left);
    Space *aliases[] = {&left, &left};
    Space *next[] = {prepared[0], prepared[0]};
    SpaceReadToken repeated[] = {versions[0], versions[0]};
    assert(!space_publish_native_replacements(aliases, next, repeated, 2));
    assert(space_contains_exact(&left, done));
    space_free(prepared[0]); free(prepared[0]);
    space_free(&left); space_free(&right);
}

static void escaped_answers(Fixture *fixture) {
    CettaLibraryContext *context = &fixture->runtime.libraries;
    Atom *handle = ask(fixture, "(collapse (superpose (7 7 8)))");
    TmAnswers *answers = tm_answers_get(context, handle);
    assert(answers && answers->bank.len == 3);
    for (CettaIndex i = 0; i < answers->bank.len; ++i)
        assert(answer_bank_get(&answers->bank, i)->result->arena_id == answers->storage->arena.identity);
    Atom *constrained = ask(fixture, "(collapse (chain (unify $x 0) $_ 7))");
    TmAnswers *captured = tm_answers_get(context, constrained);
    assert(captured && captured->bank.len == 1);
    const AnswerRecord *record = answer_bank_get(&captured->bank, 0);
    BindingsIterator iterator = {.bindings = &record->bindings};
    Binding binding;
    Atom *variable = NULL;
    while (bindings_iterator_next(&iterator, &binding)) {
        Atom *image = bindings_apply_value(&record->bindings, &fixture->retained, binding.value);
        if (image && image->kind == ATOM_GROUNDED && image->ground.gkind == GV_INT && image->ground.ival == 0) {
            variable = atom_var_with_id(&fixture->source, "captured", binding.var_id); break;
        }
    }
    assert(variable);
    Atom *conflict = atom_expr3(&fixture->source, atom_symbol(&fixture->source, "unify"), variable,
        atom_int(&fixture->source, 1));
    Atom *reflect = atom_expr2(&fixture->source, atom_symbol(&fixture->source, "reflect"), constrained);
    Atom *branch = atom_expr(&fixture->source, (Atom *[]){atom_symbol(&fixture->source, "chain"),
        conflict, atom_var_with_id(&fixture->source, "unused", fresh_var_id()), reflect}, 4);
    Atom *collect = atom_expr2(&fixture->source, atom_symbol(&fixture->source, "collapse"), branch);
    number(ask_atom(fixture, atom_expr2(&fixture->source, atom_symbol(&fixture->source, "count"), collect)), 0);
    /* Reclaim another query's builders while the escaped records remain live. */
    for (unsigned i = 0; i < 24; ++i) {
        Atom *again = ask(fixture, "(collapse (collapse (superpose (a a b))))");
        assert(tm_answers_get(context, again));
        assert(tm_answers_get(context, handle)->bank.len == 3);
    }
    Atom *thunks = ask(fixture, "(collapse (delay (superpose (0 1))))");
    TmAnswers *saved = tm_answers_get(context, thunks);
    assert(saved && saved->bank.len == 1);
    assert(prime_need_snapshot_present(bindings_need_view(&answer_bank_get(&saved->bank, 0)->bindings)));
    Atom *reopen = atom_expr2(&fixture->source, atom_symbol(&fixture->source, "reflect"), thunks);
    Atom *query = parse(fixture, "(let $t placeholder (let $a (force $t) (let $b (force $t) (Pair $a $b))))");
    query = atom_expr(&fixture->source, (Atom *[]){query->expr.elems[0], query->expr.elems[1], reopen, query->expr.elems[3]}, 4);
    EvalOutcome result;
    eval_outcome_init(&result);
    eval_top_with_registry_outcome(&fixture->runtime.space, &fixture->output, &fixture->source,
        &fixture->runtime.registry, query, &result, NULL, NULL);
    assert(result.completion == CETTA_EVAL_COMPLETE && result.results.len == 2);
    for (CettaIndex i = 0; i < result.results.len; ++i) {
        Atom *pair = result.results.items[i];
        assert(pair->kind == ATOM_EXPR && pair->expr.len == 3 && atom_eq(pair->expr.elems[1], pair->expr.elems[2]));
    }
    eval_outcome_free(&result);
}

static void shared_dag(Fixture *fixture) {
    Atom *leaf = atom_var_with_id(&fixture->source, "dag", fresh_var_id());
    Atom *node = leaf;
    for (unsigned i = 0; i < 40; ++i)
        node = atom_expr3(&fixture->source, atom_symbol(&fixture->source, "Node"), node, node);
    Atom *held = atom_expr2(&fixture->source, atom_symbol(&fixture->source, "hold"), node);
    Atom *alternatives = atom_expr3(&fixture->source, atom_symbol(&fixture->source, "amb"), held, held);
    Atom *reified = atom_expr2(&fixture->source, atom_symbol(&fixture->source, "reify"), alternatives);
    Atom *serialized = atom_expr3(&fixture->source, atom_symbol(&fixture->source, "serialize"),
        atom_symbol(&fixture->source, "canonical"), reified);
    TmAnswers *ordered = tm_answers_get(&fixture->runtime.libraries, ask_atom(fixture, serialized));
    assert(ordered && ordered->ordered && ordered->bank.len == 2);
    assert(atom_eq(answer_bank_get(&ordered->bank, 0)->result, answer_bank_get(&ordered->bank, 1)->result));
    Atom *result = ask_atom(fixture, held);
    for (unsigned i = 0; i < 40; ++i) {
        assert(result->kind == ATOM_EXPR && result->expr.len == 3 && result->expr.elems[1] == result->expr.elems[2]);
        result = result->expr.elems[1];
    }
    assert(result->kind == ATOM_VAR);
}

static void retained_frontiers(Fixture *fixture) {
    CettaLibraryContext *context = &fixture->runtime.libraries;
    Space *root = &fixture->runtime.space;
    Registry *registry = &fixture->runtime.registry;
    TermUniverse *universe = cetta_library_context_universe(context);
    Arena input;
    arena_init_detached(&input);
    Atom **forms = NULL;
    assert(parse_metta_text(
        "(chain (add-atom &self frontier-write) $_ "
        "(reify (amb (reify (superpose (7 7))) 8)))", &input, &forms) == 1);
    EvalOutcome result;
    eval_outcome_init(&result);
    cetta_tmetta_eval(context, root, registry, &fixture->output, forms[0], 0, &result);
    assert(result.completion == CETTA_EVAL_INCOMPLETE_FUEL && result.results.len == 0);
    Atom *paused = atom_deep_copy(&fixture->retained, result.frontier);
    assert(paused);
    free(forms);
    arena_free(&input);
    eval_outcome_free(&result);
    arena_reset(&fixture->output, (ArenaMark){0});

    term_universe_set_collection_mode(universe, TERM_UNIVERSE_COLLECTION_RECLAIM);
    bool collecting_nested = false;
    for (unsigned i = 0; i < 64; ++i) {
        assert(term_universe_collect(universe, NULL));
        CettaTmettaFrontier info;
        assert(cetta_tmetta_frontier_info(context, paused, &info) && info.runnable);
        collecting_nested |= info.collecting == 2;
        eval_outcome_init(&result);
        cetta_tmetta_resume(context, root, registry, &fixture->output, paused, 1, &result);
        if (result.completion == CETTA_EVAL_COMPLETE) break;
        assert(result.completion == CETTA_EVAL_INCOMPLETE_FUEL &&
            result.frontier && result.results.len == 0);
        eval_outcome_free(&result);
        arena_reset(&fixture->output, (ArenaMark){0});
    }
    assert(collecting_nested && result.completion == CETTA_EVAL_COMPLETE);
    assert(!result.frontier && result.results.len == 1);
    TmAnswers *outer = tm_answers_get(context, result.results.items[0]);
    assert(outer && outer->bank.len == 2);
    bool saw_inner = false, saw_eight = false;
    for (CettaIndex i = 0; i < outer->bank.len; ++i) {
        Atom *value = answer_bank_get(&outer->bank, i)->result;
        TmAnswers *inner = tm_answers_get(context, value);
        if (inner) {
            assert(inner->bank.len == 2);
            number(answer_bank_get(&inner->bank, 0)->result, 7);
            number(answer_bank_get(&inner->bank, 1)->result, 7);
            saw_inner = true;
        } else {
            number(value, 8);
            saw_eight = true;
        }
    }
    assert(saw_inner && saw_eight);
    eval_outcome_free(&result);
    arena_reset(&fixture->output, (ArenaMark){0});
    number(ask(fixture, "(count (reify (match &self frontier-write seen)))"), 1);
    CettaTmettaFrontier info;
    assert(!cetta_tmetta_frontier_info(context, paused, &info));

    eval_outcome_init(&result);
    cetta_tmetta_eval(context, root, registry, &fixture->output,
        parse(fixture, "(reify (amb ready (if $open yes no)))"), -1, &result);
    assert(result.completion == CETTA_EVAL_INCOMPLETE_SUSPENDED &&
        result.results.len == 0 && result.frontier);
    assert(cetta_tmetta_frontier_info(context, result.frontier, &info) &&
        info.suspended == 1 && info.runnable == 0 && info.collecting == 1);
    paused = result.frontier;
    eval_outcome_free(&result);
    define(fixture, "(= (changed) 1)");
    eval_outcome_init(&result);
    cetta_tmetta_resume(context, root, registry, &fixture->output, paused, -1, &result);
    assert(result.completion == CETTA_EVAL_INCOMPLETE_INVALIDATED &&
        result.results.len == 0 && !result.frontier);
    eval_outcome_free(&result);
    arena_reset(&fixture->output, (ArenaMark){0});

    CettaEvalCancellationScope scope;
    _Atomic bool cancelled = true;
    eval_cancellation_enter(&scope, &cancelled);
    eval_outcome_init(&result);
    cetta_tmetta_eval(context, root, registry, &fixture->output,
        parse(fixture, "(count (reify (superpose (a a b))))"), -1, &result);
    eval_cancellation_leave(&scope);
    assert(result.completion == CETTA_EVAL_INCOMPLETE_CANCELLED &&
        result.frontier && !result.results.len);
    paused = result.frontier;
    eval_outcome_free(&result);
    eval_outcome_init(&result);
    cetta_tmetta_resume(context, root, registry, &fixture->output, paused, -1, &result);
    assert(result.completion == CETTA_EVAL_COMPLETE && result.results.len == 1);
    number(result.results.items[0], 3);
    eval_outcome_free(&result);
    arena_reset(&fixture->output, (ArenaMark){0});
}

static void ordered_answers(Fixture *fixture) {
    const char *producers[] = {
        "(amb (let $first 1 answer) (let $second 0 answer))",
        "(amb (let $renamed 0 answer) (let $other 1 answer))"
    };
    CettaLibraryContext *context = &fixture->runtime.libraries;
    for (size_t i = 0; i < 2; ++i) {
        char expression[256];
        snprintf(expression, sizeof expression, "(serialize canonical (reify %s))", producers[i]);
        TmAnswers *answers = tm_answers_get(context, ask(fixture, expression));
        assert(answers && answers->ordered && answers->bank.len == 2);
        for (CettaIndex j = 0; j < 2; ++j) {
            const AnswerRecord *record = answer_bank_get(&answers->bank, j);
            BindingsIterator iterator = {.bindings = &record->bindings};
            Binding binding;
            assert(bindings_iterator_next(&iterator, &binding));
            number(bindings_apply_value(&record->bindings, &fixture->retained, binding.value), (int64_t)j);
            assert(!bindings_iterator_next(&iterator, &binding));
        }
    }
    Atom *tail = ask(fixture, "(cdr-atom (serialize canonical (reify (superpose (30 10 20)))))");
    TmAnswers *view = tm_answers_get(context, tail);
    assert(view && view->bank.len == 2 && view->backing);
    assert(term_universe_collect(cetta_library_context_universe(context), NULL));
    Atom *first = atom_expr2(&fixture->source, atom_symbol(&fixture->source, "car-atom"), tail);
    number(ask_atom(fixture, first), 20);
}

static void wake_inputs(Fixture *fixture) {
    CettaLibraryContext *context = &fixture->runtime.libraries;
    Space *root = &fixture->runtime.space;
    Registry *registry = &fixture->runtime.registry;
    Arena *output = &fixture->output;
    Atom *source = parse(fixture, "(chain (add-atom &self wake-marker) $_ "
        "(reify (amb ready (amb (if $a yes no) (if $b later no)))))");
    Atom *choice = source->expr.elems[3]->expr.elems[1]->expr.elems[2];
    Atom *a = choice->expr.elems[1]->expr.elems[1];
    Atom *b = choice->expr.elems[2]->expr.elems[1];
    Atom *truth = atom_bool(&fixture->source, true);
    Atom *unrelated = atom_var_with_literal(&fixture->source, "a", fresh_var_id());
    CettaTmettaFrontier info;
    EvalOutcome result;
    eval_outcome_init(&result);
    cetta_tmetta_eval(context, root, registry, output, source, -1, &result);
    assert(result.completion == CETTA_EVAL_INCOMPLETE_SUSPENDED && result.frontier);
    Atom *paused = result.frontier;
    eval_outcome_free(&result);
    assert(cetta_tmetta_frontier_info(context, paused, &info) && info.suspended == 2);

    eval_outcome_init(&result);
    assert(!cetta_tmetta_resume_with_value(context, root, registry, output,
        paused, unrelated, truth, -1, &result));
    assert(result.completion == CETTA_EVAL_INCOMPLETE_SUSPENDED && !result.results.len);
    eval_outcome_free(&result);

    eval_outcome_init(&result);
    assert(cetta_tmetta_resume_with_value(context, root, registry, output,
        paused, a, truth, 0, &result) == 1);
    assert(result.completion == CETTA_EVAL_INCOMPLETE_FUEL && !result.results.len);
    assert(cetta_tmetta_frontier_info(context, paused, &info) &&
        info.suspended == 1 && info.runnable == 1);
    eval_outcome_free(&result);

    eval_outcome_init(&result);
    assert(cetta_tmetta_resume_with_value(context, root, registry, output,
        paused, b, truth, -1, &result) == 1);
    assert(result.completion == CETTA_EVAL_COMPLETE && result.results.len == 1);
    TmAnswers *answers = tm_answers_get(context, result.results.items[0]);
    assert(answers && answers->bank.len == 3);
    assert(tm_named(answer_bank_get(&answers->bank, 0)->result, "ready"));
    assert(tm_named(answer_bank_get(&answers->bank, 1)->result, "later"));
    assert(tm_named(answer_bank_get(&answers->bank, 2)->result, "yes"));
    eval_outcome_free(&result);
    arena_reset(output, (ArenaMark){0});
    number(ask(fixture, "(count (reify (match &self wake-marker hit)))"), 1);

    source = parse(fixture, "(chain (unify $alias $input) $_ (+ $alias 1))");
    Atom *input = source->expr.elems[1]->expr.elems[2];
    eval_outcome_init(&result);
    cetta_tmetta_eval(context, root, registry, output, source, -1, &result);
    assert(result.completion == CETTA_EVAL_INCOMPLETE_SUSPENDED && result.frontier);
    paused = result.frontier;
    eval_outcome_free(&result);
    eval_outcome_init(&result);
    assert(cetta_tmetta_resume_with_value(context, root, registry, output,
        paused, input, atom_int(&fixture->source, 5), -1, &result) == 1);
    assert(result.completion == CETTA_EVAL_COMPLETE && result.results.len == 1);
    number(result.results.items[0], 6);
    eval_outcome_free(&result);
    arena_reset(output, (ArenaMark){0});

    source = parse(fixture, "(let $local $input (if $local yes no))");
    eval_outcome_init(&result);
    cetta_tmetta_eval(context, root, registry, output, source, -1, &result);
    assert(result.completion == CETTA_EVAL_INCOMPLETE_SUSPENDED && result.frontier);
    paused = result.frontier;
    eval_outcome_free(&result);
    eval_outcome_init(&result);
    assert(!cetta_tmetta_resume_with_value(context, root, registry, output,
        paused, source->expr.elems[1], truth, -1, &result));
    assert(result.completion == CETTA_EVAL_INCOMPLETE_SUSPENDED && !result.results.len);
    eval_outcome_free(&result);
    eval_outcome_init(&result);
    assert(cetta_tmetta_resume_with_value(context, root, registry, output,
        paused, source->expr.elems[2], truth, -1, &result) == 1);
    assert(result.completion == CETTA_EVAL_COMPLETE && result.results.len == 1);
    assert(tm_named(result.results.items[0], "yes"));
    eval_outcome_free(&result);
    arena_reset(output, (ArenaMark){0});
}

static void publication_failure(Fixture *fixture) {
    ask(fixture, "(bind! &left (new-space))");
    ask(fixture, "(bind! &right (new-space))");
    ask(fixture, "(add-atom &left first)");
    ask(fixture, "(add-atom &right second)");
    CettaLibraryContext *context = &fixture->runtime.libraries;
    Space *root = &fixture->runtime.space;
    Registry *registry = &fixture->runtime.registry;
    Space *left = resolve_space(registry, parse(fixture, "&left"));
    Space *right = resolve_space(registry, parse(fixture, "&right"));
    assert(left && right);
    SpaceReadToken before_left = space_read_token(left), before_right = space_read_token(right);
    EvalOutcome result;
    eval_outcome_init(&result);
    refuse_answers = true;
    cetta_tmetta_eval(context, root, registry, &fixture->output,
        parse(fixture, "(transform &left (, (take first) (take &right second)) done)"),
        -1, &result);
    refuse_answers = false;
    assert(answers_refused && result.completion == CETTA_EVAL_INCOMPLETE_CAPACITY);
    assert(!result.results.len && result.frontier);
    assert(space_read_token_matches_live_space(before_left, left));
    assert(space_read_token_matches_live_space(before_right, right));
    Atom *paused = result.frontier;
    eval_outcome_free(&result);
    eval_outcome_init(&result);
    cetta_tmetta_resume(context, root, registry, &fixture->output, paused, -1, &result);
    assert(result.completion == CETTA_EVAL_COMPLETE && result.results.len == 1);
    TmAnswers *output = tm_answers_get(context, result.results.items[0]);
    assert(output && output->bank.len == 1);
    assert(space_length64(left) == 1 && tm_named(space_get_at64(left, 0), "done"));
    assert(space_length64(right) == 0);
    eval_outcome_free(&result);
    arena_reset(&fixture->output, (ArenaMark){0});
}

static void collection_publication_failure(Fixture *fixture) {
    CettaLibraryContext *context = &fixture->runtime.libraries;
    Space *root = &fixture->runtime.space;
    Registry *registry = &fixture->runtime.registry;
    EvalOutcome result;
    eval_outcome_init(&result);
    answers_refused = false;
    refuse_answers = true;
    cetta_tmetta_eval(context, root, registry, &fixture->output,
        parse(fixture, "(reify (amb (chain (unify $x 1) $_ hit) "
                      "(chain (unify $x 2) $_ hit)))"), -1, &result);
    refuse_answers = false;
    assert(answers_refused && result.completion == CETTA_EVAL_INCOMPLETE_CAPACITY &&
        !result.results.len && result.frontier);
    Atom *paused = result.frontier;
    eval_outcome_free(&result);
    eval_outcome_init(&result);
    cetta_tmetta_resume(context, root, registry, &fixture->output, paused, -1, &result);
    assert(result.completion == CETTA_EVAL_COMPLETE && result.results.len == 1);
    TmAnswers *answers = tm_answers_get(context, result.results.items[0]);
    assert(answers && answers->bank.len == 2);
    for (CettaIndex i = 0; i < 2; ++i) {
        const AnswerRecord *record = answer_bank_get(&answers->bank, i);
        BindingsIterator it = {.bindings = &record->bindings};
        Binding binding;
        bool found = false;
        while (bindings_iterator_next(&it, &binding)) {
            Atom *value = bindings_apply_value(&record->bindings, &fixture->output, binding.value);
            if (value && value->kind == ATOM_GROUNDED && value->ground.gkind == GV_INT) {
                assert(value->ground.ival == (int64_t)i + 1);
                found = true;
            }
        }
        assert(found && tm_named(record->result, "hit"));
    }
    eval_outcome_free(&result);
    arena_reset(&fixture->output, (ArenaMark){0});
}

static void wake_guard(Fixture *fixture) {
    CettaLibraryContext *context = &fixture->runtime.libraries;
    Space *root = &fixture->runtime.space;
    Registry *registry = &fixture->runtime.registry;
    Arena *output = &fixture->output;
    for (int permit = 0; permit < 2; ++permit) {
        ask(fixture, "(bind! &gate (new-space))");
        ask(fixture, "(add-atom &gate coin)");
        Space *bank = resolve_space(registry, parse(fixture, "&gate"));
        assert(bank);
        SpaceReadToken unchanged = space_read_token(bank);
        Atom *source = parse(fixture,
            "(transform &gate (, (take coin) (let True (if $permit True False))) paid)");
        Atom *guard = source->expr.elems[2]->expr.elems[2]->expr.elems[2];
        EvalOutcome result;
        eval_outcome_init(&result);
        cetta_tmetta_eval(context, root, registry, output, source, -1, &result);
        assert(result.completion == CETTA_EVAL_INCOMPLETE_SUSPENDED &&
            result.frontier && !result.results.len);
        assert(space_read_token_matches_live_space(unchanged, bank));
        Atom *paused = result.frontier;
        eval_outcome_free(&result);
        eval_outcome_init(&result);
        assert(cetta_tmetta_resume_with_value(context, root, registry, output,
            paused, guard->expr.elems[1], atom_bool(&fixture->source, permit), -1, &result) == 1);
        assert(result.completion == CETTA_EVAL_COMPLETE && result.results.len == 1);
        TmAnswers *answers = tm_answers_get(context, result.results.items[0]);
        assert(answers && answers->bank.len == (CettaCount)permit);
        assert(space_length64(bank) == 1);
        assert(tm_named(space_get_at64(bank, 0), permit ? "paid" : "coin"));
        if (!permit) assert(space_read_token_matches_live_space(unchanged, bank));
        eval_outcome_free(&result);
        arena_reset(output, (ArenaMark){0});
    }
}

static void observed_space_revision(Fixture *fixture) {
    ask(fixture, "(add-atom &right seed)");
    CettaLibraryContext *context = &fixture->runtime.libraries;
    Space *root = &fixture->runtime.space;
    Registry *registry = &fixture->runtime.registry;
    Space *left = resolve_space(registry, parse(fixture, "&left"));
    Space *right = resolve_space(registry, parse(fixture, "&right"));
    EvalOutcome result;
    eval_outcome_init(&result);
    cetta_tmetta_eval(context, root, registry, &fixture->output,
        parse(fixture, "(reify (chain (match &right seed ok) $_ (superpose (a b c))))"),
        0, &result);
    Atom *paused = result.frontier;
    assert(paused && result.completion == CETTA_EVAL_INCOMPLETE_FUEL);
    eval_outcome_free(&result);
    CettaTmettaFrontier info;
    bool observed = false;
    for (unsigned i = 0; i < 16 && !observed; ++i) {
        eval_outcome_init(&result);
        cetta_tmetta_resume(context, root, registry, &fixture->output, paused, 1, &result);
        assert(result.completion == CETTA_EVAL_INCOMPLETE_FUEL);
        assert(cetta_tmetta_frontier_info(context, paused, &info));
        observed = info.observed_spaces == 1;
        eval_outcome_free(&result);
    }
    assert(observed);
    assert(space_admit_atom(left, NULL, parse(fixture, "unrelated")));
    eval_outcome_init(&result);
    cetta_tmetta_resume(context, root, registry, &fixture->output, paused, 1, &result);
    assert(result.completion == CETTA_EVAL_INCOMPLETE_FUEL && result.frontier);
    eval_outcome_free(&result);
    assert(space_admit_atom(right, NULL, parse(fixture, "changed")));
    eval_outcome_init(&result);
    cetta_tmetta_resume(context, root, registry, &fixture->output, paused, -1, &result);
    assert(result.completion == CETTA_EVAL_INCOMPLETE_INVALIDATED && !result.results.len);
    eval_outcome_free(&result);
    arena_reset(&fixture->output, (ArenaMark){0});
}

static void retained_code_binder(Fixture *fixture) {
    Arena *a = &fixture->source;
    Atom *name = ask(fixture, "(hold $outer)");
    assert(name->kind == ATOM_VAR && !cetta_frame_identity_is_none(name->var_id.epoch));
    Atom *inner = atom_expr(a, (Atom *[]){atom_symbol(a, "let"), name,
        parse(fixture, "(superpose (0 1))"), name}, 4);
    Atom *held = atom_expr2(a, atom_symbol(a, "hold"), inner);
    Atom *code = atom_var_with_literal(a, "code", fresh_var_id());
    Atom *activate = atom_expr(a, (Atom *[]){atom_symbol(a, "let"), code, held,
        atom_expr2(a, atom_symbol(a, "run"), code)}, 4);
    Atom *seed = atom_expr3(a, atom_symbol(a, "unify"), name, atom_int(a, 99));
    Atom *query = atom_expr(a, (Atom *[]){atom_symbol(a, "chain"), seed,
        atom_var_with_literal(a, "unused", fresh_var_id()), activate}, 4);
    EvalOutcome result;
    eval_outcome_init(&result);
    eval_top_with_registry_outcome(&fixture->runtime.space, &fixture->output,
        a, &fixture->runtime.registry, query, &result, NULL, NULL);
    assert(result.completion == CETTA_EVAL_COMPLETE && result.results.len == 2);
    int64_t first = result.results.items[0]->ground.ival;
    assert(first == 0 || first == 1);
    number(result.results.items[0], first);
    number(result.results.items[1], 1 - first);
    eval_outcome_free(&result);
    arena_reset(&fixture->output, (ArenaMark){0});
}

static Atom *resume_after_match_refusal(Fixture *fixture, const char *text, unsigned attempts) {
    Atom *source = parse(fixture, text);
    CettaNativeMemory *owner = cetta_native_memory_new(UINT64_MAX);
    CettaNativeMemoryScope memory;
    assert(owner);
    cetta_native_memory_enter(&memory, owner);
    CettaRuntimeSession *session = &fixture->runtime;
    EvalOutcome result;
    eval_outcome_init(&result);
    matches_until_refusal = attempts;
    cetta_tmetta_eval(&session->libraries, &session->space, &session->registry,
        &fixture->output, source, -1, &result);
    assert(!matches_until_refusal && result.completion == CETTA_EVAL_INCOMPLETE_CAPACITY);
    assert(!result.results.len && result.frontier);
    Atom *paused = result.frontier;
    eval_outcome_free(&result);
    cetta_native_memory_clear_failure(owner);
    eval_outcome_init(&result);
    cetta_tmetta_resume(&session->libraries, &session->space, &session->registry,
        &fixture->output, paused, -1, &result);
    assert(result.completion == CETTA_EVAL_COMPLETE && result.results.len == 1);
    Atom *answer = atom_deep_copy(&fixture->retained, result.results.items[0]);
    assert(answer);
    eval_outcome_free(&result);
    arena_reset(&fixture->output, (ArenaMark){0});
    cetta_native_memory_leave(&memory);
    cetta_native_memory_release(owner);
    return answer;
}

static void match_capacity_failure(Fixture *fixture) {
    define(fixture, "(= (budgeted (Tag $x)) match-available)");
    assert(tm_named(resume_after_match_refusal(fixture, "(budgeted (Tag 7))", 1),
                    "match-available"));

    /* Refuse the second scalar binding after the first has been written.
     * The retry keeps both bindings and exposes no half-open activation. */
    define(fixture, "(= (budgeted-scalar $x $y) (reify (Pair $x $y)))");
    Atom *result = resume_after_match_refusal(fixture, "(budgeted-scalar 7 8)", 2);
    TmAnswers *answers = tm_answers_get(&fixture->runtime.libraries, result);
    assert(answers && answers->bank.len == 1);
    const AnswerRecord *row = answer_bank_get(&answers->bank, 0);
    size_t count;
    assert(bindings_current_binding_count(&row->bindings, &count) && count == 2);
    assert(atom_eq(row->result, parse(fixture, "(Pair 7 8)")));
}

static void fresh_runtime_inputs(Fixture *fixture) {
    Atom *source = parse(fixture, "(let $x outer (let $code &foreign-code "
        "(let $a (run $code) (let $b (run $code) (Pair $a $b)))))");
    Atom *name = source->expr.elems[1];
    Atom *payload = atom_expr2(&fixture->source, atom_symbol(&fixture->source, "hold"), name);
    assert(payload);
    registry_bind_id(&fixture->runtime.registry, symbol_intern_cstr(g_symbols, "&foreign-code"), payload);
    Atom *result = ask_atom(fixture, source);
    assert(result->kind == ATOM_EXPR && result->expr.len == 3);
    const Atom *a = result->expr.elems[1], *b = result->expr.elems[2];
    assert(a->kind == ATOM_VAR && b->kind == ATOM_VAR);
    assert(!var_id_eq(a->var_id, b->var_id));
    assert(!cetta_frame_identity_is_none(a->var_id.epoch) &&
        !cetta_frame_identity_is_none(b->var_id.epoch));
}

int main(int argc, char **argv) {
    (void)argc;
    CettaNameDomain names;
    cetta_name_domain_init(&names);
    CettaNameContext previous = cetta_name_context_current();
    CettaNameContext name_context = cetta_name_domain_context(&names);
    cetta_name_context_install(&name_context);
    Fixture *fixture = calloc(1, sizeof *fixture);
    assert(fixture);
    arena_init_detached(&fixture->source); arena_init_detached(&fixture->output); arena_init_detached(&fixture->retained);
    assert(cetta_runtime_session_init(&fixture->runtime, &fixture->source, CETTA_LANGUAGE_TMETTA, NULL));
    assert(cetta_runtime_session_open_root(&fixture->runtime, &fixture->source));
    CettaLibraryContext *previous_library = eval_exchange_library_context(&fixture->runtime.libraries);
    CettaEvalResources *resources = eval_resources_exchange(fixture->runtime.resources);
    CettaEvalGc *collector = eval_collector_exchange(eval_resources_collector(fixture->runtime.resources));
    cetta_library_context_set_exec_path(&fixture->runtime.libraries, argv[0]);
    define(fixture, "(= (identity $x) $x)");
    number(ask(fixture, "(identity 7)"), 7);
    publication(fixture);
    escaped_answers(fixture);
    shared_dag(fixture);
    retained_frontiers(fixture);
    ordered_answers(fixture);
    wake_inputs(fixture);
    publication_failure(fixture);
    collection_publication_failure(fixture);
    wake_guard(fixture);
    observed_space_revision(fixture);
    retained_code_binder(fixture);
    match_capacity_failure(fixture);
    fresh_runtime_inputs(fixture);
    arena_free(&fixture->output); arena_free(&fixture->retained);
    cetta_runtime_session_dispose(&fixture->runtime);
    arena_free(&fixture->source);
    eval_collector_exchange(collector); eval_resources_exchange(resources);
    eval_exchange_library_context(previous_library);
    free(fixture);
    cetta_name_context_install(&previous);
    cetta_name_domain_free(&names);
    puts("PASS: tmetta full answers, escaped thunks, resumable frontiers, shared DAGs and atomic publication");
    return 0;
}
