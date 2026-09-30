% Load the reference src/metta.pl before this file. No CeTTa implementation
% participates in these observations of the reference relations.
:- initialization(type_policy_query_main, main).

type_policy_emit(Name, Goal, Template) :-
    findall(Template, Goal, Answers),
    write_canonical(Name-Answers), nl.

type_policy_guard(Value, Expected) :-
    ('get-type'(Value, Expected) *-> true
     ; 'get-metatype'(Value, Expected)).

type_policy_query_main :-
    type_policy_emit(literal_enumeration, 'get-type'(6, Type), Type),
    type_policy_emit(bound_undefined, 'get-type'(6, '%Undefined%'), yes),
    type_policy_emit(bound_grounded, 'get-type'(6, 'Grounded'), yes),
    type_policy_emit(grounded_guard, type_policy_guard(6, 'Grounded'), yes),
    type_policy_emit(symbol_guard, type_policy_guard(6, 'Symbol'), yes),
    type_policy_emit(unbound_subject, 'get-type'(_, 'Number'), yes),
    setup_call_cleanup(
        assertz('get-type'(6, 'Number'), Ref),
        type_policy_emit(repeated_exact_answers,
                         type_policy_guard(6, 'Number'), yes),
        erase(Ref)),
    type_policy_emit(no_fallback_after_exact_success,
                     (type_policy_guard(6, Expected), Expected = 'Grounded'),
                     yes).
