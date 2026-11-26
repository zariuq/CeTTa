; One state atom rewritten (argv 1) times.  Each rewrite reads the stored
; state, a decoded copy of the space's row, removes it and adds the next, so
; the program's live data stays the same whatever the number of rewrites.
!(bind! &st (new-space))
(= (st-set $k $v)
   (let $_ (collapse (match &st (S $k $old) (remove-atom &st (S $k $old))))
     (add-atom &st (S $k $v))))
(= (tick $n)
   (once (let $_ (st-set "rpc" (R $n "owner" "input" () 0))
           (if (<= $n 0) True False))))
(= (loop $n) (if (tick $n) done (loop (- $n 1))))
!(loop (argv 1))
!(match &st (S $k $v) $v)
