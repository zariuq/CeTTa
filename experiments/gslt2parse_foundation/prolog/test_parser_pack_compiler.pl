:- module(test_parser_pack_compiler,
          [ run_parser_pack_gate/2
          ]).

:- use_module(finite_horn_gslt_v1).
:- use_module(finite_horn_eval).
:- use_module(parser_pack_eval).
:- use_module(parser_pack_reference_compile).
:- use_module(library(crypto)).

:- initialization(main, main).

main :-
    catch(run, Error, (print_message(error, Error), halt(1))),
    halt.

run :-
    current_prolog_flag(argv, Arguments),
    ( Arguments = [PresentationRoot] -> true
    ; throw(error(gate_failed(expected_presentation_root),
                  test_parser_pack_compiler))
    ),
    run_parser_pack_gate(PresentationRoot, Summary),
    Summary = summary(Languages, Closed, Partial, Semantic, Mutations),
    Total is Languages + Semantic + Mutations,
    format('(ParserPackCompilerPeTTaSummary ~d ~d ~d ~d ~d ~d 0)~n',
           [Total, Languages, Closed, Partial, Semantic, Mutations]).

run_parser_pack_gate(PresentationRoot, Summary) :-
    findall(Result,
            ( pack_case(Label, StartName, RelativePaths, ProofCount,
                        ClassProofCount, ProductionCount,
                        ClassClauseCount, Digest, ExpectedMissing),
              run_pack_case(PresentationRoot, Label, StartName,
                            RelativePaths, ProofCount, ClassProofCount,
                            ProductionCount, ClassClauseCount, Digest,
                            ExpectedMissing, Result)
            ),
            Results),
    require(length(Results, 4), exact_language_matrix),
    include(closed_case, Results, ClosedResults),
    include(partial_case, Results, PartialResults),
    require(length(ClosedResults, 3), exact_closed_language_count),
    require(length(PartialResults, 1), exact_partial_language_count),
    require(PartialResults = [case_result(cetta_prime, _, _, _, _, _)],
            prime_is_only_partial_pack),
    prime_peek_boundary_gate(Results),
    action_representation_gate(Results),
    class_representation_gate(Results),
    semantic_matrix_gate(Results, SemanticDigest),
    require(SemanticDigest ==
                '8d353e84e3f9fbc3d773a16b12f6e9fc8e13d55ca6d2e93e786bccf752e51537',
            exact_source_pack_semantic_matrix),
    mutation_gate(Results),
    closure_reachability_gate,
    generic_purity_gate(PresentationRoot),
    Summary = summary(4, 3, 1, 10, 15).

run_pack_case(PresentationRoot, Label, StartName, RelativePaths,
              ExpectedProofCount, ExpectedClassProofCount,
              ExpectedProductionCount, ExpectedClassClauseCount,
              ExpectedDigest, ExpectedMissing,
              case_result(Label, StartState, Source, Program,
                          CompilerAnswers, Pack)) :-
    parser_pack_reference_compile(
        PresentationRoot, RelativePaths, 4096,
        ReferenceOutcome),
    require(ReferenceOutcome = completed(parser_pack_compilation(
                Source, Program, CompilerAnswers, Pack)),
            completed_reference_pack_compilation(Label)),
    CompilerAnswers = compiler_answers(ProductionAnswers, ClassAnswers),
    length(ProductionAnswers, ProofCount),
    require(ProofCount =:= ExpectedProofCount,
            exact_pack_proof_count(Label)),
    length(ClassAnswers, ClassProofCount),
    require(ClassProofCount =:= ExpectedClassProofCount,
            exact_class_proof_count(Label)),
    append(ProductionAnswers, ClassAnswers, Answers),
    maplist(replay_compiler_answer(Program), Answers),
    Pack = parser_pack_v1(Productions, ClassClauses),
    length(Productions, ProductionCount),
    require(ProductionCount =:= ExpectedProductionCount,
            exact_pack_production_count(Label)),
    length(ClassClauses, ClassClauseCount),
    require(ClassClauseCount =:= ExpectedClassClauseCount,
            exact_pack_class_clause_count(Label)),
    parser_pack_digest(Pack, Digest),
    require(Digest == ExpectedDigest, exact_pack_digest(Label)),
    parser_pack_compilation_provenance(
        Source, Program, CompilerAnswers, Provenance),
    parser_pack_provenance_gate(
        Label, Pack, ProductionAnswers, ClassAnswers, Provenance),
    StartState = list([sym('pp-def'), sym(StartName)]),
    parser_pack_missing_states(Pack, [StartState], Missing),
    require(Missing == ExpectedMissing, exact_pack_closure(Label)).

parser_pack_provenance_gate(
    Label,
    parser_pack_v1(Productions, ClassClauses),
    ProductionAnswers,
    ClassAnswers,
    parser_pack_provenance(
        source_digest(SourceDigest),
        compiler_digest(CompilerDigest),
        environment_digest(EnvironmentDigest),
        production_evidence(ProductionEvidence),
        class_evidence(ClassEvidence))) :-
    expected_provenance(
        Label, ExpectedSourceDigest, ExpectedCompilerDigest,
        ExpectedEnvironmentDigest, ExpectedProductionRoots,
        ExpectedClassRoots),
    require(SourceDigest == ExpectedSourceDigest,
            exact_source_digest(Label)),
    require(CompilerDigest == ExpectedCompilerDigest,
            exact_compiler_digest(Label)),
    require(EnvironmentDigest == ExpectedEnvironmentDigest,
            exact_environment_digest(Label)),
    require(SourceDigest \== CompilerDigest,
            distinct_source_compiler_digest(Label)),
    require(SourceDigest \== EnvironmentDigest,
            distinct_source_environment_digest(Label)),
    require(CompilerDigest \== EnvironmentDigest,
            distinct_compiler_environment_digest(Label)),
    provenance_evidence_gate(
        'compile-pack-production', ProductionAnswers, Productions,
        ProductionEvidence, ExpectedProductionRoots, Label),
    provenance_evidence_gate(
        'compile-pack-class-clause', ClassAnswers, ClassClauses,
        ClassEvidence, ExpectedClassRoots, Label).

