:- module(petta_document_native_v1,
          [ petta_document_native_parse_v1/8,
            'gslt2parse-petta-document-native-v1'/8
          ]).

:- use_module(finite_horn_gslt_v1,
              [ read_ground_term_text/2
              ]).
:- use_module(petta_form_gslt_reference_v1,
              [ petta_form_value_term_v1/2,
                petta_form_value_classification_v1/2
              ]).
:- use_module(library(pairs)).
:- use_module(library(utf8)).

:- prolog_load_context(directory, ModuleDirectory),
   directory_file_path(
       ModuleDirectory,
       'native/petta_document_pipeline_ffi_v1.so',
       ForeignLibrary),
   use_foreign_library(ForeignLibrary).

/*
  PeTTa-specific reflective adapter over the generic native parser kernels.
  The foreign response contains canonical carrier terms.  Host terms are
  projected structurally from semantic actions; source text is never parsed
  again at this boundary.
*/

petta_document_native_parse_v1(
    SplitterAbi,
    FormAbi,
    GuardNFA,
    GuardEvidence,
    InputText,
    CompilerDigest,
    Limits,
    Outcome) :-
    must_be(atom, SplitterAbi),
    must_be(atom, FormAbi),
    must_be(atom, GuardNFA),
    must_be(atom, GuardEvidence),
    must_be(string, InputText),
    digest_atom(CompilerDigest),
    limits_values(Limits, LimitValues),
    native_foreign_outcome(
        SplitterAbi, FormAbi, GuardNFA, GuardEvidence,
        InputText, CompilerDigest, LimitValues, ForeignOutcome),
    checked_foreign_outcome(InputText, ForeignOutcome, Outcome).

native_foreign_outcome(
    SplitterAbi, FormAbi, GuardNFA, GuardEvidence,
    InputText, CompilerDigest, LimitValues, Outcome) :-
    catch(
        ( petta_document_pipeline_call_v1(
              SplitterAbi, FormAbi, GuardNFA, GuardEvidence,
              InputText, CompilerDigest, LimitValues, ResponseText),
          Outcome = response(ResponseText)
        ),
        error(petta_document_pipeline_error(Message), _),
        Outcome = error(Message)).

checked_foreign_outcome(_, error(Message),
                        invalid_native_request(Message)).
checked_foreign_outcome(InputText, response(ResponseText), Outcome) :-
    ( catch(
          ( read_ground_term_text(ResponseText, Response),
            checked_response(InputText, Response, Checked)
          ),
          Error,
          Validation = exception(Error)) ->
        ( var(Validation) -> Outcome = completed(Checked)
        ; Validation = exception(Error),
          Outcome = invalid_native_response(Error)
        )
    ; Outcome = invalid_native_response(validation_failed)
    ).

