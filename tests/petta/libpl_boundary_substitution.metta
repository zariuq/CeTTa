(= (boundary_collect $key)
   (let $goal
      (Predicate (boundary_pair (bound $key) $value))
      (msort
         (collapse
            (let $_ (callPredicate $goal) $value)))))

(= (install_boundary_universal)
   (let $term (Predicate (boundary_universal $any))
      (let $_
         (callPredicate
            (Predicate (assertz $term $ref)))
         $ref)))

(= (boundary_universal_roundtrip)
   (let*
      (($ref (install_boundary_universal))
       ($left
          (let $_
             (callPredicate
                (Predicate (boundary_universal left)))
             left))
       ($right
          (let $_
             (callPredicate
                (Predicate (boundary_universal right)))
             right))
       ($_ (callPredicate (Predicate (erase $ref)))))
      (universal $left $right)))

!(let $predicate
   (Predicate (boundary_pair (bound alpha) first))
   (assertzPredicate $predicate))
!(let $predicate
   (Predicate (boundary_pair (bound alpha) second))
   (assertzPredicate $predicate))
!(let $predicate
   (Predicate (boundary_pair (bound alpha) first))
   (assertzPredicate $predicate))
!(boundary_collect alpha)
!(boundary_collect absent)
!(boundary_universal_roundtrip)
