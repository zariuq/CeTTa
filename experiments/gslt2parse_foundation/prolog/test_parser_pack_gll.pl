:- module(test_parser_pack_gll,
          [ run_parser_pack_gll_gate/2,
            parser_pack_gll_real_rows/2
          ]).

:- use_module(finite_horn_eval).
:- use_module(finite_horn_gslt_v1, [render_term/2]).
:- use_module(parser_pack_eval).
:- use_module(parser_pack_forest).
:- use_module(parser_pack_gll).
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
                  test_parser_pack_gll))
    ),
    run_parser_pack_gll_gate(PresentationRoot, Summary),
    Summary = summary(HandCases, LanguageCases, Mutations, Replays, Digest),
    Total is HandCases + LanguageCases + Mutations,
    format('(ParserPackGLLPeTTaSummary ~d ~d ~d ~d ~d ~w 0)~n',
           [Total, HandCases, LanguageCases, Mutations, Replays, Digest]).

run_parser_pack_gll_gate(PresentationRoot,
                         summary(3, LanguageCaseCount, 8, Replays,
                                 MatrixDigest)) :-
    hand_pack_gate(HandReplays),
    typed_outcome_and_mutation_gate(MutationReplays),
    real_language_gate(PresentationRoot, LanguageCaseCount,
                       LanguageReplays, MatrixDigest),
    generic_purity_gate,
    Replays is HandReplays + MutationReplays + LanguageReplays.

hand_pack_gate(Replays) :-
    productive_left_recursive_pack(LeftPack, LeftStart),
    codepoints_node([97, 97, 97], LeftInput),
    parser_pack_gll_parse(LeftPack, [], LeftStart, LeftInput, 10000,
                          LeftOutcome),
    require(LeftOutcome = completed(LeftResult),
            left_recursive_gll_completed),
    require(result_decision(LeftResult, accepted),
            productive_left_recursion_accepted),
    require(parser_pack_replay_forest(
                LeftPack, [], LeftStart, LeftInput, LeftResult),
            left_recursive_forest_replay),
    result_forest(LeftResult, Forest),
    parser_pack_forest_results(LeftPack, LeftInput,
                               Forest, 128, LeftSemanticOutcome),
    require(LeftSemanticOutcome = completed(LeftResults),
            left_recursive_semantics_completed),
    expected_left_recursive_results(ExpectedLeftResults),
    require(LeftResults == ExpectedLeftResults,
            complete_left_recursive_results),

    ambiguous_pack(AmbiguousPack, AmbiguousStart),
    codepoints_node([97], AmbiguousInput),
    parser_pack_gll_parse(AmbiguousPack, [], AmbiguousStart,
                          AmbiguousInput, 1000, AmbiguousOutcome),
    require(AmbiguousOutcome = completed(AmbiguousResult),
            ambiguous_gll_completed),
    require(result_decision(AmbiguousResult, accepted),
            ambiguous_input_accepted),
    require(parser_pack_replay_forest(
                AmbiguousPack, [], AmbiguousStart, AmbiguousInput,
                AmbiguousResult),
            ambiguous_forest_replay),
    result_forest(AmbiguousResult, AmbiguousForest),
    parser_pack_forest_results(
        AmbiguousPack, AmbiguousInput, AmbiguousForest, 64,
        AmbiguousSemanticOutcome),
    require(AmbiguousSemanticOutcome = completed(AmbiguousResults),
            ambiguous_semantics_completed),
    expected_ambiguous_results(ExpectedAmbiguousResults),
    require(AmbiguousResults == ExpectedAmbiguousResults,
            complete_ambiguous_results),
    require(result_ambiguity(AmbiguousResult, 1, 2),
            packed_ambiguity_recorded),

    epsilon_pack(EpsilonPack, EpsilonStart),
    codepoints_node([], EmptyInput),
    parser_pack_gll_parse(EpsilonPack, [], EpsilonStart, EmptyInput,
                          100, EpsilonOutcome),
    require(EpsilonOutcome = completed(EpsilonResult),
            epsilon_gll_completed),
    require(result_decision(EpsilonResult, accepted), epsilon_accepted),
    require(parser_pack_replay_forest(
                EpsilonPack, [], EpsilonStart, EmptyInput, EpsilonResult),
            epsilon_forest_replay),
    result_forest(EpsilonResult, EpsilonForest),
    parser_pack_forest_results(
        EpsilonPack, EmptyInput, EpsilonForest, 32,
        completed(EpsilonResults)),
    require(EpsilonResults == [result(sym(empty), sym(nil))],
            exact_epsilon_result),
    Replays = 3.