provenance_evidence_gate(Relation, Answers, Artifacts, Evidence,
                         ExpectedRootCount, Label) :-
    maplist(evidence_artifact, Evidence, EvidenceArtifacts),
    require(EvidenceArtifacts == Artifacts,
            exact_provenance_artifact_order(Label, Relation)),
    require(
        forall(member(parser_pack_evidence(Artifact, Roots), Evidence),
               ( Roots = [_|_],
                 evidence_roots_strict(Roots),
                 forall(member(Root, Roots),
                        provenance_root_matches(
                            Relation, Artifact, Answers, Root))
               )),
        valid_provenance_roots(Label, Relation)),
    findall(Root,
            ( member(parser_pack_evidence(_, Roots), Evidence),
              member(Root, Roots)
            ),
            ActualRoots),
    length(ActualRoots, RootCount),
    require(RootCount =:= ExpectedRootCount,
            exact_provenance_root_count(Label, Relation)),
    findall(Root,
            ( member(Root, Answers),
              Root = answer(
                  list([sym(Relation), _, Artifact]), _),
              memberchk(Artifact, Artifacts)
            ),
            ExpectedRoots),
    msort(ActualRoots, SortedActualRoots),
    msort(ExpectedRoots, SortedExpectedRoots),
    require(SortedActualRoots == SortedExpectedRoots,
            complete_provenance_root_set(Label, Relation)).

evidence_artifact(parser_pack_evidence(Artifact, _), Artifact).

evidence_roots_strict(Roots) :-
    maplist(evidence_root_key, Roots, Keys),
    strictly_increasing_strings(Keys).

evidence_root_key(answer(Answer, Proof), Key) :-
    render_term(Answer, AnswerText),
    render_term(Proof, ProofText),
    atomics_to_string([AnswerText, "\u0000", ProofText], Key).

strictly_increasing_strings([]).
strictly_increasing_strings([_]).
strictly_increasing_strings([Left, Right|More]) :-
    compare(<, Left, Right),
    strictly_increasing_strings([Right|More]).

provenance_root_matches(Relation, Artifact, Answers, Root) :-
    Root = answer(Answer, Proof),
    ground(Root),
    Answer = list([sym(Relation), _, Candidate]),
    Candidate == Artifact,
    ground(Proof),
    memberchk(Root, Answers).

compile_pack_answers(Program,
                     compiler_answers(ProductionAnswers, ClassAnswers)) :-
    admit_presentations(Program),
    ProductionQuery = list([sym('compile-pack-production'),
                            var(owner), var(production)]),
    ClassQuery = list([sym('compile-pack-class-clause'),
                       var(owner), var(class_clause)]),
    horn_query(Program, ProductionQuery, 4096, ProductionOutcome),
    horn_query(Program, ClassQuery, 4096, ClassOutcome),
    require(ProductionOutcome = completed(ProductionAnswers),
            completed_pack_production_compilation),
    require(ClassOutcome = completed(ClassAnswers),
            completed_pack_class_compilation).

replay_compiler_answer(Program, answer(Answer, Proof)) :-
    require(horn_replay(Program, Answer, Proof), pack_proof_replay).

closed_case(case_result(_, Start, _, _, _, Pack)) :-
    parser_pack_missing_states(Pack, [Start], []).

partial_case(Case) :-
    \+ closed_case(Case).

closure_reachability_gate :-
    Root = list([sym('pp-def'), sym(root)]),
    Ghost = list([sym('pp-def'), sym(ghost)]),
    ReachableMissing = list([sym('pp-def'), sym('reachable-missing')]),
    UnreachableMissing =
        list([sym('pp-def'), sym('unreachable-missing')]),
    EmptyItems = sym('pp-items-nil'),
    ReachableItems =
        list([sym('pp-items-cons'),
              list([sym('pp-nonterminal'), ReachableMissing]),
              EmptyItems]),
    UnreachableItems =
        list([sym('pp-items-cons'),
              list([sym('pp-nonterminal'), UnreachableMissing]),
              EmptyItems]),
    RootClosed =
        list([sym('pp-production'), sym('root-closed'), Root,
              EmptyItems, sym('pa-empty')]),
    RootOpen =
        list([sym('pp-production'), sym('root-open'), Root,
              ReachableItems, sym('pa-empty')]),
    GhostOpen =
        list([sym('pp-production'), sym('ghost-open'), Ghost,
              UnreachableItems, sym('pa-empty')]),
    parser_pack_missing_states(
        parser_pack_v1([RootClosed, GhostOpen], []),
        [Root], ClosedMissing),
    require(ClosedMissing == [], unreachable_missing_state_is_ignored),
    parser_pack_missing_states(
        parser_pack_v1([RootOpen, GhostOpen], []),
        [Root], OpenMissing),
    require(OpenMissing == [ReachableMissing],
            reachable_missing_state_is_reported).

prime_peek_boundary_gate(Results) :-
    case_with_label(Results, cetta_prime,
                    case_result(_, _, _, Program, _, Pack)),
    prime_missing_states(ExpectedMissing),
    parser_pack_missing_states(
        Pack,
        [list([sym('pp-def'), sym('cetta-prime-file')])],
        ExpectedMissing),
    maplist(actual_prime_peek_witness(Program),
            ['cetta-prime-word-amp',
             'cetta-prime-word-bare-amp',
             'cetta-prime-word-general',
             'cetta-prime-word-star',
             'cetta-prime-anonymous-variable',
             'cetta-prime-variable',
             'cetta-prime-unquote'],
            ExpectedMissing),
    Pack = parser_pack_v1(Productions, _),
    forall(member(Missing, ExpectedMissing),
           require(\+ pack_defines_state(Productions, Missing),
                   unsupported_peek_not_fabricated(Missing))).

