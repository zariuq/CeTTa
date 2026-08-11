:- module(rhocalc_parser_pack_oracle_v1, []).

:- use_module(finite_horn_eval, [horn_query/4]).
:- use_module(finite_horn_gslt_v1, [render_term/2]).
:- use_module(parser_pack_eval,
              [ parser_pack_canonical_results/2,
                parser_pack_parse_results/6
              ]).
:- use_module(parser_pack_forest,
              [ parser_pack_forest_digest/2,
                parser_pack_forest_results/5,
                parser_pack_replay_forest/5
              ]).
:- use_module(parser_pack_gll, [parser_pack_gll_parse/6]).
:- use_module(parser_pack_reference_compile,
              [parser_pack_reference_compile/4]).

:- initialization(main, main).

main :-
    catch(main_, Error, (print_message(error, Error), halt(1))),
    halt.

main_ :-
    current_prolog_flag(argv, [PresentationRoot]),
    compile_pack(PresentationRoot, strict, StrictSource, StrictPack,
                 StrictStart),
    compile_pack(PresentationRoot, cost, CostSource, CostPack, CostStart),
    cases(Cases),
    format('rhocalc-parser-pack-oracle-v1~n', []),
    maplist(write_case(StrictSource, StrictPack, StrictStart,
                       CostSource, CostPack, CostStart),
            Cases),
    format('end~n', []).

compile_pack(PresentationRoot, Profile, Source, Pack, Start) :-
    pack_case(Profile, RelativePaths, StartName),
    parser_pack_reference_compile(PresentationRoot, RelativePaths, 4096,
                                  Outcome),
    require(Outcome = completed(parser_pack_compilation(
                Source, _, compiler_answers(_, _), Pack)),
            compiler_completed(Profile)),
    Start = list([sym('pp-def'), sym(StartName)]).

write_case(StrictSource, StrictPack, StrictStart,
           CostSource, CostPack, CostStart,
           case(Profile, Label, Text, ExpectedDecision)) :-
    ( Profile == strict ->
        Source = StrictSource, Pack = StrictPack, Start = StrictStart
    ; Profile == cost ->
        Source = CostSource, Pack = CostPack, Start = CostStart
    ),
    string_codes(Text, Codes),
    codepoints_node(Codes, Input),
    source_results(Source, Start, Input, SourceOutcome),
    parser_pack_parse_results(Pack, Source, Start, Input, 16384,
                              ReferenceOutcome),
    parser_pack_gll_parse(Pack, Source, Start, Input, 2000000,
                          GLLOutcome),
    require(SourceOutcome = completed(SourceResults),
            source_completed(Profile, Label)),
    require(ReferenceOutcome = completed(SourceResults),
            reference_agrees(Profile, Label)),
    require(GLLOutcome = completed(GLLResult),
            gll_completed(Profile, Label)),
    require(parser_pack_replay_forest(
                Pack, Source, Start, Input, GLLResult),
            forest_replays(Profile, Label)),
    GLLResult = parser_result(decision(Decision), _, _, Forest, _),
    require(Decision == ExpectedDecision,
            expected_decision(Profile, Label, ExpectedDecision, Decision)),
    parser_pack_forest_results(Pack, Input, Forest, 16384,
                               SemanticOutcome),
    require(SemanticOutcome = completed(SourceResults),
            forest_semantics_agree(Profile, Label)),
    parser_pack_forest_digest(Forest, ForestDigest),
    format('case\t~w\t~w\t~w\t~w~n',
           [Profile, Label, Decision, ForestDigest]),
    maplist(write_result, SourceResults),
    format('end-case~n', []).

source_results(Source, Start, Input, Outcome) :-
    Start = list([sym('pp-def'), sym(StartName)]),
    Grammar = list([sym(ref), sym(StartName)]),
    Query = list([sym(parse), Grammar, Input, var(value), var(rest)]),
    horn_query(Source, Query, 16384, HornOutcome),
    ( HornOutcome = completed(Answers) ->
        findall(result(Value, Rest),
                member(answer(list([sym(parse), _, _, Value, Rest]), _),
                       Answers),
                RawResults),
        parser_pack_canonical_results(RawResults, Results),
        Outcome = completed(Results)
    ; Outcome = HornOutcome
    ).

write_result(result(Value, Rest)) :-
    render_term(list([sym(result), Value, Rest]), Text),
    format('result\t~s~n', [Text]).

codepoints_node([], sym(nil)).
codepoints_node([Codepoint|Codepoints],
                list([sym(cons), list([sym(cp), int(Codepoint)]), Rest])) :-
    codepoints_node(Codepoints, Rest).

pack_case(
    strict,
    [ 'core/syntax_core_v1.metta',
      'shared/lookahead_core_v1.metta',
      'shared/ground_relations_v1.metta',
      'shared/char_core_v1.metta',
      'shared/cetta_rho_scalar_classes_v1.metta',
      'shared/cetta_rho_lexical_v1.metta',
      'languages/cetta_rho_reader_v1.metta'
    ],
    'cetta-rho-document').

pack_case(
    cost,
    [ 'core/syntax_core_v1.metta',
      'shared/lookahead_core_v1.metta',
      'shared/ground_relations_v1.metta',
      'shared/char_core_v1.metta',
      'shared/cetta_rho_scalar_classes_v1.metta',
      'shared/cetta_rho_lexical_v1.metta',
      'languages/cetta_cost_rho_reader_v1.metta'
    ],
    'cetta-cost-rho-document').

cases(
    [ case(strict, 'strict-nil', "0", accepted),
      case(strict, 'strict-send', "@{0}!(0)", accepted),
      case(strict, 'strict-receive-parallel',
           "for ($x <- @{0}) {*$x} | @{0}!(0)", accepted),
      case(strict, 'strict-bare-bound-name',
           "for (x <- @{0}) {*x}", accepted),
      case(strict, 'strict-comments-and-layout',
           "/* block */ @{0} ! ( // line\n 0 )", accepted),
      case(cost, 'cost-signed', "{0}alice", accepted),
      case(cost, 'cost-signature-product', "{0}alice * bob", accepted),
      case(cost, 'cost-purse-stack',
           "purse pay {alice : bob : ()}", accepted),
      case(cost, 'cost-composite',
           "{for ($m <- pay) {{0}cont}}alice | {pay!({0}payload)}bob | purse pay {alice : ()} | purse pay {bob : ()}",
           accepted),
      case(strict, 'strict-empty', "", rejected),
      case(strict, 'strict-name-is-not-process', "@{0}", rejected),
      case(strict, 'strict-unbound-bare-name',
           "for (x <- @{0}) {*y}", accepted),
      case(strict, 'strict-reserved-for-prefix', "for?!(0)", rejected),
      case(strict, 'strict-nonascii-identifier', "é!(0)", rejected),
      case(strict, 'strict-unterminated-comment', "0 /*", rejected),
      case(strict, 'strict-trailing-input', "0 trailing", rejected),
      case(cost, 'cost-bare-process', "0", rejected),
      case(cost, 'cost-missing-signature', "{0}", rejected),
      case(cost, 'cost-malformed-stack', "purse pay {alice}", rejected)
    ]).

require(Condition, Label) :-
    ( call(Condition) -> true
    ; throw(error(gate_failed(Label), rhocalc_parser_pack_oracle_v1))
    ).
