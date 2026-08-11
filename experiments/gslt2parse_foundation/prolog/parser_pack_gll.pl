:- module(parser_pack_gll,
          [ parser_pack_gll_parse/6
          ]).

:- use_module(parser_pack_eval,
              [ parser_pack_validate/1,
                parser_pack_canonical_terms/2,
                parser_pack_missing_states/3,
                parser_pack_class_ranges/3,
                parser_pack_scalar_in_ranges/2
              ]).
:- use_module(parser_pack_forest,
              [ parser_pack_forest_certificate/6,
                parser_pack_input_codepoints/2,
                parser_pack_canonical_forest_nodes/2,
                parser_pack_canonical_forest_choices/2,
                parser_forest_node_extent/3,
                parser_forest_choice_children/2
              ]).
:- use_module(library(gensym)).

/*
  Grammar-generic descriptor GLL over the logical ParserPack production
  fragment.  The adapter owns no language loader or grammar-shape fast path.
  Recognition builds a shared packed forest; the one ParserPack action algebra
  is replayed separately.
*/

:- dynamic pgll_production/7.
:- dynamic pgll_state_production/3.
:- dynamic pgll_pack/2.
:- dynamic pgll_input/3.
:- dynamic pgll_input_length/2.
:- dynamic pgll_class_plan/3.
:- dynamic pgll_descriptor_seen/3.
:- dynamic pgll_descriptor_pending/2.
:- dynamic pgll_descriptor_count/2.
:- dynamic pgll_descriptor_limit/2.
:- dynamic pgll_gss_edge/5.
:- dynamic pgll_gss_popped/4.
:- dynamic pgll_node/3.
:- dynamic pgll_choice/8.
:- dynamic pgll_farthest/2.
:- dynamic pgll_expectation/3.

parser_pack_gll_parse(Pack, SourcePresentations, Start, Input,
                      DescriptorLimit, Outcome) :-
    must_be(integer, DescriptorLimit),
    DescriptorLimit > 0,
    ( parser_pack_validate(Pack) ->
        parser_pack_missing_states(Pack, [Start], Missing),
        ( Missing == [] ->
            ( parser_pack_input_codepoints(Input, InputValues) ->
                run_gll(Pack, SourcePresentations, Start, Input,
                        InputValues, DescriptorLimit, Outcome)
            ; Outcome = invalid_input(codepoint_list)
            )
        ; Outcome = unsupported(missing_states(Missing))
        )
    ; Outcome = invalid_presentation(parser_pack)
    ).

run_gll(Pack, SourcePresentations, Start, Input, InputValues,
        DescriptorLimit, Outcome) :-
    gensym(parser_pack_gll_session_, Session),
    setup_call_cleanup(
        prepare_session(Session, Pack, SourcePresentations, InputValues,
                        DescriptorLimit),
        catch(
            ( seed_state(Session, Start, root, 0),
              run_session(Session),
              collect_result(Session, Pack, SourcePresentations, Start,
                             Input, Outcome)
            ),
            parser_pack_gll_resource(Reason),
            Outcome = resource_exhausted(Reason)),
        cleanup_session(Session)).

prepare_session(Session, Pack, _SourcePresentations,
                InputValues, DescriptorLimit) :-
    Pack = parser_pack_v1(Productions, _),
    assertz(pgll_pack(Session, Pack)),
    assertz(pgll_descriptor_count(Session, 0)),
    assertz(pgll_descriptor_limit(Session, DescriptorLimit)),
    assertz(pgll_farthest(Session, 0)),
    maplist(prepare_production(Session), Productions),
    prepare_input(Session, InputValues, 0),
    length(InputValues, InputLength),
    assertz(pgll_input_length(Session, InputLength)).

prepare_production(
    Session,
    list([sym('pp-production'), Label, State, ItemsNode, Action])) :-
    pack_items_list(ItemsNode, Items),
    length(Items, Length),
    assertz(pgll_production(Session, Label, State, Items, Length, Action,
                            ItemsNode)),
    assertz(pgll_state_production(Session, State, Label)).

prepare_input(_, [], _).
prepare_input(Session, [Value|Values], Position) :-
    assertz(pgll_input(Session, Position, Value)),
    Next is Position + 1,
    prepare_input(Session, Values, Next).

cleanup_session(Session) :-
    retractall(pgll_production(Session, _, _, _, _, _, _)),
    retractall(pgll_state_production(Session, _, _)),
    retractall(pgll_pack(Session, _)),
    retractall(pgll_input(Session, _, _)),
    retractall(pgll_input_length(Session, _)),
    retractall(pgll_class_plan(Session, _, _)),
    retractall(pgll_descriptor_seen(Session, _, _)),
    retractall(pgll_descriptor_pending(Session, _)),
    retractall(pgll_descriptor_count(Session, _)),
    retractall(pgll_descriptor_limit(Session, _)),
    retractall(pgll_gss_edge(Session, _, _, _, _)),
    retractall(pgll_gss_popped(Session, _, _, _)),
    retractall(pgll_node(Session, _, _)),
    retractall(pgll_choice(Session, _, _, _, _, _, _, _)),
    retractall(pgll_farthest(Session, _)),
    retractall(pgll_expectation(Session, _, _)).

