; One state atom rewritten (argv 1) times while older decoded copies stay
; live through the whole loop: a row the run read before the loop and keeps
; until after it, and a generator suspended on its first answer with its
; second still to give.  The epochs holding them stay; the loop's are freed.
!(bind! &st (new-space))
!(bind! &keep (new-space))
!(add-atom &keep (K (kept row (part 1) (part 2) (part 3))))
!(add-atom &keep (G 1 (first payload a b c)))
!(add-atom &keep (G 2 (second payload d e f)))
(= (st-set $k $v)
   (let $_ (collapse (match &st (S $k $old) (remove-atom &st (S $k $old))))
     (add-atom &st (S $k $v))))
(= (tick $n)
   (once (let $_ (st-set "rpc" (R $n "owner" "input" () 0))
           (if (<= $n 0) True False))))
(= (loop $n) (if (tick $n) done (loop (- $n 1))))
(= (gen) (match &keep (G $i $p) $p))
(= (outer $n)
   (let* (($kept (match &keep (K $x) $x)) ($g (gen)) ($_ (loop $n)))
     ($kept $g)))
!(once (outer (argv 1)))
!(match &st (S $k $v) $v)
