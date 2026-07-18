; Explicitly unlocked capacity workload: eight witnesses for each of two
; premises yield 64 proof-distinct inhabitants.

(: a0 A)
(: a1 A)
(: a2 A)
(: a3 A)
(: a4 A)
(: a5 A)
(: a6 A)
(: a7 A)
(: b0 B)
(: b1 B)
(: b2 B)
(: b3 B)
(: b4 B)
(: b5 B)
(: b6 B)
(: b7 B)
(: make-result (-> A B Result))
(chaining-rule make-result)

!(search-inhabitants &self Result 1 2000000 128)
