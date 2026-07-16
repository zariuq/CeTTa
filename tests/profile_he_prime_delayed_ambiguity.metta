; Completion regression for computed-type uniqueness. TagA is available
; immediately; TagB appears only after enough evaluator work. A prefix with one
; answer is not evidence of uniqueness.

(: Query Type)
(: q Query)
(= (delayed-tag $n)
   (if (== $n 0) TagB (delayed-tag (- $n 1))))
(= (delayed-source $x)
   (superpose (TagA (delayed-tag 600))))
(type-level-function delayed-source)

; The first call observes only TagA before explicit producer exhaustion. The
; second is the positive control in which both distinct answers are observed.
!(normalize-type &self (delayed-source q) 520)
!(normalize-type &self (delayed-source q) 20000)
