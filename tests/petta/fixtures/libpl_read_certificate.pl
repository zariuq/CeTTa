cetta_decode_nested_list(N, Term) :-
    integer(N), N >= 0,
    cetta_decode_nested_go(N, leaf, Term).
cetta_decode_nested_go(0, Term, Term).
cetta_decode_nested_go(N, Inner, Term) :-
    N > 0, Previous is N - 1,
    cetta_decode_nested_go(Previous, [Inner], Term).

cetta_decode_graph_list([Cycle]) :- Cycle = f(Cycle).
cetta_decode_open_list([a|_]).
cetta_decode_alias_pair([X,X]).
