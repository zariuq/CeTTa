/* Prolog fixture for tests/petta/libpl_eval_state_release.metta.
 *
 * Each clause builds the expression it hands to eval/2 itself, so MeTTa does
 * not evaluate it before the call, and ends eval/2 one way: pruned by a cut
 * with answers pending, exhausted, failed, abandoned by an exception, raised
 * from inside, or nested in another eval/2. */
'first-of'(N, Result) :-
    numlist(1, N, L),
    once(eval([superpose, L], Result)).
'live-while-pending'(Live) :-
    eval([superpose, [1, 2, 3]], 1),
    eval(['eval-states-live'], Live),
    !.
'all-of'(N, Results) :-
    numlist(1, N, L),
    findall(R, eval([superpose, L], R), Results).
'none-above'(N, Status) :-
    numlist(1, N, L),
    (   \+ (eval([superpose, L], X), X > N)
    ->  Status = failed
    ;   Status = matched
    ).
'first-above'(N, Status) :-
    numlist(1, N, L),
    (   eval([superpose, L], X), X > 1
    ->  Status = found(X)
    ;   Status = none
    ).
'thrown-after-answer'(Status) :-
    catch(( eval([superpose, [1, 2, 3]], _), throw(stop) ),
          stop, Status = caught).
'raised-inside'(Status) :-
    catch(eval([+, 1, not_a_number], _), _, Status = caught).
'nested-first'(Result) :-
    once(eval([superpose, [['first-of', 3], ['first-of', 4]]], Result)).
