:- use_module(finite_horn_gslt_v1).
:- use_module(finite_horn_eval).

:- initialization(main, main).

main :-
    catch(run, Error, (print_message(error, Error), halt(1))),
    halt.

run :-
    valid_text(Valid),
    read_presentation_text(Valid, canary, Presentation),
    admit_presentations([Presentation]),
    canonical_text(Presentation, Canonical),
    join_text(
        [ "(gslt-presentation-v1 ToyV1 (signature (operator p 1)) ",
          "(equations) (rewrites (rule r (head (p ?x)) (body))))\n"
        ],
        ExpectedCanonical),
    require(Canonical == ExpectedCanonical, canonical_text),

    package_digest([Presentation], Digest),
    ExpectedDigest =
        '3893469550db951cc1ab11c55d56dee50235c8e1b535213290ba43475e7dd130',
    require(Digest == ExpectedDigest, canonical_digest),

    string_concat("; ignored\n\n", Valid, Noisy),
    read_presentation_text(Noisy, noisy_canary, NoisyPresentation),
    package_digest([NoisyPresentation], NoisyDigest),
    require(NoisyDigest == Digest, layout_comment_invariance),

    canonical_text(Presentation, RoundtripText),
    read_presentation_text(RoundtripText, roundtrip_canary, Roundtrip),
    package_digest([Roundtrip], RoundtripDigest),
    require(RoundtripDigest == Digest, canonical_roundtrip),

    quotation_rule(x, y, z, QuotationRule),
    quote_rule(QuotationRule, QuotedRule),
    expected_quotation(ExpectedQuotation),
    require(QuotedRule == ExpectedQuotation, exact_ground_rule_quotation),
    quotation_rule(alpha, beta, gamma, AlphaRenamedRule),
    quote_rule(AlphaRenamedRule, AlphaRenamedQuotation),
    require(AlphaRenamedQuotation == QuotedRule,
            rule_quotation_alpha_stability),
    quotation_rule(x, x, z, AliasedRule),
    quote_rule(AliasedRule, AliasedQuotation),
    require(AliasedQuotation \== QuotedRule,
            rule_quotation_variable_topology),

    second_text(SecondText),
    read_presentation_text(SecondText, second_canary, Second),
    package_digest([Presentation, Second], OrderedDigest),
    package_digest([Second, Presentation], ReversedDigest),
    require(OrderedDigest == ReversedDigest, component_order_invariance),

    deleted_rule_text(DeletedText),
    read_presentation_text(DeletedText, deleted_rule_canary, Deleted),
    package_digest([Deleted], DeletedDigest),
    require(DeletedDigest \== Digest, rule_deletion_changes_digest),

    unicode_text(UnicodeText),
    read_presentation_text(UnicodeText, unicode_canary, Unicode),
    admit_presentations([Unicode]),
    canonical_text(Unicode, UnicodeCanonical),
    require(sub_string(UnicodeCanonical, _, _, _, "λ→∀"),
            unicode_scalar_roundtrip),

    join_text(
        [ "(gslt-presentation-v1 Bad (signature (operator p 1)) ",
          "(equations) (rewrites (rule r (head (p a b)) (body))))"
        ],
        WrongArityText),
    reject_text(
        WrongArityText,
        'undeclared operator p/2', wrong_arity),
    join_text(
        [ "(gslt-presentation-v1 Bad (signature) (equations) (rewrites) ",
          "(parser-options))"
        ],
        UnknownFieldText),
    reject_text(
        UnknownFieldText,
        'unknown presentation field', unknown_field),
    join_text(
        [ "(gslt-presentation-v1 Bad (signature (operator p 1)) ",
          "(equations (equation e a b)) (rewrites))"
        ],
        AuthoredEquationText),
    reject_text(
        AuthoredEquationText,
        'identity equations only', authored_equation),
    join_text(
        [ "(gslt-presentation-v1 Bad (signature (operator p 1)) ",
          "(equations) (rewrites ",
          "(rule r (head (p a)) (body)) ",
          "(rule r (head (p b)) (body))))"
        ],
        DuplicateRuleText),
    reject_text(
        DuplicateRuleText,
        'duplicate rule name', duplicate_rule),
    join_text(
        [ "(gslt-presentation-v1 Bad (signature (operator p 1)) ",
          "(equations) (rewrites (rule r (head (p ?_)) (body))))"
        ],
        AnonymousVariableText),
    reject_text(
        AnonymousVariableText,
        'anonymous variable', anonymous_variable),
    join_text(
        [ "(gslt-presentation-v1 Bad (signature (operator p 1)) ",
          "(equations) (rewrites (rule r (head (p \"\\ud800\")) (body))))"
        ],
        SurrogateText),
    reject_text(
        SurrogateText,
        'malformed S-expression input', unicode_surrogate),
    join_text(
        [ "(gslt-presentation-v1 Bad (signature (operator p 1)) ",
          "(equations) (rewrites ",
          "(rule r (head (p abc\"def\")) (body))))"
        ],
        QuotedSymbolFragmentText),
    reject_text(
        QuotedSymbolFragmentText,
        'undeclared operator p/2', quoted_symbol_fragment),

    join_text(
        [ "(gslt-presentation-v1 UnicodeEscapeV1 ",
          "(signature (operator probe 1)) (equations) ",
          "(rewrites (rule unicode-scalar-string ",
          "(head (probe \"\\u03bb\\ud83d\\ude00\")) (body))))"
        ],
        EscapedUnicodeText),
    join_text(
        [ "(gslt-presentation-v1 UnicodeEscapeV1 ",
          "(signature (operator probe 1)) (equations) ",
          "(rewrites (rule unicode-scalar-string ",
          "(head (probe \"λ😀\")) (body))))"
        ],
        RawUnicodeText),
    read_presentation_text(
        EscapedUnicodeText, escaped_unicode_canary, EscapedUnicode),
    read_presentation_text(RawUnicodeText, raw_unicode_canary, RawUnicode),
    package_digest([EscapedUnicode], EscapedUnicodeDigest),
    package_digest([RawUnicode], RawUnicodeDigest),
    require(EscapedUnicodeDigest == RawUnicodeDigest,
            escaped_unicode_canonical_identity),

    ground_disequality_gate,

    duplicate_composed_rule_text(DuplicateComponentText),
    read_presentation_text(
        DuplicateComponentText, duplicate_component_canary, DuplicateComponent),
    expect_schema_error(
        admit_presentations([Presentation, DuplicateComponent]),
        'duplicate composed rule name', duplicate_composed_rule),

    module_property(finite_horn_gslt_v1, file(ModulePath)),
    read_file_to_string(ModulePath, ModuleText, [encoding(utf8)]),
    downcase_atom(ModuleText, LowerModuleText),
    require(\+ sub_atom(LowerModuleText, _, _, _, metamath),
            no_guest_name_one),
    require(\+ sub_atom(LowerModuleText, _, _, _, megalodon),
            no_guest_name_two),
    require(\+ sub_atom(LowerModuleText, _, _, _, tptp),
            no_guest_name_three),

    current_prolog_flag(argv, Arguments),
    run_external_package(Arguments, ExternalCount),
    LocalCount = 31,
    Total is LocalCount + ExternalCount,
    format('(FiniteHornGSLTV1PeTTaCanarySummary ~d ~d 0)~n',
           [Total, Total]).

