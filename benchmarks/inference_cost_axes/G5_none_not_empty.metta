; `none` is not authored empty.  Admission must not swallow it.
(= (g5 $s) (if (< 2 $s) hit none))
!(g5 1)
