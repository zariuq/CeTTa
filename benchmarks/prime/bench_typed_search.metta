; Small repeatable Prime typed-search workload. This reports search behavior;
; it is not a semantic oracle.

(: a0 A)
(: a1 A)
(: a2 A)
(: a3 A)
(: b0 B)
(: b1 B)
(: b2 B)
(: b3 B)
(: make-result (-> A B Result))
(chaining-rule make-result)

!(search-inhabitants &self Result 1 300000 32)
