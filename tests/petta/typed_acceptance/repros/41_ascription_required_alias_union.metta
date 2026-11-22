; A required-side alias exposes its union representation to explicit
; narrowing.  One matching member is a sufficient overlap witness.

(: Target (Alias (| String Number)))

(: number-value (-> Number))
(= (number-value) 1)

(: use-target (-> Target))
(= (use-target) (the Target (number-value)))
