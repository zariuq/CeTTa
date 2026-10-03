; Bounded collection commits its producer and preserves nested receivers.
!(callPredicate (Predicate (quote (consult "tests/petta/fixtures/late_foreign_continuation.pl"))))
(= (bound_late $x)
   (select 2
      (superpose
         ((Before a)
          (progn (import_prolog_function cold_dif)
             (let $f cold_dif
                (let $r ($f $x)
                   (let $x (superpose (1 2 3)) (Kept $x)))))
          (progn (println! forbidden_bound_tail) (Tail a))))))
!(bound_late $x)
(= (nested_late $x)
   (collapse
      (superpose
         ((Before a)
          (collapse
             (progn (import_prolog_function cold_dif)
                (let $f cold_dif
                   (let $r ($f $x)
                      (let $x (superpose (1 2 2)) (Kept $x))))))
          (Tail a)))))
!(nested_late $x)