run_external_package([], 0).
run_external_package([ExpectedDigest|Paths], 1) :-
    Paths = [_|_],
    maplist(read_presentation, Paths, Presentations),
    package_digest(Presentations, ActualDigest),
    require(ActualDigest == ExpectedDigest, external_package_digest).
run_external_package([_], _) :-
    throw(error(gate_failed(external_package_requires_paths),
                test_finite_horn_gslt_v1)).

reject_text(Text, Needle, Label) :-
    catch(
        ( read_presentation_text(Text, Label, Presentation),
          admit_presentations([Presentation]),
          Outcome = accepted
        ),
        error(gslt_schema_error(Message), _),
        Outcome = rejected(Message)),
    ( Outcome = rejected(Message), sub_string(Message, _, _, _, Needle) -> true
    ; throw(error(gate_failed(Label, Outcome), test_finite_horn_gslt_v1))
    ).

expect_schema_error(Goal, Needle, Label) :-
    catch((call(Goal), Outcome = accepted),
          error(gslt_schema_error(Message), _),
          Outcome = rejected(Message)),
    ( Outcome = rejected(Message), sub_string(Message, _, _, _, Needle) -> true
    ; throw(error(gate_failed(Label, Outcome), test_finite_horn_gslt_v1))
    ).

require(Condition, Label) :-
    ( call(Condition) -> true
    ; throw(error(gate_failed(Label), test_finite_horn_gslt_v1))
    ).

