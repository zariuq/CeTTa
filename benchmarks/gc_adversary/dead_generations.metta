; Dead-generations adversary: a long branching episode in which every step
; captures one large, immediately-dying sibling.
;
; Shape: the spine walks N steps.  At each step the FIRST clause is a sibling
; that consumes the current accumulator blob and finishes with a tiny answer;
; the SECOND clause drops the old blob and builds a fresh one.  So every
; generation's blob is dead moments after its sibling completes, and only one
; generation is semantically live at any time.
;
; A collector with a fixed or geometric maintenance window holds memory
; independent of total episode length.  A frontier whose capture store retains
; dead-sibling terms until the episode ends holds O(steps x blob).  Backtracking
; implementations (SWI PeTTa) reclaim on choicepoint pop and sail.
;
; Pure Peano constructors only, so a relational frontier controller admits the
; root.

(= (dbl Z) Z)
(= (dbl (S $n)) (S (S (dbl $n))))

; blob-size = 2^8 = 256 cells; steps = 2^12 = 4096.
(= (blob-size) (dbl (dbl (dbl (dbl (dbl (dbl (dbl (dbl (S Z))))))))))
(= (steps) (dbl (dbl (dbl (dbl (dbl (dbl (dbl (dbl (dbl (dbl (dbl (dbl (S Z))))))))))))))

(= (blob Z $t) $t)
(= (blob (S $k) $t) (blob $k (c $t)))

(= (touch $b) hit)

(= (walk (S $n) $acc) (touch $acc))
(= (walk (S $n) $acc) (walk $n (blob (blob-size) nil)))
(= (walk Z $acc) done)

!(walk (steps) (blob (blob-size) nil))
