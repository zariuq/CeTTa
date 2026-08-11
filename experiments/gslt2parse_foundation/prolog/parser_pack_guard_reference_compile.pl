:- module(parser_pack_guard_reference_compile,
          [ parser_pack_guard_reference_program/4,
            parser_pack_guard_reference_compile/4,
            parser_pack_guard_compilation_provenance/4,
            parser_pack_guard_artifact_valid/1,
            parser_pack_guard_answer_set_digest/2
          ]).

:- use_module(finite_horn_gslt_v1).
:- use_module(finite_horn_eval).
:- use_module(library(crypto)).
:- use_module(library(pairs)).

parser_pack_guard_reference_compile(PresentationRoot, SourceRelativePaths,
                                    MaxDepth, Outcome) :-
    must_be(integer, MaxDepth),
    MaxDepth > 0,
    catch(
        parser_pack_guard_reference_compile_(
            PresentationRoot, SourceRelativePaths, MaxDepth, Outcome),
        Error,
        Outcome = invalid_presentation(Error)).

parser_pack_guard_reference_compile_(PresentationRoot, SourceRelativePaths,
                                     MaxDepth, Outcome) :-
    parser_pack_guard_reference_program(
        PresentationRoot, SourceRelativePaths, SourcePresentations, Program),
    admit_presentations(Program),
    Query = list([sym('compile-pack-positive-guard'),
                  var(owner), var(guard)]),
    horn_query(Program, Query, MaxDepth, QueryOutcome),
    ( QueryOutcome = resource_exhausted(Reason) ->
        Outcome = resource_exhausted(Reason)
    ; QueryOutcome = completed(Answers),
      ( forall(member(answer(Answer, Proof), Answers),
               ( horn_replay(Program, Answer, Proof),
                 guard_answer_artifact(Answer, Artifact),
                 parser_pack_guard_artifact_valid(Artifact)
               )) ->
          canonical_guard_artifacts(Answers, Artifacts),
          Outcome = completed(parser_pack_guard_compilation(
              SourcePresentations, Program, Answers, Artifacts))
      ; Outcome = invalid_certificate(guard_compiler_derivation)
      )
    ).

parser_pack_guard_artifact_valid(Artifact) :-
    Artifact =
        list([sym('pp-positive-guard-v1'), State,
              list([sym('pp-positive-guard-tag'), State]),
              _Body,
              list([sym('pp-production'),
                    list([sym('pp-label'), State, sym(peek)]),
                    State,
                    list([sym('pp-items-cons'),
                          list([sym('pp-terminal'),
                                list([sym('pp-span-terminal'),
                                      list([sym('pp-positive-guard-tag'),
                                            State])])]),
                          sym('pp-items-nil')]),
                    list([sym('pa-slot'), sym('q-zero')])])]),
    ground(Artifact).

guard_answer_artifact(
    list([sym('compile-pack-positive-guard'), _Owner, Artifact]), Artifact).

canonical_guard_artifacts(Answers, Artifacts) :-
    findall(Key-Artifact,
            ( member(answer(Answer, _), Answers),
              guard_answer_artifact(Answer, Artifact),
              render_term(Artifact, Key)
            ),
            Pairs0),
    sort(Pairs0, Pairs),
    pairs_values(Pairs, Artifacts).

parser_pack_guard_answer_set_digest(Answers, Digest) :-
    findall(Text,
            ( member(answer(Answer, _), Answers),
              render_term(Answer, Text)
            ),
            Texts0),
    sort(Texts0, Texts),
    atomics_to_string(Texts, "\n", Terms),
    ( Texts == [] ->
        Payload = "FiniteHornAnswerSetV1\n"
    ; string_concat("FiniteHornAnswerSetV1\n", Terms, Prefix),
      string_concat(Prefix, "\n", Payload)
    ),
    crypto_data_hash(Payload, Digest, [algorithm(sha256)]).

parser_pack_guard_compilation_provenance(
    SourcePresentations, Program, Answers,
    parser_pack_guard_provenance_v1(
        source_digest(SourceDigest),
        pre_reflection_digest(PreReflectionDigest),
        environment_digest(EnvironmentDigest),
        answer_set_digest(AnswerSetDigest),
        guard_evidence(Evidence))) :-
    package_digest(SourcePresentations, SourceDigest),
    append(WithoutReflection, [Reflected], Program),
    Reflected = presentation(_, _, _, reflected(SourceDigest)),
    package_digest(WithoutReflection, PreReflectionDigest),
    package_digest(Program, EnvironmentDigest),
    parser_pack_guard_answer_set_digest(Answers, AnswerSetDigest),
    guard_compiler_evidence(Answers, Evidence).

guard_compiler_evidence(Answers, Evidence) :-
    findall(Key-guard_evidence(Owner, Artifact, Roots),
            ( setof(answer(Answer, Proof),
                    guard_answer_with_artifact(
                        Answers, Owner, Artifact, Answer, Proof),
                    Roots),
              render_term(Artifact, ArtifactText),
              format(string(Key), '~w\u0000~s', [Owner, ArtifactText])
            ),
            Pairs0),
    keysort(Pairs0, Pairs),
    pairs_values(Pairs, Evidence).

guard_answer_with_artifact(Answers, Owner, Artifact, Answer, Proof) :-
    member(answer(Answer, Proof), Answers),
    Answer = list([sym('compile-pack-positive-guard'), sym(Owner), Artifact]).

parser_pack_guard_reference_program(PresentationRoot, SourceRelativePaths,
                                    SourcePresentations, Program) :-
    guard_compiler_relative_paths(CompilerRelativePaths),
    maplist(presentation_path(PresentationRoot), CompilerRelativePaths,
            CompilerPaths),
    maplist(presentation_path(PresentationRoot), SourceRelativePaths,
            SourcePaths),
    maplist(read_presentation, CompilerPaths, CompilerPresentations),
    maplist(read_presentation, SourcePaths, SourcePresentations),
    reflect_presentations(SourcePresentations, Reflected),
    append(CompilerPresentations, SourcePresentations, WithoutReflection),
    append(WithoutReflection, [Reflected], Program).

guard_compiler_relative_paths(
    [ 'parserpack/parser_pack_core_v1.metta',
      'reflection/finite_horn_reflection_v1.metta',
      'compiler/syntax_compiler_v1.metta',
      'compiler/relational_cfg_lowering_v1.metta',
      'compiler/relational_cfg_link_v1.metta',
      'compiler/parser_pack_compiler_v1.metta',
      'compiler/parser_pack_guard_compiler_v1.metta'
    ]).

presentation_path(Root, Relative, Path) :-
    directory_file_path(Root, Relative, Path).
