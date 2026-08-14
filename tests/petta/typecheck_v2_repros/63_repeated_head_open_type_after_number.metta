; Candidate accumulation is intentionally order-sensitive in typecheck-v2:
; an open type arriving after a concrete Number does not retroactively become
; the singleton Number witness needed by the body.
(: repeated-head-open-second (-> Number $a Number))
(= (repeated-head-open-second $value $value) (+ $value 1))
