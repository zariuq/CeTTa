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
