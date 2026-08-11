:- module(test_parser_action_bytecode_compiler_v1,
          [ parser_action_bytecode_compiler_rows/2
          ]).

:- use_module(finite_horn_eval).
:- use_module(finite_horn_gslt_v1).
:- use_module(library(crypto)).
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
                  test_parser_action_bytecode_compiler_v1))
    ),
    parser_action_bytecode_compiler_rows(PresentationRoot, Result),
    json_write_dict(current_output, Result, [width(0)]),
    nl.

parser_action_bytecode_compiler_rows(
    PresentationRoot,
    _{protocol:"parser-action-bytecode-compiler-v1",
      rows:Rows,
      replay_count:ReplayCount,
      positive_count:PositiveCount,
      negative_count:NegativeCount,
      guard_extension_count:GuardExtensionCount,
      guard_extension_digest:GuardExtensionDigest,
      guard_extension_terms:GuardExtensionTerms,
      guard_union_count:GuardUnionCount,
      guard_union_digest:GuardUnionDigest,
      guard_union_terms:GuardUnionTerms,
      guard_replay_count:GuardReplayCount}) :-
    findall(Row-Replays,
            ( action_case(Label, SourcePaths, ExpectedCount, ExpectedDigest),
              compile_case(PresentationRoot, Label, SourcePaths,
                           ExpectedCount, ExpectedDigest, Row, Replays)
            ),
            RowReplays),
    pairs_keys_values(RowReplays, Rows, ReplayCounts),
    sum_list(ReplayCounts, ReplayCount),
    action_fragment_gate(PresentationRoot, PositiveCount, NegativeCount),
    guard_action_gate(
        PresentationRoot,
        GuardExtensionCount, GuardExtensionDigest, GuardExtensionTerms,
        GuardUnionCount, GuardUnionDigest, GuardUnionTerms,
        GuardReplayCount).

compile_case(PresentationRoot, Label, SourcePaths,
             ExpectedCount, ExpectedDigest,
             _{label:LabelText, count:Count, digest:Digest, terms:Terms},
             ReplayCount) :-
    atom_string(Label, LabelText),
    action_program(PresentationRoot, SourcePaths, Program),
    admit_presentations(Program),
    Query = list([sym('compile-pack-action-program'),
                  var(owner), var(label), var(arity),
                  var(action), var(code)]),
    horn_query(Program, Query, 256, Outcome),
    require(Outcome = completed(Answers), completed_action_compilation(Label)),
    require(forall(member(answer(Answer, Proof), Answers),
                   horn_replay(Program, Answer, Proof)),
            action_compiler_proof_replay(Label)),
    maplist(answer_text, Answers, RawTerms),
    sort(RawTerms, Terms),
    length(Terms, Count),
    answer_text_set_digest(Terms, Digest),
    require(Count =:= ExpectedCount, exact_action_count(Label)),
    require(Digest == ExpectedDigest, exact_action_digest(Label)),
    ReplayCount = Count.

answer_text(answer(Answer, _), Text) :-
    render_term(Answer, Text).

answer_text_set_digest(Texts, Digest) :-
    maplist(answer_line, Texts, Lines),
    atomics_to_string(["FiniteHornAnswerSetV1\n"|Lines], Payload),
    crypto_data_hash(Payload, Digest, [algorithm(sha256)]).

answer_line(Text, Line) :-
    atomics_to_string([Text, "\n"], Line).

action_fragment_gate(PresentationRoot, 1, 2) :-
    action_case(prime, SourcePaths, _, _),
    action_program(PresentationRoot, SourcePaths, Program),
    admit_presentations(Program),
    Slot = sym('q-zero'),
    Action = list([sym('pa-apply'), sym(pair),
                   list([sym('pa-cons'),
                         list([sym('pa-slot'), Slot]),
                         list([sym('pa-cons'),
                               list([sym('pa-const'), sym(nil)]),
                               sym('pa-nil')])])]),
    Code = list([sym('pbc-cons'),
                 list([sym('pbc-push-slot'), Slot]),
                 list([sym('pbc-cons'),
                       list([sym('pbc-push-const'), sym(nil)]),
                       list([sym('pbc-cons'),
                             list([sym('pbc-apply'), sym(pair),
                                   list([sym('q-succ'),
                                         list([sym('q-succ'), Slot])])]),
                             sym('pbc-nil')])])]),
    PositiveQuery = list([sym('lower-pack-action'), Action, var(code)]),
    PositiveAnswer = list([sym('lower-pack-action'), Action, Code]),
    horn_query(Program, PositiveQuery, 64, PositiveOutcome),
    require(PositiveOutcome = completed([answer(PositiveAnswer, Proof)]),
            exact_nested_action_lowering),
    require(horn_replay(Program, PositiveAnswer, Proof),
            nested_action_proof_replay),

    MalformedAction =
        list([sym('pa-apply'), sym(pair),
              list([sym('pa-cons'),
                    list([sym('pa-slot'), Slot]), sym(malformed)])]),
    MalformedQuery =
        list([sym('lower-pack-action'), MalformedAction, var(code)]),
    horn_query(Program, MalformedQuery, 64, MalformedOutcome),
    require(MalformedOutcome = completed([]), malformed_list_rejected),

    UnknownQuery =
        list([sym('lower-pack-action'),
              list([sym('pa-unknown'), sym(x)]), var(code)]),
    horn_query(Program, UnknownQuery, 64, UnknownOutcome),
    require(UnknownOutcome = completed([]), unknown_action_rejected).