checked_response(
    InputText,
    list([sym('petta-document-native-response-v1')|RawFields]),
    petta_document_native_result_v1(
        document(
            decision(DocumentDecision),
            input(bytes(InputBytes), scalars(InputScalars)),
            source_passes(1),
            forest_digest(DocumentForestDigest)),
        provenance(
            splitter_pack_digest(SplitterPackDigest),
            form_pack_digest(FormPackDigest),
            guard_plan_digest(GuardPlanDigest),
            guard_evidence_digest(GuardEvidenceDigest),
            guard_nfa_answer_digest(GuardNFAAnswerDigest)),
        document_results(DocumentResults),
        forms(Forms))) :-
    response_fields(RawFields, Fields),
    exact_field_names(
        Fields,
        [ 'splitter-pack-digest',
          'form-pack-digest',
          'guard-plan-digest',
          'guard-evidence-digest',
          'guard-nfa-answer-digest',
          'document-decision',
          'document-gll-forest-digest',
          'document-glr-forest-digest',
          'document-gll-source-passes',
          'document-glr-source-passes',
          'source-passes',
          'input-bytes',
          'input-scalars',
          'form-count',
          'form-accepted',
          'form-rejected',
          'document-results',
          forms
        ]),
    digest_field(Fields, 'splitter-pack-digest', SplitterPackDigest),
    digest_field(Fields, 'form-pack-digest', FormPackDigest),
    digest_field(Fields, 'guard-plan-digest', GuardPlanDigest),
    digest_field(Fields, 'guard-evidence-digest', GuardEvidenceDigest),
    digest_field(Fields, 'guard-nfa-answer-digest', GuardNFAAnswerDigest),
    response_field(Fields, 'document-decision', sym(DocumentDecision)),
    memberchk(DocumentDecision, [accepted, rejected]),
    digest_field(
        Fields, 'document-gll-forest-digest', DocumentForestDigest),
    digest_field(
        Fields, 'document-glr-forest-digest', DocumentForestDigest),
    response_field(Fields, 'document-gll-source-passes', int(1)),
    response_field(Fields, 'document-glr-source-passes', int(0)),
    response_field(Fields, 'source-passes', int(1)),
    response_field(Fields, 'input-bytes', int(InputBytes)),
    response_field(Fields, 'input-scalars', int(InputScalars)),
    input_offsets(InputText, Offsets),
    length(Offsets, OffsetCount),
    InputScalars is OffsetCount - 1,
    last(Offsets, InputBytes),
    response_field(Fields, 'document-results', DocumentResultsNode),
    carrier_list(DocumentResultsNode, RawDocumentResults),
    maplist(canonical_result, RawDocumentResults, DocumentResults),
    response_field(Fields, forms, FormsNode),
    carrier_list(FormsNode, RawForms),
    response_field(Fields, 'form-count', int(FormCount)),
    length(RawForms, FormCount),
    checked_forms(RawForms, Offsets, 0, Forms),
    response_field(Fields, 'form-accepted', int(AcceptedCount)),
    response_field(Fields, 'form-rejected', int(RejectedCount)),
    form_decision_counts(Forms, AcceptedCount, RejectedCount),
    document_result_contract(
        DocumentDecision, DocumentResults, Forms).

document_result_contract(accepted, [_], _).
document_result_contract(rejected, [], []).

checked_forms([], _, _, []).
checked_forms(
    [RawForm|RawForms], Offsets, ExpectedIndex,
    [Form|Forms]) :-
    checked_form(RawForm, Offsets, ExpectedIndex, Form),
    Form = petta_document_native_form_v1(
        _, _, source_span(scalars(_, ScalarRight), _), _, _, _, _, _, _),
    NextIndex is ExpectedIndex + 1,
    checked_forms_after(
        RawForms, Offsets, NextIndex, ScalarRight, Forms).

checked_forms_after([], _, _, _, []).
checked_forms_after(
    [RawForm|RawForms], Offsets, ExpectedIndex, PreviousRight,
    [Form|Forms]) :-
    checked_form(RawForm, Offsets, ExpectedIndex, Form),
    Form = petta_document_native_form_v1(
        _, _, source_span(scalars(ScalarLeft, ScalarRight), _),
        _, _, _, _, _, _),
    PreviousRight =< ScalarLeft,
    NextIndex is ExpectedIndex + 1,
    checked_forms_after(
        RawForms, Offsets, NextIndex, ScalarRight, Forms).

