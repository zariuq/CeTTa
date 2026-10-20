; A ready value bound by let stays ready where an equality reads it.  The
; part a quotation pattern takes is ready, and so is anything let binds it
; to; passing it on, including to ==, never runs it as code
; (tests/prime/ready_values.metta).  In the CBPV reading a let-bound value is
; a value, (Bind (Return v) ...), and comparing it reads it once.  The rows
; below state that reading; the draft currently runs the bound code again
; when == receives it through a let, which doubles the answer.
!(import! &self ../../../lib/prime/cbpv.metta)

; The CBPV realization's program names coin, which has no equations here.
(cbpv:def coin () heads)
(cbpv:def coin () tails)
(= (pick) heads)
(= (pick) tails)
(= (bind-part (quote $e)) (let $x $e (if (== $x ()) empty not-empty)))
(= (direct-part (quote $e)) (if (== $e ()) empty not-empty))

; The CBPV reading: one answer.
!(cbpv:elaborated-bag
   (quote (s:let (V x) (s:quote (coin)) (s:case (V x) ((() empty) ((V other) not-empty))))))

; The part, read directly: one answer.
!(collapse (direct-part (quote (pick))))
; Through let: one answer each.
!(collapse (bind-part (quote (pick))))
!(collapse (let (quote $e) (quote (pick)) (if (== $e ()) empty not-empty)))
!(collapse (let $q (quote (pick)) (let (quote $e) $q (if (== $e ()) empty not-empty))))
; Kept as data: the code itself.
!(collapse (let (quote $e) (quote (pick)) (Kept $e)))
; Explicit evaluation runs it.
!(collapse (let (quote $e) (quote (pick)) (eval $e)))
