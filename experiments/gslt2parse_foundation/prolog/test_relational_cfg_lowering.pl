:- module(test_relational_cfg_lowering,
          [ run_cfg_lowering_gate/2
          ]).

:- use_module(finite_horn_gslt_v1).
:- use_module(finite_horn_eval).
:- use_module(relational_cfg_eval).
:- use_module(library(crypto)).

:- initialization(main, main).

main :-
    catch(run, Error, (print_message(error, Error), halt(1))),
    halt.

run :-
    current_prolog_flag(argv, Arguments),
    ( Arguments = [PresentationRoot] -> true
    ; throw(error(gate_failed(expected_presentation_root),
                  test_relational_cfg_lowering))
    ),
    run_cfg_lowering_gate(PresentationRoot, Summary),
    Summary = summary(Positive, Negative, Mutations),
    Total is Positive + Negative + Mutations,
    format('(RelationalCFGLoweringPeTTaSummary ~d ~d ~d ~d 0)~n',
           [Total, Positive, Negative, Mutations]).

run_cfg_lowering_gate(PresentationRoot, Summary) :-
    presentation_path(
        PresentationRoot, 'reflection/finite_horn_reflection_v1.metta',
        ReflectionPath),
    presentation_path(
        PresentationRoot, 'compiler/relational_cfg_lowering_v1.metta',
        LoweringPath),
    presentation_path(
        PresentationRoot, 'compiler/relational_cfg_link_v1.metta',
        LinkPath),
    presentation_path(
        PresentationRoot, 'compiler/syntax_compiler_v1.metta',
        SyntaxCompilerPath),
    presentation_path(
        PresentationRoot, 'core/syntax_core_v1.metta', CorePath),
    presentation_path(
        PresentationRoot, 'shared/char_core_v1.metta', CharPath),
    presentation_path(
        PresentationRoot, 'shared/lookahead_core_v1.metta', LookaheadPath),
    presentation_path(
        PresentationRoot, 'languages/megalodon_dynamic_v1.metta',
        LanguagePath),
    read_presentation(ReflectionPath, Reflection),
    read_presentation(LoweringPath, Lowering),
    read_presentation(LinkPath, Link),
    read_presentation(SyntaxCompilerPath, SyntaxCompiler),
    read_presentation(CorePath, Core),
    read_presentation(CharPath, Char),
    read_presentation(LookaheadPath, Lookahead),
    read_presentation(LanguagePath, Language),
    SourcePackage = [Core, Char, Lookahead, Language],
    reflect_presentations(SourcePackage, Reflected),
    admit_presentations(
        [Reflection, Lowering, Link, SyntaxCompiler,
         Core, Char, Lookahead, Language, Reflected]),
    Compiler = [Reflection, Lowering],
    ComposedCompiler =
        [Reflection, Lowering, Link, SyntaxCompiler,
         Core, Char, Lookahead, Language, Reflected],
    quoted_presentation_digest(Language, QuotationDigest),
    require(
        QuotationDigest ==
            '573cb858dcf1e678fb6656c00077a53c53d977e51b74211c5dc21b2de91829be',
        exact_language_rule_quotation),

    compile_named_rule(
        Compiler, Language, 'mg-parse-name-list',
        'mg-name-list', 2, NameListAnswer, NameListProof),
    compile_named_rule(
        Compiler, Language, 'mg-parse-name-list-tail-cons',
        'mg-name-list-tail', 3, TailConsAnswer, TailConsProof),
    compile_named_rule(
        Compiler, Language, 'mg-parse-name-list-tail-empty',
        'mg-name-list-tail', 0, TailEmptyAnswer, TailEmptyProof),
    require(NameListAnswer \== TailConsAnswer, distinct_production_answers),
    require(TailConsAnswer \== TailEmptyAnswer, distinct_tail_answers),
    cfg_answer_matrix_digest(
        [NameListAnswer, TailConsAnswer, TailEmptyAnswer], MatrixDigest),
    require(
        MatrixDigest ==
            '143ae62ce08ec90500c8af16b3f4ea0817dcd4c71d595308b9b6a4539b25b928',
        exact_cfg_lowering_matrix),

    reflected_production_gate(ComposedCompiler, ProductionAnswers),
    reflected_state_gate(ComposedCompiler),
    composed_expression_gate(ComposedCompiler, ExpressionAnswer,
                             ExpressionProof),
    cfg_pack_from_answers(ProductionAnswers, Pack),
    semantic_result_gate(Pack, SourcePackage, SemanticMatrixDigest),
    require(
        SemanticMatrixDigest ==
            '67ad26b69049874d07f5b9f97578f3adf246128443ec96194fcc40ee09084fab',
        exact_source_lowered_semantic_matrix),

    reject_named_rule(Compiler, Language, 'mg-parse-document'),
    reject_named_rule(Compiler, Language, 'mg-resolve-infix'),
    captured_cursor_rule(CapturedCursor),
    require_no_compilation(Compiler, CapturedCursor, captured_cursor_action),
    nonlinear_cursor_rule(NonlinearCursor),
    require_no_compilation(Compiler, NonlinearCursor, nonlinear_cursor_flow),
    guarded_parse_rule(GuardedParse),
    require_no_compilation(Compiler, GuardedParse, non_parse_guard),
    variable_state_rule(VariableState),
    require_no_compilation(Compiler, VariableState, variable_parse_state),
    nonground_grammar_rule(NongroundGrammar),
    require_no_compilation(Compiler, NongroundGrammar,
                           nonground_child_grammar),
    duplicate_output_rule(DuplicateOutput),
    require_no_compilation(Compiler, DuplicateOutput,
                           duplicate_child_output),
    unproduced_action_rule(UnproducedAction),
    require_no_compilation(Compiler, UnproducedAction,
                           unproduced_action_variable),
    broken_cursor_rule(BrokenCursor),
    require_no_compilation(Compiler, BrokenCursor,
                           broken_cursor_threading),

    delete_named_rule(Lowering, 'compile-cfg-chain-step', WithoutChainStep),
    named_rule(Language, 'mg-parse-name-list', NameListRule),
    quote_rule(NameListRule, QuotedNameList),
    require_no_compilation(
        [Reflection, WithoutChainStep], QuotedNameList,
        compiler_step_rule_deletion),
    corrupt_proof(NameListProof, CorruptProof),
    require(\+ horn_replay(Compiler, NameListAnswer, CorruptProof),
            corrupt_compiler_proof_rejected),
    require(horn_replay(Compiler, TailConsAnswer, TailConsProof),
            tail_cons_proof_replay),
    require(horn_replay(Compiler, TailEmptyAnswer, TailEmptyProof),
            tail_empty_proof_replay),
    require(horn_replay(ComposedCompiler, ExpressionAnswer, ExpressionProof),
            composed_expression_proof_replay),
    delete_named_rule(Link, 'compile-grammar-relational-state', WithoutLink),
    replace_presentation(Link, WithoutLink, ComposedCompiler,
                         CompilerWithoutLink),
    require_no_expression_compilation(CompilerWithoutLink,
                                      relational_link_rule_deletion),
    delete_reflected_source_rule(
        Reflected, 'mg-parse-name-list', ReflectedWithoutNameList),
    replace_presentation(Reflected, ReflectedWithoutNameList,
                         ComposedCompiler, CompilerWithoutNameList),
    require_no_expression_compilation(
        CompilerWithoutNameList, reflected_source_rule_deletion),
    corrupt_cfg_action(Pack, 'mg-parse-name-list', CorruptActionPack),
    require_semantic_difference(CorruptActionPack, SourcePackage, [97],
                                corrupted_semantic_action),
    delete_cfg_production(Pack, 'mg-parse-name-list-tail-cons',
                          MissingTailConsPack),
    require_semantic_difference(MissingTailConsPack, SourcePackage,
                                [97, 32, 98],
                                cfg_production_deletion),

    generic_source_purity(ReflectionPath),
    generic_source_purity(LoweringPath),
    generic_source_purity(LinkPath),
    source_file(relational_cfg_eval:cfg_parse_results(_, _, _, _, _, _),
                RelationalEvaluatorPath),
    generic_source_purity(RelationalEvaluatorPath),
    Summary = summary(14, 10, 6).

