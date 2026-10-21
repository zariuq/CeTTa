; A persistent accumulator keeps the preceding value as its tail.  The
; prepared alternating-arena fold declines rather than copying that growing
; spine at every step; the ordinary machine retains its sharing.
!(size-atom
   (foldl-atom
      (1 2 3 4 5 6 7 8 9 10 11 12 13 14 15 16)
      () $acc $item (cons-atom $item $acc)))

; A scalar accumulator remains in the prepared single-result region.
!(foldl-atom (1 2 3) 0 $acc $item (+ $acc $item))

; Replacing a fixed-size expression does not retain the prior state and also
; remains prepared.
!(foldl-atom (1 2 3) (state 0) $acc $item (state $item))