actual_prime_peek_witness(Program, Name, MissingState) :-
    Query = list([sym('compile-definition'), sym(Name), var(ir)]),
    horn_query(Program, Query, 256, Outcome),
    require(Outcome = completed([answer(Answer, Proof)]),
            exact_prime_peek_definition(Name)),
    require(horn_replay(Program, Answer, Proof),
            prime_peek_definition_proof(Name)),
    Answer =
        list([sym('compile-definition'), sym(Name),
              list([sym('sir-definition'), sym(Name), Grammar])]),
    sir_peek_state(
        Grammar, list([sym('pp-def'), sym(Name)]), MissingState).

sir_peek_state(list([sym('sir-peek'), _]), State, State).
sir_peek_state(list([sym('sir-peek'), Body]), State, PeekState) :-
    sir_peek_state(
        Body, list([sym('pp-sub'), State, sym(body)]), PeekState).
sir_peek_state(list([sym('sir-alt'), Left, _]), State, PeekState) :-
    sir_peek_state(
        Left, list([sym('pp-sub'), State, sym(left)]), PeekState).
sir_peek_state(list([sym('sir-alt'), _, Right]), State, PeekState) :-
    sir_peek_state(
        Right, list([sym('pp-sub'), State, sym(right)]), PeekState).
sir_peek_state(list([sym('sir-seq'), Left, _]), State, PeekState) :-
    sir_peek_state(
        Left, list([sym('pp-sub'), State, sym(left)]), PeekState).
sir_peek_state(list([sym('sir-seq'), _, Right]), State, PeekState) :-
    sir_peek_state(
        Right, list([sym('pp-sub'), State, sym(right)]), PeekState).
sir_peek_state(list([sym('sir-left'), Left, _]), State, PeekState) :-
    sir_peek_state(
        Left, list([sym('pp-sub'), State, sym(left)]), PeekState).
sir_peek_state(list([sym('sir-left'), _, Right]), State, PeekState) :-
    sir_peek_state(
        Right, list([sym('pp-sub'), State, sym(right)]), PeekState).
sir_peek_state(list([sym('sir-right'), Left, _]), State, PeekState) :-
    sir_peek_state(
        Left, list([sym('pp-sub'), State, sym(left)]), PeekState).
sir_peek_state(list([sym('sir-right'), _, Right]), State, PeekState) :-
    sir_peek_state(
        Right, list([sym('pp-sub'), State, sym(right)]), PeekState).
sir_peek_state(list([sym('sir-node'), _, Body]), State, PeekState) :-
    sir_peek_state(
        Body, list([sym('pp-sub'), State, sym(body)]), PeekState).
sir_peek_state(list([sym('sir-star'), Body]), State, PeekState) :-
    sir_peek_state(
        Body, list([sym('pp-sub'), State, sym(body)]), PeekState).
sir_peek_state(list([sym('sir-plus'), Body]), State, PeekState) :-
    sir_peek_state(
        Body, list([sym('pp-sub'), State, sym(body)]), PeekState).
sir_peek_state(list([sym('sir-opt'), Body]), State, PeekState) :-
    sir_peek_state(
        Body, list([sym('pp-sub'), State, sym(body)]), PeekState).
sir_peek_state(list([sym('sir-until'), End, _]), State, PeekState) :-
    sir_peek_state(
        End, list([sym('pp-sub'), State, sym(end)]), PeekState).
sir_peek_state(list([sym('sir-until'), _, Item]), State, PeekState) :-
    sir_peek_state(
        Item, list([sym('pp-sub'), State, sym(item)]), PeekState).

pack_defines_state(Productions, State) :-
    member(list([sym('pp-production'), _, State, _, _]), Productions).

action_representation_gate(Results) :-
    case_with_label(Results, megalodon,
                    case_result(_, _, _, _, _,
                                parser_pack_v1(MegProductions, _))),
    expected_megalodon_name_list(NameList),
    expected_megalodon_tail_cons(TailCons),
    expected_megalodon_tail_empty(TailEmpty),
    require(memberchk(NameList, MegProductions), dense_name_list_action),
    require(memberchk(TailCons, MegProductions), dense_tail_cons_action),
    require(memberchk(TailEmpty, MegProductions), constant_tail_action),
    case_with_label(Results, metamath,
                    case_result(_, _, _, _, _,
                                parser_pack_v1(MMProductions, _))),
    PairAction =
        list([sym('pa-apply'), sym(pair),
              list([sym('pa-cons'), list([sym('pa-slot'), sym('q-zero')]),
                    list([sym('pa-cons'),
                          list([sym('pa-slot'),
                                list([sym('q-succ'), sym('q-zero')])]),
                          sym('pa-nil')])])]),
    require(member(list([sym('pp-production'),
                         list([sym('pp-label'), _, sym(seq)]), _, _,
                         PairAction]),
                   MMProductions),
            structural_dense_pair_action).

class_representation_gate(Results) :-
    case_with_label(Results, cetta_prime,
                    case_result(_, _, _, PrimeProgram, _, PrimePack)),
    case_with_label(Results, megalodon,
                    case_result(_, _, _, _, _, MegPack)),
    require(parser_pack_class_ranges(
                MegPack, sym('ascii-digit'), [range(48, 57)]),
            exact_ascii_digit_interval),
    Alpha = list([sym('c-union'), sym('ascii-lower'),
                  sym('ascii-upper')]),
    require(parser_pack_class_ranges(
                MegPack, Alpha, [range(65, 90), range(97, 122)]),
            exact_composed_alpha_intervals),
    require(parser_pack_class_ranges(
                PrimePack, sym('cetta-prime-string-escape-scalar'),
                [range(0, 0xd7ff), range(0xe000, 0x10ffff)]),
            exact_unicode_scalar_universe),
    require(parser_pack_class_ranges(
                PrimePack, sym('cetta-prime-string-plain-scalar'),
                [range(0, 33), range(35, 91), range(93, 0xd7ff),
                 range(0xe000, 0x10ffff)]),
            exact_unicode_complement_intervals),
    require(parser_pack_class_ranges(
                PrimePack, sym('cetta-prime-star-payload-start-scalar'),
                [range(1, 8), range(14, 31), range(33, 40),
                 range(42, 58), range(60, 0xd7ff),
                 range(0xe000, 0x10ffff)]),
            guard_only_class_retained_in_canonical_pack),
    PrimePack = parser_pack_v1(PrimeProductions, _),
    require(\+ pack_production_terminal_class(
                    PrimeProductions,
                    sym('cetta-prime-star-payload-start-scalar')),
            guard_only_class_is_not_a_base_production_dependency),
    GuardClassUse = list([
        sym('compile-pack-class-use'), sym('cetta-prime-unquote'),
        sym('cetta-prime-star-payload-start-scalar')]),
    horn_query(PrimeProgram, GuardClassUse, 256,
               completed([answer(GuardClassUse, GuardClassProof)])),
    require(horn_replay(
                PrimeProgram, GuardClassUse, GuardClassProof),
            guard_only_class_use_proof_replays),
    class_membership_cases(Cases),
    forall(member(class_case(Language, Class, Codepoint, Expected), Cases),
           class_membership_case(
               Results, Language, Class, Codepoint, Expected)),
    Surrogate = list([sym(cp), int(0xd800)]),
    require(\+ parser_pack_class_member(
                PrimePack, sym('cetta-prime-string-escape-scalar'),
                Surrogate),
            surrogate_not_a_unicode_scalar).

