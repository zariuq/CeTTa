:- module(foreign_region_fixture,
          [domain/1, original/1, bind/2, post/2, alias/2, cyclic/1,
           watch/1, event_count/1, choices/1, body_fault/1, release_fault/1]).
:- use_module(library(clpfd)).
:- dynamic fired/1.

domain(X) :- X in 1..9.
original(X) :- var(X), fd_dom(X, 1..9).
bind(X, N) :- X = N.
post(X, Y) :- X #= Y + 1.
alias(X, Y) :- X = Y.
cyclic(X) :- X = f(X).
watch(X) :- freeze(X, assertz(fired(X))).
event_count(N) :- aggregate_all(count, fired(_), N).
choices(X) :- between(1, 3, X).
body_fault(X) :- X = 4, throw(body_failure).
release_fault(X) :-
    setup_call_cleanup(true, between(1, 3, X), throw(release_failure)).
