:- module(parser_pack_eval,
          [ parser_pack_from_compiler_answers/3,
            parser_pack_from_compiler_answers/4,
            parser_pack_validate/1,
            parser_pack_canonical_terms/2,
            parser_pack_canonical_results/2,
            parser_pack_digest/2,
            parser_pack_missing_states/3,
            parser_pack_parse_results/6,
            parser_pack_class_ranges/3,
            parser_pack_class_member/3,
            parser_pack_scalar_in_ranges/2
          ]).

:- use_module(finite_horn_gslt_v1, [render_term/2]).
:- use_module(parser_pack_action).
:- use_module(library(crypto)).
:- use_module(library(pairs)).

/*
  Bounded semantic reference for the logical ParserPack production fragment.
  It is deliberately independent of GLL and GLR.  Parser backends consume the
  recognition projection; this evaluator checks the production action algebra
  against direct source interpretation before an adapter is accepted.
*/

parser_pack_from_compiler_answers(ProductionAnswers, ClassAnswers,
                                  Pack) :-
    parser_pack_from_compiler_answers(
        ProductionAnswers, ClassAnswers, [], Pack).

parser_pack_from_compiler_answers(ProductionAnswers, ClassAnswers,
                                  RequiredClassExpressions, Pack) :-
    maplist(answer_pack_production, ProductionAnswers, RawProductions),
    parser_pack_canonical_terms(RawProductions, Productions),
    maplist(answer_pack_class_clause, ClassAnswers, RawClassClauses0),
    maplist(canonical_class_clause, RawClassClauses0, RawClassClauses1),
    parser_pack_canonical_terms(RawClassClauses1, RawClassClauses),
    referenced_class_leaves(Productions, ProductionClasses),
    class_expressions_leaves(RequiredClassExpressions, RequiredClasses),
    append(ProductionClasses, RequiredClasses, RawReferencedClasses),
    sort(RawReferencedClasses, ReferencedClasses),
    include(class_clause_referenced(ReferencedClasses),
            RawClassClauses, ClassClauses),
    Pack = parser_pack_v1(Productions, ClassClauses),
    parser_pack_validate(Pack),
    forall(member(Class, RequiredClasses),
           parser_pack_class_ranges(Pack, Class, _)).

/*
  ParserPack collections use the canonical finite-Horn rendering as their
  interchange order.  Host-language term order is deliberately not part of
  the ABI: in particular, its ordering of integer-bearing class clauses is
  not portable to C.  The renderer is injective on the admitted ground term
  carrier, so an equal byte key denotes the same term.
*/

parser_pack_canonical_terms(Terms, Canonical) :-
    map_list_to_pairs(parser_pack_term_key, Terms, Pairs),
    keysort(Pairs, SortedPairs),
    canonical_pairs_unique(SortedPairs, UniquePairs),
    pairs_values(UniquePairs, Canonical).

parser_pack_term_key(Term, Key) :-
    ground(Term),
    render_term(Term, Key).

parser_pack_canonical_results(Results, Canonical) :-
    map_list_to_pairs(parser_pack_result_key, Results, Pairs),
    keysort(Pairs, SortedPairs),
    canonical_pairs_unique(SortedPairs, UniquePairs),
    pairs_values(UniquePairs, Canonical).

parser_pack_result_key(result(Value, Rest), Key) :-
    ground(result(Value, Rest)),
    render_term(list([sym(result), Value, Rest]), Key).

canonical_pairs_unique([], []).
canonical_pairs_unique([Key-Term|Pairs], [Key-Term|Unique]) :-
    canonical_pairs_drop_same(Pairs, Key, Term, Remaining),
    canonical_pairs_unique(Remaining, Unique).

canonical_pairs_drop_same([Key-Other|Pairs], Key, Term, Remaining) :-
    !,
    ( Other == Term -> true
    ; throw(error(parser_pack_render_collision(Key, Term, Other),
                  parser_pack_eval))
    ),
    canonical_pairs_drop_same(Pairs, Key, Term, Remaining).
canonical_pairs_drop_same(Pairs, _, _, Pairs).

parser_pack_validate(Pack) :-
    Pack = parser_pack_v1(Productions, ClassClauses),
    Productions = [_|_],
    ground(Pack),
    maplist(validate_pack_production, Productions),
    maplist(pack_production_label, Productions, Labels),
    sort(Labels, UniqueLabels),
    same_length(Labels, UniqueLabels),
    parser_pack_canonical_terms(Productions, Productions),
    maplist(validate_class_clause, ClassClauses),
    parser_pack_canonical_terms(ClassClauses, ClassClauses),
    referenced_class_leaves(Productions, ReferencedClasses),
    forall(member(Class, ReferencedClasses),
           parser_pack_class_ranges(Pack, Class, _)).

