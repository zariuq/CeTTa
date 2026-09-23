; The guard variable is the second argument.  Using the first argument
; (dummy 1) would wrongly take empty and drop `hit`.
(= (g4 $a $s) (if (< 2 $s) hit (empty)))
!(g4 1 5)