typed_outcome_and_mutation_gate(Replays) :-
    epsilon_pack(EpsilonPack, EpsilonStart),
    invalid_slot_pack(InvalidPack, InvalidStart),
    codepoints_node([], EmptyInput),
    parser_pack_gll_parse(InvalidPack, [], InvalidStart, EmptyInput, 100,
                          InvalidOutcome),
    require(InvalidOutcome == invalid_presentation(parser_pack),
            invalid_action_fails_closed),

    open_pack(OpenPack, OpenStart),
    parser_pack_gll_parse(OpenPack, [], OpenStart, EmptyInput, 100,
                          OpenOutcome),
    OpenMissing = [sym('missing-state')],
    require(OpenOutcome == unsupported(missing_states(OpenMissing)),
            open_pack_is_unsupported),

    productive_left_recursive_pack(LeftPack, LeftStart),
    codepoints_node([97], OneInput),
    parser_pack_gll_parse(LeftPack, [], LeftStart, OneInput, 1,
                          ResourceOutcome),
    require(ResourceOutcome == resource_exhausted(descriptors(1)),
            descriptor_limit_is_resource_outcome),

    SurrogateInput = list([sym(cons), list([sym(cp), int(0xd800)]),
                           sym(nil)]),
    parser_pack_gll_parse(EpsilonPack, [], EpsilonStart, SurrogateInput,
                          100, UnicodeOutcome),
    require(UnicodeOutcome == invalid_input(codepoint_list),
            surrogate_is_invalid_input),

    invalid_terminal_pack(InvalidTerminalPack, InvalidTerminalStart),
    parser_pack_gll_parse(InvalidTerminalPack, [], InvalidTerminalStart,
                          EmptyInput, 100, InvalidTerminalOutcome),
    require(InvalidTerminalOutcome == invalid_presentation(parser_pack),
            invalid_terminal_scalar_fails_closed),

    ambiguous_pack(AmbiguousPack, AmbiguousStart),
    codepoints_node([97], AmbiguousInput),
    parser_pack_gll_parse(AmbiguousPack, [], AmbiguousStart,
                          AmbiguousInput, 1000,
                          completed(AmbiguousResult)),
    corrupt_forest_certificate(AmbiguousResult, CorruptCertificateResult),
    require(\+ parser_pack_replay_forest(
                AmbiguousPack, [], AmbiguousStart, AmbiguousInput,
                CorruptCertificateResult),
            corrupt_forest_certificate_rejected),
    delete_forest_terminal_and_recertify(
        AmbiguousResult, MissingNodeResult),
    require(\+ parser_pack_replay_forest(
                AmbiguousPack, [], AmbiguousStart, AmbiguousInput,
                MissingNodeResult),
            missing_reachable_node_rejected),
    SubstituteSource = [presentation(substitute, [], [], synthetic)],
    require(\+ parser_pack_replay_forest(
                AmbiguousPack, SubstituteSource, AmbiguousStart,
                AmbiguousInput, AmbiguousResult),
            substituted_source_package_rejected),
    Replays = 2.

real_language_gate(PresentationRoot, CaseCount, Replays, MatrixDigest) :-
    compile_language_pack(PresentationRoot, prime, PrimeSource,
                          PrimePack, PrimeStart),
    codepoints_node([], EmptyInput),
    parser_pack_gll_parse(PrimePack, PrimeSource, PrimeStart, EmptyInput,
                          1000, PrimeOutcome),
    prime_missing_states(PrimeMissing),
    require(PrimeOutcome == unsupported(missing_states(PrimeMissing)),
            prime_peek_boundary_is_unsupported),

    parser_pack_gll_real_rows(PresentationRoot, Rows),
    length(Rows, CaseCount),
    maplist(real_row_text, Rows, Texts),
    atomics_to_string(Texts, "\n", Payload),
    crypto_data_hash(Payload, MatrixDigest, [algorithm(sha256)]),
    Replays = CaseCount.

parser_pack_gll_real_rows(PresentationRoot, Rows) :-
    compile_language_pack(PresentationRoot, megalodon, MegSource,
                          MegPack, _),
    compile_language_pack(PresentationRoot, metamath, MMSource,
                          MMPack, _),
    compile_language_pack(PresentationRoot, tptp, TPTPSource,
                          TPTPPack, _),
    real_cases(Cases),
    maplist(real_case_row(MegSource, MegPack, MMSource, MMPack,
                          TPTPSource, TPTPPack),
            Cases, Rows).

