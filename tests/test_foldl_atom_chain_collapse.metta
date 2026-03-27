; Regression: CeTTa keeps foldl-atom stable when the input list comes from
; either chain-bound or let-bound collapse. Upstream HE currently diverges on
; the chain form, so translators should lower through let(collapse ...) when
; targeting the common benchmark-preserving path.

!(chain (collapse (superpose (1 2 3))) $xs
    (foldl-atom $xs 0 $a $b (eval (+ $a $b))))
!(let $xs (collapse (superpose (1 2 3)))
    (foldl-atom $xs 0 $a $b (eval (+ $a $b))))