presentation_path(Root, Relative, Path) :-
    directory_file_path(Root, Relative, Path).

compile_named_rule(Compiler, Language, RuleName, StateName, CallCount,
                   Answer, Proof) :-
    named_rule(Language, RuleName, Rule),
    quote_rule(Rule, QuotedRule),
    compile_quoted_rule(Compiler, QuotedRule, Answers),
    require(Answers = [answer(Answer, Proof)],
            exact_single_cfg_lowering(RuleName)),
    require(horn_replay(Compiler, Answer, Proof),
            cfg_lowering_proof_replay(RuleName)),
    Answer = list([sym('compile-cfg-rule'), QuotedRule, IR]),
    IR = list([sym('sir-production'), sym(RuleName),
               sym(StateName), Items, Action]),
    sir_item_count(Items, ActualCallCount),
    require(ActualCallCount =:= CallCount,
            exact_cfg_call_count(RuleName)),
    quoted_rule_action(QuotedRule, Action),
    !.

reject_named_rule(Compiler, Language, RuleName) :-
    named_rule(Language, RuleName, Rule),
    quote_rule(Rule, QuotedRule),
    require_no_compilation(Compiler, QuotedRule,
                           unsupported_source_rule(RuleName)).

compile_quoted_rule(Compiler, QuotedRule, Answers) :-
    Query = list([sym('compile-cfg-rule'), QuotedRule, var(ir)]),
    horn_query(Compiler, Query, 512, Outcome),
    require(Outcome = completed(Answers), completed_cfg_lowering).

