#include "parser.h"
#include "petta_search_machine.h"
#include "library_pattern.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>

typedef struct { size_t budget; } Authority;

static Atom *parse(Arena *arena, const char *text) {
    Atom **forms = NULL;
    int n = parse_metta_text(text, arena, &forms);
    assert(n == 1 && forms);
    Atom *value = forms[0];
    free(forms);
    return value;
}

static PettaMachineHostMode classify(void *context, Space *space, Atom *call) {
    (void)context; (void)space;
    SymbolId head = atom_head_symbol_id(call);
    if (head == g_builtin_syms.pat_match_forward ||
           head == g_builtin_syms.pat_match_reverse || head == g_builtin_syms.pat_query) return PETTA_MACHINE_HOST_PATTERN_QUERY;
    return cetta_pattern_adapter_arity(head) ? PETTA_MACHINE_HOST_PATTERN_ADAPTER
        : PETTA_MACHINE_HOST_NONE;
}

static bool permit(void *context) {
    Authority *authority = context;
    if (!authority->budget) return false;
    --authority->budget;
    return true;
}

static CettaBranchAdmission admit(void *context, Space *space) {
    (void)context; (void)space;
    return (CettaBranchAdmission){.status = CETTA_BRANCH_ADMISSION_AVAILABLE,
        .capacity = CETTA_BRANCH_CAPTURE_MULTI_SHOT,
        .authority = {.words = {0x706174}, .length = 1u}};
}

static PettaMachineStep next(PettaMachine *machine, Atom **answer) {
    Bindings environment;
    PettaMachineStep step = petta_machine_next(machine, answer, &environment);
    bindings_free(&environment);
    return step;
}

static Atom *first_row(Atom *hit) {
    assert(atom_is_symbol(hit->expr.elems[0], "pat:hit"));
    Atom *rows = hit->expr.elems[1];
    assert(rows->expr.len >= 1u);
    return rows->expr.elems[0];
}

static size_t drain(PettaMachine *machine) {
    size_t n = 0u;
    Atom *answer;
    PettaMachineStep step;
    while ((step = next(machine, &answer)) == PETTA_MACHINE_STEP_ANSWER) {
        assert(atom_is_symbol(answer->expr.elems[0], "pat:hit"));
        ++n;
    }
    assert(step == PETTA_MACHINE_STEP_EXHAUSTED);
    return n;
}

static void check_collection(Space *space, Arena *persistent, Arena *arena,
    PettaMachineHost *host) {
    space_add(space, parse(persistent, "(= (spin Z $keep) $keep)"));
    space_add(space, parse(persistent,
        "(= (spin (S $n) $keep) (spin $n (spin $n $keep)))"));
    Atom *counter = atom_symbol(arena, "Z");
    Atom *successor = atom_symbol(arena, "S");
    for (unsigned n = 0; n < 12u; ++n)
        counter = atom_expr2(arena, successor, counter);
    Atom *call = parse(arena,
        "(pat:let% $keep (pat:query &self match% (p $x)) "
        "(pat:let% $tag (superpose (left right)) "
        "(pat:match% &self (q a) "
        "(pat:let% $spun (spin COUNTER $keep) $spun))))");
    Atom *query = call->expr.elems[2];
    Atom *last = call->expr.elems[3]->expr.elems[3]->expr.elems[3];
    Atom *body = last->expr.elems[2];
    last->expr.elems[2] = atom_expr3(arena, body->expr.elems[0],
        counter, body->expr.elems[2]);
    PettaMachine machine;
    assert(petta_machine_init(&machine, space, arena, call, NULL, host));
    Atom *first;
    PettaMachineStep first_step = next(&machine, &first);
    assert(first_step == PETTA_MACHINE_STEP_ANSWER);
    assert(first_row(first)->expr.elems[1]->expr.elems[3]->ground.ival == 0);
    PettaMachineStats stats;
    assert(petta_machine_stats(&machine, &stats));
    fprintf(stderr, "directional cursor collections: %llu, reclaimed: %llu\n",
        (unsigned long long)stats.choice_nursery_evacuations,
        (unsigned long long)stats.choice_nursery_bytes_reclaimed);
    assert(stats.choice_nursery_evacuations > 0u);
    assert(stats.choice_nursery_bytes_reclaimed > 0u);
    CettaOwnedContinuation saved;
    cetta_owned_continuation_init(&saved);
    CettaContinuationMachine owner = petta_machine_continuation_machine(&machine);
    assert(cetta_continuation_capture(owner, &saved) == CETTA_CONTINUATION_READY);
    assert(drain(&machine) == 5u);
    assert(cetta_continuation_restore(owner, &saved) == CETTA_CONTINUATION_READY);
    assert(drain(&machine) == 5u);
    petta_machine_destroy(&machine);
    cetta_owned_continuation_destroy(&saved);
    Atom *pattern = query->expr.elems[3];
    Atom *image = bindings_apply_saved(arena, first->expr.elems[2], pattern);
    assert(atom_eq(image, parse(arena, "(p a)")));
}

