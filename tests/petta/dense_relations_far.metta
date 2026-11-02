; Rows of a relation whose numbers lie far apart.  Two rows hold a run of
; small numbers, a filler numbers seventy thousand more, and the rows then
; take numbers past them: one each, a dense run past the first 65,536, and a
; sparse spread across them; then they lose some again, and the filler goes,
; so the numbers are given anew.  Every reading answers alike whether the
; relation is kept as bits: either column fixed, both, counts, removals and
; the whole relation.
(= (fill $a $k $n $step)
   (if (>= $k $n) done
       (let $_ (add-atom &self (r $a $k)) (fill $a (+ $k $step) $n $step))))
(= (drop $a $k $n $step)
   (if (>= $k $n) done
       (let $_ (remove-atom &self (r $a $k)) (drop $a (+ $k $step) $n $step))))
(= (holders $k) (sort-atom (collapse (match &self (r $a $k) $a))))
!(fill x 0 100 1)
!(fill y 0 100 1)
!(fill f 1000 71000 1)
!(add-atom &self (r x 70999))
!(add-atom &self (r y 70999))
!(fill y 67000 70000 1)
!(fill z 1000 71000 997)
!(size-atom (collapse (match &self (r x $b) $b)))
!(size-atom (collapse (match &self (r y $b) $b)))
!(size-atom (collapse (match &self (r z $b) $b)))
!(holders 70999)
!(holders 67001)
!(holders 1997)
!(holders 50)
!(collapse (match &self (r z 69793) yes))
!(collapse (match &self (r z 69796) yes))
; the far numbers go, every other of the dense run, and a third of the spread
!(remove-atom &self (r x 70999))
!(remove-atom &self (r y 70999))
!(drop y 67000 70000 2)
!(drop z 1000 71000 2991)
!(size-atom (collapse (match &self (r x $b) $b)))
!(size-atom (collapse (match &self (r y $b) $b)))
!(size-atom (collapse (match &self (r z $b) $b)))
!(holders 70999)
!(holders 68000)
!(holders 68001)
!(collapse (match &self (r y 69998) yes))
!(collapse (match &self (r y 69999) yes))
!(collapse (match &self (r z 3991) yes))
!(collapse (match &self (r z 2994) yes))
; the filler goes: most numbers die and the rest are numbered anew
!(drop f 1000 71000 1)
!(size-atom (collapse (match &self (r $a $b) $a)))
!(size-atom (collapse (match &self (r y $b) $b)))
!(holders 68001)
!(holders 69999)
!(holders 1997)
!(collapse (match &self (r y 68000) yes))
!(sort-atom (collapse (match &self (r z $b) $b)))
; and the rows take far numbers again
!(fill x 69000 69010 1)
!(size-atom (collapse (match &self (r x $b) $b)))
!(holders 69005)
