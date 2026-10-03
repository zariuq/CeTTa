cold_watch(X, true) :- freeze(X, writeln(cold_woke)).
cold_dif(X, true) :- dif(X, 1).
cold_mixed_answer(X, first) :- X = 1.
cold_mixed_answer(X, second) :- dif(X, 1), freeze(X, writeln(second_woke)).
cold_immediate_raise(_, _) :- throw(error(cold_immediate_raise, control)).
