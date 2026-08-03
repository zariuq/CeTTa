; PeTTa's ordinary foldl follows SWI's element-first accumulator convention.
(= (nest-step $item $acc)
   (pair $item $acc))

; Positive: order and accumulator threading are both observable.
!(foldl nest-step (a b) z)

; Negative: failure of any relational step prunes the complete fold.
(= (a-only a $acc)
   (kept $acc))
!(foldl a-only (a b) z)