guard_action_gate(PresentationRoot,
                  3,
                  '510667b98747982e7c56a4f8495d5014c6c6650f2ba3a0a67aece90dc029e1f0',
                  ExtensionTerms,
                  282,
                  '9c4ff108eb75369434f5058333b5d921c9da217884eeb8ea02ff26ad3d926df4',
                  UnionTerms,
                  285) :-
    action_case(he, SourcePaths, _, _),
    guard_action_program(PresentationRoot, SourcePaths, Program),
    admit_presentations(Program),
    compile_guard_action_relation(
        Program, 'compile-positive-guard-action-program',
        3,
        '510667b98747982e7c56a4f8495d5014c6c6650f2ba3a0a67aece90dc029e1f0',
        ExtensionTerms, ExtensionReplays),
    compile_guard_action_relation(
        Program, 'compile-guard-extended-action-program',
        282,
        '9c4ff108eb75369434f5058333b5d921c9da217884eeb8ea02ff26ad3d926df4',
        UnionTerms, UnionReplays),
    require(ExtensionReplays + UnionReplays =:= 285,
            exact_guard_action_replay_count).

compile_guard_action_relation(Program, Relation,
                              ExpectedCount, ExpectedDigest,
                              Terms, ReplayCount) :-
    Query = list([sym(Relation), var(owner), var(label), var(arity),
                  var(action), var(code)]),
    horn_query(Program, Query, 256, Outcome),
    require(Outcome = completed(Answers),
            completed_guard_action_compilation(Relation)),
    require(forall(member(answer(Answer, Proof), Answers),
                   horn_replay(Program, Answer, Proof)),
            guard_action_compiler_proof_replay(Relation)),
    maplist(answer_text, Answers, RawTerms),
    sort(RawTerms, Terms),
    length(Terms, Count),
    answer_text_set_digest(Terms, Digest),
    require(Count =:= ExpectedCount, exact_guard_action_count(Relation)),
    require(Digest == ExpectedDigest, exact_guard_action_digest(Relation)),
    ReplayCount = Count.

action_program(PresentationRoot, SourceRelativePaths, Program) :-
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

guard_action_program(PresentationRoot, SourceRelativePaths, Program) :-
    compiler_relative_paths(BaseCompilerRelativePaths),
    append(BaseCompilerRelativePaths,
           [ 'compiler/parser_pack_guard_compiler_v1.metta',
             'compiler/parser_pack_guard_action_link_v1.metta'
           ],
           CompilerRelativePaths),
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
      'compiler/parser_pack_compiler_v1.metta',
      'compiler/parser_action_bytecode_compiler_v1.metta'
    ]).

presentation_path(Root, Relative, Path) :-
    directory_file_path(Root, Relative, Path).

action_case(
    he,
    [ 'core/syntax_core_v1.metta',
      'shared/lookahead_core_v1.metta',
      'shared/ground_relations_v1.metta',
      'shared/he_reader_scalar_classes_v1.metta',
      'languages/he_reader_v1.metta'
    ],
    279,
    '4ea00d441b832d4d33fde53ea47ea8495c89da661821916634aedc9698957b8f').

action_case(
    prime,
    [ 'core/syntax_core_v1.metta',
      'shared/lookahead_core_v1.metta',
      'shared/ground_relations_v1.metta',
      'shared/cetta_prime_scalar_classes_v1.metta',
      'languages/cetta_prime_reader_v1.metta'
    ],
    179,
    b9194af90380048ffbf7a50f0b54f560fe7c5dac97116c961a17bbfa56a4f833).

action_case(
    metamath,
    [ 'core/syntax_core_v1.metta',
      'shared/lookahead_core_v1.metta',
      'languages/metamath_appendix_e_v1.metta'
    ],
    270,
    '738417b94ad546c87d59dab83985475a8abff6f4e441a4f93148974576150e39').

action_case(
    megalodon,
    [ 'core/syntax_core_v1.metta',
      'shared/lookahead_core_v1.metta',
      'shared/char_core_v1.metta',
      'languages/megalodon_dynamic_v1.metta'
    ],
    138,
    '954beee010b5e577b86a642fafb1fd520e2d0d03b9ba75587b948d755ffd51b8').

action_case(
    tptp,
    [ 'core/syntax_core_v1.metta',
      'shared/lookahead_core_v1.metta',
      'shared/char_core_v1.metta',
      'languages/tptp_fof_cnf_v1.metta'
    ],
    688,
    d8882993f494f523f6214bffef652e772e3663dac518cd0e917748dc06e4fbbd).

require(Condition, Label) :-
    ( call(Condition) -> true
    ; throw(error(gate_failed(Label),
                  test_parser_action_bytecode_compiler_v1))
    ).
