; Object providers are shared within one call and isolated across choices.
; Root preparation preserves quoted data and ordinary suspensions.
!(import! &self ../../../examples/atp/tptp_resolution/unification.metta)
(= (port:atom $name)
   (fo-resolution:atom-predicate
     (fo-resolution:symbol-name (fo-resolution:symbol-lower) $name)
     (fo-resolution:terms-nil)))
(= (port:bump) (let $_ (println! produced) (port:atom "p")))
(= (port:choose)
   (let $_ (println! chose) (superpose ((port:atom "p") (port:atom "q")))))
(= (port:consume $object)
   (tptp-resolution-example:unify:equalized
     (tptp-resolution-example:unify:unify-atom () $object (port:atom "p"))
     $object (port:atom "p")))
!(assertEqual (port:consume (port:bump)) True)
!(assertEqual (collapse (port:consume (port:choose))) (True False))
!(assertEqual
   (tptp-resolution-example:unify:unify-atom () (quote (port:bump)) (port:atom "p"))
   (FOUnify.Fail AtomConstructorMismatch))
!(assertEqual
   (tptp-resolution-example:unify:unify-atom () (delay (port:bump)) (port:atom "p"))
   (FOUnify.Fail AtomConstructorMismatch))
!(println! (PrimeResolutionDemandSummary 4 4 0))