require_no_compilation(Compiler, QuotedRule, Label) :-
    compile_quoted_rule(Compiler, QuotedRule, Answers),
    require(Answers == [], Label).

named_rule(presentation(_, _, Rules, _), RuleName, Rule) :-
    member(Rule, Rules),
    Rule = rule(RuleName, _, _),
    !.

quoted_rule_action(
    list([sym('q-rule'), _,
          list([sym('q-app'), list([sym('q-sym'), sym(parse)]),
                list([sym('q-cons'), _,
                      list([sym('q-cons'), _,
                            list([sym('q-cons'), Action,
                                  list([sym('q-cons'), _, sym('q-nil')])])])])]),
          _]),
    Action).

sir_item_count(sym('sir-items-nil'), 0).
sir_item_count(list([sym('sir-items-cons'),
                     list([sym('sir-call'), _, _]), Tail]), Count) :-
    sir_item_count(Tail, TailCount),
    Count is TailCount + 1.

cfg_answer_matrix_digest(Answers, Digest) :-
    maplist(answer_ir_text, Answers, Texts),
    atomics_to_string(Texts, "\n", Payload),
    crypto_data_hash(Payload, Digest, [algorithm(sha256)]).

answer_ir_text(list([sym('compile-cfg-rule'), _, IR]), Text) :-
    render_term(IR, Text).

reflected_production_gate(Compiler, Answers) :-
    Query = list([sym('compile-cfg-production'),
                  var(quoted_rule), var(source_production)]),
    horn_query(Compiler, Query, 1024, Outcome),
    require(Outcome = completed(Answers),
            reflected_production_query_completed),
    maplist(replay_answer(Compiler), Answers),
    maplist(reflected_production_shape, Answers, Shapes0),
    sort(Shapes0, Shapes),
    Expected =
        [ production('mg-parse-name-list', 'mg-name-list', 2),
          production('mg-parse-name-list-tail-cons',
                     'mg-name-list-tail', 3),
          production('mg-parse-name-list-tail-empty',
                     'mg-name-list-tail', 0)
        ],
    require(Shapes == Expected, exact_reflected_cfg_productions).

reflected_production_shape(
    answer(
        list([sym('compile-cfg-production'), _,
              list([sym('sir-source-production'),
                    list([sym('q-sym'), sym('MegalodonDynamicV1')]),
                    list([sym('sir-production'), sym(Rule), sym(State),
                          Items, _])])]),
        _),
    production(Rule, State, CallCount)) :-
    sir_item_count(Items, CallCount).

reflected_state_gate(Compiler) :-
    Query = list([sym('compile-cfg-state'), var(state)]),
    horn_query(Compiler, Query, 1024, Outcome),
    require(Outcome = completed(Answers), reflected_state_query_completed),
    maplist(replay_answer(Compiler), Answers),
    maplist(reflected_state_name, Answers, StateNames0),
    sort(StateNames0, StateNames),
    require(StateNames == ['mg-name-list', 'mg-name-list-tail'],
            exact_reflected_cfg_states),
    length(Answers, ProofCount),
    require(ProofCount =:= 3, exact_reflected_state_proof_count).

