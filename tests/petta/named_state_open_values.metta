; Named state over open values: a write keeps a copy whose variables are
; fresh, and every read of the stored value shares them, in an equation's
; body, through a called function and at top level; a collection copies
; them.  The tier answers as the machine does (CETTA_OPEN_EQUATIONS_REFERENCE).
!(bind! shared-state (new-state 0))
(= (two-reads) (let $u (change-state! shared-state (g $z)) (let $a (get-state shared-state) (let $b (get-state shared-state) (let (g 1) $a $b)))))
!(two-reads)
(= (writer-unlinked) (let $u (change-state! shared-state (g $z)) (let $a (get-state shared-state) (let (g 2) $a $z))))
!(writer-unlinked)
(= (read-pair) (let $u (change-state! shared-state (p $x $x)) (let $a (get-state shared-state) (let (p 3 $y) $a (get-state shared-state)))))
!(read-pair)
(= (branches) (let $u (change-state! shared-state (h $w)) (let $v (superpose (4 5)) (let $a (get-state shared-state) (let (h $v) $a (get-state shared-state))))))
!(sort-atom (collapse (branches)))
!(let $u (change-state! shared-state (g $z)) (let $a (get-state shared-state) (let $b (get-state shared-state) (let (g 1) $a $b))))
(= (read-s) (get-state shared-state))
(= (through-call) (let $u (change-state! shared-state (k $q)) (let $a (read-s) (let $b (read-s) (let (k 7) $a $b)))))
!(through-call)
(= (collected) (let $u (change-state! shared-state (m $r)) (let $l (collapse (get-state shared-state)) (let (m 8) (get-state shared-state) $l))))
!(collected)
