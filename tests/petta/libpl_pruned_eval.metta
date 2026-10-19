; A Prolog caller that commits to the first answer of an eval with several
; answers cuts the rest: the eval's state goes with them, every time.
!(import! &self (library lib_import))
!(import_prolog_functions_from_file
    "tests/petta/fixtures/libpl_pruned_eval.pl"
    (eval-first eval-first-caught eval-first-nested))
(= (pruned-loop $n)
   (if (== $n 0) done (let $_ (eval-first) (pruned-loop (- $n 1)))))
!(eval-first)
!(pruned-loop 200)
!(eval-first-caught)
!(eval-first-nested)
