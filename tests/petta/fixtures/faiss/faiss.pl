:- dynamic fixture_faiss_next_id/1.
:- dynamic fixture_faiss_entry/4.

fixture_faiss_next_id(1).

faiss_create(Dimension, fixture_faiss(Index)) :-
    retract(fixture_faiss_next_id(Index)),
    Next is Index + 1,
    assertz(fixture_faiss_next_id(Next)),
    integer(Dimension),
    Dimension > 0.

faiss_add(Index, Pairs, true) :-
    forall(
        member([Atom, Vector], Pairs),
        fixture_faiss_insert(Index, Atom, Vector)
    ).

fixture_faiss_insert(Index, Atom, Vector) :-
    findall(Order, fixture_faiss_entry(Index, Order, _, _), Orders),
    length(Orders, Order),
    assertz(fixture_faiss_entry(Index, Order, Atom, Vector)).

faiss_search(Index, Query, Count, Results) :-
    findall(
        Key-[Atom, Distance],
        (
            fixture_faiss_entry(Index, Order, Atom, Vector),
            fixture_faiss_distance(Query, Vector, Distance),
            Key = Distance-Order
        ),
        Scored
    ),
    keysort(Scored, Sorted),
    fixture_faiss_take(Count, Sorted, Results).

faiss_remove(Index, Atom, true) :-
    retractall(fixture_faiss_entry(Index, _, Atom, _)).

fixture_faiss_distance(Left, Right, Distance) :-
    fixture_numeric_vector(Left),
    fixture_numeric_vector(Right),
    !,
    fixture_squared_distance(Left, Right, 0.0, Distance).
fixture_faiss_distance(Left, Right, Distance) :-
    fixture_tokens(Left, LeftTokens0),
    fixture_tokens(Right, RightTokens0),
    sort(LeftTokens0, LeftTokens),
    sort(RightTokens0, RightTokens),
    intersection(LeftTokens, RightTokens, Shared),
    length(Shared, SharedCount),
    Distance is -SharedCount.

fixture_numeric_vector([]).
fixture_numeric_vector([Head|Tail]) :-
    number(Head),
    fixture_numeric_vector(Tail).

fixture_squared_distance([], [], Distance, Distance).
fixture_squared_distance(
    [Left|LeftTail],
    [Right|RightTail],
    Accumulator,
    Distance
) :-
    Delta is Left - Right,
    Next is Accumulator + Delta * Delta,
    fixture_squared_distance(
        LeftTail, RightTail, Next, Distance
    ).

fixture_tokens(Term, [Term]) :-
    atomic(Term),
    !.
fixture_tokens([], []) :-
    !.
fixture_tokens([Head|Tail], Tokens) :-
    !,
    fixture_tokens(Head, HeadTokens),
    fixture_tokens(Tail, TailTokens),
    append(HeadTokens, TailTokens, Tokens).
fixture_tokens(Term, Tokens) :-
    Term =.. [_|Arguments],
    fixture_tokens(Arguments, Tokens).

fixture_faiss_take(Count, _, []) :-
    Count =< 0,
    !.
fixture_faiss_take(_, [], []) :-
    !.
fixture_faiss_take(
    Count,
    [_-[Atom, Distance]|Tail],
    [[Atom, Distance]|Results]
) :-
    Next is Count - 1,
    fixture_faiss_take(Next, Tail, Results).
