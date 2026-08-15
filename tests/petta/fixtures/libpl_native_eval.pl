'eval-once'(Expression, Result) :- once(eval(Expression, Result)).
'eval-caught'(Expression, Result) :-
    catch((once(eval(Expression, Raw)) -> Status = ok(Raw)
                                      ; Status = failed),
          Exception, Status = exception(Exception)),
    Status = ok(Result).
'eval-many'(Result) :- eval([superpose, [20, 22]], Result).
'libpl-double'(Value, Result) :- Result is Value * 2.
'eval-foreign'(Result) :- eval(['libpl-double', 21], Result).
