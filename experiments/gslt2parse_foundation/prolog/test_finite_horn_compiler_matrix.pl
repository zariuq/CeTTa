:- module(test_finite_horn_compiler_matrix,
          [ run_compiler_matrix/3
          ]).

:- use_module(finite_horn_gslt_v1).
:- use_module(finite_horn_eval).
:- use_module(library(crypto)).
:- use_module(library(ordsets), [ord_subtract/3]).

:- initialization(main, main).

main :-
    catch(run, Error, (print_message(error, Error), halt(1))),
    halt.

run :-
    current_prolog_flag(argv, Arguments),
    ( Arguments = [PresentationRoot] -> true
    ; throw(error(gate_failed(expected_presentation_root),
                  test_finite_horn_compiler_matrix))
    ),
    run_compiler_matrix(PresentationRoot, Complete, Partial),
    format('(FiniteHornCompilerMatrixPeTTaSummary 4 ~d ~d 0)~n',
           [Complete, Partial]).

run_compiler_matrix(PresentationRoot, Complete, Partial) :-
    findall(Coverage,
            ( compiler_case(_Label, RootName, RelativePaths,
                            ExpectedAnswerHash, ExpectedProofHash,
                            ExpectedCoverage, ExpectedDefinitionHash),
              run_compiler_case(PresentationRoot, RootName, RelativePaths,
                                ExpectedAnswerHash, ExpectedProofHash,
                                ExpectedCoverage, ExpectedDefinitionHash,
                                Coverage)
            ),
            Coverages),
    include(is_complete_coverage, Coverages, CompleteCases),
    include(is_partial_coverage, Coverages, PartialCases),
    length(CompleteCases, Complete),
    length(PartialCases, Partial),
    length(Coverages, 4).

run_compiler_case(PresentationRoot, RootName, RelativePaths,
                  ExpectedAnswerHash, ExpectedProofHash,
                  ExpectedCoverage, ExpectedDefinitionHash,
                  Coverage) :-
    compiler_program(PresentationRoot, RelativePaths, Presentations),
    admit_presentations(Presentations),
    Query = list([sym('compile-root'), sym(RootName), var(result)]),
    horn_query(Presentations, Query, 256, Outcome),
    require(Outcome = completed([answer(Answer, Proof)]),
            exact_single_compiler_answer(RootName)),
    render_term(Answer, AnswerText),
    crypto_data_hash(AnswerText, AnswerHash, [algorithm(sha256)]),
    require(AnswerHash == ExpectedAnswerHash,
            exact_compiler_answer_hash(RootName)),
    render_term(Proof, ProofText),
    crypto_data_hash(ProofText, ProofHash, [algorithm(sha256)]),
    require(ProofHash == ExpectedProofHash,
            exact_compiler_proof_hash(RootName)),
    require(horn_replay(Presentations, Answer, Proof),
            compiler_proof_replay(RootName)),

    DefinitionsQuery =
        list([sym('compile-definition'), var(name), var(ir)]),
    horn_query(Presentations, DefinitionsQuery, 256, DefinitionsOutcome),
    require(DefinitionsOutcome = completed(Definitions),
            completed_definition_compilation(RootName)),
    require(forall(member(answer(Definition, DefinitionProof), Definitions),
                   horn_replay(Presentations, Definition, DefinitionProof)),
            compiled_definition_proof_replay(RootName)),
    definition_matrix_hash(Definitions, DefinitionHash),
    require(DefinitionHash == ExpectedDefinitionHash,
            exact_compiled_definition_matrix(RootName)),

    CFGQuery =
        list([sym('compile-cfg-production'),
              var(quoted_rule), var(source_production)]),
    horn_query(Presentations, CFGQuery, 1024, CFGOutcome),
    require(CFGOutcome = completed(CFGProductions),
            completed_cfg_production_inventory(RootName)),
    require(forall(member(answer(CFGProduction, CFGProof), CFGProductions),
                   horn_replay(Presentations, CFGProduction, CFGProof)),
            cfg_production_proof_replay(RootName)),
    cfg_state_inventory(CFGProductions, CFGStates),

    SourceQuery = list([sym(definition), var(name), var(grammar)]),
    horn_query(Presentations, SourceQuery, 256, SourceOutcome),
    require(SourceOutcome = completed(SourceDefinitions),
            completed_source_definition_inventory(RootName)),
    definition_coverage(SourceDefinitions, Definitions, DefinitionCoverage),
    root_sir(Answer, RootName, RootIR),
    sir_closure(RootIR, Definitions, CFGStates, Closure),
    Coverage = coverage(DefinitionCoverage, Closure),
    require(Coverage == ExpectedCoverage,
            exact_compiler_coverage(RootName)).

