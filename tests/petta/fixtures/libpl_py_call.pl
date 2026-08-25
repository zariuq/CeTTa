libpl_python_string(Value, Result) :-
    'py-call'(['builtins.str', Value], Result).
