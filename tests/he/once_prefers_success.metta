; once publishes its body's first success; an error answer is published only
; when the body is exhausted with no success, as a collected bag keeps an
; error only when no success is in it.  The body is never run again.
(= (err-then-ok) (superpose ((/ 1 0) 7)))
(= (ok-then-err) (superpose (7 (/ 1 0))))
(= (only-errs) (superpose ((/ 1 0) (/ 2 0))))
(= (two-oks) (superpose (7 8)))
(= (deep) (superpose ((only-errs) (err-then-ok))))
(= (said) (superpose ((let $_ (println! first-tried) (/ 1 0))
                      (let $_ (println! second-tried) 7))))
!(once (err-then-ok))
!(once (ok-then-err))
!(collapse (once (only-errs)))
!(once (two-oks))
!(once (deep))
!(once (once (err-then-ok)))
!(collapse (once (err-then-ok)))
!(once (collapse (err-then-ok)))
!(once (said))
!(select 2 (superpose ((/ 1 0) 7 (/ 2 0) 8 9)))
!(collapse (select 2 (superpose ((/ 1 0) (/ 2 0) (/ 3 0)))))
; controls: a collected bag keeps its errors only without a success
!(collapse (only-errs))
!(collapse (err-then-ok))
