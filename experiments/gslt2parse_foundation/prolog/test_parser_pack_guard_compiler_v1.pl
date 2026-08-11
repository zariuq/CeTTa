:- module(test_parser_pack_guard_compiler_v1,
          [ parser_pack_guard_compiler_rows/2
          ]).

:- use_module(parser_pack_guard_reference_compile).
:- use_module(finite_horn_eval).
:- use_module(finite_horn_gslt_v1, [render_term/2]).
:- use_module(library(http/json)).
:- use_module(library(pairs)).

:- initialization(main, main).

main :-
    catch(run, Error, (print_message(error, Error), halt(1))),
    halt.

run :-
    current_prolog_flag(argv, Arguments),
    ( Arguments = [PresentationRoot] -> true
    ; throw(error(gate_failed(expected_presentation_root),
                  test_parser_pack_guard_compiler_v1))
    ),
    parser_pack_guard_compiler_rows(PresentationRoot, Result),
    json_write_dict(current_output, Result, [width(0)]),
    nl.

parser_pack_guard_compiler_rows(PresentationRoot,
                                _{protocol:Protocol,
                                  rows:Rows,
                                  replay_count:ReplayCount,
                                  negative_count:NegativeCount}) :-
    Protocol = "parser-pack-positive-guard-compiler-v1",
    compile_case(PresentationRoot, he, HECompilation, HERow),
    compile_case(PresentationRoot, petta, PeTTaCompilation, PeTTaRow),
    compile_case(PresentationRoot, prime, PrimeCompilation, PrimeRow),
    compile_case(PresentationRoot, canary, CanaryCompilation, CanaryRow),
    Rows = [HERow, PeTTaRow, PrimeRow, CanaryRow],
    exact_he_gate(HECompilation),
    exact_petta_gate(PeTTaCompilation),
    exact_prime_gate(PrimeCompilation),
    exact_canary_gate(CanaryCompilation),
    mutation_gate(CanaryCompilation, NegativeCount),
    compilation_answer_count(HECompilation, HEReplays),
    compilation_answer_count(PeTTaCompilation, PeTTaReplays),
    compilation_answer_count(PrimeCompilation, PrimeReplays),
    compilation_answer_count(CanaryCompilation, CanaryReplays),
    ReplayCount is HEReplays + PeTTaReplays + PrimeReplays + CanaryReplays,
    generic_purity_gate(PresentationRoot).

compile_case(PresentationRoot, Label, Compilation,
             _{label:LabelText, count:Count, digest:Digest, terms:Terms,
               source_digest:SourceDigest,
               pre_reflection_digest:PreReflectionDigest,
               environment_digest:EnvironmentDigest}) :-
    atom_string(Label, LabelText),
    guard_case(Label, SourcePaths),
    parser_pack_guard_reference_compile(
        PresentationRoot, SourcePaths, 256,
        completed(Compilation)),
    Compilation = parser_pack_guard_compilation(
        SourcePresentations, Program, Answers, _Artifacts),
    maplist(answer_text, Answers, RawTerms),
    sort(RawTerms, Terms),
    length(Terms, Count),
    parser_pack_guard_answer_set_digest(Answers, Digest),
    parser_pack_guard_compilation_provenance(
        SourcePresentations, Program, Answers,
        parser_pack_guard_provenance_v1(
            source_digest(SourceDigest),
            pre_reflection_digest(PreReflectionDigest),
            environment_digest(EnvironmentDigest),
            answer_set_digest(Digest),
            guard_evidence(Evidence))),
    length(Evidence, Count).

answer_text(answer(Answer, _), Text) :-
    render_term(Answer, Text).

compilation_answer_count(
    parser_pack_guard_compilation(_, _, Answers, _), Count) :-
    length(Answers, Count).

exact_he_gate(parser_pack_guard_compilation(_, _, Answers, _)) :-
    answer_artifacts(Answers, Pairs),
    expected_he_pairs(Expected),
    require(Pairs == Expected, exact_he_positive_guards).

expected_he_pairs(Pairs) :-
    findall(Owner-Artifact,
            ( member(Owner-Body-Path,
                     [ 'he-comment'-'he-comment-boundary'-[body,right,right],
                       'he-variable'-'he-variable-boundary'-[body,right],
                       'he-word'-'he-word-boundary'-[body,right]
                     ]),
              nested_he_state(Owner, Path, State),
              GuardBody = list([sym('sir-ref'), sym(Body)]),
              expected_artifact(State, GuardBody, Artifact)
            ),
            Pairs).

