:- module(test_parser_pack_glr,
          [ run_parser_pack_glr_gate/2
          ]).

:- use_module(parser_pack_eval).
:- use_module(parser_pack_forest).
:- use_module(parser_pack_gll).
:- use_module(parser_pack_glr).
:- use_module(test_parser_pack_gll, []).
:- use_module(finite_horn_gslt_v1, [render_term/2]).
:- use_module(library(assoc)).
:- use_module(library(crypto)).

:- initialization(main, main).

main :-
    catch(run, Error, (print_message(error, Error), halt(1))),
    halt.

run :-
    current_prolog_flag(argv, Arguments),
    ( Arguments = [PresentationRoot] -> true
    ; throw(error(gate_failed(expected_presentation_root),
                  test_parser_pack_glr))
    ),
    run_parser_pack_glr_gate(PresentationRoot, Summary),
    Summary = summary(HandCases, LanguageCases, Boundaries,
                      TableCases, Replays, Digest),
    Total is HandCases + LanguageCases + Boundaries,
    format('(ParserPackGLRPeTTaSummary ~d ~d ~d ~d ~d ~d ~w 0)~n',
           [Total, HandCases, LanguageCases, Boundaries,
            TableCases, Replays, Digest]).

run_parser_pack_glr_gate(
    PresentationRoot,
    summary(3, LanguageCaseCount, 7, 3, Replays, MatrixDigest)) :-
    structural_agreement_gate(StructuralReplays),
    compile_real_plans(PresentationRoot, RealPlans),
    real_language_agreement_gate(
        RealPlans, LanguageCaseCount, LanguageReplays, MatrixDigest),
    boundary_and_mutation_gate(PresentationRoot, MutationReplays),
    generic_purity_gate,
    Replays is StructuralReplays + LanguageReplays + MutationReplays.

structural_agreement_gate(Replays) :-
    structural_case(left, LeftPack, LeftStart, LeftInput,
                    glr_table_summary(3, 4, 7, 1, 0)),
    structural_case(ambiguous, AmbiguousPack, AmbiguousStart,
                    AmbiguousInput,
                    glr_table_summary(3, 3, 4, 1, 1)),
    structural_case(epsilon, EpsilonPack, EpsilonStart, EpsilonInput,
                    glr_table_summary(2, 2, 2, 1, 0)),
    run_structural_case(left, LeftPack, LeftStart, LeftInput),
    run_structural_case(ambiguous, AmbiguousPack, AmbiguousStart,
                        AmbiguousInput),
    run_structural_case(epsilon, EpsilonPack, EpsilonStart, EpsilonInput),
    Replays = 3.

structural_case(left, Pack, Start, Input, ExpectedTable) :-
    test_parser_pack_gll:productive_left_recursive_pack(Pack, Start),
    test_parser_pack_gll:codepoints_node([97, 97, 97], Input),
    require_table(Pack, Start, ExpectedTable).
structural_case(ambiguous, Pack, Start, Input, ExpectedTable) :-
    test_parser_pack_gll:ambiguous_pack(Pack, Start),
    test_parser_pack_gll:codepoints_node([97], Input),
    require_table(Pack, Start, ExpectedTable).
structural_case(epsilon, Pack, Start, Input, ExpectedTable) :-
    test_parser_pack_gll:epsilon_pack(Pack, Start),
    test_parser_pack_gll:codepoints_node([], Input),
    require_table(Pack, Start, ExpectedTable).

require_table(Pack, Start, Expected) :-
    parser_pack_glr_compile(Pack, Start, 1000, completed(Plan)),
    parser_pack_glr_table_summary(Plan, Summary),
    require(Summary == Expected, exact_structural_glr_table(Start)).

run_structural_case(Label, Pack, Start, Input) :-
    parser_pack_gll_parse(Pack, [], Start, Input, 10000, GLLOutcome),
    parser_pack_glr_parse(Pack, [], Start, Input, 1000, 100000,
                          GLROutcome),
    require(GLROutcome == GLLOutcome,
            exact_structural_gll_glr_agreement(Label)),
    require(GLROutcome = completed(Result),
            structural_glr_completed(Label)),
    require(parser_pack_replay_forest(Pack, [], Start, Input, Result),
            structural_glr_forest_replay(Label)),
    test_parser_pack_gll:result_forest(Result, Forest),
    parser_pack_forest_results(Pack, Input, Forest, 256, SemanticOutcome),
    require(SemanticOutcome = completed([_|_]),
            structural_glr_semantics(Label)).

