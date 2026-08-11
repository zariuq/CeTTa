:- module(test_finite_horn_ground_terms,
          [ run_ground_term_gate/2
          ]).

:- use_module(finite_horn_gslt_v1).
:- use_module(finite_horn_eval).

:- initialization(main, main).

main :-
    catch(run, Error, (print_message(error, Error), halt(1))),
    halt.

run :-
    current_prolog_flag(argv, Arguments),
    ( Arguments = [PresentationRoot] -> true
    ; throw(error(gate_failed(expected_presentation_root),
                  test_finite_horn_ground_terms))
    ),
    run_ground_term_gate(PresentationRoot, Passed),
    format('(FiniteHornGroundTermsPeTTaSummary ~d ~d 0)~n',
           [Passed, Passed]).

run_ground_term_gate(PresentationRoot, Passed) :-
    directory_file_path(
        PresentationRoot,
        'canaries/finite_horn_ground_terms_v1.metta', Path),
    read_presentation(Path, Presentation),
    Program = [Presentation],
    admit_presentations(Program),
    string_codes(Control, [0x3bb, 10, 8, 12, 9, 13, 34, 92, 31]),
    string_codes(Nul, [0]),
    exact_query_gate(Program, list([sym('string-fact'), str(Control)])),
    exact_query_gate(Program, list([sym('string-fact'), str(Nul)])),
    exact_query_gate(Program, list([sym('string-fact'), str("same")])),
    exact_query_gate(Program, list([sym('string-fact'), sym(same)])),
    exact_query_gate(Program, list([sym('string-copy'), str("same")])),
    all_string_answers_gate(Program, Control, Nul),
    exact_query_gate(
        Program,
        list([sym('integer-fact'),
              int(123456789012345678901234567890)])),
    exact_query_gate(
        Program,
        list([sym('integer-fact'),
              int(-123456789012345678901234567890)])),
    no_answer_gate(
        Program,
        list([sym('integer-fact'),
              str("123456789012345678901234567890")])),
    Passed = 9.

exact_query_gate(Program, Query) :-
    horn_query(Program, Query, 64, completed([answer(Answer, Proof)])),
    require(Answer == Query, exact_ground_answer(Query)),
    require(horn_replay(Program, Answer, Proof), replay_ground_answer(Query)).

no_answer_gate(Program, Query) :-
    horn_query(Program, Query, 64, completed([])).

all_string_answers_gate(Program, Control, Nul) :-
    Query = list([sym('string-fact'), var(value)]),
    horn_query(Program, Query, 64, completed(Answers)),
    maplist(replay_answer(Program), Answers),
    maplist(answer_term, Answers, AnswerTerms),
    sort(AnswerTerms, SortedAnswers),
    sort([ list([sym('string-fact'), str("same")]),
           list([sym('string-fact'), sym(same)]),
           list([sym('string-fact'), str(Control)]),
           list([sym('string-fact'), str(Nul)])
         ],
         ExpectedAnswers),
    require(SortedAnswers == ExpectedAnswers, complete_string_answer_set).

replay_answer(Program, answer(Answer, Proof)) :-
    require(horn_replay(Program, Answer, Proof), replay_string_answer).

answer_term(answer(Answer, _), Answer).

require(Condition, Label) :-
    ( call(Condition) -> true
    ; throw(error(gate_failed(Label), test_finite_horn_ground_terms))
    ).
