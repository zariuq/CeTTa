'callback-complete'(Mode, Result) :-
    ( Mode == data -> eval([quote, ['Error', payload, datum]], Result)
    ; Mode == empty -> findall(X, eval([empty], X), Result)
    ; Mode == duplicates -> findall(X, eval([superpose, [7,7,9]], X), Result)
    ; Mode == raised -> catch(eval([+, 1, bad], _), _, Result = caught)
    ; Mode == caught -> eval([catch, [+, 1, bad]], Result)
    ; Mode == nested -> eval(['callback-complete', duplicates], Result)
    ; Mode == first -> once(eval([superpose, [7,7,9]], Result))
    ).

'callback-limited'(Mode, Result) :-
    format('callback-entered~n', []),
    ( Mode == branch ->
        (eval(['callback-spin'], _) -> format('callback-success~n', [])
                                    ; format('callback-failure~n', []))
    ; Mode == catch ->
        catch(eval(['callback-spin'], _), _, format('callback-caught~n', []))
    ; Mode == partial ->
        findall(X, eval(['callback-repeat'], X), Values),
        format('callback-partial(~w)~n', [Values])
    ; Mode == cleanup ->
        setup_call_cleanup(true, eval(['callback-spin'], _),
                           format('callback-cleanup~n', []))
    ; Mode == cleanup_throw ->
        setup_call_cleanup(true, eval(['callback-spin'], _),
            (format('callback-cleanup~n', []), throw(cleanup_fault)))
    ; Mode == cleanup_reenter ->
        setup_call_cleanup(true, eval(['callback-spin'], _),
            (format('callback-cleanup~n', []), eval(['callback-tick'], _),
             format('callback-cleanup-after~n', [])))
    ; Mode == cleanup_signatures ->
        setup_call_cleanup(true, eval(['callback-spin'], _),
            (format('callback-cleanup~n', []), add_builtin_signatures('&self', _),
             format('callback-cleanup-after~n', [])))
    ; Mode == nested -> eval(['callback-limited', cleanup], _)
    ; Mode == resume ->
        context_module(Module), user:callback_resume_probe(Module,
            [progn, ['println!', 'callback-inner-effect'], ['callback-spin']])
    ; Mode == noyield ->
        context_module(Module), user:callback_noyield_probe(Module, ['callback-spin'])
    ; Mode == guest_yield -> user:callback_guest_yield(_)
    ),
    format('callback-after~n', []), Result = done.

'callback-signatures'(Space, Result) :-
    format('callback-entered~n', []),
    setup_call_cleanup(true, add_builtin_signatures(Space, _),
                       format('callback-cleanup~n', [])),
    format('callback-after~n', []), Result = done.

'callback-injected'(Cause, Result) :-
    user:callback_inject_completion(Cause),
    setup_call_cleanup(true, eval(['callback-tick'], _),
        (format('callback-cleanup~n', []), throw(cleanup_fault))),
    format('callback-after~n', []), Result = done.

callback_unwind_caught(Expression) :-
    catch(eval(Expression, _), _, format('callback-unwind-recovery~n', [])),
    format('callback-unwind-after~n', []).

'callback-source-steps'(Count, Count) :-
    forall(between(1, Count, _),
        (eval(7, 7), format('callback-source-paid~n', []))).
