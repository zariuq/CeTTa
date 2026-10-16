;; The Prolog eval/2 bridge keeps a state while answers are pending. Every way
;; a call can end releases it: a cut with answers pending (SWI then calls the
;; predicate with no arguments, only its context), exhaustion, failure, an
;; exception that abandons the choice point, an error raised inside, and calls
;; nested in another eval/2. The gauge counts states held now.
!(import! &self (library lib_import))
!(import_prolog_functions_from_file
    "tests/petta/fixtures/libpl_eval_state_release.pl"
    (first-of live-while-pending all-of none-above first-above
     thrown-after-answer raised-inside nested-first))
(= (eval-states-live)
   (match (runtime-stats!) (runtime-counter petta-libpl-eval-state-live $n) $n))
(= (cuts $n) (if (== $n 0) done (let $_ (first-of 64) (cuts (- $n 1)))))
;; Two pending states while the inner call reads the gauge: the outer
;; call's, and the reading call's own.
!(held-while-pending (live-while-pending) then (eval-states-live))
!(let $_ (cuts 100) (cut-with-answers-pending (first-of 3) (eval-states-live)))
!(exhausted (all-of 5) (eval-states-live))
!(failed (none-above 5) (eval-states-live))
!(pruned-by-if-then-else (first-above 5) (eval-states-live))
!(exception (thrown-after-answer) (eval-states-live))
!(raised (raised-inside) (eval-states-live))
!(nested (nested-first) (eval-states-live))
