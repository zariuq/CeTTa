(= (relevance-plain $x) (+ $x 1))
(= (relevance-deep $x) deep-ok)

; A first-order call cannot supply a higher-order specialization selector.
!(relevance-plain 41)

; A wide first-order value must fall back after bounded inspection.  Repeating
; the same relation exercises the learned authoritative-matcher route.
!(relevance-deep
  (d a a a a a a a a a a a a a a a a a a a a a a a a a a a a a a a a
     a a a a a a a a a a a a a a a a a a a a a a a a a a a a a a a a
     a a a a a a))
!(relevance-deep
  (d a a a a a a a a a a a a a a a a a a a a a a a a a a a a a a a a
     a a a a a a a a a a a a a a a a a a a a a a a a a a a a a a a a
     a a a a a a))
