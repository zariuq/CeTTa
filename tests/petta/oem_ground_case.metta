(= (ground-case-loop $remaining)
   (if (< $remaining 1) done
     (case (Node a (Leaf 2))
       (((Node $left (Leaf $right))
         (if (== $left a)
             (ground-case-loop (- $remaining 1))
             wrong))
        ($fallback wrong)))))

; Repeated variables keep unification's equality condition.
(= (ground-case-repeat $value)
   (case $value (((Pair $x $x) same) ($other distinct))))
; A refuted literal arm is skipped; the constructor arm still binds directly.
(= (ground-case-tag $value)
   (case $value (((Tag a) wrong) ((Tag $x) $x))))
; A ground logical list uses the same head/suffix view as full unification.
(= (ground-case-list $value)
   (case $value (((cons $head $tail) (HeadTail $head $tail)))))

!(ground-case-loop 1000)
!(ground-case-repeat (Pair a a))
!(ground-case-repeat (Pair a b))
!(ground-case-tag (Tag b))
!(ground-case-list (1 2 3))
