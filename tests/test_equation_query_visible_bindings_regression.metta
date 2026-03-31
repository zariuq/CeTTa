; Equation queries must only export bindings that are visible in the caller's
; query. Positive example: concrete sibling calls can both succeed and feed a
; grounded op. Negative example: leaking private equation-local vars like
; `$gene`/`$pw` out of each call, which makes sibling argument merges fail.

(= (leaf-score (pathway $gene $pw)) 1)

!(assertEqual
  (+ (leaf-score (pathway ensg_a same_pathway))
     (leaf-score (pathway ensg_b same_pathway)))
  2)
