:- dynamic prolog_probe_dyn/1.

prolog_probe_add1(X, Y) :-
    Y is X + 1.

prolog_probe_echo(X, X).

prolog_probe_fact(alpha).

prolog_probe_pair(A, B, [A, B]).

prolog_probe_compound(wrapper(alpha, 7)).

prolog_probe_choice(red).
prolog_probe_choice(blue).
