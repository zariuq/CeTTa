% Terms built on the Prolog side, sharing subterms or holding themselves,
% compared by PeTTa's ==/3.  Forty levels of sharing unfold to 2^40 leaves,
% and the last two terms share their loop nodes at alternating depths.
dag(0, Leaf, Leaf) :- !.
dag(N, Leaf, node(T,T)) :- M is N-1, dag(M, Leaf, T).

graphs(Result) :-
    dag(40, 1, A), dag(40, 1.0, B), '=='(A, B, Kinds),
    dag(40, 1, C), dag(40, 2, D), '=='(C, D, Values),
    N is nan, dag(40, N, E), '=='(E, E, SharedNaN),
    X = f(1, X), Y = f(1.0, f(1, Y)), '=='(X, Y, Periods),
    P = f(P, 1), Q = f(Z, 1), Z = f(Q, 2), '=='(P, Q, Pairs),
    U = f(g(U)), V = f(W), W = g(f(W)), '=='(U, V, Phases),
    Result = graphs(Kinds, Values, SharedNaN, Periods, Pairs, Phases).
