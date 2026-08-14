; Negative mirror: exposing a required-side alias must not turn a union with
; no compatible member into a successful ascription.

(: Target (Alias (| String Bool)))

(: number-value (-> Number))
(= (number-value) 1)

(: use-target (-> Target))
(= (use-target) (the Target (number-value)))
