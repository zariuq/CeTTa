; add-reduct must evaluate its payload, preserve nondeterministic results,
; and insert each reduced atom into every destination space branch.
!(bind! &ka (new-space))
!(bind! &kb (new-space))
!(assertEqualToResult (add-reduct (superpose (&ka &kb)) (+ 1 2)) (() ()))
!(assertEqualToResult (match &ka $x $x) (3))
!(assertEqualToResult (match &kb $x $x) (3))
!(bind! &kc (new-space))
!(assertEqualToResult (add-reduct &kc (superpose ((item 1) (item 2)))) (() ()))
!(assertEqualToResult (match &kc (item $x) $x) (1 2))
