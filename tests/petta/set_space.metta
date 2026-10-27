; A set space holds each atom once, so a rule that stores what a join
; derives stores each derived atom once, as an MM2 exec does.
(edge 1 2)
(edge 2 3)
(edge 1 3)
(edge 3 1)
(edge 2 1)
!(bind! &s (new-space set))
!(bind! &b (new-space))
!(size-atom (collapse (match &self (, (edge $x $y) (edge $y $z)) (add-atom &s (trans $x $z)))))
!(size-atom (collapse (match &self (, (edge $x $y) (edge $y $z)) (add-atom &b (trans $x $z)))))
!(let $held (collapse (match &s (trans $x $z) ($x $z))) (sort-atom $held))
; The set answers what the bag answers with its duplicates removed.
!(let* (($set (collapse (match &s (trans $x $z) ($x $z))))
        ($bag (collapse (match &b (trans $x $z) ($x $z)))))
   (== (sort-atom $set) (sort-atom (unique-atom $bag))))
; Alpha-variants are one atom.
!(add-atom &s (rule $a $a))
!(add-atom &s (rule $b $b))
!(add-atom &s (rule $c $d))
!(size-atom (collapse (match &s (rule $p $q) r)))
