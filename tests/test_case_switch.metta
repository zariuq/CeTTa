; HE conformance: case and switch
; Tests pattern-matching control flow

; case: evaluate scrutinee then match
!(assertEqual (case (+ 1 1) ((2 yes) (3 no))) yes)

; case: no match returns Empty
!(assertEqualToResult (case 99 ((1 a) (2 b))) ())

; case: variable pattern binds
!(assertEqual (case hello (($x (tagged $x)))) (tagged hello))

; switch: direct pattern matching (scrutinee already evaluated)
!(assertEqual (switch A ((A found) (B nope))) found)

; switch: no match returns Empty
!(assertEqualToResult (switch Z ((A ok) (B ok))) ())
