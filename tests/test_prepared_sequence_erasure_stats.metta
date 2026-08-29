!(import! &self clist)

; The generated Clist range row licenses representation erasure and fused
; folding.  A user-defined producer has no such row and must take the ordinary
; materialize-then-fold path instead.
(= (prepared-sequence:test-produced)
   (clist:from-list (1 2 3)))

!(assertEqual
  (clist:foldl (clist:range 10) 0 $acc $item (+ $acc $item))
  45)
!(assertEqual
  (clist:foldl
    (prepared-sequence:test-produced)
    0 $acc $item (+ $acc $item))
  6)
