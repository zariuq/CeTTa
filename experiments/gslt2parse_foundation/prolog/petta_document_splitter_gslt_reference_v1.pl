:- module(petta_document_splitter_gslt_reference_v1,
          [ petta_document_splitter_gslt_reference_v1/4
          ]).

:- use_module(finite_horn_eval).
:- use_module(finite_horn_gslt_v1).
:- use_module(library(http/json)).
:- use_module(library(readutil)).
:- use_module(library(utf8)).

:- initialization(main, main).

main :-
    catch(run, Error, (print_message(error, Error), halt(1))),
    halt.

run :-
    current_prolog_flag(argv, Arguments),
    ( Arguments = [PresentationRoot, InputPath, DepthAtom],
      atom_number(DepthAtom, MaxDepth),
      integer(MaxDepth),
      MaxDepth > 0 ->
        true
    ; throw(error(gate_failed(expected_arguments),
                  petta_document_splitter_gslt_reference_v1))
    ),
    petta_document_splitter_gslt_reference_v1(
        PresentationRoot, InputPath, MaxDepth, Row),
    json_write_dict(current_output, Row, [width(0)]),
    nl.

petta_document_splitter_gslt_reference_v1(
    PresentationRoot, InputPath, MaxDepth, Row) :-
    read_input_scalars(InputPath, DecodeOutcome),
    ( DecodeOutcome = valid(Scalars) ->
        splitter_presentations(PresentationRoot, Presentations),
        admit_presentations(Presentations),
        codepoint_list(Scalars, Input),
        app(ref, [sym('petta-document')], Start),
        app(parse, [Start, Input, var(value), var(rest)], Query),
        horn_query(Presentations, Query, MaxDepth, Outcome),
        reference_row(Presentations, Scalars, Outcome, Row)
    ; DecodeOutcome = invalid_utf8,
      Row = _{protocol:"petta-document-splitter-gslt-reference-v1",
              decode:"invalid-utf8",
              input_scalars:0,
              outcome:"invalid-input",
              decision:"rejected",
              semantic_count:0,
              derivation_count:0,
              replay_count:0,
              results:[]}
    ).

splitter_presentations(PresentationRoot, Presentations) :-
    maplist(presentation_path(PresentationRoot),
            ['core/syntax_core_v1.metta',
             'shared/ground_relations_v1.metta',
             'shared/petta_document_splitter_scalar_classes_v1.metta',
             'languages/petta_document_splitter_v1.metta'],
            Paths),
    maplist(read_presentation, Paths, Presentations).

presentation_path(Root, Relative, Path) :-
    directory_file_path(Root, Relative, Path).

read_input_scalars(Path, Outcome) :-
    setup_call_cleanup(
        open(Path, read, Stream, [type(binary)]),
        read_stream_to_codes(Stream, Bytes),
        close(Stream)),
    ( catch(phrase(utf8_codes(Scalars), Bytes), _, fail) ->
        Outcome = valid(Scalars)
    ; Outcome = invalid_utf8
    ).

reference_row(Presentations, Scalars, completed(Answers), Row) :-
    !,
    maplist(replayed_result(Presentations), Answers, RawResults),
    sort(RawResults, Results),
    length(Scalars, ScalarCount),
    length(Answers, DerivationCount),
    length(Results, SemanticCount),
    ( SemanticCount > 0 -> Decision = "accepted"
    ; Decision = "rejected"
    ),
    Row = _{protocol:"petta-document-splitter-gslt-reference-v1",
            decode:"valid-utf8",
            input_scalars:ScalarCount,
            outcome:"completed",
            decision:Decision,
            semantic_count:SemanticCount,
            derivation_count:DerivationCount,
            replay_count:DerivationCount,
            results:Results}.
reference_row(_, Scalars, resource_exhausted(Reason), Row) :-
    length(Scalars, ScalarCount),
    term_string(Reason, ReasonText, [quoted(true)]),
    Row = _{protocol:"petta-document-splitter-gslt-reference-v1",
            decode:"valid-utf8",
            input_scalars:ScalarCount,
            outcome:"resource-exhausted",
            reason:ReasonText,
            decision:"unresolved",
            semantic_count:0,
            derivation_count:0,
            replay_count:0,
            results:[]}.

replayed_result(Presentations, answer(Answer, Proof), ResultText) :-
    horn_replay(Presentations, Answer, Proof),
    Answer = list([sym(parse), _, _, Value, Rest]),
    render_term(list([sym(result), Value, Rest]), ResultText).

codepoint_list([], sym(nil)).
codepoint_list([Value|Values], Term) :-
    app(cp, [int(Value)], Head),
    codepoint_list(Values, Tail),
    app(cons, [Head, Tail], Term).

app(Name, Arguments, list([sym(Name)|Arguments])).
