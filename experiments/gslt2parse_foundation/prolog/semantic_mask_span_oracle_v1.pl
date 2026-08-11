:- module(semantic_mask_span_oracle_v1,
          [ semantic_mask_answer_set_v1/5,
            semantic_mask_root_link_answer_set_v1/4,
            write_semantic_mask_span_oracle_v1/4
          ]).

:- use_module(regular_span_oracle_v1, [span_answer_set_v1/7]).
:- use_module(library(crypto)).
:- use_module(library(pairs)).

:- initialization(main, main).

semantic_mask_compiler_relative_paths(
    [ 'reflection/finite_horn_reflection_v1.metta',
      'compiler/syntax_compiler_v1.metta',
      'compiler/semantic_mask_span_compiler_v1.metta'
    ]).

semantic_mask_answer_set_v1(PresentationRoot, RelativePaths, Labels,
                            SourceDigest, answer_set(Digest, AnswerTexts)) :-
    semantic_mask_compiler_relative_paths(CompilerRelativePaths),
    findall(
        SourceDigest0-Texts,
        ( member(Label, Labels),
          Query = list([sym('compile-semantic-mask-nfa'),
                        sym(Label), var(edge)]),
          span_answer_set_v1(
              PresentationRoot, CompilerRelativePaths, RelativePaths,
              Query, 100000, SourceDigest0, answer_set(_, Texts))
        ),
        Rows),
    Rows = [_|_],
    maplist(same_source_digest(SourceDigest), Rows),
    pairs_values(Rows, NestedTexts),
    append(NestedTexts, UnsortedTexts),
    sort(UnsortedTexts, AnswerTexts),
    answer_set_digest(AnswerTexts, Digest).

semantic_mask_root_link_answer_set_v1(
        PresentationRoot, RelativePaths, SourceDigest,
        answer_set(Digest, AnswerTexts)) :-
    semantic_mask_compiler_relative_paths(CompilerRelativePaths),
    Query = list([sym('compile-semantic-mask-root-link'),
                  var(name), var(label)]),
    span_answer_set_v1(
        PresentationRoot, CompilerRelativePaths, RelativePaths,
        Query, 100000, SourceDigest, answer_set(_, AnswerTexts)),
    answer_set_digest(AnswerTexts, Digest).

same_source_digest(SourceDigest, SourceDigest-_) :- !.

answer_set_digest(AnswerTexts, Digest) :-
    maplist(answer_line, AnswerTexts, Lines),
    atomics_to_string(["FiniteHornAnswerSetV1\n"|Lines], Payload),
    crypto_data_hash(Payload, Digest, [algorithm(sha256)]).

answer_line(Text, Line) :-
    atomics_to_string([Text, "\n"], Line).

write_semantic_mask_span_oracle_v1(PresentationRoot, RelativePaths,
                                   Labels, Stream) :-
    semantic_mask_answer_set_v1(
        PresentationRoot, RelativePaths, Labels,
        SourceDigest, answer_set(Digest, AnswerTexts)),
    length(AnswerTexts, AnswerCount),
    format(Stream, 'semantic-mask-span-oracle-v1~n', []),
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
    ( Arguments = [PresentationRoot, LabelText|RelativePaths],
      RelativePaths = [_|_] -> true
    ; throw(error(expected_presentation_root_labels_and_sources,
                  semantic_mask_span_oracle_v1))
    ),
    ( LabelText = '--root-links' ->
        semantic_mask_root_link_answer_set_v1(
            PresentationRoot, RelativePaths,
            SourceDigest, answer_set(Digest, AnswerTexts)),
        length(AnswerTexts, AnswerCount),
        format('semantic-mask-span-oracle-v1~n', []),
        format('source-digest\t~w~n', [SourceDigest]),
        format('answer-digest\t~w~n', [Digest]),
        format('answers\t~d~n', [AnswerCount]),
        forall(member(Text, AnswerTexts),
               format('answer\t~s~n', [Text])),
        format('end~n', [])
    ; split_string(LabelText, ",", "", LabelStrings),
      LabelStrings = [_|_],
      maplist(atom_string, Labels, LabelStrings),
      write_semantic_mask_span_oracle_v1(
          PresentationRoot, RelativePaths, Labels, current_output)
    ).
