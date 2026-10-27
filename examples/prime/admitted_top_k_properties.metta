; A reusable program library and proof library inhabit one mathematical scope.
!(import! &math natural_top_k_properties)

!(type:check &math topk-bound
  (-> (b : Nat) (k : Nat) (rs : Rows)
      (id B (leq (len (topk b k rs)) k) tt)))
!(type:check &math topk-sorted
  (-> (b : Nat) (k : Nat) (rs : Rows) (id B (sorted (topk b k rs)) tt)))
!(type:check &math topk-eligible
  (-> (b : Nat) (k : Nat) (rs : Rows) (id B (all-lt b (topk b k rs)) tt)))
; Source indices describe source order. Stability is conditional on the
; input being listed in that order; this premise is visible in the type.
!(type:check &math topk-stable
  (-> (b : Nat) (k : Nat) (rs : Rows)
      (id B (bimp (incr rs) (stable (topk b k rs))) tt)))

(sample (RCons (row Z (S Z) pa)
          (RCons (row (S Z) (S (S Z)) pb)
            (RCons (row (S (S Z)) (S (S Z)) pc) RNil))))
; The conditional theorem is exercised with a true premise and a nonempty
; result containing an equal-score tie, not just on an empty collection.
!(match &self (sample $rows) (eval (incr $rows) (admitted &math)))
!(match &self (sample $rows)
   (type:check &math
     (topk-stable (S (S (S Z))) (S (S Z)) $rows)
     (id B (stable (topk (S (S (S Z))) (S (S Z)) $rows)) tt)))
!(match &self (sample $rows)
   (eval (topk (S (S (S Z))) (S (S Z)) $rows) (admitted &math)))

; The predicates distinguish descending score order, source eligibility,
; and increasing source indices at equal scores.
!(type:check &math (refl tt)
   (id B (sorted (RCons (row Z Z pa) (RCons (row (S Z) (S Z) pb) RNil))) tt))
!(type:check &math (refl tt)
   (id B (all-lt (S Z) (RCons (row (S Z) (S Z) pb) RNil)) tt))
!(type:check &math (refl tt)
   (id B (stable
     (RCons (row (S (S Z)) (S Z) pc) (RCons (row (S Z) (S Z) pb) RNil))) tt))

; Inspecting the actual declaration exposes the condition to other programs.
!(match &math (hol:known topk-stable (define $type $equation)
                definition $evidence $dependencies $revision $signature)
   (DeclaredStability $type))