answer_pack_production(
    answer(list([sym('compile-pack-production'), _, Production]), _),
    Production).

answer_pack_class_clause(
    answer(list([sym('compile-pack-class-clause'), _, Clause]), _),
    Clause).

pack_production_label(
    list([sym('pp-production'), Label, _, _, _]), Label).

validate_pack_production(
    list([sym('pp-production'), _, _, Items, Action])) :-
    pack_item_count(Items, Arity),
    parser_pack_action_valid(Action, Arity).

pack_item_count(sym('pp-items-nil'), 0).
pack_item_count(list([sym('pp-items-cons'), Item, More]), Count) :-
    valid_pack_item(Item),
    pack_item_count(More, TailCount),
    Count is TailCount + 1.

valid_pack_item(list([sym('pp-nonterminal'), _])).
valid_pack_item(list([sym('pp-terminal'), Matcher])) :-
    valid_terminal_matcher(Matcher).

valid_terminal_matcher(sym('pp-terminal-any')).
valid_terminal_matcher(sym('pp-terminal-eof')).
valid_terminal_matcher(list([sym('pp-terminal-char'), Codepoint])) :-
    unicode_scalar_node(Codepoint).
valid_terminal_matcher(list([sym('pp-terminal-class'), _])).

canonical_class_clause(
    list([sym('pp-class-point'), Class, Codepoint]),
    list([sym('pp-class-point'), Class, Codepoint])).
canonical_class_clause(
    list([sym('pp-class-except'), Class, PointsNode]),
    list([sym('pp-class-except'), Class, CanonicalPointsNode])) :-
    points_node_list(PointsNode, Points),
    sort(Points, CanonicalPoints),
    points_list_node(CanonicalPoints, CanonicalPointsNode).

validate_class_clause(
    list([sym('pp-class-point'), Class, Codepoint])) :-
    ground(Class),
    unicode_scalar_node(Codepoint).
validate_class_clause(
    list([sym('pp-class-except'), Class, PointsNode])) :-
    ground(Class),
    points_node_list(PointsNode, Points),
    maplist(unicode_scalar_node, Points),
    sort(Points, Points).

points_node_list(sym('pp-points-nil'), []).
points_node_list(list([sym('pp-points-cons'), Point, More]),
                 [Point|Points]) :-
    points_node_list(More, Points).

points_list_node([], sym('pp-points-nil')).
points_list_node([Point|Points],
                 list([sym('pp-points-cons'), Point, More])) :-
    points_list_node(Points, More).

referenced_class_leaves(Productions, Classes) :-
    findall(Class,
            ( member(Production, Productions),
              production_class_expression(Production, Expression),
              class_leaf(Expression, Class)
            ),
            RawClasses),
    sort(RawClasses, Classes).

class_expressions_leaves(Expressions, Classes) :-
    findall(Class,
            ( member(Expression, Expressions),
              class_leaf(Expression, Class)
            ),
            RawClasses),
    sort(RawClasses, Classes).

production_class_expression(
    list([sym('pp-production'), _, _, Items, _]), Class) :-
    packed_terminal_matcher(
        Items, list([sym('pp-terminal-class'), Class])).

packed_terminal_matcher(
    list([sym('pp-items-cons'),
          list([sym('pp-terminal'), Matcher]), _]), Matcher).
packed_terminal_matcher(
    list([sym('pp-items-cons'), _, More]), Matcher) :-
    packed_terminal_matcher(More, Matcher).

class_leaf(list([sym('c-union'), Left, Right]), Class) :-
    !,
    ( class_leaf(Left, Class)
    ; class_leaf(Right, Class)
    ).
class_leaf(Class, Class).

class_clause_referenced(Classes, Clause) :-
    class_clause_key(Clause, Class),
    memberchk(Class, Classes).

class_clause_key(
    list([sym('pp-class-point'), Class, _]), Class).
class_clause_key(
    list([sym('pp-class-except'), Class, _]), Class).

parser_pack_class_ranges(parser_pack_v1(_, ClassClauses), Class, Ranges) :-
    ground(Class),
    class_expression_ranges(ClassClauses, Class, RawRanges),
    RawRanges = [_|_],
    normalize_ranges(RawRanges, Ranges).

