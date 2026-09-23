; The middle query has a large finite occurrence stream, whose publication
; exceeds the speculation purse.  A speculative producer may abandon its
; private prefix, but canonical replay must retain every occurrence.  Separate
; small queries check that later invocations can still use the producer after
; an earlier invocation declined.
(= (resource-fan Z) (leaf))
(= (resource-fan (S $n)) (resource-fan $n))
(= (resource-fan (S $n)) (resource-fan $n))
!(resource-fan (S Z))
!(resource-fan (S (S (S (S (S (S (S (S (S (S (S (S (S (S (S Z))))))))))))))))
!(resource-fan (S Z))
