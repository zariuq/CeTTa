; Stdlib if regressions for lazy branches and binding flow.
; These are the semantic tripwires that must pass before the C builtin
; handler can be removed.

; Dead branches must remain unevaluated.
!(assertEqual (if True ok (+ "boom" 1)) ok)
!(assertEqual (if False (+ "boom" 1) ok) ok)
!(assertEqual (if True ok (binary-fn 1)) ok)
!(assertEqual (if False (binary-fn 1) ok) ok)

; Bindings produced while evaluating the condition must flow into the
; selected branch.
(= (pair-info) (pair alpha beta))
!(assertEqual
  (if (match &self (= (pair-info) (pair $x $y)) True)
      (chosen $x $y)
      impossible)
  (chosen alpha beta))

!(assertEqual
  (if (unify (pair alpha beta) (pair $x $x) True False)
      impossible
      (fallback))
  (fallback))

; Well-typed symbolic conditions stay inert until they reduce to True/False.
!(assertEqualToResult (if (<= $c 0.9999) 1 2)
  ((if (<= $c 0.9999) 1 2)))
!(assertEqualToResult (if (<= $c 0.9999) (revise 8) (keep))
  ((if (<= $c 0.9999) (revise 8) (keep))))

; Concrete non-Bool conditions must still fail loudly.
!(assertEqualToResult (if 5 1 2)
  ((Error (if 5 1 2) (BadArgType 1 Bool Number))))
!(assertEqualToResult (if "hi" 1 2)
  ((Error (if "hi" 1 2) (BadArgType 1 Bool String))))
