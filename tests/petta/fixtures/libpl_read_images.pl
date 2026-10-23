:- dynamic cetta_image_fact/1.
cetta_image_ref(R) :- assertz(cetta_image_fact(alive), R).
cetta_image_one([R]) :- cetta_image_ref(R).
cetta_image_alias([R,R]) :- cetta_image_ref(R).
cetta_image_distinct([R,S]) :- cetta_image_ref(R), cetta_image_ref(S).
cetta_image_nested([[R],[R]]) :- cetta_image_ref(R).
cetta_image_open([R,X,X]) :- cetta_image_ref(R).
cetta_image_cycle(Cycle) :- cetta_image_ref(R), Cycle=[R|Cycle].
cetta_image_answers([R]) :- between(1,3,_), cetta_image_ref(R).
cetta_image_data([X,X]).
cetta_image_constrained([X,X]) :- dif(X, blocked).
cetta_image_distinct_data(Input, [result_datum]) :- Input=[input_datum].