nested_he_state(Owner, Path, State) :-
    foldl(he_state_sub, Path, list([sym('pp-def'), sym(Owner)]), State).

he_state_sub(Label, Parent,
             list([sym('pp-sub'), Parent, sym(Label)])).

exact_petta_gate(parser_pack_guard_compilation(_, _, Answers, _)) :-
    answer_artifacts(Answers, Pairs),
    State = list([sym('pp-sub'),
                  list([sym('pp-def'), sym('petta-token-like')]),
                  sym(right)]),
    Body = list([sym('sir-ref'), sym('petta-token-boundary')]),
    expected_artifact(State, Body, Artifact),
    require(Pairs == ['petta-token-like'-Artifact],
            exact_petta_positive_guard).

exact_prime_gate(parser_pack_guard_compilation(_, _, Answers, _)) :-
    answer_artifacts(Answers, Pairs),
    expected_prime_pairs(Expected),
    require(Pairs == Expected, exact_prime_positive_guards).

expected_prime_pairs(Pairs) :-
    findall(Owner-Artifact,
            ( member(Owner-Path-BodyKind-BodyName,
                     [ 'cetta-prime-anonymous-variable'-[body,right]-ref-
                           'cetta-prime-token-boundary',
                       'cetta-prime-unquote'-[body,right,left]-class-
                           'cetta-prime-star-payload-start-scalar',
                       'cetta-prime-variable'-[body,right]-ref-
                           'cetta-prime-token-boundary',
                       'cetta-prime-word-amp'-[right]-ref-
                           'cetta-prime-token-boundary',
                       'cetta-prime-word-bare-amp'-[right]-ref-
                           'cetta-prime-token-boundary',
                       'cetta-prime-word-general'-[right]-ref-
                           'cetta-prime-token-boundary',
                       'cetta-prime-word-star'-[right]-ref-
                           'cetta-prime-star-boundary'
                     ]),
              prime_state(Owner, Path, State),
              ( BodyKind == class ->
                  Body = list([sym('sir-class'), sym(BodyName)])
              ; Body = list([sym('sir-ref'), sym(BodyName)])
              ),
              expected_artifact(State, Body, Artifact)
            ),
            Pairs).

prime_state(Owner, Path, State) :-
    foldl(he_state_sub, Path, list([sym('pp-def'), sym(Owner)]), State).

exact_canary_gate(parser_pack_guard_compilation(_, _, Answers, _)) :-
    answer_artifacts(Answers, Pairs),
    expected_canary_pairs(Expected),
    require(Pairs == Expected, exact_generic_positive_guards).

expected_canary_pairs(Pairs) :-
    RootState = list([sym('pp-def'), sym('guard-root')]),
    RootBody = list([sym('sir-alt'),
                     list([sym('sir-eps'), sym('left-value')]),
                     list([sym('sir-char'), list([sym(cp), int(955)])])]),
    NestedLeft = list([sym('pp-sub'),
                       list([sym('pp-sub'),
                             list([sym('pp-def'), sym('guard-nested')]),
                             sym(body)]),
                       sym(left)]),
    NestedLeftBody = list([sym('sir-char'), list([sym(cp), int(97)])]),
    NestedRight = list([sym('pp-sub'),
                        list([sym('pp-sub'),
                              list([sym('pp-sub'),
                                    list([sym('pp-def'),
                                          sym('guard-nested')]),
                                    sym(body)]),
                              sym(right)]),
                        sym(body)]),
    expected_artifact(RootState, RootBody, RootArtifact),
    expected_artifact(NestedLeft, NestedLeftBody, NestedLeftArtifact),
    expected_artifact(NestedRight, sym('sir-eof'), NestedRightArtifact),
    Pairs = [ 'guard-nested'-NestedLeftArtifact,
              'guard-nested'-NestedRightArtifact,
              'guard-root'-RootArtifact
            ].

expected_artifact(State, Body,
                  list([sym('pp-positive-guard-v1'), State, Tag, Body,
                        Production])) :-
    Tag = list([sym('pp-positive-guard-tag'), State]),
    Production =
        list([sym('pp-production'),
              list([sym('pp-label'), State, sym(peek)]),
              State,
              list([sym('pp-items-cons'),
                    list([sym('pp-terminal'),
                          list([sym('pp-span-terminal'), Tag])]),
                    sym('pp-items-nil')]),
              list([sym('pa-slot'), sym('q-zero')])]).

