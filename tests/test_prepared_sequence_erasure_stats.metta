!(import! &self clist)

; A listed representation erasure still needs an admitted producer executor.
; Range delegates to a native producer outside the private VM fragment, and
; a user-defined producer has no erasure row. Both retain ordinary fallback.
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

; Unwrapping an already constructed expression needs no producer execution.
; This is a positive fused-fold control for both frontends, independently of
; the native range implementation or the Atom-only call-return interface.
!(assertEqual
  (clist:foldl
    (clist:from-list (1 2 3))
    0 $acc $item (+ $acc $item))
  6)
