'libpl-error-identity'(Value, Value).
'libpl-invalid-arity'(Result) :-
    eval(['libpl-error-identity', one, two, three], Result).
'libpl-caught-arity'(Result) :-
    catch('libpl-invalid-arity'(Result), _, Result = caught).
'libpl-invalid-arithmetic'(Result) :-
    eval([+, 1, not_a_number], Result).
'libpl-caught-arithmetic'(Result) :-
    catch('libpl-invalid-arithmetic'(Result), _, Result = caught).
'libpl-error-data'(Result) :-
    catch(eval([quote, ['Error', ordinary, payload]], Result),
          _, Result = incorrectly_caught).
