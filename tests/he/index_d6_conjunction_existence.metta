; HE's empty-only case over a conjunction observes the conjunction's
; answers, as the enumeration does; a conjunction is no stored atom.
!(bind! &s (new-space))
!(add-atom &s (zr a))
!(add-atom &s (zr b))
!(d6 (collapse (case (collapse (match &s (, (zr a) (zr b)) yes)) ((() none)))))
!(d6-enumerated (collapse (match &s (, (zr a) (zr b)) yes)))
!(d6-open (collapse (case (collapse (match &s (, (zr $x) (zr b)) yes)) ((() none)))))
!(d6-absent (collapse (case (collapse (match &s (, (zr a) (zr c)) yes)) ((() none)))))