class_expression_ranges(ClassClauses,
                        list([sym('c-union'), Left, Right]), Ranges) :-
    !,
    class_expression_ranges(ClassClauses, Left, LeftRanges),
    class_expression_ranges(ClassClauses, Right, RightRanges),
    append(LeftRanges, RightRanges, Ranges).
class_expression_ranges(ClassClauses, Class, Ranges) :-
    findall(ClauseRanges,
            ( member(Clause, ClassClauses),
              class_clause_key(Clause, Class),
              class_clause_ranges(Clause, ClauseRanges)
            ),
            NestedRanges),
    NestedRanges = [_|_],
    append(NestedRanges, Ranges).

class_clause_ranges(
    list([sym('pp-class-point'), _, list([sym(cp), int(Codepoint)])]),
    [range(Codepoint, Codepoint)]).
class_clause_ranges(
    list([sym('pp-class-except'), _, PointsNode]), Ranges) :-
    points_node_list(PointsNode, Points),
    maplist(codepoint_value, Points, Excluded0),
    sort(Excluded0, Excluded),
    scalar_universe_without(Excluded, Ranges).

codepoint_value(list([sym(cp), int(Codepoint)]), Codepoint).

scalar_universe_without(Excluded, Ranges) :-
    range_without_points(0, 0xd7ff, Excluded, BeforeSurrogates),
    range_without_points(0xe000, 0x10ffff, Excluded, AfterSurrogates),
    append(BeforeSurrogates, AfterSurrogates, Ranges).

range_without_points(Low, High, Excluded, Ranges) :-
    include(integer_between(Low, High), Excluded, Inside),
    ranges_between_points(Low, High, Inside, Ranges).

integer_between(Low, High, Value) :-
    Value >= Low,
    Value =< High.

ranges_between_points(Start, High, [], Ranges) :-
    ( Start =< High -> Ranges = [range(Start, High)] ; Ranges = [] ).
ranges_between_points(Start, High, [Point|Points], Ranges) :-
    BeforeHigh is Point - 1,
    NextStart is Point + 1,
    ranges_between_points(NextStart, High, Points, More),
    ( Start =< BeforeHigh -> Ranges = [range(Start, BeforeHigh)|More]
    ; Ranges = More
    ).

normalize_ranges(RawRanges, Ranges) :-
    sort(RawRanges, Sorted),
    merge_ranges(Sorted, Ranges).

merge_ranges([], []).
merge_ranges([range(Low, High)|Ranges], Merged) :-
    merge_ranges(Ranges, Low, High, Merged).

merge_ranges([], Low, High, [range(Low, High)]).
merge_ranges([range(NextLow, NextHigh)|Ranges], Low, High, Merged) :-
    ( NextLow =< High + 1 ->
        CombinedHigh is max(High, NextHigh),
        merge_ranges(Ranges, Low, CombinedHigh, Merged)
    ; Merged = [range(Low, High)|More],
      merge_ranges(Ranges, NextLow, NextHigh, More)
    ).

parser_pack_class_member(Pack, Class,
                         Codepoint) :-
    parser_pack_class_ranges(Pack, Class, Ranges),
    parser_pack_scalar_in_ranges(Codepoint, Ranges).

parser_pack_scalar_in_ranges(
    list([sym(cp), int(Codepoint)]), Ranges) :-
    unicode_scalar_node(list([sym(cp), int(Codepoint)])),
    member(range(Low, High), Ranges),
    Codepoint >= Low,
    Codepoint =< High,
    !.

unicode_scalar_node(list([sym(cp), int(Codepoint)])) :-
    integer(Codepoint),
    Codepoint >= 0,
    Codepoint =< 0x10ffff,
    \+ (Codepoint >= 0xd800, Codepoint =< 0xdfff).

parser_pack_digest(parser_pack_v1(Productions, ClassClauses), Digest) :-
    node_list(Productions, ProductionNode),
    node_list(ClassClauses, ClassNode),
    Canonical = list([sym('parser-pack-v1'), ProductionNode, ClassNode]),
    render_term(Canonical, Payload),
    crypto_data_hash(Payload, Digest, [algorithm(sha256)]).

node_list([], sym(nil)).
node_list([Node|Nodes], list([sym(cons), Node, More])) :-
    node_list(Nodes, More).

parser_pack_missing_states(
    parser_pack_v1(Productions, _), StartStates, Missing) :-
    sort(StartStates, Agenda),
    pack_missing_states_agenda(
        Agenda, Productions, [], [], RawMissing),
    sort(RawMissing, Missing).

