!(import! &self lts:rho:cost)
!(pragma! num-threads 4)

(= (test:contended-branch-state)
   (rho:cost:par
     (rho:cost:signed
       (rho:par
         (rho:recv shared $a (rho:cost:signed rho:nil done-a))
         (rho:send shared (rho:cost:signed rho:nil payload-a)))
       spend)
     (rho:cost:signed
       (rho:par
         (rho:recv shared $b (rho:cost:signed rho:nil done-b))
         (rho:send shared (rho:cost:signed rho:nil payload-b)))
       spend)
     (rho:cost:purse shared
       (rho:cost:stack-cons spend rho:cost:stack-empty))))

; A shared purse makes these alternative one-event prefixes. The executor's
; fair wave anchor exposes both branches across runs in one session; neither
; branch is identified as the canonical concurrent result.
!(assertAlphaEqualToResult
  (lts:rho:cost:causal-prefix 1 1000 (test:contended-branch-state))
  ((lts:rho:cost:prefix
    lts:rho:cost:fuel-exhausted
    (lts:rho:cost:receipt
      ((lts:rho:cost:event
         0 () ((lts:rho:cost:funding shared spend)) spend))
      (rho:cost:par
        (rho:cost:purse shared rho:cost:stack-empty)
        (rho:cost:signed rho:nil done-a)
        (rho:cost:signed
          (rho:par
            (rho:recv shared $b (rho:cost:signed rho:nil done-b))
            (rho:send shared (rho:cost:signed rho:nil payload-b)))
          spend))))))

!(assertAlphaEqualToResult
  (lts:rho:cost:causal-prefix 1 1000 (test:contended-branch-state))
  ((lts:rho:cost:prefix
    lts:rho:cost:fuel-exhausted
    (lts:rho:cost:receipt
      ((lts:rho:cost:event
         0 () ((lts:rho:cost:funding shared spend)) spend))
      (rho:cost:par
        (rho:cost:purse shared rho:cost:stack-empty)
        (rho:cost:signed rho:nil done-b)
        (rho:cost:signed
          (rho:par
            (rho:recv shared $a (rho:cost:signed rho:nil done-a))
            (rho:send shared (rho:cost:signed rho:nil payload-a)))
          spend))))))
