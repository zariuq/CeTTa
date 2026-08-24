:- module(parser_pack_guard_evidence_export_v1,
          [ write_parser_pack_guard_evidence_v1/3
          ]).

:- use_module(finite_horn_gslt_v1, [render_term/2]).
:- use_module(parser_pack_guard_reference_compile).
:- use_module(library(pairs)).

:- initialization(main, main).

/*
  Canonical proof-carrying evidence for the positive-lookahead productions
  derived from an authored syntax package.  The stream is consumed by the
  native guard-plan builder; it carries no target-language policy.
*/

write_parser_pack_guard_evidence_v1(
    PresentationRoot, RelativePaths, Stream) :-
    parser_pack_guard_reference_compile(
        PresentationRoot, RelativePaths, 4096,
        completed(parser_pack_guard_compilation(
            Sources, Program, Answers, _Artifacts))),
    parser_pack_guard_compilation_provenance(
        Sources, Program, Answers,
        parser_pack_guard_provenance_v1(
            source_digest(SourceDigest),
            pre_reflection_digest(PreReflectionDigest),
            environment_digest(EnvironmentDigest),
            answer_set_digest(AnswerSetDigest),
            guard_evidence(_Evidence))),
    canonical_derivations(Answers, Derivations),
    format(Stream, 'parser-pack-positive-guard-evidence-v1~n', []),
    format(Stream, 'source-digest\t~w~n', [SourceDigest]),
    format(Stream, 'pre-reflection-digest\t~w~n',
           [PreReflectionDigest]),
    format(Stream, 'environment-digest\t~w~n', [EnvironmentDigest]),
    format(Stream, 'answer-set-digest\t~w~n', [AnswerSetDigest]),
    forall(member(AnswerText-CertificateText, Derivations),
           format(Stream, 'derivation\t~s\t~s~n',
                  [AnswerText, CertificateText])),
    format(Stream, 'end~n', []).

canonical_derivations(Answers, Derivations) :-
    findall(Key-(AnswerText-CertificateText),
            ( member(answer(Answer, Certificate), Answers),
              render_term(Answer, AnswerText),
              render_term(Certificate, CertificateText),
              atomics_to_string(
                  [AnswerText, "\u0000", CertificateText], Key)
            ),
            Pairs0),
    sort(Pairs0, Pairs),
    pairs_values(Pairs, Derivations).

main :-
    catch(main_, Error, (print_message(error, Error), halt(1))),
    halt.

main_ :-
    current_prolog_flag(argv, Arguments),
    ( Arguments = [PresentationRoot|RelativePaths],
      RelativePaths = [_|_] -> true
    ; throw(error(expected_presentation_root_and_sources,
                  parser_pack_guard_evidence_export_v1))
    ),
    write_parser_pack_guard_evidence_v1(
        PresentationRoot, RelativePaths, current_output).