real_case_row(MegSource, MegPack, MMSource, MMPack,
              TPTPSource, TPTPPack,
              real_case(Language, Label, SourceGrammar, PackState,
                        Codes, ExpectedShape),
              real_row(Language, Label, Decision, Results, ForestDigest)) :-
    ( Language == megalodon -> Source = MegSource, Pack = MegPack
    ; Language == metamath -> Source = MMSource, Pack = MMPack
    ; Language == tptp -> Source = TPTPSource, Pack = TPTPPack
    ),
    codepoints_node(Codes, Input),
    source_parse_results(Source, SourceGrammar, Input, 1024, SourceOutcome),
    parser_pack_parse_results(Pack, Source, PackState, Input, 1024,
                              ReferenceOutcome),
    parser_pack_gll_parse(Pack, Source, PackState, Input, 1000000,
                          GLLOutcome),
    require(SourceOutcome = completed(SourceResults),
            source_semantics_completed(Language, Label)),
    require(ReferenceOutcome = completed(ReferenceResults),
            pack_reference_completed(Language, Label)),
    require(GLLOutcome = completed(GLLResult),
            gll_completed(Language, Label)),
    require(parser_pack_replay_forest(
                Pack, Source, PackState, Input, GLLResult),
            parser_forest_replay(Language, Label)),
    result_forest(GLLResult, Forest),
    parser_pack_forest_results(Pack, Input, Forest, 4096, SemanticOutcome),
    require(SemanticOutcome = completed(GLLResults),
            gll_semantics_completed(Language, Label)),
    require(ReferenceResults == SourceResults,
            reference_source_complete_agreement(Language, Label)),
    require(GLLResults == SourceResults,
            gll_source_complete_agreement(Language, Label)),
    require_result_shape(ExpectedShape, SourceResults, Language, Label),
    expected_full_decision(SourceResults, ExpectedDecision),
    require(result_decision(GLLResult, ExpectedDecision),
            full_decision_agreement(Language, Label)),
    result_decision(GLLResult, Decision),
    Results = SourceResults,
    parser_pack_forest_digest(Forest, ForestDigest).

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

expected_full_decision(Results, accepted) :-
    memberchk(result(_, sym(nil)), Results),
    !.
expected_full_decision(_, rejected).

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

compile_language_pack(PresentationRoot, Language, Source, Pack, Start) :-
    language_pack_case(Language, RelativePaths, StartName),
    parser_pack_reference_compile(PresentationRoot, RelativePaths, 4096,
                                  Outcome),
    require(Outcome = completed(parser_pack_compilation(
                Source, _, compiler_answers(_, _), Pack)),
            compiler_completed(Language)),
    Start = list([sym('pp-def'), sym(StartName)]).

productive_left_recursive_pack(
    parser_pack_v1([Grow, Leaf], []), Start) :-
    Start = sym('test-left'),
    terminal_char_item(97, A),
    nonterminal_item(Start, Self),
    pack_items([Self, A], GrowItems),
    apply_action(grow, [slot(0), slot(1)], GrowAction),
    Grow = list([sym('pp-production'), sym('left-grow'), Start,
                 GrowItems, GrowAction]),
    pack_items([A], LeafItems),
    apply_action(leaf, [slot(0)], LeafAction),
    Leaf = list([sym('pp-production'), sym('left-leaf'), Start,
                 LeafItems, LeafAction]).

ambiguous_pack(parser_pack_v1([Left, Right], []), Start) :-
    Start = sym('test-ambiguous'),
    terminal_char_item(97, A),
    pack_items([A], Items),
    apply_action(left, [slot(0)], LeftAction),
    apply_action(right, [slot(0)], RightAction),
    Left = list([sym('pp-production'), sym('ambiguous-left'), Start,
                 Items, LeftAction]),
    Right = list([sym('pp-production'), sym('ambiguous-right'), Start,
                  Items, RightAction]).

epsilon_pack(parser_pack_v1([Production], []), Start) :-
    Start = sym('test-epsilon'),
    Production = list([sym('pp-production'), sym('epsilon'), Start,
                       sym('pp-items-nil'),
                       list([sym('pa-const'), sym(empty)])]).

invalid_slot_pack(parser_pack_v1([Production], []), Start) :-
    Start = sym('test-invalid'),
    slot_action(0, InvalidAction),
    Production = list([sym('pp-production'), sym('invalid-slot'), Start,
                       sym('pp-items-nil'), InvalidAction]).

open_pack(parser_pack_v1([Production], []), Start) :-
    Start = sym('test-open'),
    nonterminal_item(sym('missing-state'), Missing),
    pack_items([Missing], Items),
    slot_action(0, Action),
    Production = list([sym('pp-production'), sym('open'), Start, Items,
                       Action]).

