:- table tabled_boundary_reach/2.
tabled_boundary_edge(a,b).
tabled_boundary_edge(b,c).
tabled_boundary_edge(c,b).
tabled_boundary_edge(c,d).
tabled_boundary_reach(X,Y) :- tabled_boundary_edge(X,Y).
tabled_boundary_reach(X,Y) :-
    tabled_boundary_reach(X,Z), tabled_boundary_edge(Z,Y).
:- table tabled_boundary_number/1.
tabled_boundary_number(1).
tabled_boundary_number(2).
tabled_boundary_number(2).
tabled_boundary_number(3).
:- table tabled_boundary_error/1.
tabled_boundary_error(_) :- throw(tabled_boundary_fault).
