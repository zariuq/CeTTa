libpl_phase_value(called).
libpl_opaque_empty([]).
libpl_opaque_size(List, Size) :- length(List, Size).
libpl_opaque_head([Head|_], Head).
libpl_opaque_tail([_|Tail], Tail).
libpl_opaque_cons(Head, Tail, [Head|Tail]).