invalid_terminal_pack(parser_pack_v1([Production], []), Start) :-
    Start = sym('test-invalid-terminal'),
    InvalidTerminal =
        list([sym('pp-terminal'),
              list([sym('pp-terminal-char'),
                    list([sym(cp), int(0xd800)])])]),
    pack_items([InvalidTerminal], Items),
    slot_action(0, Action),
    Production = list([sym('pp-production'), sym('invalid-terminal'), Start,
                       Items, Action]).

terminal_char_item(Codepoint,
                   list([sym('pp-terminal'),
                         list([sym('pp-terminal-char'),
                               list([sym(cp), int(Codepoint)])])])).

nonterminal_item(State, list([sym('pp-nonterminal'), State])).

pack_items([], sym('pp-items-nil')).
pack_items([Item|Items],
           list([sym('pp-items-cons'), Item, More])) :-
    pack_items(Items, More).

apply_action(Head, Slots,
             list([sym('pa-apply'), sym(Head), Arguments])) :-
    action_arguments(Slots, Arguments).

action_arguments([], sym('pa-nil')).
action_arguments([slot(Index)|Slots],
                 list([sym('pa-cons'), Slot, More])) :-
    slot_action(Index, Slot),
    action_arguments(Slots, More).

slot_action(Index, list([sym('pa-slot'), Peano])) :-
    integer(Index),
    Index >= 0,
    peano(Index, Peano).

peano(0, sym('q-zero')).
peano(Index, list([sym('q-succ'), Previous])) :-
    Index > 0,
    Prior is Index - 1,
    peano(Prior, Previous).

expected_left_recursive_results(Expected) :-
    Cp = list([sym(cp), int(97)]),
    Leaf = list([sym(leaf), Cp]),
    Grow2 = list([sym(grow), Leaf, Cp]),
    Grow3 = list([sym(grow), Grow2, Cp]),
    codepoints_node([97, 97], Rest2),
    codepoints_node([97], Rest1),
    sort([result(Leaf, Rest2), result(Grow2, Rest1),
          result(Grow3, sym(nil))], Expected).

expected_ambiguous_results(Expected) :-
    Cp = list([sym(cp), int(97)]),
    sort([result(list([sym(left), Cp]), sym(nil)),
          result(list([sym(right), Cp]), sym(nil))], Expected).

result_decision(parser_result(decision(Decision), _, _, _, _), Decision).

result_ambiguity(parser_result(_, _, ambiguity(roots(Roots),
                                                packed_choices(Choices)),
                               _, _), Roots, Choices).

result_forest(parser_result(_, _, _, Forest, _), Forest).

corrupt_forest_certificate(
    parser_result(Decision, Coverage, Ambiguity, Forest,
                  evidence(forest_certificate(
                      parser_forest_certificate_v1(
                          PackDigest, SourceDigest, Start, InputDigest, _)))),
    parser_result(Decision, Coverage, Ambiguity, Forest,
                  evidence(forest_certificate(
                      parser_forest_certificate_v1(
                          PackDigest, SourceDigest, Start, InputDigest,
                          corrupt))))).

delete_forest_terminal_and_recertify(
    parser_result(Decision, Coverage, Ambiguity,
                  parser_forest(Start, Length, Roots, Nodes0, Choices),
                  evidence(forest_certificate(
                      parser_forest_certificate_v1(
                          PackDigest, SourceDigest, CertStart,
                          InputDigest, _)))),
    parser_result(Decision, Coverage, Ambiguity, Forest,
                  evidence(forest_certificate(
                      parser_forest_certificate_v1(
                          PackDigest, SourceDigest, CertStart,
                          InputDigest, Digest))))) :-
    select(parser_terminal(_, _, _, _), Nodes0, Nodes),
    Forest = parser_forest(Start, Length, Roots, Nodes, Choices),
    parser_pack_forest_digest(Forest, Digest),
    !.

codepoints_node([], sym(nil)).
codepoints_node([Codepoint|Codepoints],
                list([sym(cons), list([sym(cp), int(Codepoint)]), Rest])) :-
    codepoints_node(Codepoints, Rest).

prime_missing_states(
    [ list([sym('pp-sub'),
            list([sym('pp-sub'),
                  list([sym('pp-def'), sym('cetta-prime-variable')]),
                  sym(body)]),
            sym(right)]),
      list([sym('pp-sub'),
            list([sym('pp-sub'),
                  list([sym('pp-def'),
                        sym('cetta-prime-word-bare-dollar')]),
                  sym(body)]),
            sym(right)]),
      list([sym('pp-sub'),
            list([sym('pp-sub'),
                  list([sym('pp-def'), sym('cetta-prime-word-general')]),
                  sym(body)]),
            sym(right)])
    ]).

