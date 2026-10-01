/* Prolog fixture for tests/petta/libpl_query_release.metta.
 *
 * Shapes the lifecycle of setup_call_cleanup:
 *  - qrel_gen/2: two-answer generator; its cleanup honours the mode.
 *  - qrel_oneblow/1: single answer whose teardown cleanup can throw.
 *  - qrel_throw_body_only/0: body raises under a quiet cleanup.
 *  - qrel_throw_both/0: body raises, and the teardown cleanup also raises.
 *
 * Distinct functors let MeTTa tell body faults from cleanup faults by
 * value, never by display text. */
cleanup_action(quiet) :- true.
cleanup_action(throw) :- throw(error(qrel_cleanup_fault, context(qrel_fixture, 2))).

qrel_gen(Mode, X) :-
    setup_call_cleanup(true,
                       (X = one ; X = two),
                       cleanup_action(Mode)).

qrel_oneblow(Mode) :-
    setup_call_cleanup(true,
                       true,
                       cleanup_action(Mode)).

qrel_throw_body_only :-
    setup_call_cleanup(true,
                       throw(error(qrel_body_fault, context(qrel_fixture, 1))),
                       cleanup_action(quiet)).

qrel_throw_both :-
    setup_call_cleanup(true,
                       throw(error(qrel_body_fault, context(qrel_fixture, 1))),
                       cleanup_action(throw)).

/* One-argument raisers, importable through the Prolog-predicate import
 * convention (one MeTTa input); the boundary transports their faults. */
qrel_raise_body(_) :-
    throw(error(qrel_body_fault, context(qrel_fixture, 1))).

qrel_raise_both(_) :-
    setup_call_cleanup(true,
                       throw(error(qrel_body_fault, context(qrel_fixture, 1))),
                       cleanup_action(throw)).