join_text(Parts, Text) :-
    atomics_to_string(Parts, Text).

valid_text(Text) :-
    join_text(
        [ "(gslt-presentation-v1 ToyV1 ",
          "(signature (operator p 1)) ",
          "(equations) ",
          "(rewrites (rule r (head (p ?x)) (body))))"
        ],
        Text).

second_text(Text) :-
    join_text(
        [ "(gslt-presentation-v1 OtherV1 ",
          "(signature (operator q 1)) ",
          "(equations) ",
          "(rewrites (rule s (head (q atom)) (body))))"
        ],
        Text).

deleted_rule_text(Text) :-
    join_text(
        [ "(gslt-presentation-v1 ToyV1 ",
          "(signature (operator p 1)) ",
          "(equations) ",
          "(rewrites))"
        ],
        Text).

unicode_text(Text) :-
    join_text(
        [ "(gslt-presentation-v1 UnicodeV1 ",
          "(signature (operator probe 1)) ",
          "(equations) ",
          "(rewrites (rule unicode-scalar-string ",
          "(head (probe \"λ→∀\")) (body))))"
        ],
        Text).

duplicate_composed_rule_text(Text) :-
    join_text(
        [ "(gslt-presentation-v1 DuplicateComponentV1 ",
          "(signature (operator p 1)) ",
          "(equations) ",
          "(rewrites (rule r (head (p atom)) (body))))"
        ],
        Text).

ground_disequality_gate :-
    Declared = presentation(
        'GroundDifferentV1', [operator(different, 2)], [], intrinsic_canary),
    admit_presentations([Declared]),
    Distinct = list([sym(different), sym(left), sym(right)]),
    horn_query([Declared], Distinct, 8,
               completed([answer(Distinct, Proof)])),
    horn_query_checked([Declared], Distinct, 8,
                       completed([Distinct])),
    require(horn_replay([Declared], Distinct, Proof),
            ground_disequality_proof_replay),
    require(horn_replay_answers(
                [Declared],
                [answer(Distinct, Proof), answer(Distinct, Proof)]),
            ground_disequality_batch_proof_replay),
    require(\+ horn_replay(
                [Declared], Distinct,
                list([sym(cert), sym('not-ground-different'), list([])])),
            corrupt_ground_disequality_proof_rejected),
    require(\+ horn_replay_answers(
                [Declared],
                [ answer(Distinct, Proof),
                  answer(
                      Distinct,
                      list([sym(cert), sym('not-ground-different'), list([])]))
                ]),
            corrupt_ground_disequality_batch_rejected),
    Equal = list([sym(different), sym(same), sym(same)]),
    horn_query([Declared], Equal, 8, completed([])),
    Undeclared = presentation(
        'NoGroundDifferentV1', [], [], intrinsic_canary),
    horn_query([Undeclared], Distinct, 8, completed([])).

quotation_rule(First, Second, Third,
               rule(quoted-rule,
                    list([sym(edge), var(First),
                          list([sym(pair), var(Second), var(First)])]),
                    [list([sym(step), var(Second), var(Third)])])).

expected_quotation(
    list([sym('q-rule'),
          list([sym('q-sym'), sym(quoted-rule)]),
          list([sym('q-app'),
                list([sym('q-sym'), sym(edge)]),
                list([sym('q-cons'),
                      list([sym('q-var'), sym('q-zero')]),
                      list([sym('q-cons'),
                            list([sym('q-app'),
                                  list([sym('q-sym'), sym(pair)]),
                                  list([sym('q-cons'),
                                        list([sym('q-var'),
                                              list([sym('q-succ'),
                                                    sym('q-zero')])]),
                                        list([sym('q-cons'),
                                              list([sym('q-var'),
                                                    sym('q-zero')]),
                                              sym('q-nil')])])]),
                            sym('q-nil')])])]),
          list([sym('q-cons'),
                list([sym('q-app'),
                      list([sym('q-sym'), sym(step)]),
                      list([sym('q-cons'),
                            list([sym('q-var'),
                                  list([sym('q-succ'), sym('q-zero')])]),
                            list([sym('q-cons'),
                                  list([sym('q-var'),
                                        list([sym('q-succ'),
                                              list([sym('q-succ'),
                                                    sym('q-zero')])])]),
                                  sym('q-nil')])])]),
                sym('q-nil')])])).
