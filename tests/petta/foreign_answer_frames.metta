; A Prolog answer binds variables of the equation body that made the call.
; Its bindings join the body's own bindings whatever else the body already
; holds there: a term with fresh variables bound by `let`, a variable a
; machine step made, the result a clause reference. The answers are
; SWI-PeTTa's.
(= (a1) (let $term (Predicate (fab1 $any)) (callPredicate (Predicate (assertz $term)))))
(= (a2) (let $term (Predicate (fab2 1)) (callPredicate (Predicate (assertz $term $ref)))))
(= (a3 $any) (let $term (Predicate (fab3 $any)) (callPredicate (Predicate (assertz $term $ref)))))
(= (a4) (callPredicate (Predicate (assertz (Predicate (fab4 $any)) $ref))))
(= (a5) (let $term (Predicate (fab5 $any)) (callPredicate (Predicate (assertz $term $ref)))))
(= (a7) (let $term (Predicate (fab7 $any)) (callPredicate (Predicate (true)))))
(= (a8) (let $term (fab8 $any) (callPredicate (Predicate (assertz (Predicate $term) $ref)))))
(= (roundtrip)
   (let* (($term (Predicate (fab9 $any)))
          ($_ (callPredicate (Predicate (assertz $term $ref))))
          ($_ (callPredicate (Predicate (fab9 left))))
          ($_ (callPredicate (Predicate (fab9 right))))
          ($_ (callPredicate (Predicate (erase $ref)))))
      (erased left right)))
!(a1)
!(a2)
!(a3 $z)
!(a4)
!(a5)
!(a7)
!(a8)
!(roundtrip)
!(collapse (callPredicate (Predicate (fab9 left))))
