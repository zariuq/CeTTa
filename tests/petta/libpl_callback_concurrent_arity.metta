; Hyperpose workers call a Prolog goal whose MeTTa callback reads the
; program, while other workers decide calls of the imported names, which
; needs the engine's arity of each.  The callback holds the engine and waits
; for the shared-space observation; a decision made inside an observation
; reads the arities the engine published and never waits for the engine, so
; every run ends.
!(import! &self (library lib_import))
!(import_prolog_functions_from_file
    "tests/petta/fixtures/libpl_pruned_eval.pl" (eval-first))
!(import_prolog_functions_from_file
    "tests/petta/fixtures/libpl_native_eval.pl" (libpl-double))
(= (callbacks $n $acc)
   (if (== $n 0) $acc
       (let $v (eval-first) (callbacks (- $n 1) (+ $acc $v)))))
(= (doubles $n $acc)
   (if (== $n 0) $acc
       (let $d (libpl-double $n) (doubles (- $n 1) (+ $acc $d)))))
(= (partials $n)
   (if (== $n 0) partials-done
       (let $p (libpl-double) (partials (- $n 1)))))
!(msort (collapse (hyperpose
    ((callbacks 200 0) (partials 200) (callbacks 200 0) (doubles 200 0)
     (partials 200) (callbacks 200 0)))))