compiler_program(PresentationRoot, SourceRelativePaths, Presentations) :-
    compiler_relative_paths(CompilerRelativePaths),
    maplist(presentation_path(PresentationRoot), CompilerRelativePaths,
            CompilerPaths),
    maplist(presentation_path(PresentationRoot), SourceRelativePaths,
            SourcePaths),
    maplist(read_presentation, CompilerPaths, CompilerPresentations),
    maplist(read_presentation, SourcePaths, SourcePresentations),
    reflect_presentations(SourcePresentations, Reflected),
    append(CompilerPresentations, SourcePresentations, WithoutReflection),
    append(WithoutReflection, [Reflected], Presentations).

compiler_relative_paths(
    [ 'reflection/finite_horn_reflection_v1.metta',
      'compiler/syntax_compiler_v1.metta',
      'compiler/relational_cfg_lowering_v1.metta',
      'compiler/relational_cfg_link_v1.metta'
    ]).

definition_coverage(SourceDefinitions, CompiledDefinitions, Coverage) :-
    maplist(source_definition_name, SourceDefinitions, RawSourceNames),
    maplist(compiled_definition_name, CompiledDefinitions, RawCompiledNames),
    sort(RawSourceNames, SourceNames),
    sort(RawCompiledNames, CompiledNames),
    length(SourceNames, SourceCount),
    length(CompiledNames, CompiledCount),
    ord_subtract(SourceNames, CompiledNames, Missing),
    ord_subtract(CompiledNames, SourceNames, Extra),
    ( Missing == [], Extra == [] ->
        Coverage = complete(SourceCount)
    ; Coverage = partial(SourceCount, CompiledCount, Missing, Extra)
    ).

source_definition_name(
    answer(list([sym(definition), sym(Name), _Grammar]), _Proof), Name).

compiled_definition_name(
    answer(list([sym('compile-definition'), sym(Name), _IR]), _Proof), Name).

root_sir(
    list([sym('compile-root'), sym(RootName),
          list([sym('sir-root'), sym(RootName), RootIR])]),
    RootName, RootIR).

sir_closure(RootIR, Definitions, CFGStates, Closure) :-
    sir_refs(RootIR, InitialRefs),
    sir_rel_states(RootIR, InitialRelStates),
    missing_rel_states(InitialRelStates, CFGStates, InitialRelMissing),
    closure_refs(InitialRefs, Definitions, CFGStates, [],
                 InitialRelMissing, RawMissing),
    sort(RawMissing, Missing),
    ( Missing == [] -> Closure = closed ; Closure = open(Missing) ).

closure_refs([], _, _, _, Missing, Missing).
closure_refs([Name|Names], Definitions, CFGStates, Seen,
             Missing0, Missing) :-
    ( memberchk(Name, Seen) ->
        closure_refs(Names, Definitions, CFGStates, Seen,
                     Missing0, Missing)
    ; compiled_definition_ir(Definitions, Name, IR) ->
        sir_refs(IR, Refs),
        sir_rel_states(IR, RelStates),
        missing_rel_states(RelStates, CFGStates, RelMissing),
        append(Refs, Names, Pending),
        append(RelMissing, Missing0, Missing1),
        closure_refs(Pending, Definitions, CFGStates, [Name|Seen],
                     Missing1, Missing)
    ; closure_refs(Names, Definitions, CFGStates, [Name|Seen],
                   [definition(Name)|Missing0], Missing)
    ).

compiled_definition_ir(
    [answer(list([sym('compile-definition'), sym(Name),
                  list([sym('sir-definition'), sym(Name), IR])]), _)|_],
    Name, IR) :-
    !.
compiled_definition_ir([_|Definitions], Name, IR) :-
    compiled_definition_ir(Definitions, Name, IR).

sir_refs(Node, Refs) :-
    sir_refs_(Node, RawRefs),
    sort(RawRefs, Refs).

sir_refs_(list([sym('sir-ref'), sym(Name)]), [Name]) :-
    !.
sir_refs_(list(Nodes), Refs) :-
    !,
    maplist(sir_refs_, Nodes, NestedRefs),
    append(NestedRefs, Refs).
sir_refs_(_, []).