static void check_control_continuations(Space *space, Arena *arena,
    PettaMachineHost *host, Authority *authority) {
    Atom *call = parse(arena,
        "(pat:let% $x (superpose (a b)) (pat:let% $y $x (pair $x $y)))");
    Atom *expected[] = {parse(arena, "(pair a a)"), parse(arena, "(pair b b)")};
    unsigned captures = 0u;
    for (size_t budget = 1u; budget < 36u; ++budget) {
        authority->budget = budget;
        PettaMachine machine;
        assert(petta_machine_init(&machine, space, arena, call, NULL, host));
        Atom *answer;
        size_t published = 0u;
        PettaMachineStep step;
        while ((step = next(&machine, &answer)) == PETTA_MACHINE_STEP_ANSWER) {
            assert(published < 2u && atom_eq(answer, expected[published++]));
        }
        if (step == PETTA_MACHINE_STEP_SUSPENDED) {
            CettaContinuationMachine owner = petta_machine_continuation_machine(&machine);
            CettaOwnedContinuation saved;
            cetta_owned_continuation_init(&saved);
            assert(cetta_continuation_capture(owner, &saved) == CETTA_CONTINUATION_READY);
            for (unsigned resume = 0u; resume < 2u; ++resume) {
                authority->budget = SIZE_MAX;
                size_t count = published;
                while ((step = next(&machine, &answer)) == PETTA_MACHINE_STEP_ANSWER)
                    assert(count < 2u && atom_eq(answer, expected[count++]));
                assert(step == PETTA_MACHINE_STEP_EXHAUSTED && count == 2u);
                if (!resume)
                    assert(cetta_continuation_restore(owner, &saved) == CETTA_CONTINUATION_READY);
            }
            cetta_owned_continuation_destroy(&saved);
            ++captures;
        } else assert(step == PETTA_MACHINE_STEP_EXHAUSTED && published == 2u);
        petta_machine_destroy(&machine);
    }
    assert(captures >= 4u);

    call = parse(arena, "(pat:unify% ($x $u) ($u a) bad (empty))");
    for (size_t budget = 1u; budget < 16u; ++budget) {
        authority->budget = budget;
        PettaMachine machine;
        assert(petta_machine_init(&machine, space, arena, call, NULL, host));
        Atom *answer;
        PettaMachineStep step = next(&machine, &answer);
        assert(step != PETTA_MACHINE_STEP_ANSWER);
        if (step == PETTA_MACHINE_STEP_SUSPENDED) {
            CettaContinuationMachine owner = petta_machine_continuation_machine(&machine);
            CettaOwnedContinuation saved;
            cetta_owned_continuation_init(&saved);
            assert(cetta_continuation_capture(owner, &saved) == CETTA_CONTINUATION_READY);
            authority->budget = SIZE_MAX;
            assert(next(&machine, &answer) == PETTA_MACHINE_STEP_EXHAUSTED);
            assert(cetta_continuation_restore(owner, &saved) == CETTA_CONTINUATION_READY);
            assert(next(&machine, &answer) == PETTA_MACHINE_STEP_EXHAUSTED);
            cetta_owned_continuation_destroy(&saved);
        } else assert(step == PETTA_MACHINE_STEP_EXHAUSTED);
        petta_machine_destroy(&machine);
    }
    authority->budget = SIZE_MAX;
}

