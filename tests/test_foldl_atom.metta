; HE-backed fold surface: the public stdlib wrapper returns the final value,
; while the explicit-space helper returns the internal return-wrapped step/final
; value that HE exposes through _minimal-foldl-atom.

!(foldl-atom () 1 $a $b (eval (+ $a $b)))
!(foldl-atom (1 2 3) 0 $a $b (eval (+ $a $b)))
!(_minimal-foldl-atom () 1 $a $b (eval (+ $a $b)) (context-space))
!(_minimal-foldl-atom (1 2 3) 0 $a $b (eval (+ $a $b)) (context-space))
