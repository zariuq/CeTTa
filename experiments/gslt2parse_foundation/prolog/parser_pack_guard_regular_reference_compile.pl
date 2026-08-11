:- module(parser_pack_guard_regular_reference_compile,
          [ parser_pack_guard_regular_reference_compile/4,
            parser_pack_guard_regular_program/4,
            parser_pack_guard_nfa_bijection/3,
            parser_pack_guard_nfa_tags/2
          ]).

:- use_module(finite_horn_eval).
:- use_module(finite_horn_gslt_v1).
:- use_module(parser_pack_eval, [parser_pack_canonical_terms/2]).
:- use_module(parser_pack_guard_reference_compile).

parser_pack_guard_regular_reference_compile(
    PresentationRoot, SourceRelativePaths, MaxDepth, Outcome) :-
    must_be(integer, MaxDepth),
    MaxDepth > 0,
    catch(
        parser_pack_guard_regular_reference_compile_(
            PresentationRoot, SourceRelativePaths, MaxDepth, Outcome),
        Error,
        Outcome = invalid_presentation(Error)).

parser_pack_guard_regular_reference_compile_(
    PresentationRoot, SourceRelativePaths, MaxDepth, Outcome) :-
    parser_pack_guard_regular_program(
        PresentationRoot, SourceRelativePaths,
        SourcePresentations, Program),
    admit_presentations(Program),
    GuardQuery = list([sym('compile-pack-positive-guard'),
                       var(owner), var(guard)]),
    NFAQuery = list([sym('compile-span-nfa'),
                     list([sym('pp-positive-guard-tag'), var(state)]),
                     var(edge)]),
    horn_query(Program, GuardQuery, MaxDepth, GuardOutcome),
    ( GuardOutcome = resource_exhausted(Reason) ->
        Outcome = resource_exhausted(guard(Reason))
    ; GuardOutcome = completed(GuardAnswers),
      horn_query(Program, NFAQuery, MaxDepth, NFAOutcome),
      ( NFAOutcome = resource_exhausted(Reason) ->
          Outcome = resource_exhausted(nfa(Reason))
      ; NFAOutcome = completed(NFAAnswers),
        ( forall(member(answer(Answer, Proof), GuardAnswers),
                 ( horn_replay(Program, Answer, Proof),
                   guard_answer_valid(Answer)
                 )),
          forall(member(answer(Answer, Proof), NFAAnswers),
                 ( horn_replay(Program, Answer, Proof),
                   guard_nfa_answer_valid(Answer)
                 )),
          parser_pack_guard_nfa_bijection(
              GuardAnswers, NFAAnswers, Tags) ->
            Outcome = completed(parser_pack_guard_regular_compilation(
                SourcePresentations, Program,
                GuardAnswers, NFAAnswers, Tags))
        ; Outcome = invalid_certificate(guard_regular_derivation)
        )
      )
    ).

guard_answer_valid(
    list([sym('compile-pack-positive-guard'), _Owner, Artifact])) :-
    parser_pack_guard_artifact_valid(Artifact).

guard_nfa_answer_valid(
    list([sym('compile-span-nfa'), Tag, Edge])) :-
    Tag = list([sym('pp-positive-guard-tag'), _State]),
    ground(Tag),
    guard_nfa_edge_valid(Tag, Edge),
    ground(Edge).

guard_nfa_edge_valid(_, list([sym('nfa-start'), _State])).
guard_nfa_edge_valid(Tag,
                     list([sym('nfa-accept'), _State, Tag])).
guard_nfa_edge_valid(_, list([sym('nfa-epsilon'), _From, _To])).
guard_nfa_edge_valid(_, list([sym('nfa-any'), _From, _To])).
guard_nfa_edge_valid(_, list([sym('nfa-eof'), _From, _To])).
guard_nfa_edge_valid(_, list([sym('nfa-char'), _From, _Codepoint, _To])).
guard_nfa_edge_valid(_, list([sym('nfa-class'), _From, _Class, _To])).

parser_pack_guard_nfa_bijection(GuardAnswers, NFAAnswers, Tags) :-
    findall(Tag,
            ( member(answer(Answer, _), GuardAnswers),
              guard_answer_tag(Answer, Tag)
            ),
            RawGuardTags),
    parser_pack_canonical_terms(RawGuardTags, GuardTags),
    same_length(RawGuardTags, GuardTags),
    parser_pack_guard_nfa_tags(NFAAnswers, NFATags),
    GuardTags == NFATags,
    Tags = GuardTags,
    forall(member(Tag, Tags),
           ( nfa_tag_has_start(NFAAnswers, Tag),
             nfa_tag_has_accept(NFAAnswers, Tag)
           )).

guard_answer_tag(
    list([sym('compile-pack-positive-guard'), _Owner,
          list([sym('pp-positive-guard-v1'),
                _State, Tag, _Body, _Production])]),
    Tag).

parser_pack_guard_nfa_tags(NFAAnswers, Tags) :-
    findall(Tag,
            ( member(answer(Answer, _), NFAAnswers),
              nfa_answer_tag(Answer, Tag)
            ),
            RawTags),
    parser_pack_canonical_terms(RawTags, Tags).

nfa_answer_tag(
    list([sym('compile-span-nfa'), Tag, _Edge]), Tag).

nfa_tag_has_start(NFAAnswers, Tag) :-
    member(answer(
               list([sym('compile-span-nfa'), Tag,
                     list([sym('nfa-start'), _State])]), _),
           NFAAnswers),
    !.

nfa_tag_has_accept(NFAAnswers, Tag) :-
    member(answer(
               list([sym('compile-span-nfa'), Tag,
                     list([sym('nfa-accept'), _State, Tag])]), _),
           NFAAnswers),
    !.

parser_pack_guard_regular_program(
    PresentationRoot, SourceRelativePaths,
    SourcePresentations, Program) :-
    parser_pack_guard_reference_program(
        PresentationRoot, SourceRelativePaths,
        SourcePresentations, GuardProgram),
    append(WithoutReflection, [Reflected], GuardProgram),
    Reflected = presentation(_, _, _, reflected(_)),
    append(GuardCompiler, SourcePresentations, WithoutReflection),
    !,
    guard_regular_relative_paths(RegularRelativePaths),
    maplist(presentation_path(PresentationRoot),
            RegularRelativePaths, RegularPaths),
    maplist(read_presentation, RegularPaths, RegularPresentations),
    append(GuardCompiler, RegularPresentations, Compiler),
    append(Compiler, SourcePresentations, PreReflection),
    append(PreReflection, [Reflected], Program).

guard_regular_relative_paths(
    [ 'compiler/regular_span_compiler_v1.metta',
      'compiler/parser_pack_guard_regular_link_v1.metta'
    ]).

presentation_path(Root, Relative, Path) :-
    directory_file_path(Root, Relative, Path).