class_membership_case(Results, Language, Class, Codepoint, Expected) :-
    case_with_label(Results, Language,
                    case_result(_, _, Source, _, _, Pack)),
    Value = list([sym(cp), int(Codepoint)]),
    Query = list([sym(member), Class, Value]),
    horn_query(Source, Query, 256, SourceOutcome),
    require(SourceOutcome = completed(SourceAnswers),
            source_class_query_completed(Language, Class, Codepoint)),
    truth_value(SourceAnswers = [_|_], SourceTruth),
    truth_value(parser_pack_class_member(Pack, Class, Value), PackTruth),
    require(SourceTruth == Expected,
            source_class_expected(Language, Class, Codepoint)),
    require(PackTruth == SourceTruth,
            source_pack_class_agreement(Language, Class, Codepoint)).

truth_value(Goal, true) :-
    call(Goal),
    !.
truth_value(_, false).

class_membership_cases(
    [ class_case(cetta_prime, sym('cetta-prime-whitespace-scalar'),
                 9, true),
      class_case(cetta_prime, sym('cetta-prime-whitespace-scalar'),
                 955, false),
      class_case(cetta_prime, sym('cetta-prime-string-escape-scalar'),
                 0x10ffff, true),
      class_case(cetta_prime, sym('cetta-prime-non-newline-scalar'),
                 10, false),
      class_case(cetta_prime, sym('cetta-prime-string-plain-scalar'),
                 34, false),
      class_case(cetta_prime, sym('cetta-prime-string-plain-scalar'),
                 955, true),
      class_case(cetta_prime, sym('cetta-prime-token-scalar'),
                 40, false),
      class_case(cetta_prime, sym('cetta-prime-token-scalar'),
                 955, true),
      class_case(megalodon, sym('ascii-digit'), 48, true),
      class_case(megalodon, sym('ascii-digit'), 955, false),
      class_case(megalodon,
                 list([sym('c-union'), sym('ascii-lower'),
                       sym('ascii-upper')]), 90, true),
      class_case(megalodon, sym('mg-apostrophe'), 39, true),
      class_case(metamath, sym(whitespace), 32, true),
      class_case(metamath, sym(whitespace), 955, false),
      class_case(tptp, sym('tptp-whitespace'), 10, true),
      class_case(tptp, sym('ascii-lower'), 955, false)
    ]).

semantic_matrix_gate(Results, Digest) :-
    semantic_cases(Cases),
    maplist(semantic_case_result(Results), Cases, Rows),
    maplist(semantic_row_text, Rows, RowTexts),
    atomics_to_string(RowTexts, "\n", Payload),
    crypto_data_hash(Payload, Digest, [algorithm(sha256)]).

semantic_case_result(
    Results,
    semantic_case(Language, Label, Grammar, PackState, Codes,
                  ExpectedShape),
    semantic_row(Language, Label, SourceResults)) :-
    case_with_label(
        Results, Language,
        case_result(_, _, Source, _, _, Pack)),
    codepoints_node(Codes, Input),
    source_parse_results(Source, Grammar, Input, 1024, SourceOutcome),
    parser_pack_parse_results(Pack, Source, PackState, Input, 1024,
                              PackOutcome),
    parser_pack_parse_results(Pack, [], PackState, Input, 1024,
                              SelfContainedOutcome),
    require(SourceOutcome = completed(SourceResults),
            source_semantics_completed(Language, Label)),
    require(PackOutcome = completed(PackResults),
            pack_semantics_completed(Language, Label)),
    require(SelfContainedOutcome = completed(SelfContainedResults),
            self_contained_pack_completed(Language, Label)),
    require(PackResults == SourceResults,
            complete_result_agreement(Language, Label)),
    require(SelfContainedResults == SourceResults,
            self_contained_result_agreement(Language, Label)),
    require_result_shape(ExpectedShape, SourceResults, Language, Label).

source_parse_results(SourcePresentations, Grammar, Input, MaxDepth,
                     Outcome) :-
    Query = list([sym(parse), Grammar, Input, var(value), var(rest)]),
    horn_query(SourcePresentations, Query, MaxDepth, HornOutcome),
    ( HornOutcome = completed(Answers) ->
        findall(result(Value, Rest),
                member(answer(list([sym(parse), _, _, Value, Rest]), _),
                       Answers),
                RawResults),
        parser_pack_canonical_results(RawResults, Results),
        Outcome = completed(Results)
    ; Outcome = HornOutcome
    ).

require_result_shape(empty, Results, Language, Label) :-
    require(Results == [], expected_empty(Language, Label)).
require_result_shape(nonempty, Results, Language, Label) :-
    require(Results = [_|_], expected_nonempty(Language, Label)).

semantic_row_text(semantic_row(Language, Label, Results), Text) :-
    maplist(semantic_result_text, Results, ResultTexts),
    atomics_to_string(ResultTexts, "|", ResultPayload),
    format(string(Text), '~w/~w:~s', [Language, Label, ResultPayload]).

