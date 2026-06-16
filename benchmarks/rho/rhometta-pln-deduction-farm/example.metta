; rhometta PLN deduction farm.
; rho orchestrates parallel MeTTa/PLN inference: each COMM rendezvous
; evaluates one L-hop Truth_Deduction chain over the knowledge base.
; One job per channel -> confluent -> exactly one quiescent residual.

!(import! &self rhometta)
!(import! &self lib_pln)

; Knowledge base: link truth values for 4 jobs x 8 hops.
(link-tv 0 1 (stv 0.8 0.9)) (link-tv 0 2 (stv 0.7 0.9))
(link-tv 0 3 (stv 0.8 0.9)) (link-tv 0 4 (stv 0.7 0.9))
(link-tv 0 5 (stv 0.8 0.9)) (link-tv 0 6 (stv 0.7 0.9))
(link-tv 0 7 (stv 0.8 0.9)) (link-tv 0 8 (stv 0.7 0.9))
(link-tv 1 1 (stv 0.9 0.8)) (link-tv 1 2 (stv 0.6 0.8))
(link-tv 1 3 (stv 0.9 0.8)) (link-tv 1 4 (stv 0.6 0.8))
(link-tv 1 5 (stv 0.9 0.8)) (link-tv 1 6 (stv 0.6 0.8))
(link-tv 1 7 (stv 0.9 0.8)) (link-tv 1 8 (stv 0.6 0.8))
(link-tv 2 1 (stv 0.7 0.7)) (link-tv 2 2 (stv 0.7 0.7))
(link-tv 2 3 (stv 0.7 0.7)) (link-tv 2 4 (stv 0.7 0.7))
(link-tv 2 5 (stv 0.7 0.7)) (link-tv 2 6 (stv 0.7 0.7))
(link-tv 2 7 (stv 0.7 0.7)) (link-tv 2 8 (stv 0.7 0.7))
(link-tv 3 1 (stv 0.5 0.9)) (link-tv 3 2 (stv 0.95 0.9))
(link-tv 3 3 (stv 0.5 0.9)) (link-tv 3 4 (stv 0.95 0.9))
(link-tv 3 5 (stv 0.5 0.9)) (link-tv 3 6 (stv 0.95 0.9))
(link-tv 3 7 (stv 0.5 0.9)) (link-tv 3 8 (stv 0.95 0.9))

; Channels are quoted processes; one if-guarded equation keeps minting
; deterministic (two overlapping equations would fork garbage branches).
(= (chan $n)
   (if (== $n 0)
       (rho:quote rho:nil)
       (rho:quote (rho:send (chan (- $n 1)) rho:nil))))

; One deduction job: fold hops L..1 with PLN Truth_Deduction.  Node truth
; values are fixed at (stv 0.5 0.9) so the consistency precondition holds.
(= (link-tv-of $i $k) (match &self (link-tv $i $k $t) $t))
(= (deduce-chain $i $k $acc)
   (if (== $k 0)
       (conclusion (job $i) $acc)
       (deduce-chain $i (- $k 1)
         (Truth_Deduction (stv 0.5 0.9) (stv 0.5 0.9) (stv 0.5 0.9)
                          $acc (link-tv-of $i $k)))))
(= (deduce-job $i $len) (deduce-chain $i (- $len 1) (link-tv-of $i $len)))

; The farm: each cell defers one job to COMM time on its own channel.
(= (cell $i $len)
   (rho:par
     (rho:send (chan $i) (rhometta:eval (deduce-job $i $len)))
     (rho:recv (chan $i) $y (rho:drop $y))))

!("deduction farm: 4 jobs x 8 hops (confluent -> one residual)")
!(collapse (rhometta:run
   (rho:par (cell 0 8) (cell 1 8) (cell 2 8) (cell 3 8))))