answer_artifacts(Answers, Pairs) :-
    findall(Key-(Owner-Artifact),
            ( member(answer(
                         list([sym('compile-pack-positive-guard'),
                               sym(Owner), Artifact]), _),
                     Answers),
              render_term(Artifact, ArtifactText),
              format(string(Key), '~w\u0000~s', [Owner, ArtifactText])
            ),
            Keyed0),
    keysort(Keyed0, Keyed),
    pairs_values(Keyed, Pairs).

mutation_gate(parser_pack_guard_compilation(_, Program, Answers, _), 5) :-
    Query = list([sym('compile-pack-positive-guard'),
                  var(owner), var(guard)]),
    delete_program_rule(
        'ParserPackGuardCompilerV1',
        'collect-sir-positive-guard-here', Program, WithoutHere),
    horn_query(WithoutHere, Query, 256, completed([])),
    require(\+ member(answer(
                         list([sym('compile-pack-positive-guard'),
                               sym('guard-absent'), _]), _),
                     Answers),
            no_guard_definition_emits_nothing),
    expected_artifact(sym(state), sym(body), Valid),
    require(parser_pack_guard_artifact_valid(Valid),
            valid_guard_artifact_admitted),
    corrupt_action(Valid, BadAction),
    require(\+ parser_pack_guard_artifact_valid(BadAction),
            corrupt_guard_action_rejected),
    corrupt_terminal(Valid, BadTerminal),
    require(\+ parser_pack_guard_artifact_valid(BadTerminal),
            corrupt_guard_terminal_rejected).

corrupt_action(
    list([Head, State, Tag, Body,
          list([Production, Label, Lhs, Items, _])]),
    list([Head, State, Tag, Body,
          list([Production, Label, Lhs, Items,
                list([sym('pa-const'), sym(corrupt)])])])).

corrupt_terminal(
    list([Head, State, Tag, Body,
          list([Production, Label, Lhs,
                list([ItemsCons,
                      list([Terminal,
                            list([SpanTerminal, _])]),
                      ItemsNil]),
                Action])]),
    list([Head, State, Tag, Body,
          list([Production, Label, Lhs,
                list([ItemsCons,
                      list([Terminal,
                            list([SpanTerminal, sym(corrupt)])]),
                      ItemsNil]),
                Action])])).

delete_program_rule(PresentationName, RuleName, Program0, Program) :-
    select(presentation(PresentationName, Operators, Rules0, Source),
           Program0,
           presentation(PresentationName, Operators, Rules, Source),
           Program),
    select(rule(RuleName, _, _), Rules0, Rules),
    !.

generic_purity_gate(PresentationRoot) :-
    directory_file_path(
        PresentationRoot,
        'compiler/parser_pack_guard_compiler_v1.metta', Path),
    read_file_to_string(Path, Text, [encoding(utf8)]),
    downcase_atom(Text, Lower),
    forall(member(Guest, [metamath, megalodon, tptp, 'cetta-prime', petta]),
           require(\+ sub_atom(Lower, _, _, _, Guest),
                   generic_guard_compiler_names_guest(Guest))).

guard_case(
    he,
    [ 'core/syntax_core_v1.metta',
      'shared/lookahead_core_v1.metta',
      'shared/ground_relations_v1.metta',
      'shared/he_reader_scalar_classes_v1.metta',
      'languages/he_reader_v1.metta'
    ]).
guard_case(
    petta,
    [ 'core/syntax_core_v1.metta',
      'shared/lookahead_core_v1.metta',
      'shared/ground_relations_v1.metta',
      'shared/petta_form_reader_scalar_classes_v1.metta',
      'languages/petta_form_reader_v1.metta'
    ]).
guard_case(
    prime,
    [ 'core/syntax_core_v1.metta',
      'shared/lookahead_core_v1.metta',
      'shared/ground_relations_v1.metta',
      'shared/cetta_prime_scalar_classes_v1.metta',
      'languages/cetta_prime_reader_v1.metta'
    ]).
guard_case(
    canary,
    [ 'core/syntax_core_v1.metta',
      'shared/lookahead_core_v1.metta',
      'canaries/positive_guard_v1.metta'
    ]).

require(Condition, Label) :-
    ( call(Condition) -> true
    ; throw(error(gate_failed(Label), test_parser_pack_guard_compiler_v1))
    ).
