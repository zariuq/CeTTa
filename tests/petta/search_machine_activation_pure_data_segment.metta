(= (pure-box $x $tag)
   (Box (+ $x 1) (Keep $tag)))

; Equal source occurrences remain distinct results.
(= (duplicate-box $x) (Box (+ $x 1) same))
(= (duplicate-box $x) (Box (+ $x 1) same))

; Rule-local aliases must survive the activation boundary.
(= (produce $fs $xs $f $x $b)
   (MkSized (+ (+ $fs $xs) 1) (: (mp $f $x) $b)))
(= (use $fs $xs $f $x $b)
   (let* (((MkSized $s (: $p $t))
            (produce $fs $xs $f $x $b)))
     (Out $s $p $t)))

; A body-local variable remains correlated within its one occurrence.
(= (exist-produce $x)
   (Box (+ $x 1) (Pair $fresh $fresh)))
(= (exist-use $x)
   (let* (((Box $n (Pair $a $b)) (exist-produce $x)))
     (Exist $n $a $b)))

; A nested executable data field is outside this direct-field segment.
(= (nested-box $x)
   (Outer (+ $x 1) (Inner (+ $x 2))))
(= (use-nested-box $x)
   (let* (((Outer $first $second) (nested-box $x)))
     (Nested $first $second)))

; Relational and effectful fields retain the canonical path.
(= (pick) left)
(= (pick) right)
(= (rel-box $tag) (Box (pick) (Keep $tag)))

!(bind! segment-state (new-state 0))
(= (effect-box)
   (Box (change-state! segment-state
          (+ (get-state segment-state) 1))))

!(pure-box 4 tag)
!(duplicate-box 1)
!(use 1 2 ax1 ax2 T)
!(exist-use 2)
!(use-nested-box 2)
!(rel-box tag)
!(effect-box)
!(get-state segment-state)