run_session(Session) :-
    ( retract(pgll_descriptor_pending(
                  Session,
                  descriptor(Label, Dot, Gss, Prefix, Position))) ->
        process_descriptor(Session, Label, Dot, Gss, Prefix, Position),
        run_session(Session)
    ; true
    ).

add_descriptor(Session, Label, Dot, Gss, Prefix, Position) :-
    Descriptor = descriptor(Label, Dot, Gss, Prefix, Position),
    term_hash(Descriptor, Hash),
    ( pgll_descriptor_seen(Session, Hash, Descriptor) -> true
    ; bump_descriptor_count(Session),
      assertz(pgll_descriptor_seen(Session, Hash, Descriptor)),
      assertz(pgll_descriptor_pending(Session, Descriptor))
    ).

bump_descriptor_count(Session) :-
    retract(pgll_descriptor_count(Session, Count)),
    pgll_descriptor_limit(Session, Limit),
    ( Count >= Limit ->
        assertz(pgll_descriptor_count(Session, Count)),
        throw(parser_pack_gll_resource(descriptors(Limit)))
    ; Next is Count + 1,
      assertz(pgll_descriptor_count(Session, Next))
    ).

process_descriptor(Session, Label, Dot, Gss, Prefix, Position) :-
    touch_position(Session, Position),
    pgll_production(Session, Label, _, Items, Length, _, _),
    ( Dot < Length ->
        nth0(Dot, Items, Item),
        process_item(Session, Label, Dot, Gss, Prefix, Position, Item)
    ; complete_production(Session, Label, Gss, Prefix, Position, Length)
    ).

process_item(Session, Label, Dot, Gss, Prefix, Position,
             terminal(Matcher)) :-
    record_expectation(Session, Position, Matcher),
    ( match_terminal(Session, Matcher, Position, Value, NextPosition) ->
        NextDot is Dot + 1,
        Terminal = parser_terminal(Matcher, Value, Position, NextPosition),
        add_node(Session, Terminal),
        get_node(Session, Label, NextDot, Prefix, Terminal, Parent),
        add_descriptor(Session, Label, NextDot, Gss, Parent, NextPosition)
    ; true
    ).
process_item(Session, Label, Dot, Gss, Prefix, Position,
             nonterminal(State)) :-
    NextDot is Dot + 1,
    create_gss(Session, Label, NextDot, Gss, Prefix, Position, ChildGss),
    seed_state(Session, State, ChildGss, Position).

complete_production(Session, Label, Gss, _, Position, 0) :-
    !,
    Epsilon = parser_epsilon(Position),
    add_node(Session, Epsilon),
    get_node(Session, Label, 0, none, Epsilon, Symbol),
    pop_gss(Session, Gss, Symbol).
complete_production(Session, _, Gss, Prefix, _, _) :-
    Prefix \== none,
    pop_gss(Session, Gss, Prefix).

seed_state(Session, State, Gss, Position) :-
    forall(pgll_state_production(Session, State, Label),
           add_descriptor(Session, Label, 0, Gss, none, Position)).

create_gss(Session, Label, Dot, ParentGss, Prefix, Position, Gss) :-
    Gss = gss(Label, Dot, Position),
    term_hash(Gss, Hash),
    ( pgll_gss_edge(Session, Hash, Gss, Prefix, ParentGss) -> true
    ; assertz(pgll_gss_edge(Session, Hash, Gss, Prefix, ParentGss)),
      forall(pgll_gss_popped(Session, Hash, Gss, Popped),
             resume_gss_edge(Session, Gss, Prefix, ParentGss, Popped))
    ).

pop_gss(Session, Gss, Popped) :-
    term_hash(Gss, Hash),
    ( pgll_gss_popped(Session, Hash, Gss, Popped) -> true
    ; assertz(pgll_gss_popped(Session, Hash, Gss, Popped)),
      ( Gss == root -> true
      ; forall(pgll_gss_edge(Session, Hash, Gss, Prefix, ParentGss),
               resume_gss_edge(Session, Gss, Prefix, ParentGss, Popped))
      )
    ).

resume_gss_edge(Session, gss(Label, Dot, _), Prefix, ParentGss, Popped) :-
    get_node(Session, Label, Dot, Prefix, Popped, Parent),
    parser_forest_node_extent(Popped, _, Right),
    add_descriptor(Session, Label, Dot, ParentGss, Parent, Right).

get_node(Session, Label, Dot, Prefix, Child, Parent) :-
    parser_forest_node_extent(Child, Pivot, Right),
    ( Prefix == none -> Left = Pivot
    ; parser_forest_node_extent(Prefix, Left, _)
    ),
    pgll_production(Session, Label, State, _, Length, _, _),
    ( Dot =:= Length -> Parent = parser_symbol(State, Left, Right)
    ; Parent = parser_intermediate(Label, Dot, Left, Right)
    ),
    add_node(Session, Parent),
    add_choice(Session, Parent, Label, Prefix, Child, Pivot).

