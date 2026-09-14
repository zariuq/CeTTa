; Repeated slots exercise both a successful nonlinear equality and an exact
; refutation whose partial writes must be rolled back.
(= (nonlinear-match (Nonlinear $x $x $y $y)) ok)
(= (nonlinear-run $n)
   (if (== $n 0)
       nonlinear-done
       (let $_ (nonlinear-match (Nonlinear a a b b))
         (let $_ (collapse (nonlinear-match (Nonlinear a b c c)))
           (nonlinear-run (- $n 1))))))

!(nonlinear-run 50000)
