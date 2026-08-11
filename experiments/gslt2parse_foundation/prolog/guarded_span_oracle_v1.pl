:- module(guarded_span_oracle_v1,
          [ guarded_span_answer_set_v1/6,
            write_guarded_span_oracle_v1/5
          ]).

:- use_module(regular_span_oracle_v1, [span_answer_set_v1/7]).

:- initialization(main, main).

guarded_span_answer_set_v1(
    PresentationRoot, RelativePaths, Tag, Limit,
    SourceDigest, AnswerSet) :-
    compiler_relative_paths(CompilerRelativePaths),
    Query = list([
        sym('compile-guarded-span-nfa'),
        sym(Tag), var(state), var(guard), var(edge)
    ]),
    span_answer_set_v1(
        PresentationRoot, CompilerRelativePaths, RelativePaths,
        Query, Limit, SourceDigest, AnswerSet).

compiler_relative_paths(
    [ 'parserpack/parser_pack_core_v1.metta',
      'reflection/finite_horn_reflection_v1.metta',
      'compiler/syntax_compiler_v1.metta',
      'compiler/regular_span_compiler_v1.metta',
      'compiler/guarded_regular_span_compiler_v1.metta'
    ]).

write_guarded_span_oracle_v1(
    PresentationRoot, RelativePaths, Tag, Limit, Stream) :-
    guarded_span_answer_set_v1(
        PresentationRoot, RelativePaths, Tag, Limit,
        SourceDigest, answer_set(Digest, AnswerTexts)),
    length(AnswerTexts, AnswerCount),
    format(Stream, 'guarded-span-oracle-v1~n', []),
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
                  guarded_span_oracle_v1))
    ),
    text_atom(TagValue, Tag),
    write_guarded_span_oracle_v1(
        PresentationRoot, RelativePaths, Tag, 4096, current_output).

text_atom(Value, Atom) :-
    ( atom(Value) -> Atom = Value
    ; string(Value) -> atom_string(Atom, Value)
    ).
