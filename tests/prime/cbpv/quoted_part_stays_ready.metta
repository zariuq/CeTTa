; The code a quotation pattern takes from its argument is ready data.  The
; parts a pattern takes from a value are ready, and passing a ready value on
; never runs it as code; only an explicit evaluation does
; (tests/prime/ready_values.metta).  In the CBPV reading a pattern variable is
; a value variable, read by (Return (Var n)), so every route hands on the code
; itself.  The rows below state that reading.  Through let and case the draft
; agrees; through an equation head it currently runs the code again when the
; part is passed to another function: (Got 1) and Grounded instead of
; (Got (k 1 2)) and Expression.  The CBPV realization runs on the same
; evaluator, so the code it carries names kk, which has no equations here.
!(import! &self ../../../lib/prime/cbpv.metta)

(cbpv:def kk ((V x) (V y)) (V x))
(cbpv:def via-id ((V x)) (V x))
(cbpv:def pass-id ((quote (V e))) (Got (via-id (V e))))
(= (k $x $y) $x)
(= (via-id $x) $x)
(= (via-meta $x) (get-metatype $x))
(= (pass-id (quote $e)) (Got (via-id $e)))
(= (pass-meta (quote $e)) (via-meta $e))

; The CBPV reading.
!(cbpv:elaborated-bag (quote (pass-id (s:quote (kk 1 2)))))

; Through let and case.
!(let (quote $e) (quote (k 1 2)) (Got (via-id $e)))
!(case (quote (k 1 2)) (((quote $e) (Got (via-id $e)))))
!(let (quote $e) (quote (k 1 2)) (via-meta $e))

; Through an equation head.
!(pass-id (quote (k 1 2)))
!(pass-meta (quote (k 1 2)))

; Explicit evaluation runs the code.
!(let (quote $e) (quote (k 1 2)) (eval $e))
