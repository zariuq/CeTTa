:- module(parser_pack_reference_compile,
          [ parser_pack_reference_program/4,
            parser_pack_reference_compile/4,
            parser_pack_compilation_provenance/4
          ]).

:- use_module(finite_horn_gslt_v1).
:- use_module(finite_horn_eval).
:- use_module(parser_pack_eval,
              [ parser_pack_from_compiler_answers/3,
                parser_pack_from_compiler_answers/4,
                parser_pack_canonical_terms/2
              ]).
:- use_module(library(pairs)).

/* Generic PeTTa/SWI execution path for the admitted compiler GSLT. */

parser_pack_reference_compile(PresentationRoot, SourceRelativePaths,
                              MaxDepth, Outcome) :-
    must_be(integer, MaxDepth),
    MaxDepth > 0,
    catch(
        parser_pack_reference_compile_(
            PresentationRoot, SourceRelativePaths, MaxDepth, Outcome),
        Error,
        Outcome = invalid_presentation(Error)).

parser_pack_reference_compile_(PresentationRoot, SourceRelativePaths,
                               MaxDepth, Outcome) :-
    parser_pack_reference_program(PresentationRoot, SourceRelativePaths,
                                  SourcePresentations, Program),
    admit_presentations(Program),
    ProductionQuery = list([sym('compile-pack-production'),
                            var(owner), var(production)]),
    ClassQuery = list([sym('compile-pack-class-clause'),
                       var(owner), var(class_clause)]),
    ClassUseQuery = list([sym('compile-pack-class-use'),
                          var(owner), var(class_expression)]),
    horn_query(Program, ProductionQuery, MaxDepth, ProductionOutcome),
    horn_query(Program, ClassQuery, MaxDepth, ClassOutcome),
    horn_query(Program, ClassUseQuery, MaxDepth, ClassUseOutcome),
    ( ProductionOutcome = resource_exhausted(Reason) ->
        Outcome = resource_exhausted(Reason)
    ; ClassOutcome = resource_exhausted(Reason) ->
        Outcome = resource_exhausted(Reason)
    ; ClassUseOutcome = resource_exhausted(Reason) ->
        Outcome = resource_exhausted(Reason)
    ; ProductionOutcome = completed(ProductionAnswers),
      ClassOutcome = completed(ClassAnswers),
      ClassUseOutcome = completed(ClassUseAnswers),
      append([ProductionAnswers, ClassAnswers, ClassUseAnswers], Answers),
      ( forall(member(answer(Answer, Proof), Answers),
               horn_replay(Program, Answer, Proof)) ->
          ( class_use_expressions(ClassUseAnswers, ClassExpressions),
            parser_pack_from_compiler_answers(
                ProductionAnswers, ClassAnswers, ClassExpressions, Pack) ->
              Outcome = completed(parser_pack_compilation(
                  SourcePresentations, Program,
                  compiler_answers(ProductionAnswers, ClassAnswers),
                  Pack))
          ; Outcome = invalid_presentation(parser_pack)
          )
      ; Outcome = invalid_certificate(compiler_derivation)
      )
    ).

/*
  Provenance is a sidecar over the existing compiler-answer streams.  It does
  not add another ParserPack root or move authority away from the syntax GSLT.
  The three digests distinguish the language sources, the admitted compiler
  environment before reflection, and the exact reflected execution package.
  Each unique pack artifact retains every independently produced derivation
  root that compiled to it.
*/

parser_pack_compilation_provenance(
    SourcePresentations, Program,
    compiler_answers(ProductionAnswers, ClassAnswers),
    parser_pack_provenance(
        source_digest(SourceDigest),
        compiler_digest(CompilerDigest),
        environment_digest(EnvironmentDigest),
        production_evidence(ProductionEvidence),
        class_evidence(ClassEvidence))) :-
    package_digest(SourcePresentations, SourceDigest),
    append(CompilerEnvironment, [Reflected], Program),
    Reflected = presentation(_, _, _, reflected(SourceDigest)),
    package_digest(CompilerEnvironment, CompilerDigest),
    package_digest(Program, EnvironmentDigest),
    parser_pack_class_use_answers(Program, 4096, ClassUseAnswers),
    class_use_expressions(ClassUseAnswers, ClassExpressions),
    parser_pack_from_compiler_answers(
        ProductionAnswers, ClassAnswers, ClassExpressions,
        parser_pack_v1(Productions, ClassClauses)),
    compiler_evidence(
        ProductionAnswers, 'compile-pack-production', Productions,
        ProductionEvidence),
    compiler_evidence(
        ClassAnswers, 'compile-pack-class-clause', ClassClauses,
        ClassEvidence).

parser_pack_class_use_answers(Program, MaxDepth, ClassUseAnswers) :-
    ClassUseQuery = list([sym('compile-pack-class-use'),
                          var(owner), var(class_expression)]),
    horn_query(Program, ClassUseQuery, MaxDepth,
               completed(ClassUseAnswers)),
    forall(member(answer(Answer, Proof), ClassUseAnswers),
           horn_replay(Program, Answer, Proof)).

class_use_expressions(ClassUseAnswers, ClassExpressions) :-
    findall(Expression,
            member(answer(
                list([sym('compile-pack-class-use'), _, Expression]), _),
                ClassUseAnswers),
            RawExpressions),
    parser_pack_canonical_terms(RawExpressions, ClassExpressions).

compiler_evidence(Answers, Relation, Artifacts0, Evidence) :-
    parser_pack_canonical_terms(Artifacts0, Artifacts),
    maplist(artifact_evidence(Answers, Relation), Artifacts, Evidence).

compiler_answer_artifact(
    Relation, list([sym(Relation), _, Artifact]), Artifact).

artifact_evidence(Answers, Relation, Artifact,
                  parser_pack_evidence(Artifact, Roots)) :-
    findall(Key-answer(Answer, Proof),
            ( member(answer(Answer, Proof), Answers),
              compiler_answer_artifact(Relation, Answer, Candidate),
              Candidate == Artifact,
              compiler_derivation_key(Answer, Proof, Key)
            ),
            RootPairs),
    keysort(RootPairs, SortedRootPairs),
    pairs_values(SortedRootPairs, Roots),
    Roots = [_|_].

compiler_derivation_key(Answer, Proof, Key) :-
    render_term(Answer, AnswerText),
    render_term(Proof, ProofText),
    atomics_to_string([AnswerText, "\u0000", ProofText], Key).

parser_pack_reference_program(PresentationRoot, SourceRelativePaths,
                              SourcePresentations, Program) :-
    compiler_relative_paths(CompilerRelativePaths),
    maplist(presentation_path(PresentationRoot), CompilerRelativePaths,
            CompilerPaths),
    maplist(presentation_path(PresentationRoot), SourceRelativePaths,
            SourcePaths),
    maplist(read_presentation, CompilerPaths, CompilerPresentations),
    maplist(read_presentation, SourcePaths, SourcePresentations),
    reflect_presentations(SourcePresentations, Reflected),
    append(CompilerPresentations, SourcePresentations, WithoutReflection),
    append(WithoutReflection, [Reflected], Program).

compiler_relative_paths(
    [ 'parserpack/parser_pack_core_v1.metta',
      'reflection/finite_horn_reflection_v1.metta',
      'compiler/syntax_compiler_v1.metta',
      'compiler/relational_cfg_lowering_v1.metta',
      'compiler/relational_cfg_link_v1.metta',
      'compiler/parser_pack_compiler_v1.metta'
    ]).

presentation_path(Root, Relative, Path) :-
    directory_file_path(Root, Relative, Path).
