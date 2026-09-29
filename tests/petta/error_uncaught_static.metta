; An uncaught error in a static call ends the run with status 2: the effects
; before it stand, no later query or effect runs, and no answer prints, the
; earlier queries' included.
!(+ 1 2)
!(println! before)
!(sqrt-math -1.0)
!(println! after)
!(+ 5 6)