pack_missing_states_agenda([], _, _, Missing, Missing).
pack_missing_states_agenda(
    [State|Agenda], Productions, Seen, Missing0, Missing) :-
    ( memberchk(State, Seen) ->
        pack_missing_states_agenda(
            Agenda, Productions, Seen, Missing0, Missing)
    ; pack_state_targets(Productions, State, Defined, Targets),
      ( Defined == true ->
          append(Targets, Agenda, NextAgenda),
          Missing1 = Missing0
      ; NextAgenda = Agenda,
        Missing1 = [State|Missing0]
      ),
      pack_missing_states_agenda(
          NextAgenda, Productions, [State|Seen], Missing1, Missing)
    ).

pack_state_targets(Productions, State, Defined, Targets) :-
    ( member(Production, Productions),
      pack_production_state(Production, State) ->
        Defined = true,
        findall(Target,
                ( member(StateProduction, Productions),
                  pack_production_state(StateProduction, State),
                  pack_production_nonterminal(StateProduction, Target)
                ),
                RawTargets),
        sort(RawTargets, Targets)
    ; Defined = false,
      Targets = []
    ).

pack_production_state(
    list([sym('pp-production'), _, State, _, _]), State).

pack_production_nonterminal(
    list([sym('pp-production'), _, _, Items, _]), Target) :-
    pack_items_nonterminal(Items, Target).

pack_items_nonterminal(
    list([sym('pp-items-cons'),
          list([sym('pp-nonterminal'), Target]), _]), Target).
pack_items_nonterminal(
    list([sym('pp-items-cons'), _, More]), Target) :-
    pack_items_nonterminal(More, Target).

parser_pack_parse_results(Pack, SourcePresentations, Start, Input, MaxDepth,
                          Outcome) :-
    must_be(integer, MaxDepth),
    MaxDepth > 0,
    ground(Start),
    ground(Input),
    catch(
        findall(result(Value, Rest),
                pack_parse_state(Pack, SourcePresentations, Start, Input,
                                 Value, Rest, MaxDepth),
                RawResults),
        parser_pack_depth_exhausted,
        RawResults = parser_pack_depth_exhausted),
    ( RawResults == parser_pack_depth_exhausted ->
        Outcome = resource_exhausted(depth)
    ; parser_pack_canonical_results(RawResults, Results),
      Outcome = completed(Results)
    ).

pack_parse_state(_, _, _, _, _, _, 0) :-
    throw(parser_pack_depth_exhausted).
pack_parse_state(Pack, SourcePresentations,
                 State, Input, Value, Rest, Depth) :-
    Pack = parser_pack_v1(Productions, _),
    member(list([sym('pp-production'), _, State, Items, Action]),
           Productions),
    NextDepth is Depth - 1,
    pack_parse_items(Pack, SourcePresentations, Items, Input, Rest,
                     ChildValues, NextDepth),
    parser_pack_apply_action(Action, ChildValues, Value).

pack_parse_items(_, _, sym('pp-items-nil'), Input, Input, [], _).
pack_parse_items(
    Pack, SourcePresentations,
    list([sym('pp-items-cons'), Item, More]),
    Input, Rest, [Value|Values], Depth) :-
    pack_parse_item(Pack, SourcePresentations, Item, Input, Value, Middle,
                    Depth),
    pack_parse_items(Pack, SourcePresentations, More, Middle, Rest, Values,
                     Depth).

pack_parse_item(Pack, SourcePresentations,
                list([sym('pp-nonterminal'), State]),
                Input, Value, Rest, Depth) :-
    pack_parse_state(Pack, SourcePresentations, State, Input, Value, Rest,
                     Depth).
pack_parse_item(Pack, _SourcePresentations,
                list([sym('pp-terminal'), Matcher]),
                Input, Value, Rest, Depth) :-
    match_pack_terminal(Pack, Matcher, Input, Value, Rest, Depth).

match_pack_terminal(_, sym('pp-terminal-any'),
                    list([sym(cons), Codepoint, Rest]), Codepoint, Rest,
                    _).
match_pack_terminal(_, sym('pp-terminal-eof'),
                    sym(nil), sym(eof), sym(nil), _).
match_pack_terminal(_, list([sym('pp-terminal-char'), Codepoint]),
                    list([sym(cons), Codepoint, Rest]), Codepoint, Rest, _).
match_pack_terminal(Pack, list([sym('pp-terminal-class'), Class]),
                    list([sym(cons), Codepoint, Rest]), Codepoint, Rest,
                    _) :-
    parser_pack_class_member(Pack, Class, Codepoint).