semantic_result_text(result(Value, Rest), Text) :-
    render_term(list([sym(result), Value, Rest]), Text).

mutation_gate(Results) :-
    case_with_label(Results, megalodon,
                    case_result(_, _, MegSource, MegProgram, MegAnswers,
                                MegPack)),
    case_with_label(Results, metamath,
                    case_result(_, MMStart, _, MMProgram, MMAnswers,
                                MMPack)),
    MegAnswers = compiler_answers(MegProductionAnswers, MegClassAnswers),
    MMAnswers = compiler_answers(MMProductionAnswers, MMClassAnswers),
    corrupt_first_proof(
        MegProductionAnswers, CorruptAnswer, CorruptProof),
    require(\+ horn_replay(MegProgram, CorruptAnswer, CorruptProof),
            corrupt_pack_proof_rejected),
    invalid_slot_answers(MegProductionAnswers, InvalidSlotAnswers),
    require(\+ parser_pack_from_compiler_answers(
                InvalidSlotAnswers, MegClassAnswers, _),
            invalid_action_slot_rejected),
    corrupt_megalodon_action(MegPack, CorruptActionPack),
    require_semantic_difference(CorruptActionPack, MegSource,
                                sym('mg-name-list'),
                                list([sym('pp-rel'), sym('mg-name-list')]),
                                [97], corrupt_action_detected),
    delete_tail_cons(MegPack, MissingTailPack),
    require_semantic_difference(MissingTailPack, MegSource,
                                sym('mg-name-list'),
                                list([sym('pp-rel'), sym('mg-name-list')]),
                                [97, 32, 98], production_deletion_detected),
    delete_program_rule('ParserPackCompilerV1',
                        'flatten-sir-node-parent', MMProgram,
                        WithoutNodeRule),
    compile_pack_answers(WithoutNodeRule, WithoutNodeAnswers),
    WithoutNodeAnswers = compiler_answers(
        WithoutNodeProductions, WithoutNodeClasses),
    require(parser_pack_from_compiler_answers(
                WithoutNodeProductions, WithoutNodeClasses,
                WithoutNodePack),
            node_rule_deletion_still_yields_pack),
    parser_pack_missing_states(WithoutNodePack, [MMStart], NodeMissing),
    require(NodeMissing \== [], compiler_rule_deletion_detected),
    delete_program_rule('ParserPackCompilerV1',
                        'compile-pack-relational-production', MegProgram,
                        WithoutRelationalRule),
    compile_pack_answers(WithoutRelationalRule, WithoutRelationalAnswers),
    WithoutRelationalAnswers = compiler_answers(
        WithoutRelationalProductions, WithoutRelationalClasses),
    require(parser_pack_from_compiler_answers(
                WithoutRelationalProductions, WithoutRelationalClasses,
                WithoutRelationalPack),
            relational_rule_deletion_still_yields_pack),
    parser_pack_missing_states(
        WithoutRelationalPack,
        [list([sym('pp-def'), sym('mg-document')])], RelationalMissing),
    require(memberchk(list([sym('pp-rel'), sym('mg-name-list')]),
                      RelationalMissing),
            relational_compiler_rule_deletion_detected),
    class_mutation_gate(Results),
    require(MMProductionAnswers = [_|_],
            metamath_production_mutation_basis_present),
    require(MMClassAnswers = [_|_],
            metamath_class_mutation_basis_present),
    require(MMPack = parser_pack_v1([_|_], [_|_]),
            metamath_pack_basis_present).

class_mutation_gate(Results) :-
    case_with_label(Results, cetta_prime,
                    case_result(_, _, _, PrimeProgram, PrimeAnswers, _)),
    case_with_label(Results, megalodon,
                    case_result(_, _, _, MegProgram, MegAnswers, _)),

    delete_program_rule('ParserPackCompilerV1',
                        'compile-pack-class-point', MegProgram,
                        WithoutPointCompiler),
    compile_pack_answers(WithoutPointCompiler, WithoutPointAnswers),
    require(\+ pack_from_compiler_answers(WithoutPointAnswers, _),
            class_point_compiler_deletion_fails_closed),

    delete_program_rule('ParserPackCompilerV1',
                        'compile-pack-class-except', PrimeProgram,
                        WithoutExceptCompiler),
    compile_pack_answers(WithoutExceptCompiler, WithoutExceptAnswers),
    require(\+ pack_from_compiler_answers(WithoutExceptAnswers, _),
            class_except_compiler_deletion_fails_closed),

    mutate_reflected_rule(
        PrimeProgram, 'member-cetta-prime-token-scalar', extra_guard,
        ExtraGuardProgram),
    compile_pack_answers(ExtraGuardProgram, ExtraGuardAnswers),
    require(\+ pack_from_compiler_answers(ExtraGuardAnswers, _),
            extra_class_guard_fails_closed),

    mutate_reflected_rule(
        PrimeProgram, 'member-cetta-prime-token-scalar', captured_variable,
        CapturedVariableProgram),
    compile_pack_answers(CapturedVariableProgram, CapturedVariableAnswers),
    require(\+ pack_from_compiler_answers(CapturedVariableAnswers, _),
            captured_class_variable_fails_closed),

    mutate_reflected_rule(
        PrimeProgram, 'member-cetta-prime-token-scalar', non_member_head,
        NonMemberProgram),
    compile_pack_answers(NonMemberProgram, NonMemberAnswers),
    require(\+ pack_from_compiler_answers(NonMemberAnswers, _),
            non_member_class_head_fails_closed),

    PrimeAnswers = compiler_answers(
        PrimeProductionAnswers, PrimeClassAnswers),
    corrupt_class_scalar_answers(
        PrimeClassAnswers, InvalidScalarClassAnswers),
    require(\+ parser_pack_from_compiler_answers(
                PrimeProductionAnswers, InvalidScalarClassAnswers, _),
            invalid_compiled_class_scalar_rejected),

    exclude(compiled_star_payload_class,
            PrimeClassAnswers, MissingGuardClassAnswers),
    length(PrimeClassAnswers, PrimeClassAnswerCount),
    length(MissingGuardClassAnswers, MissingGuardClassAnswerCount),
    require(MissingGuardClassAnswerCount =:= PrimeClassAnswerCount - 1,
            deleted_exactly_one_guard_only_class_clause),
    require(\+ parser_pack_from_compiler_answers(
                PrimeProductionAnswers, MissingGuardClassAnswers,
                [sym('cetta-prime-star-payload-start-scalar')], _),
            missing_guard_only_class_fails_closed),

    MegAnswers = compiler_answers(_, MegClassAnswers),
    corrupt_first_proof(MegClassAnswers, ClassAnswer, CorruptClassProof),
    require(\+ horn_replay(MegProgram, ClassAnswer, CorruptClassProof),
            corrupt_class_compiler_proof_rejected).

