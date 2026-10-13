; A let pattern over decons that repeats a variable compares the head with
; the tail instead of binding them, so the let can fail.
(: split-again (-[det]-> Expression Bool))
(= (split-again $term)
   (if (and (is-expr $term) (not (== $term ())))
      (let ($first $first) (decons $term) true)
      false))
