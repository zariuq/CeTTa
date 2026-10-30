; A relation of two columns of atoms equal only to themselves may be kept
; as bits.  Every way of reading it answers alike whether it is: matches
; with each column fixed, bound by an earlier pattern or free, counts,
; removals of present and absent facts, copies of a fact in a bag, a set
; that holds each fact once, and a listing of the whole space.
!(bind! &bag (new-space))
!(bind! &set (new-space set))
(= (fill $space $k $n)
   (if (>= $k $n) done
       (let* (($k1 (+ $k 1))
              ($k2 (+ $k 2))
              ($j (% (* $k 7) 13))
              ($a (add-atom $space (lt $k $k1)))
              ($b (add-atom $space (lt $k $k2)))
              ($c (add-atom $space (edge $k $j))))
         (fill $space $k1 $n))))
!(fill &bag 0 40)
!(fill &set 0 40)
; a copy of a fact the bag holds already, and the same added to the set
!(add-atom &bag (lt 3 4))
!(add-atom &set (lt 3 4))
!(size-atom (collapse (match &bag (lt $x $y) $x)))
!(size-atom (collapse (match &set (lt $x $y) $x)))
; one column fixed, then the other, then both
!(collapse (match &bag (lt 5 $y) $y))
!(collapse (match &bag (lt $x 7) $x))
!(collapse (match &bag (lt 3 4) yes))
!(collapse (match &bag (lt 4 3) yes))
; a conjunction binding a column from an earlier pattern
!(size-atom (collapse (match &bag (, (edge $k $j) (lt $j $m)) ($k $j $m))))
!(collapse (match &bag (, (lt 10 $y) (lt $y $z)) ($y $z)))
; the same variable in both columns
!(collapse (match &bag (lt $x $x) $x))
; removals: a fact held once, a copy, a fact never held
!(remove-atom &bag (lt 3 4))
!(size-atom (collapse (match &bag (lt 3 4) yes)))
!(remove-atom &bag (lt 3 4))
!(size-atom (collapse (match &bag (lt 3 4) yes)))
!(remove-atom &bag (lt 100 200))
!(remove-atom &set (lt 3 4))
!(size-atom (collapse (match &set (lt 3 4) yes)))
; a variable head reads every relation
!(size-atom (collapse (match &bag ($h 5 $y) ($h $y))))
; the whole space, as a bag
!(size-atom (collapse (get-atoms &bag)))
!(size-atom (collapse (get-atoms &set)))
; a fact with a variable stays a row and still matches
!(add-atom &bag (lt 50 $any))
!(collapse (match &bag (lt 50 $w) $w))
!(size-atom (collapse (match &bag (lt $x $y) $x)))
