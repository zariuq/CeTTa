; The middle query has a large finite occurrence stream, whose published
; answers exceed the speculation purse.  They are the call's result, not
; speculative scratch, so the producer publishes every occurrence.  Separate
; small queries check that later invocations can still use the producer.
(= (resource-fan Z) (leaf))
(= (resource-fan (S $n)) (resource-fan $n))
(= (resource-fan (S $n)) (resource-fan $n))
!(resource-fan (S Z))
!(resource-fan (S (S (S (S (S (S (S (S (S (S (S (S (S (S (S Z))))))))))))))))
!(resource-fan (S Z))
