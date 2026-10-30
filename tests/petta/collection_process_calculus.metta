;; Processes in a set space (MM2's process calculus bench at 20 + 20): a send
;; meets a receive with the same channel and payload, a parallel composition
;; runs both sides.  The set knows which processes it holds by keys no row
;; holds, so collections inside the rounds must keep those keys, or variants
;; added later are taken for new processes and the space grows.
!(bind! &p (new-space set))
!(add-atom &p (recv (add $ret) ((S $x) $y) (par (send (add (PN $x $y)) ($x $y)) (recv (PN $x $y) $z (send $ret (S $z))))))
!(add-atom &p (recv (add $ret) (Z $y) (send $ret $y)))
!(add-atom &p (send (add result) ((S (S (S (S (S (S (S (S (S (S (S (S (S (S (S (S (S (S (S (S Z)))))))))))))))))))) (S (S (S (S (S (S (S (S (S (S (S (S (S (S (S (S (S (S (S (S Z)))))))))))))))))))))))
(= (communicate) (match &p (, (recv $channel $payload $body) (send $channel $payload)) (add-atom &p $body)))
(= (split) (match &p (par $l $r) (let $_ (add-atom &p $l) (add-atom &p $r))))
(= (rounds $k) (if (== $k 0) done (let* (($c (collapse (communicate))) ($s (collapse (split)))) (rounds (- $k 1)))))
!(rounds 50)
!(size-atom (collapse (match &p $process t)))
