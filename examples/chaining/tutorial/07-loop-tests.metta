!(import! &self pat)
(= (seen-variant $goal $ancestors)
   (if (== $ancestors ())
       False
       (let $ancestor (car-atom $ancestors)
         (if (pat:is-variant $goal $ancestor)
             True
             (seen-variant $goal (cdr-atom $ancestors))))))

!(seen-variant (reachable alice $where) ((reachable alice $old)))
!(seen-variant (reachable alice bob) ((reachable alice $old)))
!(seen-variant (pair $x $x) ((pair $a $b)))
!(pat:is-instance (reachable alice bob) (reachable alice $old))

; A more general ancestor covers this goal syntactically. That alone does
; not show that the ancestor's answers have already been computed.
; Pruning recursive calls and completing a table are different algorithms.
