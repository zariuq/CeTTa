; Spec-driven: collapse-bind is deterministic and returns ONE packed
; expression whose items are (<atom> (Bindings ...)) pairs.
(= (multi) a)
(= (multi) b)
!(assertEqualToResult
  (collapse-bind (multi))
  (((a (Bindings () ())) (b (Bindings () ())))))

; Empty branches are internal no-result sentinels and must not be serialized
; into the packed collapse-bind payload.
!(assertEqualToResult
  (collapse-bind (let $x (superpose (a b)) (if (== $x a) (empty) $x)))
  (((b (Bindings (($x b)) ())))))
