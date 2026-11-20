; A counted join of three patterns meets 2.0 and 2 as one value in HE, as
; the enumeration of the same join does.
!(bind! &s (new-space))
!(add-atom &s (link a 2.0))
!(add-atom &s (right 2 b))
!(add-atom &s (tail b e))
!(d4 (let $r (collapse (match &s (, (link $x $y) (right $y $z) (tail $z $w)) yes)) (size-atom $r)))
!(d4-enumerated (collapse (match &s (, (link $x $y) (right $y $z) (tail $z $w)) ($x $w))))
