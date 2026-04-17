; HE conformance: function/return
; Tests recursive evaluation loop with return signal

; function unwraps return literally — no further evaluation of the returned value
!(assertEqual (function (return hello)) hello)

; function(return(expr)) returns expr unevaluated
; Use assertEqualToResult to avoid assertEqual evaluating the expected side
!(assertEqualToResult (function (return (+ 1 2))) ((+ 1 2)))

; To get evaluation BEFORE return, use chain inside the function body:
; chain evaluates (+ $x 10) → 15, binds $r = 15, then (return 15) → 15
(= (my-fn $x) (function (chain (+ $x 10) $r (return $r))))
!(assertEqual (my-fn 5) 15)
