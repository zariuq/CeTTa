:- module(parser_pack_forest,
          [ parser_pack_forest_results/5,
            parser_pack_replay_forest/5,
            parser_pack_forest_digest/2,
            parser_pack_forest_certificate/6,
            parser_pack_input_codepoints/2,
            parser_pack_canonical_forest_nodes/2,
            parser_pack_canonical_forest_choices/2,
            parser_forest_node_extent/3,
            parser_forest_choice_children/2
          ]).

:- use_module(finite_horn_gslt_v1,
              [ package_digest/2,
                render_term/2
              ]).
:- use_module(parser_pack_action).
:- use_module(parser_pack_eval,
              [ parser_pack_validate/1,
                parser_pack_canonical_terms/2,
                parser_pack_canonical_results/2,
                parser_pack_digest/2,
                parser_pack_class_member/3
              ]).
:- use_module(library(crypto)).
:- use_module(library(pairs)).

/*
  Backend-neutral packed-forest contract for ParserPack recognizers.
  Recognition engines construct these terms independently; semantic action
  replay, certificate construction, validation, and canonical hashing live
  here so no parser backend owns the shared evidence format.
*/

parser_pack_forest_results(Pack, Input, Forest, MaxDepth, Outcome) :-
    must_be(integer, MaxDepth),
    MaxDepth > 0,
    catch(
        findall(result(Value, Rest),
                forest_result(Pack, Input, Forest, MaxDepth, Value, Rest),
                RawResults),
        parser_pack_forest_resource(Reason),
        RawResults = resource(Reason)),
    ( RawResults = resource(Reason) ->
        Outcome = resource_exhausted(Reason)
    ; parser_pack_canonical_results(RawResults, Results),
      Outcome = completed(Results)
    ).

forest_result(Pack, Input,
              parser_forest(_, _, Roots, Nodes, Choices), MaxDepth,
              Value, Rest) :-
    member(Root, Roots),
    forest_symbol_value(Pack, Nodes, Choices, Root, [], MaxDepth, Value),
    Root = parser_symbol(_, 0, End),
    drop_input(Input, End, Rest).

forest_symbol_value(_, _, _, Node, Path, _, _) :-
    memberchk(Node, Path),
    throw(parser_pack_forest_resource(cyclic_forest(Node))).
forest_symbol_value(Pack, Nodes, Choices, Node, Path, Depth, Value) :-
    Node = parser_symbol(State, _, _),
    memberchk(Node, Nodes),
    member(parser_choice(Node, Label, Prefix, Child, _), Choices),
    pack_production(Pack, Label, State, Items, Action),
    length(Items, Arity),
    ( Arity =:= 0 -> ChildNodes = []
    ; choice_child_nodes(Choices, Prefix, Child, Depth, ChildNodes),
      length(ChildNodes, Arity)
    ),
    maplist(forest_child_value(Pack, Nodes, Choices,
                               [Node|Path], Depth),
            ChildNodes, ChildValues),
    parser_pack_apply_action(Action, ChildValues, Value).

choice_child_nodes(_, none, Child, _, [Child]).
choice_child_nodes(Choices, Prefix, Child, Depth, Nodes) :-
    Prefix \== none,
    prefix_child_nodes(Choices, Prefix, Depth, PrefixNodes),
    append(PrefixNodes, [Child], Nodes).

prefix_child_nodes(_, _, 0, _) :-
    throw(parser_pack_forest_resource(depth)).
prefix_child_nodes(Choices, Prefix, Depth, Nodes) :-
    member(parser_choice(Prefix, _, Previous, Child, _), Choices),
    NextDepth is Depth - 1,
    ( Previous == none -> Nodes = [Child]
    ; prefix_child_nodes(Choices, Previous, NextDepth, PreviousNodes),
      append(PreviousNodes, [Child], Nodes)
    ).

forest_child_value(_, Nodes, _, _, _, Node, Value) :-
    Node = parser_terminal(_, Value, _, _),
    memberchk(Node, Nodes),
    !.
forest_child_value(Pack, Nodes, Choices, Path, Depth,
                   Node, Value) :-
    Node = parser_symbol(_, _, _),
    forest_symbol_value(Pack, Nodes, Choices, Node, Path, Depth, Value).

