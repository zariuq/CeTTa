; Reachability among the routers of a 10x10 torus with a chain of 20 routers
; hanging off it (PeTTa, --profile extended), kept by a maintained view
; (lib/petta/lib_support.metta) while links fail and come back: 14,400 routes.
; A torus link failing changes no reachability; a chain link failing cuts the
; routers beyond it off, and restoring it restores their routes.  Every state
; is compared with saturating the remaining links from scratch.  With
; CETTA_SUPPORT_REFERENCE=1 the library's own definitions run, matching every
; directive over the whole view in every round and recomputing the founded
; routes on every retraction.
!(import! &self (library lib_support))
!(import! &self (library lib_exec))

(= (side) 10)
(= (chain) 20)
(= (id $i $j) (+ (* $i (side)) $j))

(= (both $g $a $b)
   (let* (($_x (add-atom $g (link $a $b))) ($_y (add-atom $g (link $b $a)))) ()))

(= (torus-row $g $i $j)
   (if (< $j (side))
       (let* (($_r (both $g (id $i $j) (id $i (% (+ $j 1) (side)))))
              ($_d (both $g (id $i $j) (id (% (+ $i 1) (side)) $j))))
         (torus-row $g $i (+ $j 1)))
       ()))
(= (torus $g $i)
   (if (< $i (side)) (let $_ (torus-row $g $i 0) (torus $g (+ $i 1))) ()))
(= (spur $g $k)
   (if (< $k (chain))
       (let* (($at (+ (* (side) (side)) $k))
              ($from (if (== $k 0) 0 (- $at 1)))
              ($_l (both $g $from $at)))
         (spur $g (+ $k 1)))
       ()))

(= (rules) ((exec hop (, (link $x $y)) (, (reach $x $y)))
            (exec step (, (reach $x $y) (link $y $z)) (, (reach $x $z)))))
(= (route-count) (size-atom (collapse (match &net (reach $x $y) found))))
(= (now) (sort-atom (collapse (match &net (reach $x $y) (reach $x $y)))))
(= (fresh $failed)
   (let* (($s (new-space))
          ($_a (collapse (match &net (link $x $y)
                   (if (is-alpha-member (link $x $y) $failed) () (add-atom $s (link $x $y))))))
          ($ds (rules))
          ($_r (exec:saturate $s $ds)))
     (sort-atom (collapse (match $s (reach $x $y) (reach $x $y))))))

!(bind! &net (new-space))
!(torus &net 0)
!(spur &net 0)
!(let* (($h (support:open &net (rules)))
        ($all (route-count))
        ((removed $t1) (support:retract $h (link 0 1)))
        ((removed $t2) (support:retract $h (link 1 0)))
        ($ok1 (== (now) (fresh ((link 0 1) (link 1 0)))))
        ((removed $c1) (support:retract $h (link 109 110)))
        ((removed $c2) (support:retract $h (link 110 109)))
        ($cut (route-count))
        ($ok2 (== (now) (fresh ((link 0 1) (link 1 0) (link 109 110) (link 110 109)))))
        ((added $a1) (support:assert $h (link 109 110)))
        ((added $a2) (support:assert $h (link 110 109)))
        ((added $a3) (support:assert $h (link 0 1)))
        ((added $a4) (support:assert $h (link 1 0)))
        ($ok3 (== (now) (fresh ()))))
   ((routes $all) (torus-link (size-atom $t1) (size-atom $t2) $ok1)
    (chain-link (size-atom $c1) (size-atom $c2) $cut $ok2)
    (restored (+ (size-atom $a1) (size-atom $a2)) (+ (size-atom $a3) (size-atom $a4)) (route-count) $ok3)))
