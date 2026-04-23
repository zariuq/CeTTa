; Runtime Petta adaptation in fresh spaces should fail honestly when helper
; declarations are missing, rather than succeeding and only later going empty.

!(bind! &s (new-space))

!(assertEqualToResult (add-atom &s (= (keep) 42)) (()))
!(assertEqualToResult (collapse (evalc (keep) &s)) ((42)))

!(assertEqualToResult
  (add-atom &s (= (grow $xs) (append (1) $xs)))
  ((Error (add-atom &s (= (grow $xs) (append (1) $xs)))
          PettaRuntimeHelperMissing)))
