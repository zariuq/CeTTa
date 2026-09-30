; py-call takes a call spec, (path args...), whose arguments are values; any
; other value, a function's result included, matches none of its clauses, so
; the call has no answer.
(= (two) 2)
!(py-call (math.floor 2.5))
!(py-call ("math.floor" 2.5))
!(py-call (str (two)))
!(py-call (str (quote (two))))
!(py-call (repr 1))
!(py-call (size-atom (1 2)))
!(py-call 5)
!(py-call ())
!(py-call time.time)
!(let $f (py-call abs) ($f -2))
!(py-call (abs -3))
