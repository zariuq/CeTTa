; A quotation is a value whose content is data.  The part a quotation pattern
; takes from it is a value in turn: passed on, it is never run as code, through
; an equation head as through let and case, and also where the quotation sits
; inside another argument.  An explicit evaluation runs it.

(= (k $x $y) $x)
(= (via-id $x) $x)
(= (via-meta $x) (get-metatype $x))
(= (pass-id (quote $e)) (Got (via-id $e)))
(= (pass-meta (quote $e)) (via-meta $e))
(= (nested (Box (quote $e))) (Got (via-id $e)))
(= (run-part (quote $e)) (eval $e))

; Through let and case.
!(let (quote $e) (quote (k 1 2)) (Got (via-id $e)))
!(case (quote (k 1 2)) (((quote $e) (Got (via-id $e)))))
!(let (quote $e) (quote (k 1 2)) (via-meta $e))

; Through an equation head, and inside another argument.
!(pass-id (quote (k 1 2)))
!(pass-meta (quote (k 1 2)))
!(nested (Box (quote (k 1 2))))

; Explicit evaluation runs the part, and opening runs the quotation.
!(let (quote $e) (quote (k 1 2)) (eval $e))
!(run-part (quote (k 1 2)))
!(unquote (quote (k 1 2)))

; A part with two answers, compared through let and unify: compared once,
; never run, so one answer.
(= (pick) heads)
(= (pick) tails)
!(collapse (let (quote $e) (quote (pick)) (if (== $e ()) empty not-empty)))
!(collapse (let $q (quote (pick)) (let (quote $e) $q (if (== $e ()) empty not-empty))))
!(collapse (unify (quote (pick)) (quote $e) (if (== $e ()) empty not-empty) no))