static void check_template_continuations(Space *space, Arena *arena,
    PettaMachineHost *host, Authority *authority) {
    const char *calls[] = {
        "(pat:match% &self (, (p $x) (q $x)) (answer $x))",
        "(pat:%match &self (p a) $stored $stored)"};
    for (unsigned mode = 0u; mode < 2u; ++mode) {
        Atom *call = parse(arena, calls[mode]);
        Atom *expected[] = {parse(arena, mode ? "(p a)" : "(answer a)"),
            parse(arena, mode ? "(p a)" : "(answer a)"), parse(arena, "(p $u)")};
        size_t total = mode ? 3u : 2u;
        unsigned captures = 0u;
        for (size_t budget = 1u; budget < 45u; ++budget) {
            authority->budget = budget;
            PettaMachine machine;
            assert(petta_machine_init(&machine, space, arena, call, NULL, host));
            Atom *answer;
            size_t published = 0u;
            PettaMachineStep step;
            while ((step = next(&machine, &answer)) == PETTA_MACHINE_STEP_ANSWER) {
                CettaTermMatch images;
                assert(published < total && term_graph_match_many(arena,
                    &(CettaTermMatchPair){answer, expected[published++]}, 1u,
                    CETTA_TERM_MATCH_VARIANT, atom_eq, &images) == CETTA_TERM_MATCH_OK);
            }
            if (step == PETTA_MACHINE_STEP_SUSPENDED) {
                CettaContinuationMachine owner = petta_machine_continuation_machine(&machine);
                CettaOwnedContinuation saved;
                cetta_owned_continuation_init(&saved);
                assert(cetta_continuation_capture(owner, &saved) == CETTA_CONTINUATION_READY);
                for (unsigned replay = 0u; replay < 2u; ++replay) {
                    authority->budget = SIZE_MAX;
                    size_t count = published;
                    while ((step = next(&machine, &answer)) == PETTA_MACHINE_STEP_ANSWER) {
                        CettaTermMatch images;
                        assert(count < total && term_graph_match_many(arena,
                            &(CettaTermMatchPair){answer, expected[count++]}, 1u,
                            CETTA_TERM_MATCH_VARIANT, atom_eq, &images) == CETTA_TERM_MATCH_OK);
                    }
                    assert(step == PETTA_MACHINE_STEP_EXHAUSTED && count == total);
                    if (!replay)
                        assert(cetta_continuation_restore(owner, &saved) == CETTA_CONTINUATION_READY);
                }
                cetta_owned_continuation_destroy(&saved);
                ++captures;
            } else assert(step == PETTA_MACHINE_STEP_EXHAUSTED && published == total);
            petta_machine_destroy(&machine);
        }
        assert(captures >= 4u);
    }
    authority->budget = SIZE_MAX;
}

