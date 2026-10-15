; A specialization selected by a value read through a cons pattern is not a
; position of a call that passes the list flat: (P ()) stays the head here.
(: map-each (-[$e]-> (-[$e]-> $a $b) (List $a) (List $b)))
(= (map-each $f ()) ())
(= (map-each $f (cons $x $xs)) (cons ($f $x) (map-each $f $xs)))
(: Acc (-[det]-> Counts $v Counts))
(: P (-[det]-> (List $v) Counts))
(= (Acc $counts $v) $counts)
(= (mat $term)
   (if (== $term ()) ()
   (if (is-var $term) $term
      (if (is-expr $term)
         (if (== (car-atom $term) |->) (eval $term)
            (cons (mat (car-atom $term))
               (map-each mat (cdr-atom $term))))
         $term))))
!(mat (F (X $a) $a (P ()) Acc -> $c))
!(mat (F (Y $p) $p (P ()) Acc -> $c))