pack_from_compiler_answers(
    compiler_answers(ProductionAnswers, ClassAnswers), Pack) :-
    parser_pack_from_compiler_answers(
        ProductionAnswers, ClassAnswers, Pack).

compiled_star_payload_class(
    answer(list([sym('compile-pack-class-clause'), _,
                 list([sym('pp-class-except'),
                       sym('cetta-prime-star-payload-start-scalar'), _])]),
           _)).

pack_production_terminal_class(Productions, Class) :-
    member(list([sym('pp-production'), _, _, Items, _]), Productions),
    pack_items_terminal_class(Items, Class).

pack_items_terminal_class(
    list([sym('pp-items-cons'),
          list([sym('pp-terminal'),
                list([sym('pp-terminal-class'), Expression])]), _]),
    Class) :-
    class_expression_mentions(Expression, Class).
pack_items_terminal_class(
    list([sym('pp-items-cons'), _, More]), Class) :-
    pack_items_terminal_class(More, Class).

class_expression_mentions(list([sym('c-union'), Left, Right]), Class) :-
    ( class_expression_mentions(Left, Class)
    ; class_expression_mentions(Right, Class)
    ).
class_expression_mentions(Class, Class).

require_semantic_difference(Pack, Source, Grammar, PackState, Codes, Label) :-
    codepoints_node(Codes, Input),
    source_parse_results(Source, Grammar, Input, 1024,
                         completed(SourceResults)),
    parser_pack_parse_results(Pack, Source, PackState, Input, 1024,
                              completed(PackResults)),
    require(PackResults \== SourceResults, Label).

corrupt_first_proof([answer(Answer, Proof)|_], Answer, CorruptProof) :-
    Proof = list([sym(cert), _, Children]),
    CorruptProof =
        list([sym(cert), sym('no-such-parser-pack-rule'), Children]).

invalid_slot_answers(Answers0, Answers) :-
    select(answer(Query0, Proof), Answers0,
           answer(Query, Proof), Answers),
    Query0 = list([sym('compile-pack-production'), Owner,
                   list([sym('pp-production'), Label, State,
                         sym('pp-items-nil'), _])]),
    Query = list([sym('compile-pack-production'), Owner,
                  list([sym('pp-production'), Label, State,
                        sym('pp-items-nil'),
                        list([sym('pa-slot'), sym('q-zero')])])]),
    !.

mutate_reflected_rule(Program0, SourceRuleName, Mutation, Program) :-
    select(presentation(ReflectionName, Operators, Rules0, Source),
           Program0,
           presentation(ReflectionName, Operators, Rules, Source),
           Program),
    atom_concat('ReflectedFiniteHorn_', _, ReflectionName),
    select(rule(ReflectedName,
                list([sym('source-rule'), Presentation, Quoted0]), []),
           Rules0,
           rule(ReflectedName,
                list([sym('source-rule'), Presentation, Quoted]), []),
           Rules),
    Quoted0 = list([sym('q-rule'),
                    list([sym('q-sym'), sym(SourceRuleName)]), _, _]),
    mutate_quoted_class_rule(Mutation, Quoted0, Quoted),
    !.

mutate_quoted_class_rule(
    extra_guard,
    list([sym('q-rule'), Name, Head, Body]),
    list([sym('q-rule'), Name, Head,
          list([sym('q-cons'), ExtraGuard, Body])])) :-
    ExtraGuard =
        list([sym('q-app'),
              list([sym('q-sym'), sym('unsupported-class-guard')]),
              list([sym('q-cons'),
                    list([sym('q-var'), sym('q-zero')]),
                    sym('q-nil')])]).
mutate_quoted_class_rule(
    captured_variable,
    list([sym('q-rule'), Name, Head,
          list([sym('q-cons'), Goal0, Body])]),
    list([sym('q-rule'), Name, Head,
          list([sym('q-cons'), Goal, Body])])) :-
    Goal0 =
        list([sym('q-app'), list([sym('q-sym'), sym(different)]),
              list([sym('q-cons'),
                    list([sym('q-var'), Index]),
                    list([sym('q-cons'), Point, sym('q-nil')])])]),
    Goal =
        list([sym('q-app'), list([sym('q-sym'), sym(different)]),
              list([sym('q-cons'),
                    list([sym('q-var'),
                          list([sym('q-succ'), Index])]),
                    list([sym('q-cons'), Point, sym('q-nil')])])]).
mutate_quoted_class_rule(
    non_member_head,
    list([sym('q-rule'), Name,
          list([sym('q-app'), list([sym('q-sym'), sym(member)]),
                Arguments]),
          Body]),
    list([sym('q-rule'), Name,
          list([sym('q-app'), list([sym('q-sym'), sym('not-member')]),
                Arguments]),
          Body])).

corrupt_class_scalar_answers(Answers0, Answers) :-
    select(answer(Query0, Proof), Answers0,
           answer(Query, Proof), Answers),
    Query0 =
        list([sym('compile-pack-class-clause'), Owner,
              list([sym('pp-class-point'),
                    sym('cetta-prime-whitespace-scalar'), _])]),
    Query =
        list([sym('compile-pack-class-clause'), Owner,
              list([sym('pp-class-point'),
                    sym('cetta-prime-whitespace-scalar'),
                    list([sym(cp), int(0xd800)])])]),
    !.

