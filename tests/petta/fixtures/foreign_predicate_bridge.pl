cetta_foreign_add_one(Input, Output) :-
    Output is Input + 1.

cetta_foreign_compound_result(fixture_handle(7)).

cetta_foreign_echo(Value, Value).

cetta_foreign_term_kind(Value, list) :-
    is_list(Value),
    !.
cetta_foreign_term_kind(Value, compound) :-
    compound(Value),
    !.
cetta_foreign_term_kind(_, atomic).
