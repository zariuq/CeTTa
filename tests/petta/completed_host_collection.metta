!(callPredicate (Predicate (quote (consult "tests/petta/fixtures/late_foreign_continuation.pl"))))
!(import_prolog_function cold_mixed_answer)
(= (completion_owned $x)
   (collapse (let $tag (cold_mixed_answer $x) (Pair $tag $x))))
!(collapse
   (let ((Pair first 1) (Pair second $y)) (completion_owned $x)
      (let $y 1 forbidden)))
!(collapse
   (let ((Pair first 1) (Pair second $y)) (completion_owned $x)
      (let $x 1 (let $y 2 (Independent $x $y)))))
(= (completion_owned_bound $x)
   (collapse (let $tag (cold_mixed_answer $x)
      (let $x (superpose (1 2)) (Pair $tag $x)))))
!(completion_owned_bound $x)
