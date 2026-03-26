; Spec-driven: collapse-bind is deterministic and returns ONE packed
; expression whose items are (<atom> (Bindings ...)) pairs.
(= (multi) a)
(= (multi) b)
!(assertEqualToResult
  (collapse-bind (multi))
  (((a (Bindings () ())) (b (Bindings () ())))))
