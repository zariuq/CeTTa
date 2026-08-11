:- module(test_finite_horn_compiler,
          [ run_compiler_gate/2
          ]).

:- use_module(finite_horn_gslt_v1).
:- use_module(finite_horn_eval).

:- initialization(main, main).

main :-
    catch(run, Error, (print_message(error, Error), halt(1))),
    halt.

run :-
    current_prolog_flag(argv, Paths),
    run_compiler_gate(Paths, Total),
    format('(FiniteHornCompilerPeTTaCanarySummary ~d ~d 0)~n',
           [Total, Total]).

run_compiler_gate(Paths, Total) :-
    ( Paths = [CorePath, LookaheadPath, CompilerPath, LanguagePath] -> true
    ; throw(error(
          gate_failed(expected_core_lookahead_compiler_language_paths),
          test_finite_horn_compiler))
    ),
    maplist(read_presentation,
            [CorePath, LookaheadPath, CompilerPath, LanguagePath],
            Presentations),
    admit_presentations(Presentations),
    compile_query(Query),
    expected_answer(ExpectedAnswer),

    horn_query(Presentations, Query, 32, completed(Answers)),
    require(Answers = [answer(ExpectedAnswer, Proof)], exact_compiler_answer),
    require(horn_replay(Presentations, ExpectedAnswer, Proof), proof_replay),

    corrupt_root_rule(Proof, CorruptProof),
    require(\+ horn_replay(Presentations, ExpectedAnswer, CorruptProof),
            corrupt_proof_rejected),

    remove_rule(Presentations, 'compile-char', WithoutCharRule),
    admit_presentations(WithoutCharRule),
    horn_query(WithoutCharRule, Query, 32, completed(NoCharAnswers)),
    require(NoCharAnswers == [], compiler_rule_deletion_blocks_derivation),

    mutate_rule_output(Presentations, 'compile-seq', 'sir-seq', 'sir-alt',
                       MutatedCompiler),
    admit_presentations(MutatedCompiler),
    horn_query(MutatedCompiler, Query, 32, completed(MutatedAnswers)),
    expected_mutated_answer(ExpectedMutatedAnswer),
    require(MutatedAnswers = [answer(ExpectedMutatedAnswer, MutatedProof)],
            compiler_semantic_mutation_changes_sir),
    require(\+ memberchk(answer(ExpectedAnswer, _), MutatedAnswers),
            compiler_semantic_mutation_rejects_old_sir),
    require(horn_replay(MutatedCompiler, ExpectedMutatedAnswer, MutatedProof),
            mutated_proof_replay),

    horn_query(Presentations, Query, 1, Exhausted),
    require(Exhausted == resource_exhausted(depth),
            bounded_resource_exhaustion_is_explicit),

    local_source_has_no_guest_names,
    Total = 8.

compile_query(
    list([sym('compile-root'), sym('two-char-root'), var(result)])).

expected_answer(
    list([sym('compile-root'), sym('two-char-root'),
          list([sym('sir-root'), sym('two-char-root'),
                list([sym('sir-node'), sym('two-char'),
                      list([sym('sir-seq'),
                            list([sym('sir-char'), list([sym(cp), int(97)])]),
                            list([sym('sir-char'), list([sym(cp), int(98)])])])])])])).

expected_mutated_answer(
    list([sym('compile-root'), sym('two-char-root'),
          list([sym('sir-root'), sym('two-char-root'),
                list([sym('sir-node'), sym('two-char'),
                      list([sym('sir-alt'),
                            list([sym('sir-char'), list([sym(cp), int(97)])]),
                            list([sym('sir-char'), list([sym(cp), int(98)])])])])])])).

corrupt_root_rule(
    list([sym(cert), _RuleName, Children]),
    list([sym(cert), sym('nonexistent-compiler-rule'), Children])).

remove_rule([presentation(Name, Operators, Rules0, Source)|Presentations],
            RuleName,
            [presentation(Name, Operators, Rules, Source)|Presentations]) :-
    select(rule(RuleName, _, _), Rules0, Rules),
    !.
remove_rule([Presentation|Presentations0], RuleName,
            [Presentation|Presentations]) :-
    remove_rule(Presentations0, RuleName, Presentations).

mutate_rule_output(
    [presentation(Name, Operators, Rules0, Source)|Presentations],
    RuleName, OldFunctor, NewFunctor,
    [presentation(Name, Operators, Rules, Source)|Presentations]) :-
    select(rule(RuleName, Head0, Body), Rules0,
           rule(RuleName, Head, Body), Rules),
    replace_functor_node(Head0, OldFunctor, NewFunctor, Head),
    !.
mutate_rule_output([Presentation|Presentations0], RuleName,
                   OldFunctor, NewFunctor,
                   [Presentation|Presentations]) :-
    mutate_rule_output(Presentations0, RuleName, OldFunctor, NewFunctor,
                       Presentations).

replace_functor_node(list([sym(OldFunctor)|Arguments]), OldFunctor, NewFunctor,
                     list([sym(NewFunctor)|Arguments])) :-
    !.
replace_functor_node(list(Nodes0), OldFunctor, NewFunctor, list(Nodes)) :-
    maplist(replace_functor_in(OldFunctor, NewFunctor), Nodes0, Nodes).
replace_functor_node(Node, _, _, Node).

replace_functor_in(OldFunctor, NewFunctor, Node0, Node) :-
    replace_functor_node(Node0, OldFunctor, NewFunctor, Node).

local_source_has_no_guest_names :-
    module_property(finite_horn_eval, file(EvaluatorPath)),
    read_file_to_string(EvaluatorPath, EvaluatorText, [encoding(utf8)]),
    downcase_atom(EvaluatorText, LowerEvaluatorText),
    require(\+ sub_atom(LowerEvaluatorText, _, _, _, metamath),
            no_guest_name_one),
    require(\+ sub_atom(LowerEvaluatorText, _, _, _, megalodon),
            no_guest_name_two),
    require(\+ sub_atom(LowerEvaluatorText, _, _, _, tptp),
            no_guest_name_three).

require(Condition, Label) :-
    ( call(Condition) -> true
    ; throw(error(gate_failed(Label), test_finite_horn_compiler))
    ).