parser_pack_replay_forest(Pack, SourcePresentations, Start, Input,
                          parser_result(
                              decision(Decision),
                              coverage(farthest(Farthest),
                                       expected(Expectations),
                                       productions(UsedLabels)),
                              ambiguity(roots(RootCount),
                                        packed_choices(ChoiceCount)),
                              Forest,
                              evidence(forest_certificate(Certificate)))) :-
    parser_pack_validate(Pack),
    parser_pack_input_codepoints(Input, InputValues),
    length(InputValues, InputLength),
    Forest = parser_forest(Start, InputLength, Roots, Nodes, Choices),
    parser_pack_canonical_forest_nodes(Roots, Roots),
    parser_pack_canonical_forest_nodes(Nodes, Nodes),
    parser_pack_canonical_forest_choices(Choices, Choices),
    forall(member(Root, Roots),
           valid_root(Root, Start, InputLength, Nodes)),
    expected_decision(Roots, Start, InputLength, Decision),
    integer(Farthest),
    Farthest >= 0,
    Farthest =< InputLength,
    parser_pack_canonical_terms(Expectations, Expectations),
    findall(Label, member(parser_choice(_, Label, _, _, _), Choices),
            RawLabels),
    parser_pack_canonical_terms(RawLabels, UsedLabels),
    length(Roots, RootCount),
    length(Choices, ChoiceCount),
    forall(member(Node, Nodes),
           valid_forest_node(Pack, SourcePresentations, InputValues,
                             InputLength, Choices, Node)),
    forall(member(Choice, Choices),
           valid_forest_choice(Pack, SourcePresentations, InputValues,
                               InputLength, Nodes, Choice)),
    reachable_lists(Roots, Choices, ReachableNodes, ReachableChoices),
    ReachableNodes == Nodes,
    ReachableChoices == Choices,
    parser_pack_forest_certificate(
        Pack, SourcePresentations, Start, Input, Forest, Certificate).

parser_pack_forest_certificate(Pack, SourcePresentations, Start, Input,
                               Forest, Certificate) :-
    parser_pack_digest(Pack, PackDigest),
    source_package_binding(SourcePresentations, SourceBinding),
    input_digest(Input, InputDigest),
    parser_pack_forest_digest(Forest, ForestDigest),
    Certificate = parser_forest_certificate_v1(
        PackDigest, SourceBinding, Start, InputDigest, ForestDigest).

source_package_binding([], no_source_presentations).
source_package_binding(Presentations, source_package_digest(Digest)) :-
    Presentations = [_|_],
    package_digest(Presentations, Digest).

valid_root(parser_symbol(Start, 0, End), Start, InputLength, Nodes) :-
    End >= 0,
    End =< InputLength,
    memberchk(parser_symbol(Start, 0, End), Nodes).

expected_decision(Roots, Start, InputLength, accepted) :-
    memberchk(parser_symbol(Start, 0, InputLength), Roots),
    !.
expected_decision(_, _, _, rejected).

valid_forest_node(_, _, _, InputLength, _, parser_epsilon(Position)) :-
    valid_position(Position, InputLength).
valid_forest_node(Pack, _, InputValues, InputLength, _,
                  parser_terminal(Matcher, Value, Left, Right)) :-
    valid_position(Left, InputLength),
    valid_position(Right, InputLength),
    replay_terminal(Pack, InputValues, InputLength, Matcher,
                    Left, Value, Right).
valid_forest_node(Pack, _, _, InputLength, Choices, Node) :-
    Node = parser_symbol(State, Left, Right),
    valid_extent(Left, Right, InputLength),
    pack_has_state(Pack, State),
    member(parser_choice(Node, _, _, _, _), Choices).
valid_forest_node(Pack, _, _, InputLength, Choices, Node) :-
    Node = parser_intermediate(Label, Dot, Left, Right),
    valid_extent(Left, Right, InputLength),
    pack_production(Pack, Label, _, Items, _),
    length(Items, Length),
    integer(Dot),
    Dot > 0,
    Dot < Length,
    member(parser_choice(Node, Label, _, _, _), Choices).

valid_forest_choice(Pack, _, InputValues, InputLength, Nodes,
                    parser_choice(Parent, Label, Prefix, Child, Pivot)) :-
    memberchk(Parent, Nodes),
    memberchk(Child, Nodes),
    ( Prefix == none ; memberchk(Prefix, Nodes) ),
    pack_production(Pack, Label, State, Items, _),
    length(Items, Length),
    ( Length =:= 0 ->
        Parent = parser_symbol(State, Pivot, Pivot),
        Prefix == none,
        Child = parser_epsilon(Pivot),
        valid_position(Pivot, InputLength)
    ; choice_dot(Parent, Label, State, Length, Dot, Left, Right),
      Dot > 0,
      ItemIndex is Dot - 1,
      nth0(ItemIndex, Items, Item),
      parser_forest_node_extent(Child, Pivot, Right),
      valid_choice_prefix(Prefix, Label, Dot, Left, Pivot),
      valid_item_child(Pack, InputValues, InputLength, Item, Child)
    ).

