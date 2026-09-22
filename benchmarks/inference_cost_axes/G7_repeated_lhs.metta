; Repeated lhs variable: admission must decline (support is not a unique
; leaf).  Ordinary matching still checks consistency and the guard.
(= (g7 $s $s) (if (< 2 $s) hit (empty)))
!(g7 5 5)
!(g7 5 1)
