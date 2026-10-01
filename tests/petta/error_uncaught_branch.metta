; An error in a later branch ends the run; the earlier branch's answer is
; never printed.
!(let $x (superpose (4.0 -1.0 9.0)) (sqrt-math $x))
!(println! after)
