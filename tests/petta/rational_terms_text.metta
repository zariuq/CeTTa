; Writing a term that is not a finite tree is an error: uncaught, it stops
; the run with status 2, no later query runs and no answer is written.
; SWI-PeTTa's output and status.
!(println! before)
!(let $_ (translatePredicate (= $c (f $c))) (println! $c))
!(println! after)
!(+ 1 2)
