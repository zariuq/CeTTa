; Loowoz shape: several heads, only mp has an empty else.  A false prune of
; the ax head (because $s is 1) would drop `ax-hit`.
(= (g6 $s (: ax T)) ax-hit)
(= (g6 $s (: (mp $f $x) T))
   (if (< 2 $s) mp-hit (empty)))
!(g6 1 (: ax T))
!(g6 1 (: (mp a b) T))
!(g6 5 (: (mp a b) T))
