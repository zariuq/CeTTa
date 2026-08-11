:- module(test_parser_pack_guard_regular_v1,
          [ parser_pack_guard_regular_rows/2
          ]).

:- use_module(finite_horn_eval).
:- use_module(finite_horn_gslt_v1, [render_term/2]).
:- use_module(parser_pack_guard_reference_compile,
              [parser_pack_guard_answer_set_digest/2]).
:- use_module(parser_pack_guard_regular_reference_compile).
:- use_module(library(http/json)).

:- initialization(main, main).

main :-
    catch(run, Error, (print_message(error, Error), halt(1))),
    halt.

run :-
    current_prolog_flag(argv, Arguments),
    ( Arguments = [PresentationRoot] -> true
    ; throw(error(gate_failed(expected_presentation_root),
                  test_parser_pack_guard_regular_v1))
    ),
    parser_pack_guard_regular_rows(PresentationRoot, Result),
    json_write_dict(current_output, Result, [width(0)]),
    nl.

parser_pack_guard_regular_rows(
    PresentationRoot,
    _{protocol:"parser-pack-positive-guard-regular-v1",
      rows:[HERow, PeTTaRow, PrimeRow],
      replay_count:ReplayCount,
      negative_count:NegativeCount}) :-
    compile_regular_case(
        PresentationRoot, he, _, HERow, HEReplayCount),
    compile_regular_case(
        PresentationRoot, petta, _, PeTTaRow, PeTTaReplayCount),
    compile_regular_case(
        PresentationRoot, prime, PrimeCompilation,
        PrimeRow, PrimeReplayCount),
    PrimeCompilation = parser_pack_guard_regular_compilation(
        _, Program, GuardAnswers, NFAAnswers, Tags),
    mutation_gate(Program, GuardAnswers, NFAAnswers, Tags, NegativeCount),
    ReplayCount is HEReplayCount + PeTTaReplayCount + PrimeReplayCount.

compile_regular_case(
    PresentationRoot, Label, Compilation,
    _{label:LabelText,
      count:Count,
      digest:Digest,
      tag_count:TagCount,
      tags:TagTexts,
      terms:Terms},
    ReplayCount) :-
    atom_string(Label, LabelText),
    guard_case(Label, SourcePaths),
    parser_pack_guard_regular_reference_compile(
        PresentationRoot, SourcePaths, 512,
        completed(Compilation)),
    Compilation = parser_pack_guard_regular_compilation(
        _, _, GuardAnswers, NFAAnswers, Tags),
    maplist(answer_text, NFAAnswers, RawTerms),
    sort(RawTerms, Terms),
    length(Terms, Count),
    parser_pack_guard_answer_set_digest(NFAAnswers, Digest),
    maplist(render_term, Tags, TagTexts),
    length(Tags, TagCount),
    length(GuardAnswers, GuardReplayCount),
    length(NFAAnswers, NFAReplayCount),
    ReplayCount is GuardReplayCount + NFAReplayCount.

answer_text(answer(Answer, _), Text) :-
    render_term(Answer, Text).

mutation_gate(Program, GuardAnswers, NFAAnswers, [Tag|_], 3) :-
    NFAQuery = list([sym('compile-span-nfa'),
                     list([sym('pp-positive-guard-tag'), var(state)]),
                     var(edge)]),
    delete_program_rule(
        'ParserPackGuardRegularLinkV1',
        'compile-span-definition-positive-guard',
        Program, WithoutLink),
    horn_query(WithoutLink, NFAQuery, 512, completed([])),
    GuardAnswers = [_|GuardTail],
    require(\+ parser_pack_guard_nfa_bijection(
                GuardTail, NFAAnswers, _),
            nfa_tag_without_guard_body_rejected),
    exclude(answer_has_tag(Tag), NFAAnswers, WithoutTag),
    require(\+ parser_pack_guard_nfa_bijection(
                GuardAnswers, WithoutTag, _),
            guard_body_without_nfa_tag_rejected).

answer_has_tag(
    Tag,
    answer(list([sym('compile-span-nfa'), AnswerTag, _]), _)) :-
    AnswerTag == Tag.

delete_program_rule(PresentationName, RuleName, Program0, Program) :-
    select(presentation(PresentationName, Operators, Rules0, Source),
           Program0,
           presentation(PresentationName, Operators, Rules, Source),
           Program),
    select(rule(RuleName, _, _), Rules0, Rules),
    !.

guard_case(
    he,
    [ 'core/syntax_core_v1.metta',
      'shared/lookahead_core_v1.metta',
      'shared/ground_relations_v1.metta',
      'shared/he_reader_scalar_classes_v1.metta',
      'languages/he_reader_v1.metta'
    ]).
guard_case(
    petta,
    [ 'core/syntax_core_v1.metta',
      'shared/lookahead_core_v1.metta',
      'shared/ground_relations_v1.metta',
      'shared/petta_form_reader_scalar_classes_v1.metta',
      'languages/petta_form_reader_v1.metta'
    ]).
guard_case(
    prime,
    [ 'core/syntax_core_v1.metta',
      'shared/lookahead_core_v1.metta',
      'shared/ground_relations_v1.metta',
      'shared/cetta_prime_scalar_classes_v1.metta',
      'languages/cetta_prime_reader_v1.metta'
    ]).

require(Condition, Label) :-
    ( call(Condition) -> true
    ; throw(error(gate_failed(Label),
                  test_parser_pack_guard_regular_v1))
    ).