sir_rel_states(Node, States) :-
    sir_rel_states_(Node, RawStates),
    sort(RawStates, States).

sir_rel_states_(list([sym('sir-rel'), sym(State)]), [State]) :-
    !.
sir_rel_states_(list(Nodes), States) :-
    !,
    maplist(sir_rel_states_, Nodes, NestedStates),
    append(NestedStates, States).
sir_rel_states_(_, []).

cfg_state_inventory(Productions, States) :-
    findall(State,
            member(answer(
                       list([sym('compile-cfg-production'), _,
                             list([sym('sir-source-production'), _,
                                   list([sym('sir-production'), _,
                                         sym(State), _, _])])]), _),
                   Productions),
            RawStates),
    sort(RawStates, States).

missing_rel_states(States, CFGStates, Missing) :-
    findall(relational(State),
            ( member(State, States), \+ memberchk(State, CFGStates) ),
            Missing).

is_complete_coverage(coverage(complete(_), closed)).
is_partial_coverage(Coverage) :-
    \+ is_complete_coverage(Coverage).

definition_matrix_hash(Definitions, Hash) :-
    maplist(answer_proof_text, Definitions, Texts),
    atomics_to_string(Texts, Payload),
    crypto_data_hash(Payload, Hash, [algorithm(sha256)]).

answer_proof_text(answer(Answer, Proof), Text) :-
    render_term(Answer, AnswerText),
    render_term(Proof, ProofText),
    string_codes(Nul, [0]),
    atomics_to_string([AnswerText, Nul, ProofText, "\n"], Text).

presentation_path(Root, Relative, Path) :-
    directory_file_path(Root, Relative, Path).

compiler_case(
    cetta_prime,
    'cetta-prime-file',
    [ 'core/syntax_core_v1.metta',
      'shared/lookahead_core_v1.metta',
      'shared/ground_relations_v1.metta',
      'shared/cetta_prime_scalar_classes_v1.metta',
      'languages/cetta_prime_reader_v1.metta'
    ],
    befb69eb730a85546be83d7d6a25bd36b89e5ada148d08d92231736425ce06b5,
    '47af2cfcf9ac56cc24bd6e8e139ddc22b39dd48177c5564cf3d7abea8c68fd74',
    coverage(complete(17), closed),
    '945459cd6f2e0cc2ffbab7f6c52e311e50d95c8ff5097905a25947ae3fbbf3e4').

compiler_case(
    metamath,
    database,
    [ 'core/syntax_core_v1.metta',
      'shared/lookahead_core_v1.metta',
      'languages/metamath_appendix_e_v1.metta'
    ],
    d31e4f39e3a631a4e5b06d3a684143da45cf83999532796ffc5082f5294d8cb7,
    '9e9f8ce0a85b8c982bdf662833aaf2d66c6a4b5804e3a6f13f8f16dbecd381f1',
    coverage(complete(47), closed),
    '27a6029c913771ef2c3e88092c9e1ca9794cbc7cb3d0ba331ccf2fbc584b760c').

compiler_case(
    megalodon,
    'mg-document',
    [ 'core/syntax_core_v1.metta',
      'shared/lookahead_core_v1.metta',
      'shared/char_core_v1.metta',
      'languages/megalodon_dynamic_v1.metta'
    ],
    a62e3cd67fd2b53c20b52f6eafc99cded0f3da1c0bf5a9d853aca8ca4ffa226f,
    '7d73a9e9d9c0124fb858df1eda3103bf3520c9d74d1b00ed7d81194bcad7f88e',
    coverage(complete(18), closed),
    '964edaa48cbb73cfb2b18441d039141a924be8f82ad45773a8a2898ea5072ee6').

compiler_case(
    tptp,
    'tptp-file',
    [ 'core/syntax_core_v1.metta',
      'shared/lookahead_core_v1.metta',
      'shared/char_core_v1.metta',
      'languages/tptp_fof_cnf_v1.metta'
    ],
    cfc78689367b4ac3ca049e9cd5ad1c105f30b2cd008a8d89ed35df6e6d52f40f,
    d1e4c5d3f34489c1479c55fe3bb0be1198c3933c05f6160c2ea3e228480edb3b,
    coverage(complete(92), closed),
    '61d67552e28dd8c9c54d2cc63ed4d93ca8d9ec00b37b48d20cf43a575480f01f').

require(Condition, Label) :-
    ( call(Condition) -> true
    ; throw(error(gate_failed(Label), test_finite_horn_compiler_matrix))
    ).