reflected_state_name(
    answer(list([sym('compile-cfg-state'), sym(State)]), _), State).

composed_expression_gate(Compiler, Answer, Proof) :-
    expression_query(Query),
    horn_query(Compiler, Query, 1024, Outcome),
    require(Outcome = completed([answer(Answer, Proof)]),
            exact_single_composed_expression),
    expression_answer(Expected),
    require(Answer == Expected, exact_composed_expression_ir),
    require(horn_replay(Compiler, Answer, Proof),
            composed_expression_initial_replay).

expression_query(
    list([sym('compile-definition'), sym('mg-expression'), var(ir)])).

expression_answer(
    list([sym('compile-definition'), sym('mg-expression'),
          list([sym('sir-definition'), sym('mg-expression'),
                list([sym('sir-left'),
                      list([sym('sir-node'), sym('mg-expression'),
                            list([sym('sir-rel'),
                                  sym('mg-name-list')])]),
                      list([sym('sir-ref'), sym('mg-skip')])])])])).

replay_answer(Compiler, answer(Answer, Proof)) :-
    require(horn_replay(Compiler, Answer, Proof), reflected_proof_replay).

require_no_expression_compilation(Compiler, Label) :-
    expression_query(Query),
    horn_query(Compiler, Query, 1024, Outcome),
    require(Outcome == completed([]), Label).

semantic_result_gate(Pack, SourcePresentations, MatrixDigest) :-
    semantic_cases(Cases),
    maplist(semantic_case(Pack, SourcePresentations), Cases, Rows),
    semantic_matrix_digest(Rows, MatrixDigest).

semantic_cases(
    [ semantic_case(empty, [], empty),
      semantic_case(single_name, [97], nonempty),
      semantic_case(two_names, [97, 32, 98], nonempty),
      semantic_case(name_suffix, [97, 49, 95, 98, 39], nonempty),
      semantic_case(leading_digit, [49, 97], empty),
      semantic_case(non_ascii_initial, [955], empty)
    ]).

semantic_case(Pack, SourcePresentations,
              semantic_case(Label, Codes, ExpectedShape),
              semantic_row(Label, Results)) :-
    codepoints_node(Codes, Input),
    source_parse_results(SourcePresentations, 'mg-name-list', Input, 256,
                         SourceOutcome),
    cfg_parse_results(Pack, SourcePresentations, 'mg-name-list', Input, 256,
                      CompiledOutcome),
    require(SourceOutcome = completed(SourceResults),
            source_semantic_query_completed(Label)),
    require(CompiledOutcome = completed(CompiledResults),
            compiled_semantic_query_completed(Label)),
    require(CompiledResults == SourceResults,
            complete_semantic_result_agreement(Label)),
    require_result_shape(ExpectedShape, SourceResults, Label),
    Results = SourceResults.

source_parse_results(SourcePresentations, State, Input, MaxDepth, Outcome) :-
    Query = list([sym(parse), sym(State), Input, var(value), var(rest)]),
    horn_query(SourcePresentations, Query, MaxDepth, HornOutcome),
    ( HornOutcome = completed(Answers) ->
        maplist(source_parse_result, Answers, RawResults),
        sort(RawResults, Results),
        Outcome = completed(Results)
    ; Outcome = HornOutcome
    ).

source_parse_result(
    answer(list([sym(parse), _, _, Value, Rest]), _),
    result(Value, Rest)).

require_result_shape(empty, Results, Label) :-
    require(Results == [], expected_empty_result_set(Label)).
require_result_shape(nonempty, Results, Label) :-
    require(Results = [_|_], expected_nonempty_result_set(Label)).

semantic_matrix_digest(Rows, Digest) :-
    maplist(semantic_row_text, Rows, RowTexts),
    atomics_to_string(RowTexts, "\n", Payload),
    crypto_data_hash(Payload, Digest, [algorithm(sha256)]).

semantic_row_text(semantic_row(Label, Results), Text) :-
    maplist(semantic_result_text, Results, ResultTexts),
    atomics_to_string(ResultTexts, "|", ResultPayload),
    format(string(Text), '~w:~s', [Label, ResultPayload]).

semantic_result_text(result(Value, Rest), Text) :-
    render_term(list([sym(result), Value, Rest]), Text).

