:- module(test_petta_document_native_v1, []).

:- use_module(finite_horn_gslt_v1,
              [ read_ground_term_text/2,
                render_term/2
              ]).
:- use_module(petta_document_native_v1).
:- use_module(library(http/json)).
:- use_module(library(readutil)).

:- initialization(main, main).

main :-
    catch(run, Error, (print_message(error, Error), halt(1))),
    halt.

run :-
    current_prolog_flag(argv, Arguments),
    ( Arguments = [ SplitterAbi, FormAbi, GuardNFA, GuardEvidence,
                    InputPath, CompilerDigest ] ->
        true
    ; throw(error(gate_failed(expected_arguments),
                  test_petta_document_native_v1))
    ),
    read_file_to_string(InputPath, InputText, [encoding(utf8)]),
    Limits = petta_document_limits(
        65536, 2000000, 20000000, 2000000,
        10000000, 50000000, 4096, 1000000),
    petta_document_native_parse_v1(
        SplitterAbi, FormAbi, GuardNFA, GuardEvidence,
        InputText, CompilerDigest, Limits, Outcome),
    response_mutation_gate(
        SplitterAbi, FormAbi, GuardNFA, GuardEvidence,
        InputText, CompilerDigest, Limits),
    outcome_row(Outcome, Row0),
    Row = Row0.put(mutation_count, 1),
    json_write_dict(current_output, Row, [width(0)]),
    nl.

response_mutation_gate(
    SplitterAbi, FormAbi, GuardNFA, GuardEvidence,
    InputText, CompilerDigest, Limits) :-
    petta_document_native_v1:limits_values(Limits, LimitValues),
    petta_document_native_v1:petta_document_pipeline_call_v1(
        SplitterAbi, FormAbi, GuardNFA, GuardEvidence,
        InputText, CompilerDigest, LimitValues, ResponseText),
    read_ground_term_text(ResponseText, Response),
    mutate_source_passes(Response, Mutated),
    render_term(Mutated, MutatedText),
    petta_document_native_v1:checked_foreign_outcome(
        InputText, response(MutatedText), MutationOutcome),
    ( MutationOutcome = invalid_native_response(_) -> true
    ; throw(error(gate_failed(mutated_source_passes_accepted),
                  test_petta_document_native_v1))
    ).

mutate_source_passes(
    list([sym('petta-document-native-response-v1')|Fields0]),
    list([sym('petta-document-native-response-v1')|Fields])) :-
    select(list([sym('source-passes'), int(1)]), Fields0,
           list([sym('source-passes'), int(2)]), Fields).

outcome_row(
    completed(petta_document_native_result_v1(
        document(
            decision(Decision),
            input(bytes(InputBytes), scalars(InputScalars)),
            source_passes(SourcePasses),
            forest_digest(DocumentForestDigest)),
        provenance(
            splitter_pack_digest(SplitterPackDigest),
            form_pack_digest(FormPackDigest),
            guard_plan_digest(GuardPlanDigest),
            guard_evidence_digest(GuardEvidenceDigest),
            guard_nfa_answer_digest(GuardNFAAnswerDigest)),
        document_results(DocumentResults),
        forms(Forms))),
    _{ protocol:"petta-document-native-v1",
       outcome:"completed",
       decision:Decision,
       input_bytes:InputBytes,
       input_scalars:InputScalars,
       source_passes:SourcePasses,
       document_forest_digest:DocumentForestDigest,
       splitter_pack_digest:SplitterPackDigest,
       form_pack_digest:FormPackDigest,
       guard_plan_digest:GuardPlanDigest,
       guard_evidence_digest:GuardEvidenceDigest,
       guard_nfa_answer_digest:GuardNFAAnswerDigest,
       document_results:DocumentResultTexts,
       forms:FormRows }) :-
    maplist(result_text, DocumentResults, DocumentResultTexts),
    maplist(form_row, Forms, FormRows).
outcome_row(Outcome,
            _{ protocol:"petta-document-native-v1",
               outcome:"failure",
               detail:Text }) :-
    term_string(Outcome, Text, [quoted(true)]).

form_row(
    petta_document_native_form_v1(
        index(Index),
        kind(Kind),
        source_span(
            scalars(ScalarLeft, ScalarRight),
            bytes(ByteLeft, ByteRight)),
        filtered_scalars(Codepoints),
        decision(Decision),
        semantic_results(SemanticResults),
        host_actions(HostActions),
        receipts(source_passes(SourcePasses), guard_tokens(GuardTokens)),
        provenance(
            forest_digest(ForestDigest),
            guard_relation_digest(GuardRelationDigest))),
    _{ index:Index,
       kind:Kind,
       scalar_left:ScalarLeft,
       scalar_right:ScalarRight,
       byte_left:ByteLeft,
       byte_right:ByteRight,
       filtered:Filtered,
       decision:Decision,
       semantic_results:SemanticResultTexts,
       host_results:HostResultTexts,
       classifications:Classifications,
       source_passes:SourcePasses,
       guard_tokens:GuardTokens,
       forest_digest:ForestDigest,
       guard_relation_digest:GuardRelationDigest }) :-
    string_codes(Filtered, Codepoints),
    maplist(result_text, SemanticResults, SemanticResultTexts),
    maplist(host_action_row, HostActions, Classifications, HostResultTexts).

result_text(result(Value, Rest), Text) :-
    render_term(list([sym(result), Value, Rest]), Text).

host_action_row(host_action(Classification, Term),
                Classification, Text) :-
    copy_term(Term, Copy),
    numbervars(Copy, 0, _),
    with_output_to(
        string(Text),
        write_term(
            Copy,
            [quoted(true), numbervars(true), ignore_ops(true)] )
    ).
