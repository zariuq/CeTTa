:- module(he_reader_gslt_reference_v1,
          [ he_reader_gslt_reference_v1/4,
            he_document_value_atoms_v1/2
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
                  he_reader_gslt_reference_v1))
    ),
    he_reader_gslt_reference_v1(
        PresentationRoot, InputPath, MaxDepth, Row),
    json_write_dict(current_output, Row, [width(0)]),
    nl.

he_reader_gslt_reference_v1(
    PresentationRoot, InputPath, MaxDepth, Row) :-
    read_input_scalars(InputPath, DecodeOutcome),
    ( DecodeOutcome = valid(Scalars) ->
        he_presentations(PresentationRoot, Presentations),
        admit_presentations(Presentations),
        codepoint_list(Scalars, Input),
        app(ref, [sym('he-document')], Start),
        app(parse, [Start, Input, var(value), var(rest)], Query),
        horn_query(Presentations, Query, MaxDepth, Outcome),
        reference_row(Presentations, Scalars, Outcome, Row)
    ; DecodeOutcome = invalid_utf8,
      Row = _{protocol:"he-reader-gslt-reference-v1",
              decode:"invalid-utf8",
              input_scalars:0,
              outcome:"invalid-input",
              decision:"rejected",
              semantic_count:0,
              derivation_count:0,
              replay_count:0,
              results:[],
              host_documents:[]}
    ).