choice_dot(parser_symbol(State, Left, Right), _, State, Length,
           Length, Left, Right).
choice_dot(parser_intermediate(Label, Dot, Left, Right), Label, _, Length,
           Dot, Left, Right) :-
    Dot < Length.

valid_choice_prefix(none, _, 1, Left, Left).
valid_choice_prefix(parser_intermediate(Label, PreviousDot, Left, Pivot),
                    Label, Dot, Left, Pivot) :-
    Dot > 1,
    PreviousDot is Dot - 1.

valid_item_child(Pack, InputValues, InputLength, terminal(Matcher),
                 parser_terminal(Matcher, Value, Left, Right)) :-
    replay_terminal(Pack, InputValues, InputLength, Matcher,
                    Left, Value, Right).
valid_item_child(_, _, _, nonterminal(State),
                 parser_symbol(State, _, _)).

replay_terminal(_, InputValues, _, sym('pp-terminal-any'),
                Left, Value, Right) :-
    nth0(Left, InputValues, Value),
    Right is Left + 1.
replay_terminal(_, _, InputLength, sym('pp-terminal-eof'),
                InputLength, sym(eof), InputLength).
replay_terminal(_, InputValues, _,
                list([sym('pp-terminal-char'), Value]),
                Left, Value, Right) :-
    nth0(Left, InputValues, Value),
    Right is Left + 1.
replay_terminal(Pack, InputValues, _,
                list([sym('pp-terminal-class'), Class]),
                Left, Value, Right) :-
    nth0(Left, InputValues, Value),
    parser_pack_class_member(Pack, Class, Value),
    Right is Left + 1.

reachable_lists(Roots, Choices, Nodes, ReachableChoices) :-
    reachable_lists_agenda(Roots, Choices, [], [], RawNodes, RawChoices),
    parser_pack_canonical_forest_nodes(RawNodes, Nodes),
    parser_pack_canonical_forest_choices(RawChoices, ReachableChoices).

reachable_lists_agenda([], _, Nodes, FoundChoices, Nodes, FoundChoices).
reachable_lists_agenda([Node|Agenda], Choices, Nodes0, Found0,
                       Nodes, Found) :-
    ( memberchk(Node, Nodes0) ->
        reachable_lists_agenda(Agenda, Choices, Nodes0, Found0,
                               Nodes, Found)
    ; findall(Choice,
              ( member(Choice, Choices),
                Choice = parser_choice(Node, _, _, _, _) ),
              NodeChoices),
      parser_forest_choice_children(NodeChoices, Children),
      append(Children, Agenda, NextAgenda),
      append(NodeChoices, Found0, NextFound),
      reachable_lists_agenda(NextAgenda, Choices, [Node|Nodes0], NextFound,
                             Nodes, Found)
    ).

parser_pack_forest_digest(Forest, Digest) :-
    forest_node(Forest, CanonicalNode),
    render_term(CanonicalNode, Text),
    crypto_data_hash(Text, Digest, [algorithm(sha256)]).

parser_pack_canonical_forest_nodes(Nodes, Canonical) :-
    map_list_to_pairs(forest_tree_key, Nodes, Pairs),
    canonical_forest_pairs(Pairs, Canonical).

parser_pack_canonical_forest_choices(Choices, Canonical) :-
    map_list_to_pairs(forest_choice_key, Choices, Pairs),
    canonical_forest_pairs(Pairs, Canonical).

forest_tree_key(Node, Key) :-
    ground(Node),
    forest_tree_node(Node, Canonical),
    render_term(Canonical, Key).

forest_choice_key(Choice, Key) :-
    ground(Choice),
    forest_choice_node(Choice, Canonical),
    render_term(Canonical, Key).

canonical_forest_pairs(Pairs, Canonical) :-
    keysort(Pairs, SortedPairs),
    canonical_forest_pairs_unique(SortedPairs, UniquePairs),
    pairs_values(UniquePairs, Canonical).

canonical_forest_pairs_unique([], []).
canonical_forest_pairs_unique(
    [Key-Term|Pairs], [Key-Term|Unique]) :-
    canonical_forest_pairs_drop_same(Pairs, Key, Term, Remaining),
    canonical_forest_pairs_unique(Remaining, Unique).

canonical_forest_pairs_drop_same(
    [Key-Other|Pairs], Key, Term, Remaining) :-
    !,
    ( Other == Term -> true
    ; throw(error(parser_forest_render_collision(Key, Term, Other),
                  parser_pack_forest))
    ),
    canonical_forest_pairs_drop_same(Pairs, Key, Term, Remaining).