compile_real_plans(PresentationRoot,
                   real_plans(
                       language_plan(megalodon, MegSource, MegPack,
                                     MegState, MegPlan),
                       language_plan(metamath, MMSource, MMPack,
                                     MMState, MMPlan),
                       language_plan(tptp, TPTPSource, TPTPPack,
                                     TPTPState, TPTPPlan))) :-
    test_parser_pack_gll:compile_language_pack(
        PresentationRoot, megalodon, MegSource, MegPack, _),
    MegState = list([sym('pp-rel'), sym('mg-name-list')]),
    parser_pack_glr_compile(MegPack, MegState, 100000,
                            completed(MegPlan)),
    parser_pack_glr_table_summary(
        MegPlan, glr_table_summary(139, 17, 66, 19, 2)),

    test_parser_pack_gll:compile_language_pack(
        PresentationRoot, metamath, MMSource, MMPack, _),
    MMState = list([sym('pp-def'), sym(database)]),
    parser_pack_glr_compile(MMPack, MMState, 100000,
                            completed(MMPlan)),
    parser_pack_glr_table_summary(
        MMPlan, glr_table_summary(271, 341, 833, 652, 11)),

    test_parser_pack_gll:compile_language_pack(
        PresentationRoot, tptp, TPTPSource, TPTPPack, _),
    TPTPState = list([sym('pp-def'), sym('tptp-file')]),
    parser_pack_glr_compile(TPTPPack, TPTPState, 100000,
                            completed(TPTPPlan)),
    parser_pack_glr_table_summary(
        TPTPPlan, glr_table_summary(689, 851, 6690, 4725, 73)).

real_language_agreement_gate(RealPlans, CaseCount, Replays, MatrixDigest) :-
    test_parser_pack_gll:real_cases(Cases),
    maplist(real_case_row(RealPlans), Cases, Rows),
    length(Rows, CaseCount),
    maplist(real_row_text, Rows, Texts),
    atomics_to_string(Texts, "\n", Payload),
    crypto_data_hash(Payload, MatrixDigest, [algorithm(sha256)]),
    require(MatrixDigest ==
                '34791ce2f3fcf354d171ba43c09da37a6220cf203dc195b587cf8df9bac90141',
            exact_cross_backend_matrix_digest),
    Replays = CaseCount.

real_case_row(
    RealPlans,
    real_case(Language, Label, SourceGrammar, PackState,
              Codes, ExpectedShape),
    real_row(Language, Label, Decision, Results, ForestDigest)) :-
    select_language_plan(RealPlans, Language,
                         Source, Pack, PackState, Plan),
    test_parser_pack_gll:codepoints_node(Codes, Input),
    test_parser_pack_gll:source_parse_results(
        Source, SourceGrammar, Input, 1024, SourceOutcome),
    parser_pack_parse_results(Pack, Source, PackState, Input, 1024,
                              ReferenceOutcome),
    parser_pack_gll_parse(Pack, Source, PackState, Input, 1000000,
                          GLLOutcome),
    parser_pack_glr_parse_compiled(Plan, Source, Input, 1000000,
                                   GLROutcome),
    require(SourceOutcome = completed(SourceResults),
            source_semantics_completed(Language, Label)),
    require(ReferenceOutcome = completed(ReferenceResults),
            pack_reference_completed(Language, Label)),
    require(GLLOutcome = completed(GLLResult),
            gll_completed(Language, Label)),
    require(GLROutcome = completed(GLRResult),
            glr_completed(Language, Label)),
    require(ReferenceResults == SourceResults,
            reference_source_complete_agreement(Language, Label)),
    require(GLRResult == GLLResult,
            exact_gll_glr_result_agreement(Language, Label)),
    require(parser_pack_replay_forest(
                Pack, Source, PackState, Input, GLRResult),
            glr_forest_replay(Language, Label)),
    test_parser_pack_gll:result_forest(GLRResult, Forest),
    parser_pack_forest_results(Pack, Input, Forest, 4096, SemanticOutcome),
    require(SemanticOutcome = completed(GLRResults),
            glr_semantics_completed(Language, Label)),
    require(GLRResults == SourceResults,
            glr_source_complete_agreement(Language, Label)),
    require_result_shape(ExpectedShape, SourceResults, Language, Label),
    test_parser_pack_gll:result_decision(GLRResult, Decision),
    Results = SourceResults,
    parser_pack_forest_digest(Forest, ForestDigest).

select_language_plan(
    real_plans(language_plan(Language, Source, Pack, State, Plan), _, _),
    Language, Source, Pack, State, Plan) :- !.
select_language_plan(
    real_plans(_, language_plan(Language, Source, Pack, State, Plan), _),
    Language, Source, Pack, State, Plan) :- !.
select_language_plan(
    real_plans(_, _, language_plan(Language, Source, Pack, State, Plan)),
    Language, Source, Pack, State, Plan).

require_result_shape(empty, Results, Language, Label) :-
    require(Results == [], expected_empty(Language, Label)).
require_result_shape(nonempty, Results, Language, Label) :-
    require(Results = [_|_], expected_nonempty(Language, Label)).