corrupt_megalodon_action(parser_pack_v1(Productions0, ClassClauses),
                         parser_pack_v1(Productions, ClassClauses)) :-
    select(list([sym('pp-production'), Label, State, Items, _]),
           Productions0,
           list([sym('pp-production'), Label, State, Items,
                 list([sym('pa-const'), sym(corrupt)])]),
           Productions),
    Label = list([sym('pp-label'),
                  list([sym('pp-rel'), sym('mg-name-list')]),
                  sym('mg-parse-name-list')]),
    !.

delete_tail_cons(parser_pack_v1(Productions0, ClassClauses),
                 parser_pack_v1(Productions, ClassClauses)) :-
    exclude(is_tail_cons_production, Productions0, Productions),
    length(Productions0, Before),
    length(Productions, After),
    require(After =:= Before - 1, deleted_exactly_one_tail_production).

is_tail_cons_production(
    list([sym('pp-production'),
          list([sym('pp-label'), _, sym('mg-parse-name-list-tail-cons')]),
          _, _, _])).

delete_program_rule(PresentationName, RuleName, Program0, Program) :-
    select(presentation(PresentationName, Operators, Rules0, Source),
           Program0,
           presentation(PresentationName, Operators, Rules, Source),
           Program),
    exclude(rule_has_name(RuleName), Rules0, Rules),
    length(Rules0, Before),
    length(Rules, After),
    require(After =:= Before - 1,
            deleted_exactly_one_compiler_rule(RuleName)),
    !.

rule_has_name(Name, rule(Name, _, _)).

generic_purity_gate(PresentationRoot) :-
    presentation_path(PresentationRoot,
                      'parserpack/parser_pack_core_v1.metta', CorePath),
    presentation_path(PresentationRoot,
                      'compiler/parser_pack_compiler_v1.metta',
                      CompilerPath),
    generic_source_purity(CorePath),
    generic_source_purity(CompilerPath),
    source_file(
        parser_pack_eval:parser_pack_from_compiler_answers(_, _, _),
                EvaluatorPath),
    generic_source_purity(EvaluatorPath).

generic_source_purity(Path) :-
    read_file_to_string(Path, Text, [encoding(utf8)]),
    downcase_atom(Text, Lower),
    forall(member(Guest, [metamath, megalodon, tptp]),
           require(\+ sub_atom(Lower, _, _, _, Guest),
                   generic_source_names_guest(Path, Guest))).

case_with_label([case_result(Label, A, B, C, D, E)|_], Label,
                case_result(Label, A, B, C, D, E)) :-
    !.
case_with_label([_|Cases], Label, Result) :-
    case_with_label(Cases, Label, Result).

codepoints_node([], sym(nil)).
codepoints_node([Codepoint|Codepoints],
                list([sym(cons), list([sym(cp), int(Codepoint)]), Rest])) :-
    codepoints_node(Codepoints, Rest).

presentation_path(Root, Relative, Path) :-
    directory_file_path(Root, Relative, Path).

semantic_cases(
    [ semantic_case(megalodon, empty, sym('mg-name-list'),
                    list([sym('pp-rel'), sym('mg-name-list')]),
                    [], empty),
      semantic_case(megalodon, single_name, sym('mg-name-list'),
                    list([sym('pp-rel'), sym('mg-name-list')]),
                    [97], nonempty),
      semantic_case(megalodon, two_names, sym('mg-name-list'),
                    list([sym('pp-rel'), sym('mg-name-list')]),
                    [97, 32, 98], nonempty),
      semantic_case(megalodon, leading_digit, sym('mg-name-list'),
                    list([sym('pp-rel'), sym('mg-name-list')]),
                    [49, 97], empty),
      semantic_case(megalodon, unicode_initial, sym('mg-name-list'),
                    list([sym('pp-rel'), sym('mg-name-list')]),
                    [955], empty),
      semantic_case(metamath, empty,
                    list([sym(ref), sym(database)]),
                    list([sym('pp-def'), sym(database)]), [], nonempty),
      semantic_case(metamath, whitespace,
                    list([sym(ref), sym(database)]),
                    list([sym('pp-def'), sym(database)]), [32], nonempty),
      semantic_case(metamath, constant,
                    list([sym(ref), sym(database)]),
                    list([sym('pp-def'), sym(database)]),
                    [36, 99, 32, 97, 32, 36, 46], nonempty),
      semantic_case(metamath, truncated_constant,
                    list([sym(ref), sym(database)]),
                    list([sym('pp-def'), sym(database)]),
                    [36, 99, 32, 97, 32, 36], empty),
      semantic_case(metamath, unicode_junk,
                    list([sym(ref), sym(database)]),
                    list([sym('pp-def'), sym(database)]), [955], empty)
    ]).

prime_missing_states(
    [ list([sym('pp-sub'),
            list([sym('pp-def'), sym('cetta-prime-word-amp')]),
            sym(right)]),
      list([sym('pp-sub'),
            list([sym('pp-def'), sym('cetta-prime-word-bare-amp')]),
            sym(right)]),
      list([sym('pp-sub'),
            list([sym('pp-def'), sym('cetta-prime-word-general')]),
            sym(right)]),
      list([sym('pp-sub'),
            list([sym('pp-def'), sym('cetta-prime-word-star')]),
            sym(right)]),
      list([sym('pp-sub'),
            list([sym('pp-sub'),
                  list([sym('pp-def'),
                        sym('cetta-prime-anonymous-variable')]),
                  sym(body)]),
            sym(right)]),
      list([sym('pp-sub'),
            list([sym('pp-sub'),
                  list([sym('pp-def'), sym('cetta-prime-variable')]),
                  sym(body)]),
            sym(right)]),
      list([sym('pp-sub'),
            list([sym('pp-sub'),
                  list([sym('pp-sub'),
                        list([sym('pp-def'),
                              sym('cetta-prime-unquote')]),
                        sym(body)]),
                  sym(right)]),
            sym(left)])
    ]).