language_pack_case(
    prime,
    [ 'core/syntax_core_v1.metta',
      'shared/lookahead_core_v1.metta',
      'shared/ground_relations_v1.metta',
      'shared/cetta_prime_scalar_classes_v1.metta',
      'languages/cetta_prime_reader_v1.metta'
    ],
    'cetta-prime-file').

language_pack_case(
    metamath,
    [ 'core/syntax_core_v1.metta',
      'shared/lookahead_core_v1.metta',
      'languages/metamath_appendix_e_v1.metta'
    ],
    database).

language_pack_case(
    megalodon,
    [ 'core/syntax_core_v1.metta',
      'shared/lookahead_core_v1.metta',
      'shared/char_core_v1.metta',
      'languages/megalodon_dynamic_v1.metta'
    ],
    'mg-document').

language_pack_case(
    tptp,
    [ 'core/syntax_core_v1.metta',
      'shared/lookahead_core_v1.metta',
      'shared/char_core_v1.metta',
      'languages/tptp_fof_cnf_v1.metta'
    ],
    'tptp-file').

real_cases(
    [ real_case(megalodon, empty, sym('mg-name-list'),
                list([sym('pp-rel'), sym('mg-name-list')]), [], empty),
      real_case(megalodon, single_name, sym('mg-name-list'),
                list([sym('pp-rel'), sym('mg-name-list')]), [97], nonempty),
      real_case(megalodon, two_names, sym('mg-name-list'),
                list([sym('pp-rel'), sym('mg-name-list')]),
                [97, 32, 98], nonempty),
      real_case(megalodon, leading_digit, sym('mg-name-list'),
                list([sym('pp-rel'), sym('mg-name-list')]),
                [49, 97], empty),
      real_case(megalodon, unicode_initial, sym('mg-name-list'),
                list([sym('pp-rel'), sym('mg-name-list')]), [955], empty),
      real_case(metamath, empty, list([sym(ref), sym(database)]),
                list([sym('pp-def'), sym(database)]), [], nonempty),
      real_case(metamath, whitespace, list([sym(ref), sym(database)]),
                list([sym('pp-def'), sym(database)]), [32], nonempty),
      real_case(metamath, constant, list([sym(ref), sym(database)]),
                list([sym('pp-def'), sym(database)]),
                [36, 99, 32, 97, 32, 36, 46], nonempty),
      real_case(metamath, truncated_constant,
                list([sym(ref), sym(database)]),
                list([sym('pp-def'), sym(database)]),
                [36, 99, 32, 97, 32, 36], empty),
      real_case(metamath, unicode_junk,
                list([sym(ref), sym(database)]),
                list([sym('pp-def'), sym(database)]), [955], empty),
      real_case(tptp, empty, list([sym(ref), sym('tptp-file')]),
                list([sym('pp-def'), sym('tptp-file')]), [], nonempty),
      real_case(tptp, basic_fof, list([sym(ref), sym('tptp-file')]),
                list([sym('pp-def'), sym('tptp-file')]),
                [102,111,102,40,97,44,97,120,105,111,109,44,112,41,46,10],
                nonempty),
      real_case(tptp, missing_dot, list([sym(ref), sym('tptp-file')]),
                list([sym('pp-def'), sym('tptp-file')]),
                [102,111,102,40,97,44,97,120,105,111,109,44,112,41,10],
                empty),
      real_case(tptp, unicode_junk, list([sym(ref), sym('tptp-file')]),
                list([sym('pp-def'), sym('tptp-file')]), [955], empty)
    ]).

generic_purity_gate :-
    source_file(parser_pack_gll:parser_pack_gll_parse(_, _, _, _, _, _),
                GLLPath),
    source_file(parser_pack_forest:parser_pack_replay_forest(_, _, _, _, _),
                ForestPath),
    source_file(parser_pack_action:parser_pack_apply_action(_, _, _),
                ActionPath),
    forall(member(Path, [GLLPath, ForestPath, ActionPath]),
           generic_source_purity(Path)).

generic_source_purity(Path) :-
    read_file_to_string(Path, Text, [encoding(utf8)]),
    downcase_atom(Text, Lower),
    forall(member(Guest, [metamath, megalodon, tptp]),
           require(\+ sub_atom(Lower, _, _, _, Guest),
                   generic_source_names_guest(Path, Guest))).

require(Condition, Label) :-
    ( call(Condition) -> true
    ; throw(error(gate_failed(Label), test_parser_pack_gll))
    ).