real_row_text(real_row(Language, Label, Decision, Results, ForestDigest),
              Text) :-
    maplist(semantic_result_text, Results, ResultTexts),
    atomics_to_string(ResultTexts, "|", ResultPayload),
    format(string(Text), '~w/~w/~w/~w:~s',
           [Language, Label, Decision, ForestDigest, ResultPayload]).

semantic_result_text(result(Value, Rest), Text) :-
    render_term(list([sym(result), Value, Rest]), Text).

boundary_and_mutation_gate(PresentationRoot, Replays) :-
    test_parser_pack_gll:invalid_slot_pack(InvalidPack, InvalidStart),
    parser_pack_glr_compile(InvalidPack, InvalidStart, 100,
                            InvalidOutcome),
    require(InvalidOutcome == invalid_presentation(parser_pack),
            invalid_pack_fails_closed),

    test_parser_pack_gll:open_pack(OpenPack, OpenStart),
    parser_pack_glr_compile(OpenPack, OpenStart, 100, OpenOutcome),
    require(OpenOutcome == unsupported(missing_states([sym('missing-state')])),
            open_pack_is_unsupported),

    test_parser_pack_gll:ambiguous_pack(AmbiguousPack, AmbiguousStart),
    parser_pack_glr_compile(AmbiguousPack, AmbiguousStart, 1,
                            StateResource),
    require(StateResource == resource_exhausted(table_states(1)),
            table_state_limit_is_resource),

    parser_pack_glr_compile(AmbiguousPack, AmbiguousStart, 100,
                            completed(AmbiguousPlan)),
    test_parser_pack_gll:codepoints_node([97], AmbiguousInput),
    parser_pack_glr_parse_compiled(
        AmbiguousPlan, [], AmbiguousInput, 1, FactResource),
    require(FactResource == resource_exhausted(facts(1)),
            runtime_fact_limit_is_resource),

    SurrogateInput = list([sym(cons), list([sym(cp), int(0xd800)]),
                           sym(nil)]),
    parser_pack_glr_parse_compiled(
        AmbiguousPlan, [], SurrogateInput, 1000, UnicodeOutcome),
    require(UnicodeOutcome == invalid_input(codepoint_list),
            surrogate_is_invalid_input),

    parser_pack_gll_parse(AmbiguousPack, [], AmbiguousStart,
                          AmbiguousInput, 1000, completed(GLLResult)),
    delete_one_conflicting_reduce(AmbiguousPlan, IncompletePlan),
    parser_pack_glr_parse_compiled(
        IncompletePlan, [], AmbiguousInput, 1000,
        completed(IncompleteResult)),
    require(IncompleteResult \== GLLResult,
            deleted_reduce_detected_by_complete_result_differential),
    require(parser_pack_replay_forest(
                AmbiguousPack, [], AmbiguousStart, AmbiguousInput,
                IncompleteResult),
            incomplete_but_locally_valid_forest_replays),

    test_parser_pack_gll:compile_language_pack(
        PresentationRoot, prime, PrimeSource, PrimePack, PrimeStart),
    parser_pack_glr_compile(PrimePack, PrimeStart, 100000, PrimeOutcome),
    test_parser_pack_gll:prime_missing_states(PrimeMissing),
    require(PrimeOutcome == unsupported(missing_states(PrimeMissing)),
            prime_peek_boundary_is_unsupported),
    require(PrimeSource = [_|_], prime_source_loaded),
    Replays = 1.

delete_one_conflicting_reduce(
    parser_pack_glr_plan_v1(
        Pack, Start,
        glr_table(Productions, ProductionAssoc, States,
                  ActionAssoc0, GotoAssoc, Conflicts), Matchers),
    parser_pack_glr_plan_v1(
        Pack, Start,
        glr_table(Productions, ProductionAssoc, States,
                  ActionAssoc, GotoAssoc, Conflicts), Matchers)) :-
    assoc_to_list(ActionAssoc0, ActionPairs),
    member(Key-Bucket0, ActionPairs),
    select(reduce(_), Bucket0, Bucket),
    member(reduce(_), Bucket),
    put_assoc(Key, ActionAssoc0, Bucket, ActionAssoc),
    !.

generic_purity_gate :-
    source_file(parser_pack_glr:parser_pack_glr_parse(_, _, _, _, _, _, _),
                GLRPath),
    read_file_to_string(GLRPath, Text, [encoding(utf8)]),
    downcase_atom(Text, Lower),
    forall(member(Guest, [metamath, megalodon, tptp]),
           require(\+ sub_atom(Lower, _, _, _, Guest),
                   generic_source_names_guest(GLRPath, Guest))).

require(Condition, Label) :-
    ( call(Condition) -> true
    ; throw(error(gate_failed(Label), test_parser_pack_glr))
    ).
