:- module(relational_cfg_eval,
          [ cfg_pack_from_answers/2,
            cfg_parse_results/6
          ]).

:- use_module(finite_horn_eval).
:- use_module(library(assoc)).

/*
  Reference evaluator for the relational-CFG SIR fragment.

  A production sequences calls, records each child value in the quoted
  variable named by the call, and instantiates the production action from
  that environment.  A call to another compiled state recurses through the
  pack.  Every other ground grammar term is interpreted by the admitted
  source syntax relation.  This keeps the evaluator generic while the
  structural GLL/GLR adapters remain a separate layer.
*/

cfg_pack_from_answers(Answers, cfg_pack(Productions)) :-
    maplist(answer_production, Answers, Productions0),
    sort(Productions0, Productions).

answer_production(
    answer(
        list([sym('compile-cfg-production'), _,
              list([sym('sir-source-production'),
                    list([sym('q-sym'), sym(Presentation)]),
                    list([sym('sir-production'), sym(Rule), sym(State),
                          Items, Action])])]),
        _),
    production(Presentation, Rule, State, Items, Action)).

cfg_parse_results(Pack, SourcePresentations, State, Input, MaxDepth,
                  Outcome) :-
    must_be(atom, State),
    must_be(integer, MaxDepth),
    MaxDepth > 0,
    ground(Input),
    catch(
        findall(result(Value, Rest),
                cfg_parse_state(Pack, SourcePresentations, State, Input,
                                Value, Rest, MaxDepth),
                RawResults),
        cfg_depth_exhausted,
        RawResults = cfg_depth_exhausted),
    ( RawResults == cfg_depth_exhausted ->
        Outcome = resource_exhausted(depth)
    ; sort(RawResults, Results),
      Outcome = completed(Results)
    ).

cfg_parse_state(_, _, _, _, _, _, 0) :-
    throw(cfg_depth_exhausted).
cfg_parse_state(Pack, SourcePresentations, State, Input, Value, Rest,
                Depth) :-
    Pack = cfg_pack(Productions),
    member(production(_, _, State, Items, Action), Productions),
    empty_assoc(EmptyEnvironment),
    NextDepth is Depth - 1,
    cfg_parse_items(Pack, SourcePresentations, Items, Input, Rest,
                    EmptyEnvironment, Environment, NextDepth),
    quoted_value(Action, Environment, Value).

cfg_parse_items(_, _, sym('sir-items-nil'), Input, Input,
                Environment, Environment, _).
cfg_parse_items(
    Pack, SourcePresentations,
    list([sym('sir-items-cons'),
          list([sym('sir-call'), Grammar, OutputVariable]), MoreItems]),
    Input, Rest, Environment0, Environment, Depth) :-
    cfg_parse_call(Pack, SourcePresentations, Grammar, Input,
                   ChildValue, Middle, Depth),
    bind_output_variable(OutputVariable, ChildValue,
                         Environment0, Environment1),
    cfg_parse_items(Pack, SourcePresentations, MoreItems, Middle, Rest,
                    Environment1, Environment, Depth).

cfg_parse_call(Pack, SourcePresentations,
               list([sym('q-sym'), sym(State)]), Input, Value, Rest,
               Depth) :-
    cfg_pack_has_state(Pack, State),
    !,
    cfg_parse_state(Pack, SourcePresentations, State, Input, Value, Rest,
                    Depth).
cfg_parse_call(_, SourcePresentations, QuotedGrammar, Input, Value, Rest,
               Depth) :-
    empty_assoc(EmptyEnvironment),
    quoted_value(QuotedGrammar, EmptyEnvironment, Grammar),
    Query = list([sym(parse), Grammar, Input, var(cfg_value), var(cfg_rest)]),
    horn_query(SourcePresentations, Query, Depth, HornOutcome),
    ( HornOutcome = completed(Answers) ->
        member(answer(Answer, _), Answers),
        Answer = list([sym(parse), Grammar, Input, Value, Rest])
    ; HornOutcome == resource_exhausted(depth) ->
        throw(cfg_depth_exhausted)
    ).

cfg_pack_has_state(cfg_pack(Productions), State) :-
    memberchk(production(_, _, State, _, _), Productions).

bind_output_variable(list([sym('q-var'), Index]), Value,
                     Environment0, Environment) :-
    ( get_assoc(Index, Environment0, _) ->
        throw(error(invalid_cfg_pack(duplicate_output_variable(Index)),
                    relational_cfg_eval))
    ; put_assoc(Index, Environment0, Value, Environment)
    ).

quoted_value(list([sym('q-sym'), sym(Name)]), _, sym(Name)) :-
    !.
quoted_value(list([sym('q-int'), int(Value)]), _, int(Value)) :-
    !.
quoted_value(list([sym('q-str'), str(Text)]), _, str(Text)) :-
    !.
quoted_value(list([sym('q-var'), Index]), Environment, Value) :-
    !,
    ( get_assoc(Index, Environment, Value) -> true
    ; throw(error(invalid_cfg_pack(unbound_action_variable(Index)),
                  relational_cfg_eval))
    ).
quoted_value(
    list([sym('q-app'), list([sym('q-sym'), sym(Name)]), Arguments]),
    Environment, list([sym(Name)|Values])) :-
    !,
    quoted_values(Arguments, Environment, Values).
quoted_value(Term, _, _) :-
    throw(error(invalid_cfg_pack(malformed_quoted_term(Term)),
                relational_cfg_eval)).

quoted_values(sym('q-nil'), _, []).
quoted_values(list([sym('q-cons'), Head, Tail]), Environment,
              [Value|Values]) :-
    quoted_value(Head, Environment, Value),
    quoted_values(Tail, Environment, Values).
