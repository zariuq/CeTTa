:- module(finite_horn_eval,
          [ horn_query/4,
            horn_query_checked/4,
            horn_replay/3,
            horn_replay_answers/2
          ]).

:- use_module(library(assoc)).
:- use_module(library(pairs)).

:- use_module(finite_horn_gslt_v1, [render_term/2]).

/*
  Bounded proof interpreter for admitted finite Horn presentations.

  This is the relational reference used to execute the structural compiler.
  It uses occurs-safe unification and emits a rule tree which is replayed by a
  separate checker below.  It is deliberately not a chart parser: recursive
  source parsing belongs to the independent tabled GLL and GLR backends.
*/

horn_query(Presentations, QueryNode, MaxDepth, Outcome) :-
    must_be(integer, MaxDepth),
    MaxDepth > 0,
    presentations_program(Presentations, Program),
    node_logic(QueryNode, Query),
    catch(
        findall(Key-answer(AnswerNode, ProofNode),
                ( solve(Program, Query, Proof, MaxDepth),
                  ground(Query),
                  logic_node(Query, AnswerNode),
                  proof_node(Proof, ProofNode),
                  render_term(AnswerNode, AnswerText),
                  render_term(ProofNode, ProofText),
                  atomics_to_string([AnswerText, "\u0000", ProofText], Key)
                ),
                KeyedAnswers),
        depth_exhausted,
        KeyedAnswers = depth_exhausted),
    ( KeyedAnswers == depth_exhausted ->
        Outcome = resource_exhausted(depth)
    ; sort(KeyedAnswers, UniqueKeyed),
      pairs_values(UniqueKeyed, Answers),
      Outcome = completed(Answers)
    ).

/*
  Replay each generated certificate before retaining its answer.  This keeps
  complete answer-set oracles bounded by answer size rather than proof size.
*/
horn_query_checked(Presentations, QueryNode, MaxDepth, Outcome) :-
    must_be(integer, MaxDepth),
    MaxDepth > 0,
    presentations_program(Presentations, Program),
    node_logic(QueryNode, Query),
    catch(
        findall(Key-AnswerNode,
                ( solve(Program, Query, Proof, MaxDepth),
                  ground(Query),
                  logic_node(Query, AnswerNode),
                  proof_node(Proof, ProofNode),
                  once(replay_answer(
                      Program, answer(AnswerNode, ProofNode))),
                  render_term(AnswerNode, Key)
                ),
                KeyedAnswers),
        depth_exhausted,
        KeyedAnswers = depth_exhausted),
    ( KeyedAnswers == depth_exhausted ->
        Outcome = resource_exhausted(depth)
    ; sort(KeyedAnswers, UniqueKeyed),
      pairs_values(UniqueKeyed, Answers),
      Outcome = completed(Answers)
    ).

horn_replay(Presentations, AnswerNode, ProofNode) :-
    horn_replay_answers(
        Presentations, [answer(AnswerNode, ProofNode)]).

horn_replay_answers(Presentations, Answers) :-
    must_be(list, Answers),
    presentations_program(Presentations, Program),
    maplist(replay_answer(Program), Answers).

replay_answer(Program, answer(AnswerNode, ProofNode)) :-
    node_logic(AnswerNode, Answer),
    ground(Answer),
    node_proof(ProofNode, Proof),
    once(replay_goal(Program, Answer, Proof)).

presentations_program(Presentations,
                      program(HeadIndex, RuleIndex, Intrinsics)) :-
    findall(Clause,
            ( member(Presentation, Presentations),
              presentation_clause(Presentation, Clause)
            ),
            Clauses),
    empty_assoc(EmptyHeads),
    empty_assoc(EmptyRules),
    foldl(index_clause, Clauses,
          indexes(EmptyHeads, EmptyRules),
          indexes(ReverseHeadIndex, RuleIndex)),
    assoc_to_list(ReverseHeadIndex, ReverseHeadPairs),
    maplist(reverse_head_bucket, ReverseHeadPairs, HeadPairs),
    list_to_assoc(HeadPairs, HeadIndex),
    presentation_intrinsics(Presentations, Intrinsics).

presentation_intrinsics(Presentations, Intrinsics) :-
    findall(Name-Arity,
            ( member(presentation(_, Operators, _, _), Presentations),
              member(operator(Name, Arity), Operators),
              intrinsic_relation(Name, Arity)
            ),
            RawIntrinsics),
    sort(RawIntrinsics, Intrinsics).

intrinsic_relation(different, 2).