require_semantic_difference(Pack, SourcePresentations, Codes, Label) :-
    codepoints_node(Codes, Input),
    source_parse_results(SourcePresentations, 'mg-name-list', Input, 256,
                         completed(SourceResults)),
    cfg_parse_results(Pack, SourcePresentations, 'mg-name-list', Input, 256,
                      completed(CompiledResults)),
    require(CompiledResults \== SourceResults, Label).

corrupt_cfg_action(cfg_pack(Productions0), RuleName,
                   cfg_pack(Productions)) :-
    select(production(Presentation, RuleName, State, Items, _),
           Productions0,
           production(Presentation, RuleName, State, Items,
                      list([sym('q-sym'), sym(corrupt-action)])),
           Productions),
    !.

delete_cfg_production(cfg_pack(Productions0), RuleName,
                      cfg_pack(Productions)) :-
    exclude(cfg_production_has_rule(RuleName), Productions0, Productions),
    length(Productions0, Before),
    length(Productions, After),
    require(After =:= Before - 1, deleted_exactly_one_cfg_production).

cfg_production_has_rule(
    RuleName, production(_, RuleName, _, _, _)).

codepoints_node([], sym(nil)).
codepoints_node([Codepoint|Codepoints],
                list([sym(cons), list([sym(cp), int(Codepoint)]), Rest])) :-
    codepoints_node(Codepoints, Rest).

quoted_presentation_digest(Presentation, Digest) :-
    quote_presentation_rules(Presentation, QuotedRules),
    render_term(QuotedRules, Text),
    string_codes(Text, Bytes),
    crypto_data_hash(Bytes, Digest, [algorithm(sha256), encoding(octet)]).

delete_named_rule(presentation(Name, Operators, Rules0, Source), RuleName,
                  presentation(Name, Operators, Rules, Source)) :-
    exclude(rule_has_name(RuleName), Rules0, Rules),
    length(Rules0, Before),
    length(Rules, After),
    require(After =:= Before - 1, deleted_exactly_one_compiler_rule).

rule_has_name(Name, rule(Name, _, _)).

replace_presentation(Old, New, [Head|Tail], Result) :-
    ( Head == Old ->
        Result = [New|Tail]
    ; Result = [Head|Remaining],
      replace_presentation(Old, New, Tail, Remaining)
    ).

delete_reflected_source_rule(
    presentation(Name, Operators, Rules0, Source), SourceRuleName,
    presentation(Name, Operators, Rules, Source)) :-
    exclude(reflects_source_rule(SourceRuleName), Rules0, Rules),
    length(Rules0, Before),
    length(Rules, After),
    require(After =:= Before - 1,
            deleted_exactly_one_reflected_source_rule).

reflects_source_rule(
    SourceRuleName,
    rule(_, list([sym('source-rule'), _,
                  list([sym('q-rule'),
                        list([sym('q-sym'), sym(SourceRuleName)]), _, _])]),
         [])).

corrupt_proof(list([sym(cert), _, Children]),
              list([sym(cert), sym(no-such-compiler-rule), Children])).

captured_cursor_rule(Rule) :-
    qtest_symbol(state, State),
    qtest_symbol(atom, Grammar),
    qtest_var(0, Input),
    qtest_var(2, Rest),
    qtest_parse(State, Input, Input, Rest, Head),
    qtest_parse(Grammar, Input, Input, Rest, Goal),
    qtest_rule(captured-cursor, Head, [Goal], Rule).

nonlinear_cursor_rule(Rule) :-
    qtest_symbol(state, State),
    qtest_symbol(atom, Grammar),
    qtest_var(0, Input),
    qtest_var(1, Value),
    qtest_parse(State, Input, Value, Input, Head),
    qtest_parse(Grammar, Input, Value, Input, Goal),
    qtest_rule(nonlinear-cursor, Head, [Goal], Rule).

guarded_parse_rule(Rule) :-
    qtest_symbol(state, State),
    qtest_symbol(value, Value),
    qtest_var(0, Input),
    qtest_parse(State, Input, Value, Input, Head),
    qtest_app(environment-ok, [], Guard),
    qtest_rule(guarded-parse, Head, [Guard], Rule).

