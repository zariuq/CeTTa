:- module(parser_pack_glr,
          [ parser_pack_glr_compile/4,
            parser_pack_glr_parse/7,
            parser_pack_glr_parse_compiled/5,
            parser_pack_glr_table_summary/2
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
:- use_module(library(assoc)).
:- use_module(library(gensym)).
:- use_module(library(ordsets)).
:- use_module(library(pairs)).

/*
  Grammar-generic SLR/GLR over the logical ParserPack production fragment.
  Conflicting SLR actions are retained and explored with a graph-structured
  stack.  Explicit ParserPack EOF terminals are zero-width shifts at the final
  source position; the LR end marker remains a distinct internal symbol.
*/

:- dynamic pglr_stack_node/3.
:- dynamic pglr_stack_edge/6.
:- dynamic pglr_forest_node/3.
:- dynamic pglr_choice/8.
:- dynamic pglr_input/3.
:- dynamic pglr_input_length/2.
:- dynamic pglr_pack/2.
:- dynamic pglr_class_plan/3.
:- dynamic pglr_fact_count/2.
:- dynamic pglr_fact_limit/2.
:- dynamic pglr_farthest/2.
:- dynamic pglr_expectation/3.

parser_pack_glr_compile(Pack, Start, StateLimit, Outcome) :-
    must_be(integer, StateLimit),
    StateLimit > 0,
    ( parser_pack_validate(Pack) ->
        parser_pack_missing_states(Pack, [Start], Missing),
        ( Missing == [] ->
            catch(
                ( build_glr_table(Pack, Start, StateLimit, Table),
                  terminal_matchers(Pack, Matchers),
                  Outcome = completed(
                      parser_pack_glr_plan_v1(Pack, Start, Table, Matchers))
                ),
                parser_pack_glr_resource(Reason),
                Outcome = resource_exhausted(Reason))
        ; Outcome = unsupported(missing_states(Missing))
        )
    ; Outcome = invalid_presentation(parser_pack)
    ).

parser_pack_glr_parse(Pack, SourcePresentations, Start, Input,
                      StateLimit, FactLimit, Outcome) :-
    parser_pack_glr_compile(Pack, Start, StateLimit, CompileOutcome),
    ( CompileOutcome = completed(Plan) ->
        parser_pack_glr_parse_compiled(
            Plan, SourcePresentations, Input, FactLimit, Outcome)
    ; Outcome = CompileOutcome
    ).

parser_pack_glr_parse_compiled(Plan, SourcePresentations, Input, FactLimit,
                               Outcome) :-
    must_be(integer, FactLimit),
    FactLimit > 0,
    Plan = parser_pack_glr_plan_v1(Pack, Start, Table, Matchers),
    ( parser_pack_input_codepoints(Input, InputValues) ->
        run_glr(Pack, SourcePresentations, Start, Table, Matchers,
                Input, InputValues, FactLimit, Outcome)
    ; Outcome = invalid_input(codepoint_list)
    ).

parser_pack_glr_table_summary(
    parser_pack_glr_plan_v1(_, _,
                            glr_table(Productions, _, States,
                                      ActionAssoc, GotoAssoc, Conflicts), _),
    glr_table_summary(ProductionCount, StateCount, ActionCount, GotoCount,
                      Conflicts)) :-
    length(Productions, ProductionCount),
    length(States, StateCount),
    assoc_to_list(ActionAssoc, ActionPairs),
    findall(Action,
            ( member(_-Bucket, ActionPairs), member(Action, Bucket) ),
            Actions),
    length(Actions, ActionCount),
    assoc_to_list(GotoAssoc, GotoPairs),
    length(GotoPairs, GotoCount).

/* ---------- ParserPack to SLR table ------------------------------------ */

build_glr_table(Pack, Start, StateLimit,
                glr_table(Productions, ProductionAssoc, States,
                          ActionAssoc, GotoAssoc, ConflictCount)) :-
    normalize_productions(Pack, Start, Productions, ProductionAssoc,
                          LhsIndex),
    compute_nullable_first_follow(
        Productions, Start, _Nullable, _FirstAssoc, FollowAssoc),
    build_lr0_states(ProductionAssoc, LhsIndex, StateLimit,
                     States, Transitions),
    build_lr_actions(ProductionAssoc, States, Transitions,
                     FollowAssoc, ActionAssoc, GotoAssoc),
    action_conflict_count(ActionAssoc, ConflictCount).

normalize_productions(parser_pack_v1(Packed, _), Start,
                      [Augmented|Productions], ProductionAssoc, LhsIndex) :-
    Augmented = production(0, '$parser-glr-accept',
                           '$parser-glr-start', [nt(Start)], 1),
    normalize_pack_productions(Packed, 1, Productions),
    empty_assoc(ProductionAssoc0),
    empty_assoc(LhsIndex0),
    index_productions([Augmented|Productions],
                      ProductionAssoc0, ProductionAssoc,
                      LhsIndex0, LhsIndex).

normalize_pack_productions([], _, []).
normalize_pack_productions(
    [list([sym('pp-production'), Label, State, ItemsNode, _])|Packed],
    Index,
    [production(Index, Label, State, Symbols, Length)|Productions]) :-
    normalize_pack_items(ItemsNode, Symbols),
    length(Symbols, Length),
    Next is Index + 1,
    normalize_pack_productions(Packed, Next, Productions).

normalize_pack_items(sym('pp-items-nil'), []).
normalize_pack_items(
    list([sym('pp-items-cons'),
          list([sym('pp-terminal'), Matcher]), More]),
    [tm(Matcher)|Symbols]) :-
    normalize_pack_items(More, Symbols).
normalize_pack_items(
    list([sym('pp-items-cons'),
          list([sym('pp-nonterminal'), State]), More]),
    [nt(State)|Symbols]) :-
    normalize_pack_items(More, Symbols).

terminal_matchers(parser_pack_v1(Productions, _), Matchers) :-
    findall(Matcher,
            ( member(list([sym('pp-production'), _, _, Items, _]),
                     Productions),
              packed_item(Items, list([sym('pp-terminal'), Matcher]))
            ),
            RawMatchers),
    sort(RawMatchers, Matchers).

packed_item(list([sym('pp-items-cons'), Item, _]), Item).
packed_item(list([sym('pp-items-cons'), _, More]), Item) :-
    packed_item(More, Item).

index_productions([], ProductionAssoc, ProductionAssoc,
                  LhsIndex, LhsIndex).
index_productions([Production|Productions], ProductionAssoc0,
                  ProductionAssoc, LhsIndex0, LhsIndex) :-
    Production = production(Index, _, Lhs, _, _),
    put_assoc(Index, ProductionAssoc0, Production, ProductionAssoc1),
    ( get_assoc(Lhs, LhsIndex0, Existing) ->
        append(Existing, [Index], Indexes)
    ; Indexes = [Index]
    ),
    put_assoc(Lhs, LhsIndex0, Indexes, LhsIndex1),
    index_productions(Productions, ProductionAssoc1, ProductionAssoc,
                      LhsIndex1, LhsIndex).

build_lr0_states(ProductionAssoc, LhsIndex, StateLimit,
                 States, Transitions) :-
    ( StateLimit >= 1 -> true
    ; throw(parser_pack_glr_resource(table_states(StateLimit)))
    ),
    item_closure([item(0, 0)], ProductionAssoc, LhsIndex, Initial),
    empty_assoc(StateIndex0),
    put_assoc(Initial, StateIndex0, 0, StateIndex1),
    empty_assoc(IndexState0),
    put_assoc(0, IndexState0, Initial, IndexState1),
    empty_assoc(ClosureCache0),
    put_assoc([item(0, 0)], ClosureCache0, Initial, ClosureCache1),
    build_state_queue(0, 1, StateLimit,
                      StateIndex1, IndexState1,
                      ProductionAssoc, LhsIndex,
                      ClosureCache1, _,
                      [], RevTransitions, FinalIndexState),
    reverse(RevTransitions, Transitions),
    assoc_to_list(FinalIndexState, StatePairs),
    pairs_values(StatePairs, States).

build_state_queue(Index, Count, _, _, IndexState, _, _,
                  ClosureCache, ClosureCache,
                  Transitions, Transitions, IndexState) :-
    Index >= Count,
    !.
build_state_queue(Index, Count0, StateLimit,
                  StateIndex0, IndexState0,
                  ProductionAssoc, LhsIndex,
                  ClosureCache0, ClosureCache,
                  Transitions0, Transitions, IndexState) :-
    get_assoc(Index, IndexState0, Items),
    findall(Symbol-Shifted,
            ( member(item(ProductionIndex, Dot), Items),
              get_assoc(ProductionIndex, ProductionAssoc,
                        production(_, _, _, Rhs, _)),
              nth0(Dot, Rhs, Symbol),
              NextDot is Dot + 1,
              Shifted = item(ProductionIndex, NextDot)
            ),
            RawKernels),
    keysort(RawKernels, SortedKernels),
    group_pairs_by_key(SortedKernels, Kernels),
    add_state_transitions(Kernels, Index, StateLimit,
                          ProductionAssoc, LhsIndex,
                          Count0, Count1,
                          StateIndex0, StateIndex1,
                          IndexState0, IndexState1,
                          ClosureCache0, ClosureCache1,
                          Transitions0, Transitions1),
    Next is Index + 1,
    build_state_queue(Next, Count1, StateLimit,
                      StateIndex1, IndexState1,
                      ProductionAssoc, LhsIndex,
                      ClosureCache1, ClosureCache,
                      Transitions1, Transitions, IndexState).

add_state_transitions([], _, _, _, _, Count, Count,
                      StateIndex, StateIndex, IndexState, IndexState,
                      ClosureCache, ClosureCache,
                      Transitions, Transitions).
add_state_transitions([Symbol-RawKernel|Kernels], Source, StateLimit,
                      ProductionAssoc, LhsIndex,
                      Count0, Count,
                      StateIndex0, StateIndex,
                      IndexState0, IndexState,
                      ClosureCache0, ClosureCache,
                      Transitions0, Transitions) :-
    sort(RawKernel, Kernel),
    ( get_assoc(Kernel, ClosureCache0, TargetItems) ->
        ClosureCache1 = ClosureCache0
    ; item_closure(Kernel, ProductionAssoc, LhsIndex, TargetItems),
      put_assoc(Kernel, ClosureCache0, TargetItems, ClosureCache1)
    ),
    ( get_assoc(TargetItems, StateIndex0, Target) ->
        Count1 = Count0,
        StateIndex1 = StateIndex0,
        IndexState1 = IndexState0
    ; ( Count0 >= StateLimit ->
          throw(parser_pack_glr_resource(table_states(StateLimit)))
      ; true
      ),
      Target = Count0,
      Count1 is Count0 + 1,
      put_assoc(TargetItems, StateIndex0, Target, StateIndex1),
      put_assoc(Target, IndexState0, TargetItems, IndexState1)
    ),
    add_state_transitions(Kernels, Source, StateLimit,
                          ProductionAssoc, LhsIndex,
                          Count1, Count,
                          StateIndex1, StateIndex,
                          IndexState1, IndexState,
                          ClosureCache1, ClosureCache,
                          [transition(Source, Symbol, Target)|Transitions0],
                          Transitions).

item_closure(Seed, ProductionAssoc, LhsIndex, Closure) :-
    sort(Seed, Initial),
    empty_assoc(Seen0),
    closure_seed(Initial, Seen0, Seen),
    item_closure_agenda(Initial, Seen, ProductionAssoc, LhsIndex, Closure).

closure_seed([], Seen, Seen).
closure_seed([Item|Items], Seen0, Seen) :-
    put_assoc(Item, Seen0, true, Seen1),
    closure_seed(Items, Seen1, Seen).

item_closure_agenda([], Seen, _, _, Closure) :-
    assoc_to_keys(Seen, Closure).
item_closure_agenda([Item|Agenda0], Seen0, ProductionAssoc, LhsIndex,
                    Closure) :-
    ( item_next_symbol(Item, ProductionAssoc, nt(Nonterminal)) ->
        get_assoc(Nonterminal, LhsIndex, ProductionIndexes),
        closure_candidates(ProductionIndexes, Agenda0, Agenda,
                           Seen0, Seen)
    ; Agenda = Agenda0,
      Seen = Seen0
    ),
    item_closure_agenda(Agenda, Seen, ProductionAssoc, LhsIndex, Closure).

closure_candidates([], Agenda, Agenda, Seen, Seen).
closure_candidates([ProductionIndex|ProductionIndexes],
                   Agenda0, Agenda, Seen0, Seen) :-
    Item = item(ProductionIndex, 0),
    ( get_assoc(Item, Seen0, _) ->
        Agenda1 = Agenda0,
        Seen1 = Seen0
    ; put_assoc(Item, Seen0, true, Seen1),
      Agenda1 = [Item|Agenda0]
    ),
    closure_candidates(ProductionIndexes, Agenda1, Agenda, Seen1, Seen).

item_next_symbol(item(ProductionIndex, Dot), ProductionAssoc, Symbol) :-
    get_assoc(ProductionIndex, ProductionAssoc,
              production(_, _, _, Rhs, _)),
    nth0(Dot, Rhs, Symbol).

/* ---------- Nullable, FIRST, and FOLLOW -------------------------------- */

compute_nullable_first_follow(Productions, Start,
                              Nullable, FirstAssoc, FollowAssoc) :-
    production_nonterminals(Productions, Nonterminals),
    nullable_worklist(Productions, Nullable),
    empty_sets_assoc(Nonterminals, EmptyFirst),
    first_worklist(Productions, Nullable, EmptyFirst, FirstAssoc),
    empty_sets_assoc(Nonterminals, EmptyFollow),
    follow_worklist(Productions, Start, Nullable, FirstAssoc,
                    EmptyFollow, FollowAssoc).

production_nonterminals(Productions, Nonterminals) :-
    findall(Nonterminal,
            ( member(production(_, _, Lhs, Rhs, _), Productions),
              ( Nonterminal = Lhs
              ; member(nt(Nonterminal), Rhs)
              )
            ),
            Raw),
    sort(Raw, Nonterminals).

nullable_worklist(Productions, Nullable) :-
    empty_assoc(Dependencies0),
    foldl(add_nullable_dependencies, Productions,
          Dependencies0, Dependencies),
    findall(Lhs,
            member(production(_, _, Lhs, [], _), Productions),
            RawSeeds),
    sort(RawSeeds, Seeds),
    empty_assoc(Seen0),
    add_nullable_seeds(Seeds, Seen0, Seen, [], Agenda),
    nullable_agenda(Agenda, Dependencies, Seen, FinalSeen),
    assoc_to_keys(FinalSeen, Nullable).

add_nullable_dependencies(production(_, _, Lhs, Rhs, _), Assoc0, Assoc) :-
    findall(Nonterminal, member(nt(Nonterminal), Rhs), RawNonterminals),
    sort(RawNonterminals, Nonterminals),
    add_nullable_dependency_keys(
        Nonterminals, production(Lhs, Rhs), Assoc0, Assoc).

add_nullable_dependency_keys([], _, Assoc, Assoc).
add_nullable_dependency_keys([Key|Keys], Production, Assoc0, Assoc) :-
    assoc_prepend(Assoc0, Key, Production, Assoc1),
    add_nullable_dependency_keys(Keys, Production, Assoc1, Assoc).

add_nullable_seeds([], Seen, Seen, Agenda, Agenda).
add_nullable_seeds([Category|Categories], Seen0, Seen, Agenda0, Agenda) :-
    ( get_assoc(Category, Seen0, _) ->
        Seen1 = Seen0,
        Agenda1 = Agenda0
    ; put_assoc(Category, Seen0, true, Seen1),
      Agenda1 = [Category|Agenda0]
    ),
    add_nullable_seeds(Categories, Seen1, Seen, Agenda1, Agenda).

nullable_agenda([], _, Seen, Seen).
nullable_agenda([Category|Agenda0], Dependencies, Seen0, Seen) :-
    ( get_assoc(Category, Dependencies, Affected) -> true
    ; Affected = []
    ),
    admit_nullable_productions(Affected, Seen0, Seen1, Agenda0, Agenda),
    nullable_agenda(Agenda, Dependencies, Seen1, Seen).

admit_nullable_productions([], Seen, Seen, Agenda, Agenda).
admit_nullable_productions([production(Lhs, Rhs)|Productions],
                           Seen0, Seen, Agenda0, Agenda) :-
    ( \+ get_assoc(Lhs, Seen0, _), rhs_nullable_assoc(Rhs, Seen0) ->
        put_assoc(Lhs, Seen0, true, Seen1),
        Agenda1 = [Lhs|Agenda0]
    ; Seen1 = Seen0,
      Agenda1 = Agenda0
    ),
    admit_nullable_productions(
        Productions, Seen1, Seen, Agenda1, Agenda).

rhs_nullable_assoc([], _).
rhs_nullable_assoc([nt(Nonterminal)|Symbols], Seen) :-
    get_assoc(Nonterminal, Seen, _),
    rhs_nullable_assoc(Symbols, Seen).

rhs_nullable([], _).
rhs_nullable([nt(Nonterminal)|Symbols], Nullable) :-
    memberchk(Nonterminal, Nullable),
    rhs_nullable(Symbols, Nullable).

empty_sets_assoc(Names, Assoc) :-
    empty_assoc(Empty),
    foldl(put_empty_set, Names, Empty, Assoc).

put_empty_set(Name, Assoc0, Assoc) :-
    put_assoc(Name, Assoc0, [], Assoc).

first_worklist(Productions, Nullable, First0, First) :-
    findall(Lhs-Terminal,
            ( member(production(_, _, Lhs, Rhs, _), Productions),
              rhs_first_source(Rhs, Nullable, terminal(Terminal))
            ),
            Direct),
    findall(Source-Lhs,
            ( member(production(_, _, Lhs, Rhs, _), Productions),
              rhs_first_source(Rhs, Nullable, nonterminal(Source))
            ),
            Edges),
    dependency_assoc(Edges, Dependencies),
    propagate_pairs(Direct, Dependencies, First0, First).

rhs_first_source([tm(Terminal)|_], _, terminal(Terminal)).
rhs_first_source([nt(Nonterminal)|_], _, nonterminal(Nonterminal)).
rhs_first_source([nt(Nonterminal)|Symbols], Nullable, Source) :-
    memberchk(Nonterminal, Nullable),
    rhs_first_source(Symbols, Nullable, Source).

rhs_first([], _, _, [], true).
rhs_first([tm(Terminal)|_], _, _, [Terminal], false) :- !.
rhs_first([nt(Nonterminal)|Symbols], Nullable, FirstAssoc,
          First, IsNullable) :-
    get_assoc(Nonterminal, FirstAssoc, HeadFirst),
    ( memberchk(Nonterminal, Nullable) ->
        rhs_first(Symbols, Nullable, FirstAssoc, TailFirst, IsNullable),
        ord_union(HeadFirst, TailFirst, First)
    ; First = HeadFirst,
      IsNullable = false
    ).

follow_worklist(Productions, Start, Nullable, FirstAssoc,
                Follow0, Follow) :-
    findall(Nonterminal-Terminal,
            ( member(production(_, _, _, Rhs, _), Productions),
              rhs_occurrence(Rhs, Nonterminal, Suffix),
              rhs_first(Suffix, Nullable, FirstAssoc, SuffixFirst, _),
              member(Terminal, SuffixFirst)
            ),
            Direct),
    findall(Lhs-Nonterminal,
            ( member(production(_, _, Lhs, Rhs, _), Productions),
              rhs_occurrence(Rhs, Nonterminal, Suffix),
              rhs_nullable(Suffix, Nullable)
            ),
            Edges),
    dependency_assoc(Edges, Dependencies),
    propagate_pairs([Start-'$parser-glr-end'|Direct],
                    Dependencies, Follow0, Follow).

rhs_occurrence([nt(Nonterminal)|Suffix], Nonterminal, Suffix).
rhs_occurrence([_|Symbols], Nonterminal, Suffix) :-
    rhs_occurrence(Symbols, Nonterminal, Suffix).

dependency_assoc(Pairs, Assoc) :-
    empty_assoc(Empty),
    foldl(add_dependency_pair, Pairs, Empty, Assoc).

add_dependency_pair(Source-Target, Assoc0, Assoc) :-
    assoc_prepend(Assoc0, Source, Target, Assoc).

assoc_prepend(Assoc0, Key, Value, Assoc) :-
    ( get_assoc(Key, Assoc0, Existing) -> true ; Existing = [] ),
    ( memberchk(Value, Existing) -> Values = Existing
    ; Values = [Value|Existing]
    ),
    put_assoc(Key, Assoc0, Values, Assoc).

propagate_pairs(RawSeeds, Dependencies, Sets0, Sets) :-
    sort(RawSeeds, Seeds),
    empty_assoc(Seen0),
    add_propagation_seeds(Seeds, Seen0, Seen,
                          Sets0, Sets1, [], Agenda),
    propagation_agenda(Agenda, Dependencies, Seen, Sets1, Sets).

add_propagation_seeds([], Seen, Seen, Sets, Sets, Agenda, Agenda).
add_propagation_seeds([Source-Value|Pairs], Seen0, Seen,
                      Sets0, Sets, Agenda0, Agenda) :-
    add_propagation_pair(Source, Value, Seen0, Seen1,
                         Sets0, Sets1, Agenda0, Agenda1),
    add_propagation_seeds(Pairs, Seen1, Seen, Sets1, Sets,
                          Agenda1, Agenda).

add_propagation_pair(Source, Value, Seen0, Seen,
                     Sets0, Sets, Agenda0, Agenda) :-
    Pair = Source-Value,
    ( get_assoc(Pair, Seen0, _) ->
        Seen = Seen0,
        Sets = Sets0,
        Agenda = Agenda0
    ; put_assoc(Pair, Seen0, true, Seen),
      assoc_add_set(Sets0, Source, [Value], Sets),
      Agenda = [Pair|Agenda0]
    ).

propagation_agenda([], _, _, Sets, Sets).
propagation_agenda([Source-Value|Agenda0], Dependencies,
                   Seen0, Sets0, Sets) :-
    ( get_assoc(Source, Dependencies, Targets) -> true ; Targets = [] ),
    propagate_targets(Targets, Value, Seen0, Seen1,
                      Sets0, Sets1, Agenda0, Agenda),
    propagation_agenda(Agenda, Dependencies, Seen1, Sets1, Sets).

propagate_targets([], _, Seen, Seen, Sets, Sets, Agenda, Agenda).
propagate_targets([Target|Targets], Value, Seen0, Seen,
                  Sets0, Sets, Agenda0, Agenda) :-
    add_propagation_pair(Target, Value, Seen0, Seen1,
                         Sets0, Sets1, Agenda0, Agenda1),
    propagate_targets(Targets, Value, Seen1, Seen,
                      Sets1, Sets, Agenda1, Agenda).

assoc_add_set(Assoc0, Key, Values, Assoc) :-
    get_assoc(Key, Assoc0, Existing),
    sort(Values, SortedValues),
    ord_union(Existing, SortedValues, Combined),
    put_assoc(Key, Assoc0, Combined, Assoc).

/* ---------- LR actions -------------------------------------------------- */

build_lr_actions(ProductionAssoc, States, Transitions,
                 FollowAssoc, ActionAssoc, GotoAssoc) :-
    empty_assoc(Action0),
    empty_assoc(Goto0),
    add_transition_entries(Transitions, Action0, Action1,
                           Goto0, GotoAssoc),
    add_completed_entries(0, States, ProductionAssoc, FollowAssoc,
                          Action1, ActionAssoc).

add_transition_entries([], Action, Action, Goto, Goto).
add_transition_entries([transition(Source, tm(Terminal), Target)|Rest],
                       Action0, Action, Goto0, Goto) :-
    add_action(Action0, Source, Terminal, shift(Target), Action1),
    add_transition_entries(Rest, Action1, Action, Goto0, Goto).
add_transition_entries([transition(Source, nt(Nonterminal), Target)|Rest],
                       Action0, Action, Goto0, Goto) :-
    put_assoc(Source-Nonterminal, Goto0, Target, Goto1),
    add_transition_entries(Rest, Action0, Action, Goto1, Goto).

add_completed_entries(_, [], _, _, Action, Action).
add_completed_entries(StateIndex, [Items|States],
                      ProductionAssoc, FollowAssoc, Action0, Action) :-
    foldl(add_completed_item(StateIndex, ProductionAssoc, FollowAssoc),
          Items, Action0, Action1),
    Next is StateIndex + 1,
    add_completed_entries(Next, States, ProductionAssoc, FollowAssoc,
                          Action1, Action).

add_completed_item(State, ProductionAssoc, _, item(0, Dot),
                   Action0, Action) :-
    get_assoc(0, ProductionAssoc, production(0, _, _, _, Length)),
    Dot =:= Length,
    !,
    add_action(Action0, State, '$parser-glr-end', accept, Action).
add_completed_item(State, ProductionAssoc, FollowAssoc,
                   item(ProductionIndex, Dot), Action0, Action) :-
    ProductionIndex =\= 0,
    get_assoc(ProductionIndex, ProductionAssoc,
              production(_, _, Lhs, _, Length)),
    Dot =:= Length,
    !,
    get_assoc(Lhs, FollowAssoc, Lookaheads),
    foldl(add_reduce_action(State, ProductionIndex),
          Lookaheads, Action0, Action).
add_completed_item(_, _, _, _, Action, Action).

add_reduce_action(State, ProductionIndex, Lookahead, Action0, Action) :-
    add_action(Action0, State, Lookahead,
               reduce(ProductionIndex), Action).

add_action(Assoc0, State, Lookahead, Action, Assoc) :-
    Key = State-Lookahead,
    ( get_assoc(Key, Assoc0, Existing) -> true ; Existing = [] ),
    sort([Action|Existing], Actions),
    put_assoc(Key, Assoc0, Actions, Assoc).

action_conflict_count(ActionAssoc, Count) :-
    assoc_to_list(ActionAssoc, Pairs),
    include(conflicting_action_pair, Pairs, Conflicts),
    length(Conflicts, Count).

conflicting_action_pair(_-[_,_|_]).

/* ---------- GLR graph-structured stack --------------------------------- */

run_glr(Pack, SourcePresentations, Start, Table, Matchers,
        Input, InputValues, FactLimit, Outcome) :-
    gensym(parser_pack_glr_session_, Session),
    setup_call_cleanup(
        prepare_session(Session, Pack, SourcePresentations,
                        InputValues, FactLimit),
        catch(
            ( add_stack_node(Session, 0, 0),
              length(InputValues, InputLength),
              glr_positions(Session, Table, Matchers,
                            0, InputLength, ParseStatus),
              collect_result(Session, Pack, SourcePresentations,
                             Start, Input, ParseStatus, Outcome)
            ),
            parser_pack_glr_resource(Reason),
            Outcome = resource_exhausted(Reason)),
        cleanup_session(Session)).

prepare_session(Session, Pack, _SourcePresentations,
                InputValues, FactLimit) :-
    assertz(pglr_pack(Session, Pack)),
    assertz(pglr_fact_count(Session, 0)),
    assertz(pglr_fact_limit(Session, FactLimit)),
    assertz(pglr_farthest(Session, 0)),
    prepare_input(Session, InputValues, 0),
    length(InputValues, InputLength),
    assertz(pglr_input_length(Session, InputLength)).

prepare_input(_, [], _).
prepare_input(Session, [Value|Values], Position) :-
    assertz(pglr_input(Session, Position, Value)),
    Next is Position + 1,
    prepare_input(Session, Values, Next).

cleanup_session(Session) :-
    retractall(pglr_stack_node(Session, _, _)),
    retractall(pglr_stack_edge(Session, _, _, _, _, _)),
    retractall(pglr_forest_node(Session, _, _)),
    retractall(pglr_choice(Session, _, _, _, _, _, _, _)),
    retractall(pglr_input(Session, _, _)),
    retractall(pglr_input_length(Session, _)),
    retractall(pglr_pack(Session, _)),
    retractall(pglr_class_plan(Session, _, _)),
    retractall(pglr_fact_count(Session, _)),
    retractall(pglr_fact_limit(Session, _)),
    retractall(pglr_farthest(Session, _)),
    retractall(pglr_expectation(Session, _, _)).

glr_positions(Session, Table, Matchers, Position, InputLength, Status) :-
    touch_position(Session, Position),
    input_lookaheads(Session, Matchers, Position, InputLength,
                     Lookaheads, Consuming, ZeroWidth),
    saturate_closure(Session, Table, Position, Lookaheads, ZeroWidth),
    record_expectations(Session, Table, Matchers, Position),
    ( Position =:= InputLength ->
        ( accepts(Session, Table, Position) -> Status = accepted
        ; supplement_rejection_expectations(
              Session, Table, Matchers, Position),
          Status = rejected
        )
    ; shift_consuming(Session, Table, Position, Consuming, NextNodes),
      ( NextNodes = [_|_] ->
          Next is Position + 1,
          glr_positions(Session, Table, Matchers,
                        Next, InputLength, Status)
      ; supplement_rejection_expectations(
            Session, Table, Matchers, Position),
        Status = rejected
      )
    ).

input_lookaheads(Session, Matchers, Position, InputLength,
                 Lookaheads, Consuming, ZeroWidth) :-
    ( Position =:= InputLength ->
        include(eof_matcher, Matchers, ZeroWidth),
        Consuming = [],
        append(ZeroWidth, ['$parser-glr-end'], RawLookaheads)
    ; pglr_input(Session, Position, Value),
      include(matches_value(Session, Value), Matchers, Consuming),
      ZeroWidth = [],
      RawLookaheads = Consuming
    ),
    sort(RawLookaheads, Lookaheads).

eof_matcher(sym('pp-terminal-eof')).

matches_value(_, _, sym('pp-terminal-any')).
matches_value(_, Value,
              list([sym('pp-terminal-char'), Value])).
matches_value(Session, Value,
              list([sym('pp-terminal-class'), Class])) :-
    class_member(Session, Class, Value).

class_member(Session, Class, Value) :-
    ( pglr_class_plan(Session, Class, Ranges) -> true
    ; pglr_pack(Session, Pack),
      parser_pack_class_ranges(Pack, Class, Ranges),
      assertz(pglr_class_plan(Session, Class, Ranges))
    ),
    parser_pack_scalar_in_ranges(Value, Ranges).

saturate_closure(Session, Table, Position, Lookaheads, ZeroWidth) :-
    pglr_fact_count(Session, Before),
    reduction_round(Session, Table, Position, Lookaheads),
    zero_width_shift_round(Session, Table, Position, ZeroWidth),
    pglr_fact_count(Session, After),
    ( After =:= Before -> true
    ; saturate_closure(Session, Table, Position, Lookaheads, ZeroWidth)
    ).

reduction_round(
    Session,
    glr_table(_, ProductionAssoc, _, ActionAssoc, GotoAssoc, _),
    Position, Lookaheads) :-
    findall(Top-ProductionIndex,
            ( pglr_stack_node(Session, State, Position),
              member(Lookahead, Lookaheads),
              action_bucket(ActionAssoc, State, Lookahead, Actions),
              member(reduce(ProductionIndex), Actions),
              Top = gss(State, Position)
            ),
            RawReductions),
    sort(RawReductions, Reductions),
    forall(member(Top-ProductionIndex, Reductions),
           apply_reduction(Session, ProductionAssoc, GotoAssoc,
                           Position, Top, ProductionIndex)).

apply_reduction(Session, ProductionAssoc, GotoAssoc,
                Position, Top, ProductionIndex) :-
    get_assoc(ProductionIndex, ProductionAssoc, Production),
    Production = production(_, _, Lhs, _, Length),
    forall(gss_path(Session, Top, Length, Predecessor, ReverseChildren),
           ( reverse(ReverseChildren, Children),
             build_reduction_node(Session, Production, Position,
                                  Children, SymbolNode),
             Predecessor = gss(PreviousState, _),
             get_assoc(PreviousState-Lhs, GotoAssoc, TargetState),
             Target = gss(TargetState, Position),
             add_stack_node(Session, TargetState, Position),
             add_stack_edge(Session, Target, Predecessor, SymbolNode)
           )).

gss_path(_, Top, 0, Top, []) :- !.
gss_path(Session, Top, Length, Predecessor, [Value|Values]) :-
    Length > 0,
    term_hash(Top, TopHash),
    pglr_stack_edge(Session, TopHash, _, Top, Next, Value),
    Remaining is Length - 1,
    gss_path(Session, Next, Remaining, Predecessor, Values).

build_reduction_node(Session,
                     production(_, Label, State, _, 0),
                     Position, [], Node) :-
    !,
    Epsilon = parser_epsilon(Position),
    add_forest_node(Session, Epsilon),
    Node = parser_symbol(State, Position, Position),
    add_forest_node(Session, Node),
    add_choice(Session, Node, Label, none, Epsilon, Position).
build_reduction_node(Session,
                     production(_, Label, State, _, Length),
                     _, Children, Node) :-
    length(Children, Length),
    build_reduction_children(Session, Label, State, Length,
                             Children, 1, none, Node).

build_reduction_children(Session, Label, State, Length,
                         [Child], Dot, Prefix, Node) :-
    !,
    reduction_parent(Label, State, Length, Dot, Prefix, Child, Node, Pivot),
    add_forest_node(Session, Node),
    add_choice(Session, Node, Label, Prefix, Child, Pivot).
build_reduction_children(Session, Label, State, Length,
                         [Child|Children], Dot, Prefix, Node) :-
    reduction_parent(Label, State, Length, Dot,
                     Prefix, Child, NextPrefix, Pivot),
    add_forest_node(Session, NextPrefix),
    add_choice(Session, NextPrefix, Label, Prefix, Child, Pivot),
    NextDot is Dot + 1,
    build_reduction_children(Session, Label, State, Length,
                             Children, NextDot, NextPrefix, Node).

reduction_parent(Label, State, Length, Dot, Prefix, Child, Parent, Pivot) :-
    parser_forest_node_extent(Child, Pivot, Right),
    ( Prefix == none -> Left = Pivot
    ; parser_forest_node_extent(Prefix, Left, _)
    ),
    ( Dot =:= Length -> Parent = parser_symbol(State, Left, Right)
    ; Parent = parser_intermediate(Label, Dot, Left, Right)
    ).

zero_width_shift_round(Session,
                       glr_table(_, _, _, ActionAssoc, _, _),
                       Position, ZeroWidth) :-
    findall(Target-Matcher-Top,
            ( pglr_stack_node(Session, State, Position),
              member(Matcher, ZeroWidth),
              action_bucket(ActionAssoc, State, Matcher, Actions),
              member(shift(Target), Actions),
              Top = gss(State, Position)
            ),
            RawShifts),
    sort(RawShifts, Shifts),
    forall(member(Target-Matcher-Top, Shifts),
           shift_edge(Session, Position, Position,
                      Target, Matcher, sym(eof), Top)).

shift_consuming(Session,
                glr_table(_, _, _, ActionAssoc, _, _),
                Position, Matchers, NextNodes) :-
    pglr_input(Session, Position, Value),
    findall(Target-Matcher-Top,
            ( pglr_stack_node(Session, State, Position),
              member(Matcher, Matchers),
              action_bucket(ActionAssoc, State, Matcher, Actions),
              member(shift(Target), Actions),
              Top = gss(State, Position)
            ),
            RawShifts),
    sort(RawShifts, Shifts),
    Next is Position + 1,
    forall(member(Target-Matcher-Top, Shifts),
           shift_edge(Session, Position, Next,
                      Target, Matcher, Value, Top)),
    findall(gss(State, Next),
            pglr_stack_node(Session, State, Next), RawNextNodes),
    sort(RawNextNodes, NextNodes).

shift_edge(Session, Left, Right, TargetState, Matcher, Value, Top) :-
    Terminal = parser_terminal(Matcher, Value, Left, Right),
    add_forest_node(Session, Terminal),
    NextTop = gss(TargetState, Right),
    add_stack_node(Session, TargetState, Right),
    add_stack_edge(Session, NextTop, Top, Terminal).

accepts(Session,
        glr_table(_, _, _, ActionAssoc, _, _), Position) :-
    once(( pglr_stack_node(Session, State, Position),
           action_bucket(ActionAssoc, State, '$parser-glr-end', Actions),
           memberchk(accept, Actions)
         )).

action_bucket(ActionAssoc, State, Lookahead, Actions) :-
    ( get_assoc(State-Lookahead, ActionAssoc, Actions) -> true
    ; Actions = []
    ).

record_expectations(Session,
                    glr_table(_, _, _, ActionAssoc, _, _),
                    Matchers, Position) :-
    forall(
        ( pglr_stack_node(Session, State, Position),
          member(Matcher, Matchers),
          action_bucket(ActionAssoc, State, Matcher, Actions),
          memberchk(shift(_), Actions)
        ),
        add_expectation(Session, Position, Matcher)).

supplement_rejection_expectations(Session, Table, Matchers, Position) :-
    forall(( member(Matcher, Matchers),
             diagnostic_matcher_expected(Session, Table, Position, Matcher)
           ),
           add_expectation(Session, Position, Matcher)).

diagnostic_matcher_expected(Session, Table, Position, Matcher) :-
    gensym(parser_pack_glr_diagnostic_, Diagnostic),
    setup_call_cleanup(
        clone_stack_for_diagnostic(Session, Diagnostic),
        ( saturate_diagnostic_reductions(
              Diagnostic, Table, Position, [Matcher]),
          Table = glr_table(_, _, _, ActionAssoc, _, _),
          once(( pglr_stack_node(Diagnostic, State, Position),
                 action_bucket(ActionAssoc, State, Matcher, Actions),
                 memberchk(shift(_), Actions)
               ))
        ),
        cleanup_session(Diagnostic)).

clone_stack_for_diagnostic(Session, Diagnostic) :-
    pglr_fact_limit(Session, Limit),
    assertz(pglr_fact_count(Diagnostic, 0)),
    assertz(pglr_fact_limit(Diagnostic, Limit)),
    forall(pglr_stack_node(Session, State, Position),
           assertz(pglr_stack_node(Diagnostic, State, Position))),
    forall(pglr_stack_edge(Session, TopHash, EdgeHash,
                           Top, Predecessor, Value),
           assertz(pglr_stack_edge(Diagnostic, TopHash, EdgeHash,
                                   Top, Predecessor, Value))).

saturate_diagnostic_reductions(Session, Table, Position, Lookaheads) :-
    pglr_fact_count(Session, Before),
    reduction_round(Session, Table, Position, Lookaheads),
    pglr_fact_count(Session, After),
    ( After =:= Before -> true
    ; saturate_diagnostic_reductions(
          Session, Table, Position, Lookaheads)
    ).

touch_position(Session, Position) :-
    retract(pglr_farthest(Session, Current)),
    ( Position > Current -> Farthest = Position ; Farthest = Current ),
    assertz(pglr_farthest(Session, Farthest)).

add_expectation(Session, Position, Matcher) :-
    ( pglr_expectation(Session, Position, Matcher) -> true
    ; assertz(pglr_expectation(Session, Position, Matcher))
    ).

add_stack_node(Session, State, Position) :-
    ( pglr_stack_node(Session, State, Position) -> true
    ; bump_fact_count(Session),
      assertz(pglr_stack_node(Session, State, Position))
    ).

add_stack_edge(Session, Top, Predecessor, Value) :-
    term_hash(Top, TopHash),
    Edge = edge(Top, Predecessor, Value),
    term_hash(Edge, EdgeHash),
    ( pglr_stack_edge(Session, TopHash, EdgeHash,
                      Top, Predecessor, Value) -> true
    ; bump_fact_count(Session),
      assertz(pglr_stack_edge(Session, TopHash, EdgeHash,
                              Top, Predecessor, Value))
    ).

add_forest_node(Session, Node) :-
    term_hash(Node, Hash),
    ( pglr_forest_node(Session, Hash, Node) -> true
    ; bump_fact_count(Session),
      assertz(pglr_forest_node(Session, Hash, Node))
    ).

add_choice(Session, Parent, Label, Prefix, Child, Pivot) :-
    term_hash(Parent, Hash),
    ( pglr_choice(Session, Hash, Parent, Label, Prefix, Child, Pivot,
                  source) -> true
    ; bump_fact_count(Session),
      assertz(pglr_choice(Session, Hash, Parent, Label, Prefix, Child, Pivot,
                          source))
    ).

bump_fact_count(Session) :-
    retract(pglr_fact_count(Session, Count)),
    pglr_fact_limit(Session, Limit),
    ( Count >= Limit ->
        assertz(pglr_fact_count(Session, Count)),
        throw(parser_pack_glr_resource(facts(Limit)))
    ; Next is Count + 1,
      assertz(pglr_fact_count(Session, Next))
    ).

/* ---------- Normalized result and canonical forest --------------------- */

collect_result(Session, Pack, SourcePresentations, Start, Input,
               ParseStatus,
               completed(parser_result(
                   decision(Decision),
                   coverage(farthest(Farthest), expected(Expectations),
                            productions(UsedLabels)),
                   ambiguity(roots(RootCount), packed_choices(ChoiceCount)),
                   Forest,
                   evidence(forest_certificate(Certificate))))) :-
    pglr_input_length(Session, InputLength),
    findall(Root,
            ( pglr_forest_node(Session, _, Root),
              Root = parser_symbol(Start, 0, End),
              End =< InputLength
            ),
            RawRoots),
    parser_pack_canonical_forest_nodes(RawRoots, Roots),
    ( ParseStatus == accepted,
      memberchk(parser_symbol(Start, 0, InputLength), Roots) ->
        Decision = accepted
    ; Decision = rejected
    ),
    reachable_forest(Session, Roots, Nodes, Choices),
    findall(Label, member(parser_choice(_, Label, _, _, _), Choices),
            RawUsedLabels),
    parser_pack_canonical_terms(RawUsedLabels, UsedLabels),
    pglr_farthest(Session, Farthest),
    findall(Matcher, pglr_expectation(Session, Farthest, Matcher),
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
              pglr_choice(Session, _, Node, Label, Prefix, Child, Pivot,
                          source),
              NodeChoices),
      parser_forest_choice_children(NodeChoices, Children),
      append(Children, Agenda, NextAgenda),
      append(NodeChoices, Choices0, NextChoices),
      reachable_agenda(Session, NextAgenda, [Node|Nodes0], NextChoices,
                       Nodes, Choices)
    ).