he_presentations(PresentationRoot, Presentations) :-
    maplist(presentation_path(PresentationRoot),
            ['core/syntax_core_v1.metta',
             'shared/lookahead_core_v1.metta',
             'shared/ground_relations_v1.metta',
             'shared/he_reader_scalar_classes_v1.metta',
             'languages/he_reader_v1.metta'],
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
    maplist(replayed_host_document(Presentations), Answers, RawHostDocuments),
    sort(RawHostDocuments, HostDocuments),
    length(Scalars, ScalarCount),
    length(Answers, DerivationCount),
    length(Results, SemanticCount),
    ( SemanticCount > 0 -> Decision = "accepted"
    ; Decision = "rejected"
    ),
    Row = _{protocol:"he-reader-gslt-reference-v1",
            decode:"valid-utf8",
            input_scalars:ScalarCount,
            outcome:"completed",
            decision:Decision,
            semantic_count:SemanticCount,
            derivation_count:DerivationCount,
            replay_count:DerivationCount,
            results:Results,
            host_documents:HostDocuments}.
reference_row(_, Scalars, resource_exhausted(Reason), Row) :-
    length(Scalars, ScalarCount),
    term_string(Reason, ReasonText, [quoted(true)]),
    Row = _{protocol:"he-reader-gslt-reference-v1",
            decode:"valid-utf8",
            input_scalars:ScalarCount,
            outcome:"resource-exhausted",
            reason:ReasonText,
            decision:"unresolved",
            semantic_count:0,
            derivation_count:0,
            replay_count:0,
            results:[],
            host_documents:[]}.

replayed_result(Presentations, answer(Answer, Proof), ResultText) :-
    horn_replay(Presentations, Answer, Proof),
    Answer = list([sym(parse), _, _, Value, Rest]),
    render_term(list([sym(result), Value, Rest]), ResultText).

replayed_host_document(Presentations, answer(Answer, Proof), Atoms) :-
    horn_replay(Presentations, Answer, Proof),
    Answer = list([sym(parse), _, _, Value, sym(nil)]),
    he_document_value_atoms_v1(Value, Atoms).

he_document_value_atoms_v1(
    list([sym(node), sym(document), Values]), Atoms) :-
    semantic_values(Values, SyntaxAtoms),
    maplist(he_syntax_atom, SyntaxAtoms, Atoms).

he_syntax_atom(
    list([sym(node), sym(expression), Values]),
    _{kind:"expr", children:Children}) :-
    semantic_values(Values, SyntaxChildren),
    maplist(he_syntax_atom, SyntaxChildren, Children).
he_syntax_atom(
    list([sym(node), sym(word), Value]),
    _{kind:"sym", value:Text}) :-
    semantic_codepoints(Value, Codes),
    string_codes(Text, Codes).
he_syntax_atom(
    list([sym(node), sym(variable), Value]),
    _{kind:"var", value:Text}) :-
    semantic_codepoints(Value, Codes),
    string_codes(Text, Codes).
he_syntax_atom(
    list([sym(node), sym(string), Value]),
    _{kind:"sym", value:Text}) :-
    semantic_string_codepoints(Value, ContentCodes),
    append([0'"|ContentCodes], [0'"], QuotedCodes),
    string_codes(Text, QuotedCodes).

semantic_values(sym(nil), []).
semantic_values(
    list([sym(cons), Value, Values]), [Value|Rest]) :-
    semantic_values(Values, Rest).

semantic_codepoints(list([sym(cp), int(Codepoint)]), [Codepoint]) :-
    unicode_scalar(Codepoint).
semantic_codepoints(sym(nil), []).
semantic_codepoints(
    list([sym(cons), Left, Right]), Codepoints) :-
    semantic_codepoints(Left, LeftCodepoints),
    semantic_codepoints(Right, RightCodepoints),
    append(LeftCodepoints, RightCodepoints, Codepoints).
semantic_codepoints(
    list([sym(pair), Left, Right]), Codepoints) :-
    semantic_codepoints(Left, LeftCodepoints),
    semantic_codepoints(Right, RightCodepoints),
    append(LeftCodepoints, RightCodepoints, Codepoints).

semantic_string_codepoints(list([sym(cp), int(Codepoint)]), [Codepoint]) :-
    unicode_scalar(Codepoint).
semantic_string_codepoints(
    list([sym(node), sym('simple-escape'), Value]), [Codepoint]) :-
    semantic_codepoints(Value, [Escaped]),
    he_simple_escape(Escaped, Codepoint).
semantic_string_codepoints(
    list([sym(node), sym('byte-escape'), Value]), [Codepoint]) :-
    semantic_codepoints(Value, Digits),
    hex_scalar(Digits, Codepoint),
    between(0, 0x7f, Codepoint).
semantic_string_codepoints(
    list([sym(node), sym('unicode-escape'), Value]), [Codepoint]) :-
    semantic_codepoints(Value, Digits),
    hex_scalar(Digits, Codepoint),
    unicode_scalar(Codepoint).
semantic_string_codepoints(sym(nil), []).
semantic_string_codepoints(
    list([sym(cons), Left, Right]), Codepoints) :-
    semantic_string_codepoints(Left, LeftCodepoints),
    semantic_string_codepoints(Right, RightCodepoints),
    append(LeftCodepoints, RightCodepoints, Codepoints).
semantic_string_codepoints(
    list([sym(pair), Left, Right]), Codepoints) :-
    semantic_string_codepoints(Left, LeftCodepoints),
    semantic_string_codepoints(Right, RightCodepoints),
    append(LeftCodepoints, RightCodepoints, Codepoints).

he_simple_escape(0'n, 10) :- !.
he_simple_escape(0'r, 13) :- !.
he_simple_escape(0't, 9) :- !.
he_simple_escape(Codepoint, Codepoint) :-
    memberchk(Codepoint, [0'", 0'', 0'\\]).

hex_scalar(Digits, Value) :-
    Digits = [_|_],
    hex_scalar(Digits, 0, Value).

hex_scalar([], Value, Value).
hex_scalar([Digit|Digits], Accumulator, Value) :-
    hex_digit_value(Digit, DigitValue),
    Next is Accumulator * 16 + DigitValue,
    hex_scalar(Digits, Next, Value).

hex_digit_value(Digit, Value) :-
    between(0'0, 0'9, Digit),
    !,
    Value is Digit - 0'0.
hex_digit_value(Digit, Value) :-
    between(0'a, 0'f, Digit),
    !,
    Value is Digit - 0'a + 10.
hex_digit_value(Digit, Value) :-
    between(0'A, 0'F, Digit),
    Value is Digit - 0'A + 10.

unicode_scalar(Codepoint) :-
    integer(Codepoint),
    between(0, 0x10ffff, Codepoint),
    \+ between(0xd800, 0xdfff, Codepoint).

codepoint_list([], sym(nil)).
codepoint_list([Value|Values], Term) :-
    app(cp, [int(Value)], Head),
    codepoint_list(Values, Tail),
    app(cons, [Head, Tail], Term).

app(Name, Arguments, list([sym(Name)|Arguments])).