variable_state_rule(Rule) :-
    qtest_var(0, Input),
    qtest_var(1, Value),
    qtest_var(2, Rest),
    qtest_var(3, VariableState),
    qtest_symbol(atom, Grammar),
    qtest_parse(VariableState, Input, Value, Rest, Head),
    qtest_parse(Grammar, Input, Value, Rest, Goal),
    qtest_rule(variable-state, Head, [Goal], Rule).

nonground_grammar_rule(Rule) :-
    qtest_symbol(state, State),
    qtest_var(0, Input),
    qtest_var(1, Value),
    qtest_var(2, Rest),
    qtest_var(3, VariableGrammar),
    qtest_parse(State, Input, Value, Rest, Head),
    qtest_parse(VariableGrammar, Input, Value, Rest, Goal),
    qtest_rule(nonground-grammar, Head, [Goal], Rule).

duplicate_output_rule(Rule) :-
    qtest_symbol(state, State),
    qtest_symbol(atom, Grammar),
    qtest_var(0, Input),
    qtest_var(1, Value),
    qtest_var(2, Rest),
    qtest_var(3, Middle),
    qtest_parse(State, Input, Value, Rest, Head),
    qtest_parse(Grammar, Input, Value, Middle, First),
    qtest_parse(Grammar, Middle, Value, Rest, Second),
    qtest_rule(duplicate-output, Head, [First, Second], Rule).

unproduced_action_rule(Rule) :-
    qtest_symbol(state, State),
    qtest_symbol(atom, Grammar),
    qtest_var(0, Input),
    qtest_var(1, Action),
    qtest_var(2, Rest),
    qtest_var(3, ChildValue),
    qtest_parse(State, Input, Action, Rest, Head),
    qtest_parse(Grammar, Input, ChildValue, Rest, Goal),
    qtest_rule(unproduced-action, Head, [Goal], Rule).

broken_cursor_rule(Rule) :-
    qtest_symbol(state, State),
    qtest_symbol(atom, Grammar),
    qtest_var(0, Input),
    qtest_var(1, LeftValue),
    qtest_var(2, RightValue),
    qtest_var(3, Rest),
    qtest_var(4, Middle),
    qtest_var(5, WrongMiddle),
    qtest_app(cons, [LeftValue, RightValue], Action),
    qtest_parse(State, Input, Action, Rest, Head),
    qtest_parse(Grammar, Input, LeftValue, Middle, First),
    qtest_parse(Grammar, WrongMiddle, RightValue, Rest, Second),
    qtest_rule(broken-cursor, Head, [First, Second], Rule).

qtest_rule(Name, Head, Body,
           list([sym('q-rule'), QuotedName, Head, QuotedBody])) :-
    qtest_symbol(Name, QuotedName),
    qtest_list(Body, QuotedBody).

qtest_parse(Grammar, Input, Value, Rest, Term) :-
    qtest_app(parse, [Grammar, Input, Value, Rest], Term).

qtest_app(Name, Arguments,
          list([sym('q-app'), QuotedName, QuotedArguments])) :-
    qtest_symbol(Name, QuotedName),
    qtest_list(Arguments, QuotedArguments).

qtest_symbol(Name, list([sym('q-sym'), sym(Name)])).

qtest_var(Number, list([sym('q-var'), Index])) :-
    qtest_index(Number, Index).

qtest_index(0, sym('q-zero')) :-
    !.
qtest_index(Number, list([sym('q-succ'), Prior])) :-
    Number > 0,
    Previous is Number - 1,
    qtest_index(Previous, Prior).

qtest_list([], sym('q-nil')).
qtest_list([Head|Tail], list([sym('q-cons'), Head, QuotedTail])) :-
    qtest_list(Tail, QuotedTail).

generic_source_purity(Path) :-
    read_file_to_string(Path, Text, [encoding(utf8)]),
    downcase_atom(Text, Lower),
    require(\+ sub_atom(Lower, _, _, _, metamath),
            generic_source_has_no_metamath(Path)),
    require(\+ sub_atom(Lower, _, _, _, megalodon),
            generic_source_has_no_megalodon(Path)),
    require(\+ sub_atom(Lower, _, _, _, tptp),
            generic_source_has_no_tptp(Path)).

require(Condition, Label) :-
    ( call(Condition) -> true
    ; throw(error(gate_failed(Label), test_relational_cfg_lowering))
    ).
