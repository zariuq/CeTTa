:- module(test_lookahead_semantics_v1,
          [ lookahead_semantics_rows/2
          ]).

:- use_module(finite_horn_gslt_v1).
:- use_module(finite_horn_eval).
:- use_module(library(http/json)).

:- initialization(main, main).

main :-
    catch(run, Error, (print_message(error, Error), halt(1))),
    halt.

run :-
    current_prolog_flag(argv, Arguments),
    ( Arguments = [PresentationRoot] -> true
    ; throw(error(gate_failed(expected_presentation_root),
                  test_lookahead_semantics_v1))
    ),
    lookahead_semantics_rows(PresentationRoot, Rows),
    json_write_dict(
        current_output,
        _{protocol:"lookahead-semantics-v1", rows:Rows},
        [width(0)]),
    nl.

lookahead_semantics_rows(PresentationRoot, Rows) :-
    directory_file_path(
        PresentationRoot, 'core/syntax_core_v1.metta', CorePath),
    directory_file_path(
        PresentationRoot, 'shared/lookahead_core_v1.metta', LookaheadPath),
    read_presentation(CorePath, Core),
    read_presentation(LookaheadPath, Lookahead),
    Presentations = [Core, Lookahead],
    admit_presentations(Presentations),
    findall(Row,
            ( lookahead_case(Label, Query, ExpectedCount),
              horn_query(Presentations, Query, 64, Outcome),
              require(Outcome = completed(Answers),
                      completed_lookahead_case(Label)),
              require(forall(member(answer(Answer, Proof), Answers),
                             horn_replay(Presentations, Answer, Proof)),
                      replayed_lookahead_case(Label)),
              findall(Text,
                      ( member(answer(Answer, _), Answers),
                        render_term(Answer, Text)
                      ),
                      RawTerms),
              sort(RawTerms, Terms),
              length(Terms, SemanticCount),
              length(Answers, DerivationCount),
              require(SemanticCount =:= ExpectedCount,
                      expected_lookahead_count(Label)),
              Row = _{label:Label,
                      semantic_count:SemanticCount,
                      derivation_count:DerivationCount,
                      terms:Terms}
            ),
            Rows),
    length(Rows, 8).

lookahead_case(distinct_values, Query, 2) :-
    codepoint_list([97], Input),
    app(eps, [sym('look-left')], Left),
    app(eps, [sym('look-right')], Right),
    app(alt, [Left, Right], Body),
    parse_peek_query(Body, Input, Query).

lookahead_case(distinct_extents, Query, 2) :-
    codepoint_list([97, 98], Input),
    codepoint(97, A),
    codepoint(98, B),
    app(char, [A], Short),
    app(char, [A], First),
    app(char, [B], Second),
    app(seq, [First, Second], Long),
    app(alt, [Short, Long], Body),
    parse_peek_query(Body, Input, Query).

lookahead_case(duplicate_value, Query, 1) :-
    codepoint_list([97], Input),
    app(eps, [sym(same)], Left),
    app(eps, [sym(same)], Right),
    app(alt, [Left, Right], Body),
    parse_peek_query(Body, Input, Query).

lookahead_case(nullable_nonempty, Query, 1) :-
    codepoint_list([97], Input),
    app(eps, [sym(nullable)], Body),
    parse_peek_query(Body, Input, Query).

lookahead_case(eof_empty, Query, 1) :-
    parse_peek_query(sym(eof), sym(nil), Query).

lookahead_case(eof_nonempty, Query, 0) :-
    codepoint_list([955], Input),
    parse_peek_query(sym(eof), Input, Query).

lookahead_case(mismatch, Query, 0) :-
    codepoint_list([97], Input),
    codepoint(98, B),
    app(char, [B], Body),
    parse_peek_query(Body, Input, Query).

lookahead_case(unicode, Query, 1) :-
    codepoint_list([955], Input),
    codepoint(955, Lambda),
    app(char, [Lambda], Body),
    parse_peek_query(Body, Input, Query).

parse_peek_query(Body, Input, Query) :-
    app(peek, [Body], Peek),
    app(parse, [Peek, Input, var(value), var(rest)], Query).

codepoint(Value, Term) :-
    app(cp, [int(Value)], Term).

codepoint_list([], sym(nil)).
codepoint_list([Value|Values], Term) :-
    codepoint(Value, Head),
    codepoint_list(Values, Tail),
    app(cons, [Head, Tail], Term).

app(Name, Arguments, list([sym(Name)|Arguments])).

require(Condition, Label) :-
    ( call(Condition) -> true
    ; throw(error(gate_failed(Label), test_lookahead_semantics_v1))
    ).
