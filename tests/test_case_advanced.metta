; Advanced case/switch operations

; Case with arithmetic scrutinee
!(assertEqual (case (+ 2 3) ((5 five) (6 six))) five)

; Case with nested pattern
!(assertEqual (case (pair a b) (((pair $x $y) (got $x $y)))) (got a b))

; Case with variable fallthrough
!(assertEqual (case hello (($x (echo $x)))) (echo hello))

; Switch with multiple branches, first match wins
!(assertEqual (switch X ((X first) (X second) (Y third))) first)

; Nested case
!(assertEqual (case 1 ((1 (case 2 ((2 inner-match)))))) inner-match)
