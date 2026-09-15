!(import! &self ../../../examples/atp/tptp_resolution/unification.metta)

(= (test:source $index)
   (fo-resolution:source-digest "source"))

(= (test:variable $index $name)
   (fo-resolution:term-variable
     (fo-resolution:variable-id
       (fo-resolution:occurrence (test:source $index) $index)
       (fo-resolution:variable-name $name))))

(= (test:symbol $name)
   (fo-resolution:symbol-name
     (fo-resolution:symbol-lower) $name))

(= (test:constant $name)
   (fo-resolution:term-function
     (test:symbol $name) (fo-resolution:terms-nil)))

(= (test:unary $name $argument)
   (fo-resolution:term-function
     (test:symbol $name)
     (fo-resolution:terms-cons
       $argument (fo-resolution:terms-nil))))

(= (test:predicate2 $name $first $second)
   (fo-resolution:atom-predicate
     (test:symbol $name)
     (fo-resolution:terms-cons $first
       (fo-resolution:terms-cons $second
         (fo-resolution:terms-nil)))))

!(bind! &left
   (test:predicate2 "p"
     (test:variable 1 "X")
     (test:unary "f" (test:variable 1 "Y"))))
!(bind! &right
   (test:predicate2 "p"
     (test:unary "f" (test:constant "a"))
     (test:unary "f" (test:constant "b"))))
!(bind! &unifier (tptp-resolution-example:unify:unify-atom () &left &right))

!(assertEqual (tptp-resolution-example:unify:equalized &unifier &left &right) True)

; The occurs check is part of authored MeTTa, not delegated to host unification.
!(assertEqual
   (tptp-resolution-example:unify:unify-term ()
     (test:variable 1 "X")
     (test:unary "f" (test:variable 1 "X")))
   (FOUnify.Fail OccursCheck))

; The same display name in a different source occurrence denotes a different
; object variable and therefore yields a legitimate binding.
!(assertEqual
   (case (tptp-resolution-example:unify:unify-term ()
           (test:variable 1 "X") (test:variable 2 "X"))
     (((FOUnify.OK $bindings) True)
      ($_ False)))
   True)

!(assertEqual
   (tptp-resolution-example:unify:unify-atom ()
     (test:predicate2 "p" (test:constant "a") (test:constant "b"))
     (test:predicate2 "q" (test:constant "a") (test:constant "b")))
   (FOUnify.Fail PredicateMismatch))

!(assertEqual
   (tptp-resolution-example:unify:unify-term ()
     (fo-resolution:term-number
       (fo-resolution:number-rational) "1/2")
     (fo-resolution:term-number
       (fo-resolution:number-rational) "2/4"))
   (FOUnify.Fail NumberMismatch))

; A variable may be bound to an exact number without any arithmetic oracle.
!(assertEqual
   (case (tptp-resolution-example:unify:unify-term ()
           (test:variable 3 "N")
           (fo-resolution:term-number
             (fo-resolution:number-rational) "1/2"))
     (((FOUnify.OK $bindings)
        (== (tptp-resolution-example:unify:apply-term
              $bindings (test:variable 3 "N"))
            (fo-resolution:term-number
              (fo-resolution:number-rational) "1/2")))
      ($_ False)))
   True)

!(println! (FirstOrderResolutionInputUnificationSummary 6 6 0))
