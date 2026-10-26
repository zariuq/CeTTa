; Compute and check one admitted selector; its declarations are a reusable library.
!(import! &math natural_top_k)
!(type:check &math topk-bound
  (-> (before : Nat) (budget : Nat) (rows : Rows)
      (id B (leq (len (topk before budget rows)) budget) tt)))

; Eligibility excludes the future, highest-scored row. Equal scores preserve
; the original order, so pb precedes pc. The input and returned rows are data.
(sample
  (RCons (row Z (S Z) pa)
    (RCons (row (S Z) (S (S Z)) pb)
      (RCons (row (S (S Z)) (S (S Z)) pc)
        (RCons (row (S (S (S Z))) (S (S (S Z))) pd) RNil)))))
!(match &self (sample $rows)
   (eval (topk (S (S (S Z))) (S (S Z)) $rows) (admitted &math)))
!(match &self (sample $rows)
   (eval (topk (S (S (S Z))) Z $rows) (admitted &math)))

; Two occurrences are two rows. No set-like deduplication is implicit.
!(eval (topk (S (S Z)) (S (S Z))
    (RCons (row (S Z) (S Z) pb) (RCons (row (S Z) (S Z) pb) RNil)))
   (admitted &math))

; A bound on the result cannot certify a bound on the untruncated input.
!(match &self (sample $rows)
   (type:check &math (topk-bound (S (S (S Z))) (S Z) $rows)
     (id B (leq (len $rows) (S Z)) tt)))
