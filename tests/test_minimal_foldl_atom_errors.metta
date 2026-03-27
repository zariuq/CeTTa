; HE-backed helper surface: a non-expression list is rejected with BadArgType.
; This file stops after the first surfaced error, matching the HE CLI runner.

!(_minimal-foldl-atom 1 0 $a $b (eval (+ $a $b)) (context-space))
