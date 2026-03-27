; Spec-driven: collapse-bind packs a ground atom with an empty structural
; bindings payload.
!(assertEqualToResult
  (collapse-bind 1)
  (((1 (Bindings () ())))))
