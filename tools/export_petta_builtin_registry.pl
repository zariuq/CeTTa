:- use_module(library(http/json)).
:- initialization(main, main).

encoded(Term, Json) :-
    ( Term = '$VAR'(Index) -> Json = _{variable:Index}
    ; atomic(Term) -> Json = Term
    ; is_list(Term) -> maplist(encoded, Term, Json)
    ; Term =.. [Functor|Arguments],
      maplist(encoded, Arguments, Encoded),
      Json = _{constructor:Functor, arguments:Encoded}
    ).

row(Json) :-
    builtin_registry:builtin_spec(Name/Arity, Implementation, Typing,
                                  Evaluation, Cardinality, Lowering),
    copy_term([Typing,Cardinality], [T,C]),
    numbervars([T,C], 0, _),
    encoded(T, TJ), encoded(C, CJ),
    encoded(Implementation, IJ), encoded(Evaluation, EJ), encoded(Lowering, LJ),
    Json = _{name:Name, arity:Arity, implementation:IJ, typing:TJ,
             evaluation:EJ, cardinality:CJ, lowering:LJ}.

main([Registry]) :-
    use_module(Registry),
    findall(Row, row(Row), Rows),
    json_write_dict(current_output, Rows, [width(0)]), nl.