checked_form(
    list([sym('petta-document-native-form-v1')|RawFields]),
    Offsets,
    ExpectedIndex,
    petta_document_native_form_v1(
        index(ExpectedIndex),
        kind(Kind),
        source_span(
            scalars(ScalarLeft, ScalarRight),
            bytes(ByteLeft, ByteRight)),
        filtered_scalars(Codepoints),
        decision(Decision),
        semantic_results(SemanticResults),
        host_actions(HostActions),
        receipts(source_passes(0), guard_tokens(GuardTokens)),
        provenance(
            forest_digest(ForestDigest),
            guard_relation_digest(GuardRelationDigest)))) :-
    response_fields(RawFields, Fields),
    exact_field_names(
        Fields,
        [ index,
          kind,
          'source-scalar-left',
          'source-scalar-right',
          'source-byte-left',
          'source-byte-right',
          'form-scalars',
          'form-bytes',
          'filtered-scalars',
          decision,
          'gll-forest-digest',
          'glr-forest-digest',
          'gll-guard-relation-digest',
          'glr-guard-relation-digest',
          'scan-source-passes',
          'gll-source-passes',
          'glr-source-passes',
          'guard-tokens',
          results
        ]),
    response_field(Fields, index, int(ExpectedIndex)),
    response_field(Fields, kind, sym(Kind)),
    memberchk(Kind, [form, runnable]),
    response_field(Fields, 'source-scalar-left', int(ScalarLeft)),
    response_field(Fields, 'source-scalar-right', int(ScalarRight)),
    nth0(ScalarLeft, Offsets, ByteLeft),
    nth0(ScalarRight, Offsets, ByteRight),
    response_field(Fields, 'source-byte-left', int(ByteLeft)),
    response_field(Fields, 'source-byte-right', int(ByteRight)),
    response_field(Fields, 'filtered-scalars', FilteredNode),
    carrier_codepoints(FilteredNode, Codepoints),
    response_field(Fields, 'form-scalars', int(FormScalars)),
    length(Codepoints, FormScalars),
    phrase(utf8_codes(Codepoints), FormBytesValue),
    length(FormBytesValue, FormBytes),
    response_field(Fields, 'form-bytes', int(FormBytes)),
    response_field(Fields, decision, sym(Decision)),
    memberchk(Decision, [accepted, rejected]),
    digest_field(Fields, 'gll-forest-digest', ForestDigest),
    digest_field(Fields, 'glr-forest-digest', ForestDigest),
    digest_field(
        Fields, 'gll-guard-relation-digest', GuardRelationDigest),
    digest_field(
        Fields, 'glr-guard-relation-digest', GuardRelationDigest),
    response_field(Fields, 'scan-source-passes', int(0)),
    response_field(Fields, 'gll-source-passes', int(0)),
    response_field(Fields, 'glr-source-passes', int(0)),
    response_field(Fields, 'guard-tokens', int(GuardTokens)),
    GuardTokens >= 0,
    response_field(Fields, results, ResultsNode),
    carrier_list(ResultsNode, RawResults),
    maplist(canonical_result, RawResults, SemanticResults),
    host_actions(Kind, SemanticResults, HostActions),
    form_result_contract(Decision, SemanticResults).

canonical_result(
    list([sym(result), Value, sym(nil)]), result(Value, sym(nil))).

host_actions(_, [], []).
host_actions(Kind, [result(Value, sym(nil))|Results],
             [host_action(Classification, Term)|Actions]) :-
    petta_form_value_term_v1(Value, Term),
    ( Kind == runnable -> Classification = runnable
    ; petta_form_value_classification_v1(Value, Classification)
    ),
    host_actions(Kind, Results, Actions).

form_result_contract(accepted, [_|_]).
form_result_contract(rejected, []).

form_decision_counts(Forms, Accepted, Rejected) :-
    include(form_accepted, Forms, AcceptedForms),
    include(form_rejected, Forms, RejectedForms),
    length(AcceptedForms, Accepted),
    length(RejectedForms, Rejected).

form_accepted(petta_document_native_form_v1(
    _, _, _, _, decision(accepted), _, _, _, _)).
form_rejected(petta_document_native_form_v1(
    _, _, _, _, decision(rejected), _, _, _, _)).

response_fields(RawFields, Fields) :-
    maplist(response_field_pair, RawFields, Pairs),
    keysort(Pairs, Fields),
    pairs_keys(Fields, Keys),
    sort(Keys, UniqueKeys),
    Keys == UniqueKeys.

response_field_pair(list([sym(Name), Value]), Name-Value) :-
    atom(Name).

response_field(Fields, Name, Value) :-
    memberchk(Name-Value, Fields).

exact_field_names(Fields, Names) :-
    pairs_keys(Fields, Keys),
    sort(Names, Expected),
    Keys == Expected.

digest_field(Fields, Name, Digest) :-
    response_field(Fields, Name, str(Text)),
    string_codes(Text, Codes),
    length(Codes, 64),
    maplist(lower_hex_code, Codes),
    atom_string(Digest, Text).