canonical_forest_pairs_drop_same(Pairs, _, _, Pairs).

forest_node(parser_forest(Start, InputLength, Roots, Nodes, Choices),
            list([sym('pf-v1'), Start, int(InputLength),
                  RootNodes, ForestNodes, ChoiceNodes])) :-
    maplist(forest_tree_node, Roots, CanonicalRoots),
    maplist(forest_tree_node, Nodes, CanonicalNodes),
    maplist(forest_choice_node, Choices, CanonicalChoices),
    node_list(CanonicalRoots, RootNodes),
    node_list(CanonicalNodes, ForestNodes),
    node_list(CanonicalChoices, ChoiceNodes).

forest_tree_node(parser_symbol(State, Left, Right),
                 list([sym('pf-symbol'), State, int(Left), int(Right)])).
forest_tree_node(parser_intermediate(Label, Dot, Left, Right),
                 list([sym('pf-intermediate'), Label, int(Dot),
                       int(Left), int(Right)])).
forest_tree_node(parser_terminal(Matcher, Value, Left, Right),
                 list([sym('pf-terminal'), Matcher, Value,
                       int(Left), int(Right)])).
forest_tree_node(parser_epsilon(Position),
                 list([sym('pf-epsilon'), int(Position)])).

forest_choice_node(parser_choice(Parent, Label, Prefix, Child, Pivot),
                   list([sym('pf-choice'), ParentNode, Label, PrefixNode,
                         ChildNode, int(Pivot)])) :-
    forest_tree_node(Parent, ParentNode),
    ( Prefix == none -> PrefixNode = sym('pf-none')
    ; forest_tree_node(Prefix, PrefixNode)
    ),
    forest_tree_node(Child, ChildNode).

node_list([], sym(nil)).
node_list([Node|Nodes], list([sym(cons), Node, More])) :-
    node_list(Nodes, More).

input_digest(Input, Digest) :-
    render_term(Input, Text),
    crypto_data_hash(Text, Digest, [algorithm(sha256)]).

parser_pack_input_codepoints(sym(nil), []).
parser_pack_input_codepoints(
    list([sym(cons), Value, Rest]), [Value|Values]) :-
    unicode_scalar_node(Value),
    parser_pack_input_codepoints(Rest, Values).

unicode_scalar_node(list([sym(cp), int(Codepoint)])) :-
    integer(Codepoint),
    Codepoint >= 0,
    Codepoint =< 0x10ffff,
    \+ (Codepoint >= 0xd800, Codepoint =< 0xdfff).

drop_input(Input, 0, Input) :-
    !.
drop_input(list([sym(cons), _, Rest]), Count, Suffix) :-
    Count > 0,
    Next is Count - 1,
    drop_input(Rest, Next, Suffix).

pack_items_list(sym('pp-items-nil'), []).
pack_items_list(list([sym('pp-items-cons'), Item, More]),
                [PackedItem|Items]) :-
    pack_item(Item, PackedItem),
    pack_items_list(More, Items).

pack_item(list([sym('pp-terminal'), Matcher]), terminal(Matcher)).
pack_item(list([sym('pp-nonterminal'), State]), nonterminal(State)).

pack_production(parser_pack_v1(Productions, _),
                Label, State, Items, Action) :-
    member(list([sym('pp-production'), Label, State, ItemsNode, Action]),
           Productions),
    pack_items_list(ItemsNode, Items).

pack_has_state(Pack, State) :-
    pack_production(Pack, _, State, _, _),
    !.

parser_forest_node_extent(parser_symbol(_, Left, Right), Left, Right).
parser_forest_node_extent(
    parser_intermediate(_, _, Left, Right), Left, Right).
parser_forest_node_extent(
    parser_terminal(_, _, Left, Right), Left, Right).
parser_forest_node_extent(parser_epsilon(Position), Position, Position).

parser_forest_choice_children([], []).
parser_forest_choice_children(
    [parser_choice(_, _, Prefix, Child, _)|Choices], Children) :-
    ( Prefix == none -> Here = [Child] ; Here = [Prefix, Child] ),
    parser_forest_choice_children(Choices, More),
    append(Here, More, Children).

valid_position(Position, InputLength) :-
    integer(Position),
    Position >= 0,
    Position =< InputLength.

valid_extent(Left, Right, InputLength) :-
    valid_position(Left, InputLength),
    valid_position(Right, InputLength),
    Left =< Right.
