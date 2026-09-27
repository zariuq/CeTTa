; Run with a finite fuel.  A query whose evaluation runs out of fuel is
; reported as incomplete in its place, never as an answer list: running out
; is not the absence of answers.  The run goes on with the next query and
; exits with status 1.
(= (loop) (loop))
(= (count $n) (if (== $n 0) done (count (- $n 1))))

; Within the fuel: an answer.
!(count 5)
; Beyond it: incomplete, and so is a collapse that would claim no answers.
!(count 500)
!(collapse (count 500))
!(loop)
!(superpose (early (loop)))
; The run goes on after an incomplete query.
!(+ 1 2)
!(collapse (superpose ()))
