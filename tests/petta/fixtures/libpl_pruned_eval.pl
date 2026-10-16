'eval-first'(Result) :- once(eval([superpose, [1, 2, 3]], Result)).
'eval-first-caught'(Result) :- catch(once(eval([superpose, [4, 5]], Result)), _, fail).
'eval-first-nested'(Result) :- once(eval(['eval-first'], Result)).
