:- module(petta_form_gslt_reference_v1,
          [ petta_form_gslt_reference_v1/4,
            petta_form_value_term_v1/2,
            petta_form_value_classification_v1/2
          ]).

:- use_module(finite_horn_eval).
:- use_module(finite_horn_gslt_v1).
:- use_module(library(http/json)).
:- use_module(library(pairs)).
:- use_module(library(readutil)).
:- use_module(library(utf8)).
:- use_module(library(dcg/basics), [number//1]).

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
                  petta_form_gslt_reference_v1))
    ),
    petta_form_gslt_reference_v1(
        PresentationRoot, InputPath, MaxDepth, Row),
    json_write_dict(current_output, Row, [width(0)]),
    nl.

petta_form_gslt_reference_v1(
    PresentationRoot, InputPath, MaxDepth, Row) :-
    read_input_scalars(InputPath, DecodeOutcome),
    ( DecodeOutcome = valid(Scalars) ->
        petta_form_presentations(PresentationRoot, Presentations),
        admit_presentations(Presentations),
        codepoint_list(Scalars, Input),
        app(ref, [sym('petta-form')], Start),
        app(parse, [Start, Input, var(value), var(rest)], Query),
        horn_query(Presentations, Query, MaxDepth, Outcome),
        reference_row(Presentations, Scalars, Outcome, Row)
    ; DecodeOutcome = invalid_utf8,
      Row = _{protocol:"petta-form-gslt-reference-v1",
              decode:"invalid-utf8",
              input_scalars:0,
              outcome:"invalid-input",
              decision:"rejected",
              semantic_count:0,
              derivation_count:0,
              replay_count:0,
              results:[],
              host_results:[],
              host_classifications:[]}
    ).

petta_form_presentations(PresentationRoot, Presentations) :-
    maplist(presentation_path(PresentationRoot),
            ['core/syntax_core_v1.metta',
             'shared/lookahead_core_v1.metta',
             'shared/ground_relations_v1.metta',
             'shared/petta_form_reader_scalar_classes_v1.metta',
             'languages/petta_form_reader_v1.metta'],
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
    maplist(replayed_host_result(Presentations), Answers, RawHostResults),
    sort(RawHostResults, HostRows),
    pairs_keys_values(HostRows, HostClassifications, HostResults),
    length(Scalars, ScalarCount),
    length(Answers, DerivationCount),
    length(Results, SemanticCount),
    ( SemanticCount > 0 -> Decision = "accepted"
    ; Decision = "rejected"
    ),
    Row = _{protocol:"petta-form-gslt-reference-v1",
            decode:"valid-utf8",
            input_scalars:ScalarCount,
            outcome:"completed",
            decision:Decision,
            semantic_count:SemanticCount,
            derivation_count:DerivationCount,
            replay_count:DerivationCount,
            results:Results,
            host_results:HostResults,
            host_classifications:HostClassifications}.
reference_row(_, Scalars, resource_exhausted(Reason), Row) :-
    length(Scalars, ScalarCount),
    term_string(Reason, ReasonText, [quoted(true)]),
    Row = _{protocol:"petta-form-gslt-reference-v1",
            decode:"valid-utf8",
            input_scalars:ScalarCount,
            outcome:"resource-exhausted",
            reason:ReasonText,
            decision:"unresolved",
            semantic_count:0,
            derivation_count:0,
            replay_count:0,
            results:[],
            host_results:[],
            host_classifications:[]}.

replayed_result(Presentations, answer(Answer, Proof), ResultText) :-
    horn_replay(Presentations, Answer, Proof),
    Answer = list([sym(parse), _, _, Value, Rest]),
    render_term(list([sym(result), Value, Rest]), ResultText).

replayed_host_result(Presentations, answer(Answer, Proof), Class-TermText) :-
    horn_replay(Presentations, Answer, Proof),
    Answer = list([sym(parse), _, _, Value, sym(nil)]),
    petta_form_value_term_v1(Value, Term),
    petta_term_classification(Term, Class),
    canonical_host_term(Term, TermText).

petta_form_value_term_v1(
    list([sym(node), sym(form), Value]), Term) :-
    petta_syntax_term(Value, [], _, Term).

petta_form_value_classification_v1(Value, Class) :-
    petta_form_value_term_v1(Value, Term),
    petta_term_classification(Term, Class).

petta_term_classification(Term, Class) :-
    ( Term = [=, [Function|_], _], atom(Function) -> Class = function
    ; Class = expression
    ).

petta_syntax_term(
    list([sym(node), sym(expression), Values]),
    Environment0, Environment, Terms) :-
    semantic_values(Values, Children),
    petta_syntax_terms(Children, Environment0, Environment, Terms).
petta_syntax_term(
    list([sym(node), sym(variable), Value]),
    Environment0, Environment, Variable) :-
    semantic_codepoints(Value, Codes),
    atom_codes(Name, Codes),
    petta_variable(Name, Environment0, Environment, Variable).
petta_syntax_term(
    list([sym(node), sym(string), Value]),
    Environment, Environment, String) :-
    semantic_string_codepoints(Value, Codes),
    string_codes(String, Codes).
petta_syntax_term(
    list([sym(node), sym('quoted-token'), Value]),
    Environment, Environment, String) :-
    semantic_codepoints(Value, Encoded),
    append(Codes, [0'"], Encoded),
    string_codes(String, Codes).
petta_syntax_term(
    list([sym(node), sym(token), Value]),
    Environment, Environment, Term) :-
    semantic_codepoints(Value, Codes),
    petta_token_term(Codes, Term).
petta_syntax_term(
    list([sym(node), sym('dollar-symbol'), list([sym(cp), int(0'$)])]),
    Environment, Environment, '$').

petta_syntax_terms([], Environment, Environment, []).
petta_syntax_terms(
    [Value|Values], Environment0, Environment, [Term|Terms]) :-
    petta_syntax_term(Value, Environment0, Environment1, Term),
    petta_syntax_terms(Values, Environment1, Environment, Terms).

petta_variable('_', Environment, Environment, _).
petta_variable(Name, Environment, Environment, Variable) :-
    memberchk(Name-Existing, Environment),
    !,
    Variable = Existing.
petta_variable(Name, Environment, [Name-Variable|Environment], Variable).

petta_token_term(Codes, Term) :-
    phrase(number(Term), Codes, []),
    !.
petta_token_term(Codes, Term) :-
    atom_codes(Raw, Codes),
    ( Raw == 'True' -> Term = true
    ; Raw == 'False' -> Term = false
    ; Term = Raw
    ).

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
    list([sym(node), sym(escape), list([sym(cp), int(Escaped)])]),
    [Codepoint]) :-
    unicode_scalar(Escaped),
    petta_escape(Escaped, Codepoint).
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

petta_escape(0'n, 10) :- !.
petta_escape(0't, 9) :- !.
petta_escape(0'r, 13) :- !.
petta_escape(Codepoint, Codepoint).

unicode_scalar(Codepoint) :-
    integer(Codepoint),
    between(0, 0x10ffff, Codepoint),
    \+ between(0xd800, 0xdfff, Codepoint).

canonical_host_term(Term, String) :-
    copy_term(Term, Copy),
    numbervars(Copy, 0, _),
    with_output_to(
        string(String),
        write_term(
            Copy,
            [quoted(true), numbervars(true), ignore_ops(true)] )
    ).

codepoint_list([], sym(nil)).
codepoint_list([Value|Values], Term) :-
    app(cp, [int(Value)], Head),
    codepoint_list(Values, Tail),
    app(cons, [Head, Tail], Term).

app(Name, Arguments, list([sym(Name)|Arguments])).
