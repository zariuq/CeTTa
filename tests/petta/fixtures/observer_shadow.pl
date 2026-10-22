:- redefine_system_predicate(cyclic_term(_)).
cyclic_term(_) :- assertz(observer_seen), fail.
