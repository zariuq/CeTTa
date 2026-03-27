; HE-backed capture surface:
; - direct application evaluates in the captured context
; - bare capture is a first-class callable value
; - let-bound capture keeps its argument lazy
(= (id2 $x) $x)
!(capture (id2 7))
!(capture quote)
!(capture (quote (+ 1 2)))
!(let $f capture $f)
!(let $f capture ($f (id2 8)))
!(let $f capture ($f (quote (+ 1 2))))
