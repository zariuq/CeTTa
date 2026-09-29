; `repra` of a ground value inside an equation the open-equation tier runs:
; the tier renders it as the host renders it, SWI's term_to_atom spelling.
; The gate runs it on both routes; the answers are SWI-PeTTa's.

(= (render $x) (repra $x))
!(render (___ 1 2 3 4 5 6 7 8))
!(render false)
!(render "a \"q\" string")
!(render 3.5)
!(render -7)
!(render (a (b c) ()))
(= (render-cells $x) (repra (cons $x (cons b ()))))
!(render-cells a)
(= (key-of $x) (s (repra $x)))
!(key-of (1 (2 3)))

; Integers at the 64-bit bounds, and symbols of Prolog's graphic characters.
!(render (-5 0 42 9223372036854775807 -9223372036854775808 -1 10 100))
!(render (x + ++ <=> ___ a Ab $ \\ `x ~ .. :- # @ ?))
!(render (x 1.5 -0.0 "str" (nested (1 -2))))
