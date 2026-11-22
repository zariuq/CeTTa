(: Proof (Newtype Atom))
(: rule-kind-proof-token (-[det]-> Atom Proof))

(= (rule-kind-proof-token $term)
   (brand Proof
      (if (is-expr $term)
         (let $tail1 (cdr-atom $term)
            (if (== $tail1 ())
               $term
               (let $tail2 (cdr-atom $tail1)
                  (if (== $tail2 ())
                     $term
                     (car-atom $tail2)))))
         $term)))

!(rule-kind-proof-token (rule kind proof))