digest_atom(Digest) :-
    must_be(atom, Digest),
    atom_codes(Digest, Codes),
    length(Codes, 64),
    maplist(lower_hex_code, Codes).

lower_hex_code(Code) :-
    between(0'0, 0'9, Code), !.
lower_hex_code(Code) :-
    between(0'a, 0'f, Code).

carrier_list(sym(nil), []).
carrier_list(list([sym(cons), Value, Rest]), [Value|Values]) :-
    carrier_list(Rest, Values).

carrier_codepoints(Node, Codepoints) :-
    carrier_list(Node, Values),
    maplist(codepoint_node, Values, Codepoints).

codepoint_node(list([sym(cp), int(Codepoint)]), Codepoint) :-
    unicode_scalar(Codepoint).

unicode_scalar(Codepoint) :-
    integer(Codepoint),
    between(0, 0x10ffff, Codepoint),
    \+ between(0xd800, 0xdfff, Codepoint).

input_offsets(Text, Offsets) :-
    string_codes(Text, Codepoints),
    input_offsets(Codepoints, 0, Offsets).

input_offsets([], Offset, [Offset]).
input_offsets([Codepoint|Codepoints], Offset0, [Offset0|Offsets]) :-
    phrase(utf8_codes([Codepoint]), Bytes),
    length(Bytes, Width),
    Offset is Offset0 + Width,
    input_offsets(Codepoints, Offset, Offsets).

limits_values(
    petta_document_limits(
        DFAStates, DFATransitions, ScanWork, ScanTokens,
        WitnessWork, ParseWork, ReplayDepth, ResultLimit),
    [ DFAStates, DFATransitions, ScanWork, ScanTokens,
      WitnessWork, ParseWork, ReplayDepth, ResultLimit ]) :-
    maplist(positive_integer,
            [ DFAStates, DFATransitions, ScanWork, ScanTokens,
              WitnessWork, ParseWork, ReplayDepth, ResultLimit ]).

positive_integer(Value) :-
    must_be(integer, Value),
    Value > 0.

'gslt2parse-petta-document-native-v1'(
    SplitterAbi0,
    FormAbi0,
    GuardNFA0,
    GuardEvidence0,
    Input0,
    CompilerDigest0,
    Limits0,
    SurfaceOutcome) :-
    surface_atom(SplitterAbi0, SplitterAbi),
    surface_atom(FormAbi0, FormAbi),
    surface_atom(GuardNFA0, GuardNFA),
    surface_atom(GuardEvidence0, GuardEvidence),
    surface_text(Input0, Input),
    surface_atom(CompilerDigest0, CompilerDigest),
    surface_limits(Limits0, Limits),
    petta_document_native_parse_v1(
        SplitterAbi, FormAbi, GuardNFA, GuardEvidence,
        Input, CompilerDigest, Limits, Outcome),
    native_surface(Outcome, SurfaceOutcome).

surface_atom(Value, Value) :- atom(Value), !.
surface_atom(Value, Atom) :- string(Value), atom_string(Atom, Value).

surface_text(Value, Value) :- string(Value), !.
surface_text(Value, Text) :- atom(Value), atom_string(Value, Text).

surface_limits(
    [ petta_document_limits,
      DFAStates, DFATransitions, ScanWork, ScanTokens,
      WitnessWork, ParseWork, ReplayDepth, ResultLimit ],
    petta_document_limits(
        DFAStates, DFATransitions, ScanWork, ScanTokens,
        WitnessWork, ParseWork, ReplayDepth, ResultLimit)).

native_surface(sym(Value), Value) :- !.
native_surface(int(Value), Value) :- !.
native_surface(str(Value), Value) :- !.
native_surface(list(Values), SurfaceValues) :-
    !,
    maplist(native_surface, Values, SurfaceValues).
native_surface(Value, Value) :-
    atomic(Value), !.
native_surface(Values, [list|SurfaceValues]) :-
    is_list(Values),
    !,
    maplist(native_surface, Values, SurfaceValues).
native_surface(Term, [Functor|SurfaceArguments]) :-
    compound(Term),
    Term =.. [Functor|Arguments],
    maplist(native_surface, Arguments, SurfaceArguments).
