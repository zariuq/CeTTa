'comparison-in-query'(Result) :-
    eval([present, 1], Before),
    consult('tests/petta/fixtures/native_comparison_redefined.pl'),
    eval([present, 1], After),
    Result = [Before, After].

'comparison-after-callback'(Result) :-
    eval([present, 1], Result),
    consult('tests/petta/fixtures/native_comparison_redefined.pl').

'comparison-before-callback'(Result) :-
    consult('tests/petta/fixtures/native_comparison_redefined.pl'),
    eval([present, 1], Result).
