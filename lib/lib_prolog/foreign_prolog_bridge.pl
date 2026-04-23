:- set_prolog_flag(double_quotes, string).

cetta_foreign_bridge_main :-
    loop.

loop :-
    read_term(user_input, Command, []),
    ( Command == end_of_file ->
        true
    ; Command == shutdown ->
        true
    ; handle(Command, Reply),
      write_term(user_output, Reply,
                 [quoted(true), ignore_ops(true)]),
      write(user_output, '.'),
      nl(user_output),
      flush_output(user_output),
      loop
    ).

handle(shutdown, ok).

handle(consult_file(Path), Reply) :-
    catch((consult(Path), Reply = ok),
          Error,
          error_reply(Error, Reply)).

handle(use_module_spec(SpecText), Reply) :-
    catch(( parse_use_module_spec(SpecText, Spec),
            use_module(Spec),
            Reply = ok ),
          Error,
          error_reply(Error, Reply)).

handle(call_registered(Name, EncArgs), Reply) :-
    catch(( atom_string(NameAtom, Name),
            maplist(cetta_decode, EncArgs, Args),
            append(Args, [Out], GoalArgs),
            Goal =.. [NameAtom|GoalArgs],
            findall(Text,
                    ( call(Goal),
                      cetta_term_to_metta_text(Out, Text)
                    ),
                    Texts),
            Reply = results(Texts) ),
          Error,
          error_reply(Error, Reply)).

handle(call_goal(EncGoal), Reply) :-
    catch(( cetta_decode(EncGoal, GoalData),
            cetta_goal_term(GoalData, Goal),
            ( call(Goal) -> Reply = bool(true)
            ; Reply = bool(false) ) ),
          Error,
          error_reply(Error, Reply)).

handle(assertz_goal(EncGoal), Reply) :-
    catch(( cetta_decode(EncGoal, GoalData),
            cetta_goal_term(GoalData, Goal),
            assertz(Goal),
            Reply = bool(true) ),
          Error,
          error_reply(Error, Reply)).

handle(asserta_goal(EncGoal), Reply) :-
    catch(( cetta_decode(EncGoal, GoalData),
            cetta_goal_term(GoalData, Goal),
            asserta(Goal),
            Reply = bool(true) ),
          Error,
          error_reply(Error, Reply)).

handle(retract_goal(EncGoal), Reply) :-
    catch(( cetta_decode(EncGoal, GoalData),
            cetta_goal_term(GoalData, Goal),
            ( retract(Goal) -> Reply = bool(true)
            ; Reply = bool(false) ) ),
          Error,
          error_reply(Error, Reply)).

handle(_, error("unsupported prolog bridge command")).

error_reply(Error, error(Text)) :-
    message_to_string(Error, Text).

parse_use_module_spec(SpecText, Spec) :-
    ( catch(term_string(Spec0, SpecText), _, fail) ->
        Spec = Spec0
    ; Spec = SpecText
    ).

cetta_decode(int(I), I).
cetta_decode(float(F), F).
cetta_decode(str(S), S).
cetta_decode(sym(S), Atom) :- atom_string(Atom, S).
cetta_decode(bool(true), true).
cetta_decode(bool(false), false).
cetta_decode(var(_Name), _Var).
cetta_decode(expr(Items), Terms) :-
    maplist(cetta_decode, Items, Terms).

cetta_goal_term([Head|Args], Goal) :-
    atom(Head),
    !,
    Goal =.. [Head|Args].
cetta_goal_term(Goal, Goal).

cetta_term_to_metta_text(Term, Text) :-
    var(Term),
    !,
    Text = "$_prolog_var".
cetta_term_to_metta_text(Term, Text) :-
    string(Term),
    !,
    term_string(Term, Text, [quoted(true)]).
cetta_term_to_metta_text(Term, Text) :-
    number(Term),
    !,
    term_string(Term, Text).
cetta_term_to_metta_text(true, "True") :- !.
cetta_term_to_metta_text(false, "False") :- !.
cetta_term_to_metta_text([], "()") :- !.
cetta_term_to_metta_text(List, Text) :-
    is_list(List),
    !,
    maplist(cetta_term_to_metta_text, List, Pieces),
    atomic_list_concat(Pieces, ' ', Inner),
    format(string(Text), "(~w)", [Inner]).
cetta_term_to_metta_text(Term, Text) :-
    compound(Term),
    !,
    Term =.. [Head|Args],
    cetta_term_to_metta_text(Head, HeadText),
    maplist(cetta_term_to_metta_text, Args, Pieces),
    append([HeadText], Pieces, All),
    atomic_list_concat(All, ' ', Inner),
    format(string(Text), "(~w)", [Inner]).
cetta_term_to_metta_text(Term, Text) :-
    atom_string(Term, Text).
