:- module(regular_span_oracle_v1,
          [ regular_span_answer_set_v1/6,
            span_answer_set_v1/7,
            write_regular_span_oracle_v1/5
          ]).

:- use_module(finite_horn_gslt_v1).
:- use_module(finite_horn_eval).
:- use_module(library(crypto)).
:- use_module(library(pairs)).

:- initialization(main, main).

regular_span_answer_set_v1(PresentationRoot, RelativePaths, Tag, Limit,
                           SourceDigest, answer_set(Digest, AnswerTexts)) :-
    compiler_relative_paths(CompilerRelativePaths),
    Query = list([sym('compile-span-nfa'), sym(Tag), var(edge)]),
    span_answer_set_v1(
        PresentationRoot, CompilerRelativePaths, RelativePaths,
        Query, Limit, SourceDigest, answer_set(Digest, AnswerTexts)).

span_answer_set_v1(
    PresentationRoot, CompilerRelativePaths, RelativePaths,
    Query, Limit, SourceDigest, answer_set(Digest, AnswerTexts)) :-
    span_program_v1(
        PresentationRoot, CompilerRelativePaths, RelativePaths,
        SourcePresentations, Presentations),
    admit_presentations(Presentations),
    package_digest(SourcePresentations, SourceDigest),
    horn_query_checked(Presentations, Query, Limit, completed(Answers)),
    maplist(answer_pair, Answers, AnswerPairs0),
    keysort(AnswerPairs0, AnswerPairs),
    pairs_keys(AnswerPairs, AnswerTexts0),
    sort(AnswerTexts0, AnswerTexts),
    answer_set_digest(AnswerTexts, Digest).

span_program_v1(PresentationRoot, CompilerRelativePaths, RelativePaths,
                SourcePresentations, Program) :-
    maplist(presentation_path(PresentationRoot), CompilerRelativePaths,
            CompilerPaths),
    maplist(presentation_path(PresentationRoot), RelativePaths, SourcePaths),
    maplist(read_presentation, CompilerPaths, CompilerPresentations),
    maplist(read_presentation, SourcePaths, SourcePresentations),
    reflect_presentations(SourcePresentations, Reflected),
    append(CompilerPresentations, SourcePresentations, WithoutReflection),
    append(WithoutReflection, [Reflected], Program).

compiler_relative_paths(
    [ 'reflection/finite_horn_reflection_v1.metta',
      'compiler/syntax_compiler_v1.metta',
      'compiler/regular_span_compiler_v1.metta'
    ]).

presentation_path(Root, Relative, Path) :-
    directory_file_path(Root, Relative, Path).

answer_pair(Answer, Text-Answer) :-
    render_term(Answer, Text).

answer_set_digest(AnswerTexts, Digest) :-
    maplist(answer_line, AnswerTexts, Lines),
    atomics_to_string(["FiniteHornAnswerSetV1\n"|Lines], Payload),
    crypto_data_hash(Payload, Digest, [algorithm(sha256)]).

answer_line(Text, Line) :-
    atomics_to_string([Text, "\n"], Line).

write_regular_span_oracle_v1(PresentationRoot, RelativePaths, Tag, Limit,
                             Stream) :-
    regular_span_answer_set_v1(
        PresentationRoot, RelativePaths, Tag, Limit,
        SourceDigest, answer_set(Digest, AnswerTexts)),
    length(AnswerTexts, AnswerCount),
    format(Stream, 'regular-span-oracle-v1~n', []),
    format(Stream, 'source-digest\t~w~n', [SourceDigest]),
    format(Stream, 'answer-digest\t~w~n', [Digest]),
    format(Stream, 'answers\t~d~n', [AnswerCount]),
    forall(member(Text, AnswerTexts),
           format(Stream, 'answer\t~s~n', [Text])),
    format(Stream, 'end~n', []).

main :-
    catch(main_, Error, (print_message(error, Error), halt(1))),
    halt.

main_ :-
    current_prolog_flag(argv, Arguments),
    ( Arguments = [PresentationRoot, TagValue|RelativePaths],
      RelativePaths = [_|_] -> true
    ; throw(error(expected_presentation_root_tag_and_sources,
                  regular_span_oracle_v1))
    ),
    text_atom(TagValue, Tag),
    write_regular_span_oracle_v1(
        PresentationRoot, RelativePaths, Tag, 4096, current_output).

text_atom(Value, Atom) :-
    ( atom(Value) -> Atom = Value
    ; string(Value) -> atom_string(Atom, Value)
    ).
