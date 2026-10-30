% Small finite positive-reachability oracle, independent of the MeTTa driver.
:- dynamic edge/2.
:- table reach/2.

reach(X, Y) :- edge(X, Y).
reach(X, Z) :- edge(X, Y), reach(Y, Z).
install([X,Y]) :- assertz(edge(X,Y)).
answer(Start) :-
    findall(Target, reach(Start, Target), Bag),
    sort(Bag, Endpoints),
    write_canonical(Endpoints), nl.
main :-
    read_term(user_input, graph(Edges, Starts), []),
    maplist(install, Edges),
    maplist(answer, Starts).
