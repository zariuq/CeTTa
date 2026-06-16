!(import! &self rhometta)
!(import! &self lib_pln)

(link-tv 0 1 (stv 0.8 0.9)) (link-tv 0 2 (stv 0.7 0.9))
(link-tv 1 1 (stv 0.9 0.8)) (link-tv 1 2 (stv 0.6 0.8))

(= (chan $n)
   (if (== $n 0)
       (rho:quote rho:nil)
       (rho:quote (rho:send (chan (- $n 1)) rho:nil))))

(= (link-tv-of $i $k) (match &self (link-tv $i $k $t) $t))
(= (deduce-chain $i $k $acc)
   (if (== $k 0)
       (conclusion (job $i) $acc)
       (deduce-chain $i (- $k 1)
         (Truth_Deduction (stv 0.5 0.9) (stv 0.5 0.9) (stv 0.5 0.9)
                          $acc (link-tv-of $i $k)))))
(= (deduce-job $i $len) (deduce-chain $i (- $len 1) (link-tv-of $i $len)))

(= (cell $i $len)
   (rho:par
     (rho:send (chan $i) (rhometta:eval (deduce-job $i $len)))
     (rho:recv (chan $i) $y (rho:drop $y))))
(= (farm) (rho:par (cell 0 2) (cell 1 2)))

; Confluent farm: the run residual equals the natively-computed conclusion par.
!(assertEqual
  (collapse (rhometta:run (farm)))
  (collapse (rho:par (rho:val (deduce-job 0 2)) (rho:val (deduce-job 1 2)))))

; Guard: deduction consistency held; no job degenerated to the (stv 1 0) fallback.
(= (is-fallback (conclusion $j (stv $s $c))) (and (== $s 1) (== $c 0)))
!(assertEqual (is-fallback (deduce-job 0 2)) False)
!(assertEqual (is-fallback (deduce-job 1 2)) False)

; Threaded re-run preserves the result.
!(pragma! num-threads 2)
!(assertEqual
  (collapse (rhometta:run (farm)))
  (collapse (rho:par (rho:val (deduce-job 0 2)) (rho:val (deduce-job 1 2)))))
