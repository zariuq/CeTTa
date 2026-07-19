; Prime keeps object binding and metavariable standardization apart as separate
; mechanisms.  `sealed` freshens free metavariables consistently, while an ABT
; `(Var k)` is canonical object syntax and remains inert.

!(assertEqual
   (let (quote (ABTLetScopeV1 (pair $bound $freshened)))
        (sealed ()
          (quote (ABTLetScopeV1 (pair (Var 0) $free))))
     (pair (== $bound (Var 0)) (== $freshened $free)))
   (pair True False))

; The first argument is an exclusion set: mentioned metavariables retain their
; identity, and every unmentioned metavariable is standardized apart.
!(assertEqual
   (let (quote (pair $kept-result $fresh-result))
        (sealed ($keep) (quote (pair $keep $fresh)))
     (pair (== $kept-result $keep) (== $fresh-result $fresh)))
   (pair True False))

; `sealed` is a producer-side hygiene operation, not checker authority.  It
; neither repairs nor admits an out-of-scope canonical index.
!(assertEqual
   (__cetta_abt_scope_check
     (AbtSignatures) 0 (sealed () (Var -1)))
   False)

!(assertEqualToResult
   (sealed ())
   ((Error (sealed ()) IncorrectNumberOfArguments)))

!(println! (PrimeABTSealedSummary 4 4 0))
