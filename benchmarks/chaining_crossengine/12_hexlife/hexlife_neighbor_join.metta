; Portable relational kernel from the HexLife workload.
; This observes adjacent live-cell pairs; it does not claim to perform the
; complete B2/S2 update implemented by hexlife_structural.mm2.

(neighbors ($q $r (S $s)) ($q (S $r) $s))
(neighbors ($q $r (S $s)) ((S $q) $r $s))
(neighbors ($q (S $r) $s) ((S $q) $r $s))
(neighbors ($q (S $r) $s) ($q $r (S $s)))
(neighbors ((S $q) $r $s) ($q (S $r) $s))
(neighbors ((S $q) $r $s) ($q $r (S $s)))

(alive ((S (S Z)) (S Z) (S Z)))
(alive ((S Z) (S (S Z)) (S Z)))
(alive ((S Z) (S (S (S Z))) Z))

!(collapse
   (match &self
     (, (alive $cell) (neighbors $cell $neighbor) (alive $neighbor))
     (adjacent-live $cell $neighbor)))