int main(void) {
    SymbolTable symbols;
    symbol_table_init(&symbols);
    symbol_table_init_builtins(&symbols, &g_builtin_syms);
    g_symbols = &symbols;
    Arena persistent, answers;
    arena_init_detached(&persistent);
    arena_init_detached(&answers);
    TermUniverse universe;
    term_universe_init(&universe);
    term_universe_set_persistent_arena(&universe, &persistent);
    Space space;
    space_init_with_universe(&space, &universe);
    space_add(&space, parse(&persistent, "(p a)"));
    space_add(&space, parse(&persistent, "(p a)"));
    space_add(&space, parse(&persistent, "(q a)"));
    space_add(&space, parse(&persistent, "(p $u)"));
    space_add(&space, parse(&persistent, "(q b)"));
    Authority authority = {.budget = SIZE_MAX};
    PettaMachineHost host = {.context = &authority, .classify = classify,
        .permit_transition = permit, .admit_branch_capture = admit,
        .retain_query_variable_identity = true};
    check_control_continuations(&space, &answers, &host, &authority);
    check_template_continuations(&space, &answers, &host, &authority);
    Atom *query = parse(&answers, "(pat:query &self match% (, (p $x) (q $x)))");
    PettaMachine machine;
    assert(petta_machine_init(&machine, &space, &answers, query, NULL, &host));
    Atom *first, *second;
    assert(next(&machine, &first) == PETTA_MACHINE_STEP_ANSWER);
    Atom *first_id = first_row(first)->expr.elems[1];
    assert(first_id->expr.elems[3]->ground.ival == 0);
    CettaOwnedContinuation left, right;
    cetta_owned_continuation_init(&left);
    cetta_owned_continuation_init(&right);
    CettaContinuationMachine owner = petta_machine_continuation_machine(&machine);
    CettaContinuationStatus captured = cetta_continuation_capture(owner, &left);
    if (captured != CETTA_CONTINUATION_READY)
        fprintf(stderr, "directional capture status: %u\n", (unsigned)captured);
    assert(captured == CETTA_CONTINUATION_READY);
    assert(cetta_continuation_capture(owner, &right) == CETTA_CONTINUATION_READY);
    assert(next(&machine, &second) == PETTA_MACHINE_STEP_ANSWER);
    assert(first_row(second)->expr.elems[1]->expr.elems[3]->ground.ival == 1);
    assert(drain(&machine) == 0u);
    assert(cetta_continuation_restore(owner, &left) == CETTA_CONTINUATION_READY);
    assert(drain(&machine) == 1u);
    assert(cetta_continuation_restore(owner, &right) == CETTA_CONTINUATION_READY);
    assert(drain(&machine) == 1u);
    petta_machine_destroy(&machine);
    /* Returned witnesses and rows outlive the producer and both saved images. */
    cetta_owned_continuation_destroy(&left);
    cetta_owned_continuation_destroy(&right);
    Atom *pattern = query->expr.elems[3]->expr.elems[1];
    Atom *instantiated = bindings_apply_saved(&answers, first->expr.elems[2], pattern);
    assert(atom_eq(instantiated, parse(&answers, "(p a)")));

    /* Capture at each transition budget, including a retained partial join.
     * Cloning does not turn a previously captured subject hole into a hole
     * which a later premise may fill. */
    unsigned captures = 0u;
    for (size_t budget = 1u; budget <= 24u; ++budget) {
        authority.budget = budget;
        assert(petta_machine_init(&machine, &space, &answers, query, NULL, &host));
        Atom *answer;
        PettaMachineStep step = next(&machine, &answer);
        if (step == PETTA_MACHINE_STEP_SUSPENDED) {
            CettaOwnedContinuation saved;
            cetta_owned_continuation_init(&saved);
            owner = petta_machine_continuation_machine(&machine);
            assert(cetta_continuation_capture(owner, &saved) == CETTA_CONTINUATION_READY);
            authority.budget = SIZE_MAX;
            assert(drain(&machine) == 2u);
            assert(cetta_continuation_restore(owner, &saved) == CETTA_CONTINUATION_READY);
            assert(drain(&machine) == 2u);
            cetta_owned_continuation_destroy(&saved);
            ++captures;
        } else {
            assert(step == PETTA_MACHINE_STEP_ANSWER);
        }
        petta_machine_destroy(&machine);
    }
    assert(captures >= 4u);

    /* An active occurrence choice keeps the rows present when it started. */
    authority.budget = SIZE_MAX;
    check_collection(&space, &persistent, &answers, &host);
    Atom *single = parse(&answers, "(pat:query &self match% (p a))");
    assert(petta_machine_init(&machine, &space, &answers, single, NULL, &host));
    assert(next(&machine, &first) == PETTA_MACHINE_STEP_ANSWER);
    assert(space_remove(&space, parse(&persistent, "(p a)")));
    space_add(&space, parse(&persistent, "(p a)"));
    space_add(&space, parse(&persistent, "(p a)"));
    assert(drain(&machine) == 1u);
    petta_machine_destroy(&machine);
    assert(petta_machine_init(&machine, &space, &answers, single, NULL, &host));
    assert(drain(&machine) == 3u);
    petta_machine_destroy(&machine);

    space_free(&space);
    term_universe_free(&universe);
    arena_free(&answers);
    arena_free(&persistent);
    symbol_table_free(&symbols);
    puts("PASS: directional joins retain occurrence pins and protected witnesses across owned resumption");
    return 0;
}