add_node(Session, Node) :-
    term_hash(Node, Hash),
    ( pgll_node(Session, Hash, Node) -> true
    ; assertz(pgll_node(Session, Hash, Node))
    ).

add_choice(Session, Parent, Label, Prefix, Child, Pivot) :-
    term_hash(Parent, Hash),
    ( pgll_choice(Session, Hash, Parent, Label, Prefix, Child, Pivot,
                  source) -> true
    ; assertz(pgll_choice(Session, Hash, Parent, Label, Prefix, Child, Pivot,
                          source))
    ).

match_terminal(Session, sym('pp-terminal-any'), Position, Value, Next) :-
    pgll_input(Session, Position, Value),
    Next is Position + 1.
match_terminal(Session, sym('pp-terminal-eof'), Position, sym(eof),
               Position) :-
    pgll_input_length(Session, Position).
match_terminal(Session,
               list([sym('pp-terminal-char'), Value]), Position, Value,
               Next) :-
    pgll_input(Session, Position, Value),
    Next is Position + 1.
match_terminal(Session,
               list([sym('pp-terminal-class'), Class]), Position, Value,
               Next) :-
    pgll_input(Session, Position, Value),
    class_member(Session, Class, Value),
    Next is Position + 1.

class_member(Session, Class, Value) :-
    ( pgll_class_plan(Session, Class, Ranges) -> true
    ; pgll_pack(Session, Pack),
      parser_pack_class_ranges(Pack, Class, Ranges),
      assertz(pgll_class_plan(Session, Class, Ranges))
    ),
    parser_pack_scalar_in_ranges(Value, Ranges).

touch_position(Session, Position) :-
    retract(pgll_farthest(Session, Current)),
    ( Position > Current -> Farthest = Position ; Farthest = Current ),
    assertz(pgll_farthest(Session, Farthest)).

record_expectation(Session, Position, Matcher) :-
    ( pgll_expectation(Session, Position, Matcher) -> true
    ; assertz(pgll_expectation(Session, Position, Matcher))
    ).

collect_result(Session, Pack, SourcePresentations, Start, Input,
               completed(parser_result(
                   decision(Decision),
                   coverage(farthest(Farthest), expected(Expectations),
                            productions(UsedLabels)),
                   ambiguity(roots(RootCount), packed_choices(ChoiceCount)),
                   Forest,
                   evidence(forest_certificate(Certificate))))) :-
    pgll_input_length(Session, InputLength),
    term_hash(root, RootHash),
    findall(Root,
            ( pgll_gss_popped(Session, RootHash, root, Root),
              Root = parser_symbol(Start, 0, End),
              End =< InputLength
            ),
            RawRoots),
    parser_pack_canonical_forest_nodes(RawRoots, Roots),
    ( memberchk(parser_symbol(Start, 0, InputLength), Roots) ->
        Decision = accepted
    ; Decision = rejected
    ),
    reachable_forest(Session, Roots, Nodes, Choices),
    findall(Label, member(parser_choice(_, Label, _, _, _), Choices),
            RawUsedLabels),
    parser_pack_canonical_terms(RawUsedLabels, UsedLabels),
    pgll_farthest(Session, Farthest),
    findall(Matcher, pgll_expectation(Session, Farthest, Matcher),
            RawExpectations),
    parser_pack_canonical_terms(RawExpectations, Expectations),
    length(Roots, RootCount),
    length(Choices, ChoiceCount),
    Forest = parser_forest(Start, InputLength, Roots, Nodes, Choices),
    parser_pack_forest_certificate(
        Pack, SourcePresentations, Start, Input, Forest, Certificate).

reachable_forest(Session, Roots, Nodes, Choices) :-
    reachable_agenda(Session, Roots, [], [], RawNodes, RawChoices),
    parser_pack_canonical_forest_nodes(RawNodes, Nodes),
    parser_pack_canonical_forest_choices(RawChoices, Choices).

reachable_agenda(_, [], Nodes, Choices, Nodes, Choices).
reachable_agenda(Session, [Node|Agenda], Nodes0, Choices0,
                 Nodes, Choices) :-
    ( memberchk(Node, Nodes0) ->
        reachable_agenda(Session, Agenda, Nodes0, Choices0, Nodes, Choices)
    ; findall(parser_choice(Node, Label, Prefix, Child, Pivot),
              pgll_choice(Session, _, Node, Label, Prefix, Child, Pivot,
                          source),
              NodeChoices),
      parser_forest_choice_children(NodeChoices, Children),
      append(Children, Agenda, NextAgenda),
      append(NodeChoices, Choices0, NextChoices),
      reachable_agenda(Session, NextAgenda, [Node|Nodes0], NextChoices,
                       Nodes, Choices)
    ).

pack_items_list(sym('pp-items-nil'), []).
pack_items_list(list([sym('pp-items-cons'), Item, More]),
                [PackedItem|Items]) :-
    pack_item(Item, PackedItem),
    pack_items_list(More, Items).

pack_item(list([sym('pp-terminal'), Matcher]), terminal(Matcher)).
pack_item(list([sym('pp-nonterminal'), State]), nonterminal(State)).
