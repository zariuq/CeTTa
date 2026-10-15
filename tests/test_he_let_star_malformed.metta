; A let* pair that is not (<pattern> <atom>) is answered with HE's error,
; naming the sequence that remains when the pair is reached; a binding list
; that is not an expression, or is empty, leaves the body.
!(collapse (let* ((a)) 8))
!(collapse (let* (($a 1) (b)) (+ $a 1)))
!(collapse (let* (($a 1) ($b 2 3)) $a))
(= (late-malformed $n) (let* (($a $n) (oops)) $a))
!(collapse (late-malformed 3))
!(collapse (let* ((($a $b) (1 2)) (c d e)) $a))
; Positive cases: no error.
!(collapse (let* x (+ 1 2)))
!(collapse (let* () (+ 1 2)))
!(collapse (let* (($a 1) ($b 2)) (+ $a $b)))
