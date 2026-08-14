; A repeated head variable cannot inhabit disjoint declared parameter slots.
; Treating each occurrence independently would silently accept an impossible
; equation head.
(: repeated-head-disjoint (-> Number String Bool))
(= (repeated-head-disjoint $value $value) True)
