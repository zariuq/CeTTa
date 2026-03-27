; Spec-driven: superpose-bind restores atoms from the structural
; (Bindings ...) payload used by collapse-bind.
!(assertEqualToResult
  (superpose-bind ((x (Bindings () ()))))
  (x))

; The restored environment must thread into later tuple elements.
!(assertEqualToResult
  ((superpose-bind ((token (Bindings ((x a)) ())))) $x)
  ((token a)))

(= marker a)

; The round-trip needs a binder because superpose-bind is specified over the
; encoded expression itself, not over an unevaluated (collapse-bind ...) call.
!(assertEqualToResult
  (let $r (collapse-bind ((match &self (= marker $x) $x) $x))
    ((superpose-bind $r) $x))
  (((a a) a)))