index_clause(Clause,
             indexes(Heads0, Rules0),
             indexes(Heads, Rules)) :-
    Clause = clause(Name, Head, _),
    goal_head_key(Head, Key),
    ( get_assoc(Key, Heads0, Bucket0) -> true ; Bucket0 = [] ),
    put_assoc(Key, Heads0, [Clause|Bucket0], Heads),
    put_assoc(Name, Rules0, Clause, Rules).

reverse_head_bucket(Key-ReverseBucket, Key-Bucket) :-
    reverse(ReverseBucket, Bucket).

goal_head_key(g_app(Name, Arguments), Name-Arity) :-
    length(Arguments, Arity).

presentation_clause(presentation(_, _, Rules, _), Clause) :-
    member(Rule, Rules),
    rule_clause(Rule, Clause).

rule_clause(rule(Name, HeadNode, BodyNodes),
            clause(Name, Head, Body)) :-
    empty_assoc(Empty),
    node_logic(HeadNode, Empty, Environment, Head),
    nodes_logic(BodyNodes, Environment, _, Body).

node_logic(Node, Logic) :-
    empty_assoc(Empty),
    node_logic(Node, Empty, _, Logic).

node_logic(var(Name), Environment0, Environment, Logic) :-
    !,
    ( get_assoc(Name, Environment0, Existing) ->
        Logic = Existing,
        Environment = Environment0
    ; put_assoc(Name, Environment0, Logic, Environment)
    ).
node_logic(sym(Name), Environment, Environment, g_sym(Name)) :- !.
node_logic(str(Text), Environment, Environment, g_str(Text)) :- !.
node_logic(int(Value), Environment, Environment, g_int(Value)) :- !.
node_logic(list([sym(Name)|Arguments]), Environment0, Environment,
           g_app(Name, LogicArguments)) :-
    !,
    nodes_logic(Arguments, Environment0, Environment, LogicArguments).
node_logic(Node, _, _, _) :-
    throw(error(malformed_horn_term(Node), finite_horn_eval)).

nodes_logic([], Environment, Environment, []).
nodes_logic([Node|Nodes], Environment0, Environment,
            [Logic|LogicNodes]) :-
    node_logic(Node, Environment0, Environment1, Logic),
    nodes_logic(Nodes, Environment1, Environment, LogicNodes).

logic_node(Logic, _) :-
    var(Logic),
    !,
    fail.
logic_node(g_sym(Name), sym(Name)) :- !.
logic_node(g_str(Text), str(Text)) :- !.
logic_node(g_int(Value), int(Value)) :- !.
logic_node(g_app(Name, Arguments), list([sym(Name)|Nodes])) :-
    maplist(logic_node, Arguments, Nodes).

solve(_, _, _, 0) :-
    throw(depth_exhausted).
solve(program(_, _, Intrinsics),
      g_app(different, [Left, Right]),
      cert('$intrinsic-ground-different', []), _) :-
    memberchk(different-2, Intrinsics),
    ground(Left),
    ground(Right),
    Left \== Right.
solve(Program, Goal, cert(Name, Children), Depth) :-
    Program = program(HeadIndex, _, _),
    goal_head_key(Goal, Key),
    get_assoc(Key, HeadIndex, Templates),
    member(Template, Templates),
    copy_term(Template, clause(Name, Head, Body)),
    unify_with_occurs_check(Goal, Head),
    NextDepth is Depth - 1,
    solve_body(Program, Body, Children, NextDepth).

solve_body(_, [], [], _).
solve_body(Program, [Goal|Goals], [Proof|Proofs], Depth) :-
    solve(Program, Goal, Proof, Depth),
    solve_body(Program, Goals, Proofs, Depth).

proof_node(cert(Name, Children),
           list([sym(cert), sym(Name), list(ChildNodes)])) :-
    maplist(proof_node, Children, ChildNodes).

node_proof(list([sym(cert), sym(Name), list(ChildNodes)]),
           cert(Name, Children)) :-
    maplist(node_proof, ChildNodes, Children).

replay_goal(Program, Goal, cert(Name, Children)) :-
    Program = program(_, RuleIndex, _),
    get_assoc(Name, RuleIndex, Template),
    copy_term(Template, clause(Name, Head, Body)),
    unify_with_occurs_check(Goal, Head),
    same_length(Body, Children),
    replay_body(Program, Body, Children).
replay_goal(program(_, _, Intrinsics),
            g_app(different, [Left, Right]),
            cert('$intrinsic-ground-different', [])) :-
    memberchk(different-2, Intrinsics),
    ground(Left),
    ground(Right),
    Left \== Right.

replay_body(_, [], []).
replay_body(Program, [Goal|Goals], [Proof|Proofs]) :-
    replay_goal(Program, Goal, Proof),
    replay_body(Program, Goals, Proofs).
