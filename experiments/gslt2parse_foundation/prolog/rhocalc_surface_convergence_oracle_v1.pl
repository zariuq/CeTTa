:- module(rhocalc_surface_convergence_oracle_v1, []).

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
    compile_pack(PresentationRoot, rho, RhoSource, RhoPack, RhoStart),
    compile_pack(PresentationRoot, mrho, MRhoSource, MRhoPack, MRhoStart),
    cases(Cases),
    format('rhocalc-surface-convergence-oracle-v1~n', []),
    forall(member(case(Label, RhoText, MRhoText, ExpectedDecision), Cases),
           ( write_case(rho, Label, RhoText,
                        ExpectedDecision, RhoSource, RhoPack, RhoStart),
             write_case(mrho, Label, MRhoText,
                        ExpectedDecision, MRhoSource, MRhoPack, MRhoStart)
           )),
    format('end~n', []).

compile_pack(PresentationRoot, Surface, Source, Pack, Start) :-
    pack_case(Surface, RelativePaths, StartName),
    parser_pack_reference_compile(PresentationRoot, RelativePaths, 4096,
                                  Outcome),
    require(Outcome = completed(parser_pack_compilation(
                Source, _, compiler_answers(_, _), Pack)),
            compiler_completed(Surface)),
    Start = list([sym('pp-def'), sym(StartName)]).

write_case(Surface, Label, Text, ExpectedDecision, Source, Pack, Start) :-
    string_codes(Text, Codes),
    codepoints_node(Codes, Input),
    source_results(Source, Start, Input, SourceOutcome),
    parser_pack_parse_results(Pack, Source, Start, Input, 32768,
                              ReferenceOutcome),
    parser_pack_gll_parse(Pack, Source, Start, Input, 4000000,
                          GLLOutcome),
    require(SourceOutcome = completed(SourceResults),
            source_completed(Surface, Label)),
    require(ReferenceOutcome = completed(SourceResults),
            reference_agrees(Surface, Label)),
    require(GLLOutcome = completed(GLLResult),
            gll_completed(Surface, Label)),
    require(parser_pack_replay_forest(
                Pack, Source, Start, Input, GLLResult),
            forest_replays(Surface, Label)),
    GLLResult = parser_result(decision(Decision), _, _, Forest, _),
    require(Decision == ExpectedDecision,
            decision_agrees(Surface, Label, ExpectedDecision, Decision)),
    parser_pack_forest_results(Pack, Input, Forest, 32768,
                               SemanticOutcome),
    require(SemanticOutcome = completed(SourceResults),
            forest_semantics_agree(Surface, Label)),
    parser_pack_forest_digest(Forest, ForestDigest),
    format('case\t~w\t~w\t~w\t~w~n',
           [Surface, Label, Decision, ForestDigest]),
    maplist(write_result, SourceResults),
    format('end-case~n', []).

source_results(Source, Start, Input, Outcome) :-
    Start = list([sym('pp-def'), sym(StartName)]),
    Grammar = list([sym(ref), sym(StartName)]),
    Query = list([sym(parse), Grammar, Input, var(value), var(rest)]),
    horn_query(Source, Query, 32768, HornOutcome),
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
    rho,
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
    mrho,
    [ 'core/syntax_core_v1.metta',
      'shared/lookahead_core_v1.metta',
      'shared/ground_relations_v1.metta',
      'shared/he_reader_scalar_classes_v1.metta',
      'shared/rho_abstract_syntax_v1.metta',
      'languages/cetta_rho_mrho_reader_v1.metta'
    ],
    'cetta-rho-mrho-document').

cases(
    [ case('strict-nil',
           "0",
           "rho:nil", accepted),
      case('strict-send',
           "@{0}!(0)",
           "(rho:send (rho:quote rho:nil) rho:nil)", accepted),
      case('strict-receive-parallel',
           "for ($x <- @{0}) {*$x} | @{0}!(0)",
           "(rho:par (rho:recv (rho:quote rho:nil) $x (rho:drop $x)) (rho:send (rho:quote rho:nil) rho:nil))", accepted),
      case('strict-bare-bound-name',
           "for (x <- @{0}) {*x}",
           "(rho:recv (rho:quote rho:nil) $x (rho:drop $x))", accepted),
      case('strict-comments-and-layout',
           "/* block */ @{0} ! ( // line\n 0 )",
           "; layout\n(rho:send (rho:quote rho:nil) rho:nil)", accepted),
      case('strict-shadowing',
           "for ($x <- @{0}) {for ($x <- $x) {*$x} | $x!(0)} | @{0}!(@{0}!(0))",
           "(rho:par (rho:recv (rho:quote rho:nil) $x (rho:par (rho:recv $x $x_rho1 (rho:drop $x_rho1)) (rho:send $x rho:nil))) (rho:send (rho:quote rho:nil) (rho:send (rho:quote rho:nil) rho:nil)))", accepted),
      case('strict-quote-seals-outer-binder',
           "for ($x <- @{0}) {@{*$x}!(0)} | @{0}!(@{0}!(0))",
           "(rho:par (rho:recv (rho:quote rho:nil) $x_rho1 (rho:send (rho:quote (rho:drop $x)) rho:nil)) (rho:send (rho:quote rho:nil) (rho:send (rho:quote rho:nil) rho:nil)))", accepted),
      case('strict-quote-inner-binder-restores-scope',
           "for ($x <- @{0}) {@{for ($y <- @{0}) {*$y}}!(0) | $x!(0)} | @{0}!(@{0}!(0))",
           "(rho:par (rho:recv (rho:quote rho:nil) $x (rho:par (rho:send (rho:quote (rho:recv (rho:quote rho:nil) $y (rho:drop $y))) rho:nil) (rho:send $x rho:nil))) (rho:send (rho:quote rho:nil) (rho:send (rho:quote rho:nil) rho:nil)))", accepted),
      case('strict-open-name-variable',
           "$x!(0)",
           "(rho:send $x rho:nil)", accepted),
      case('reject-name-not-process',
           "@{0}",
           "(rho:quote rho:nil)", rejected),
      case('reject-trailing-or-wrong-arity',
           "0 trailing",
           "(rho:send (rho:quote rho:nil) rho:nil rho:nil)", rejected),
      case('reject-malformed-binder',
           "for ($ <- @{0}) {0}",
           "(rho:recv (rho:quote rho:nil) binder rho:nil)", rejected),
      case('reject-unknown-form',
           "for?!(0)",
           "(rho:fresh $x rho:nil)", rejected)
    ]).

require(Condition, Label) :-
    ( call(Condition) -> true
    ; throw(error(gate_failed(Label), rhocalc_surface_convergence_oracle_v1))
    ).