expected_megalodon_name_list(
    list([sym('pp-production'),
          list([sym('pp-label'),
                list([sym('pp-rel'), sym('mg-name-list')]),
                sym('mg-parse-name-list')]),
          list([sym('pp-rel'), sym('mg-name-list')]),
          list([sym('pp-items-cons'),
                list([sym('pp-nonterminal'),
                      list([sym('pp-def'), sym('mg-name-raw')])]),
                list([sym('pp-items-cons'),
                      list([sym('pp-nonterminal'),
                            list([sym('pp-rel'),
                                  sym('mg-name-list-tail')])]),
                      sym('pp-items-nil')])]),
          list([sym('pa-apply'), sym(cons),
                list([sym('pa-cons'),
                      list([sym('pa-slot'), sym('q-zero')]),
                      list([sym('pa-cons'),
                            list([sym('pa-slot'),
                                  list([sym('q-succ'), sym('q-zero')])]),
                            sym('pa-nil')])])])])).

expected_megalodon_tail_cons(
    list([sym('pp-production'),
          list([sym('pp-label'),
                list([sym('pp-rel'), sym('mg-name-list-tail')]),
                sym('mg-parse-name-list-tail-cons')]),
          list([sym('pp-rel'), sym('mg-name-list-tail')]),
          list([sym('pp-items-cons'),
                list([sym('pp-nonterminal'),
                      list([sym('pp-def'), sym('mg-skip1')])]),
                list([sym('pp-items-cons'),
                      list([sym('pp-nonterminal'),
                            list([sym('pp-def'), sym('mg-name-raw')])]),
                      list([sym('pp-items-cons'),
                            list([sym('pp-nonterminal'),
                                  list([sym('pp-rel'),
                                        sym('mg-name-list-tail')])]),
                            sym('pp-items-nil')])])]),
          list([sym('pa-apply'), sym(cons),
                list([sym('pa-cons'),
                      list([sym('pa-slot'),
                            list([sym('q-succ'), sym('q-zero')])]),
                      list([sym('pa-cons'),
                            list([sym('pa-slot'),
                                  list([sym('q-succ'),
                                        list([sym('q-succ'),
                                              sym('q-zero')])])]),
                            sym('pa-nil')])])])])).

expected_megalodon_tail_empty(
    list([sym('pp-production'),
          list([sym('pp-label'),
                list([sym('pp-rel'), sym('mg-name-list-tail')]),
                sym('mg-parse-name-list-tail-empty')]),
          list([sym('pp-rel'), sym('mg-name-list-tail')]),
          sym('pp-items-nil'),
          list([sym('pa-const'), sym(nil)])])).

expected_provenance(
    cetta_prime,
    '9c39249e6f56edfff37766fecd63ecea2a2664eb2a920e27d74d47c3fbc3f1dd',
    '978c2da36669eb6bede76f65181be923d61a9d755961e529b0bf4ebf563bb110',
    '5508914e399522cbb7a46b7142ff868a07369edc5413805bfdc917fe275abf96',
    179, 14).

expected_provenance(
    metamath,
    '8a233d1c49c7321064baebbd6396cbe87b5e9c628ec76a6f018e83cae52f00ab',
    '974dffd63658a7d2d3ad1d46d84035acfa655dfbdf5b574871ea96ec2cc564e2',
    'e1edf8b874950a7520948660c9df1f7341a35db682e6db84980d9318d4f93ae9',
    270, 375).

expected_provenance(
    megalodon,
    '2eba88d8bb4681540cb84bf6dd15e183cf83ad2821ae682cb94a754b4f2eb36a',
    'b0416dbc785283f29ddfeeeeb2c749555185d39c9bb122eacb982acf49278f9d',
    '47c5339c1c20a405bac48be40aa52d7a4b5fd60f41c2e624d7db34ae5c888327',
    140, 69).

expected_provenance(
    tptp,
    '36fb13b8e53ba3564893029d568d90cb054da6921f13a389439cf2dcc87f63d0',
    'f96c9437730eb1b1d6b7c7d39ea8f6e079c7398bb636d4252c61f23a3b2faa47',
    'de1638121568ddabf5838f7b9ce3a0e8039a6ffc0c143973e00db1b98fb03e07',
    688, 562).

pack_case(
    cetta_prime,
    'cetta-prime-file',
    [ 'core/syntax_core_v1.metta',
      'shared/lookahead_core_v1.metta',
      'shared/ground_relations_v1.metta',
      'shared/cetta_prime_scalar_classes_v1.metta',
      'languages/cetta_prime_reader_v1.metta'
    ],
    179, 14, 179, 14,
    '103d3ba48e6cebf4649e37387f5b379cbad3be3a9f12dc4e6a6c33c0bc17fc50',
    Missing) :-
    prime_missing_states(Missing).

pack_case(
    metamath,
    database,
    [ 'core/syntax_core_v1.metta',
      'shared/lookahead_core_v1.metta',
      'languages/metamath_appendix_e_v1.metta'
    ],
    270, 375, 270, 375,
    '98fd04dc87469b5b5d4ee462ccc1593f6c2e294261b149dffdc7f7a5bd5486a3',
    []).

pack_case(
    megalodon,
    'mg-document',
    [ 'core/syntax_core_v1.metta',
      'shared/lookahead_core_v1.metta',
      'shared/char_core_v1.metta',
      'languages/megalodon_dynamic_v1.metta'
    ],
    140, 563, 138, 69,
    'd30ec38acd7f7661b8475c66ab0011900f81dda61b4a9354f6f0a844c0ac95d8',
    []).

pack_case(
    tptp,
    'tptp-file',
    [ 'core/syntax_core_v1.metta',
      'shared/lookahead_core_v1.metta',
      'shared/char_core_v1.metta',
      'languages/tptp_fof_cnf_v1.metta'
    ],
    688, 562, 688, 562,
    'ef130f11d8e41cfd9cb0f9cecd7821727b5de63a818a0cbebeb5a7ee9a4b5c49',
    []).

require(Condition, Label) :-
    ( call(Condition) -> true
    ; throw(error(gate_failed(Label), test_parser_pack_compiler))
    ).
