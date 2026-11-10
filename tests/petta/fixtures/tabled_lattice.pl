% Shortest and longest weighted paths by SWI's mode-directed tabling: the
% reference for tabled-lattice.
:- dynamic lattice_arc/4.
:- table lattice_short(_,_,_,min).
lattice_short(G,X,Y,W) :- lattice_arc(G,X,Y,W).
lattice_short(G,X,Y,D) :- lattice_arc(G,X,Z,W), lattice_short(G,Z,Y,D1), D is W+D1.
:- dynamic lattice_dag_arc/4.
:- table lattice_long(_,_,_,max).
lattice_long(G,X,Y,W) :- lattice_dag_arc(G,X,Y,W).
lattice_long(G,X,Y,D) :- lattice_dag_arc(G,X,Z,W), lattice_long(G,Z,Y,D1), D is W+D1.
% the nodes each node reaches, joined by ordered-set union
:- table lattice_reach(_,_,lattice(lattice_union/3)).
lattice_reach(G,X,[Y]) :- lattice_arc(G,X,Y,_).
lattice_reach(G,X,S) :- lattice_arc(G,X,Y,_), lattice_reach(G,Y,S).
lattice_union(A,B,C) :- ord_union(A,B,C).
