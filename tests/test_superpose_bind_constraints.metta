; Residual equality constraints survive capture and restoration. Printing
; observes them without making their domain substitutable syntax.
!(let $r ((token (Bindings () (((pair $x) (pair a))))))
   (collapse-bind (superpose-bind $r)))

; Assignments and residual constraints are both retained.
!(let $r ((token (Bindings (($x a)) (((pair $y) (pair b))))))
   (collapse-bind (superpose-bind $r)))

; A fully ground contradiction contributes no restored answer.
!(assertEqualToResult
  (let $r ((token (Bindings () ((a b))))) (superpose-bind $r))
  ())
