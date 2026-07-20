; Same source, bounded observation: Prime returns `kept` without evaluating
; the unused omega.  Eager HE profiles consume their evaluation budget first.
(= (strategy-divergence:ignore $unused) kept)
(= (strategy-divergence:omega) (strategy-divergence:omega))
!(strategy-divergence:ignore (strategy-divergence:omega))
