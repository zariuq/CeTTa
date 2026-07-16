!(import! &self lts:rho:cost)

(= (test:prefix-status
     (lts:rho:cost:prefix $status $receipt))
   $status)

(= (test:large-cover-state)
   (rho:cost:par
     (rho:cost:signed
       (rho:par
         (rho:recv pay $message
           (rho:cost:signed rho:nil done))
         (rho:send pay
           (rho:cost:signed rho:nil payload)))
       (rho:cost:sig-mul
         coin coin coin coin coin coin coin coin coin coin))
     (rho:cost:purse pay
       (rho:cost:stack-cons coin rho:cost:stack-empty))
     (rho:cost:purse pay
       (rho:cost:stack-cons coin rho:cost:stack-empty))
     (rho:cost:purse pay
       (rho:cost:stack-cons coin rho:cost:stack-empty))
     (rho:cost:purse pay
       (rho:cost:stack-cons coin rho:cost:stack-empty))
     (rho:cost:purse pay
       (rho:cost:stack-cons coin rho:cost:stack-empty))
     (rho:cost:purse pay
       (rho:cost:stack-cons coin rho:cost:stack-empty))
     (rho:cost:purse pay
       (rho:cost:stack-cons coin rho:cost:stack-empty))
     (rho:cost:purse pay
       (rho:cost:stack-cons coin rho:cost:stack-empty))
     (rho:cost:purse pay
       (rho:cost:stack-cons coin rho:cost:stack-empty))
     (rho:cost:purse pay
       (rho:cost:stack-cons coin rho:cost:stack-empty))
     (rho:cost:purse pay
       (rho:cost:stack-cons coin rho:cost:stack-empty))
     (rho:cost:purse pay
       (rho:cost:stack-cons coin rho:cost:stack-empty))
     (rho:cost:purse pay
       (rho:cost:stack-cons coin rho:cost:stack-empty))
     (rho:cost:purse pay
       (rho:cost:stack-cons coin rho:cost:stack-empty))
     (rho:cost:purse pay
       (rho:cost:stack-cons coin rho:cost:stack-empty))
     (rho:cost:purse pay
       (rho:cost:stack-cons coin rho:cost:stack-empty))
     (rho:cost:purse pay
       (rho:cost:stack-cons coin rho:cost:stack-empty))
     (rho:cost:purse pay
       (rho:cost:stack-cons coin rho:cost:stack-empty))
     (rho:cost:purse pay
       (rho:cost:stack-cons coin rho:cost:stack-empty))
     (rho:cost:purse pay
       (rho:cost:stack-cons coin rho:cost:stack-empty))))

; Zero reduction fuel performs no exact-cover search, even when the latent
; cover family has C(20,10) members.
!(assertEqualToResult
  (test:prefix-status
    (lts:rho:cost:causal-prefix 0 0 (test:large-cover-state)))
  (lts:rho:cost:fuel-exhausted))

; The first exact occurrence cover is yielded without materializing the other
; 184,755 covers.
!(assertEqualToResult
  (test:prefix-status
    (lts:rho:cost:causal-prefix 1 12 (test:large-cover-state)))
  (lts:rho:cost:fuel-exhausted))

; A low search allowance on a genuine redex with no purse cover is incomplete,
; never quiescent.
!(assertEqualToResult
  (test:prefix-status
    (lts:rho:cost:causal-prefix 1 1
      (rho:cost:signed
        (rho:par
          (rho:recv pay $message
            (rho:cost:signed rho:nil done))
          (rho:send pay
            (rho:cost:signed rho:nil payload)))
        coin)))
  (lts:rho:cost:search-exhausted))

; The same finite no-cover search becomes quiescent at its exact exhaustive
; allowance: one unit for the redex task and one for the empty cover frame.
!(assertEqualToResult
  (test:prefix-status
    (lts:rho:cost:causal-prefix 1 2
      (rho:cost:signed
        (rho:par
          (rho:recv pay $message
            (rho:cost:signed rho:nil done))
          (rho:send pay
            (rho:cost:signed rho:nil payload)))
        coin)))
  (lts:rho:cost:quiescent))

; The exhaustive frontier retains two concrete purse occurrences when their
; equal heads lead to observably different tails.
!(assertEqualToResult
  (collapse
    (lts:rho:cost:steps
      (rho:cost:par
        (rho:cost:signed
          (rho:par
            (rho:recv pay $message
              (rho:cost:signed rho:nil done))
            (rho:send pay
              (rho:cost:signed rho:nil payload)))
          coin)
        (rho:cost:purse pay
          (rho:cost:stack-cons coin
            (rho:cost:stack-cons left rho:cost:stack-empty)))
        (rho:cost:purse pay
          (rho:cost:stack-cons coin
            (rho:cost:stack-cons right rho:cost:stack-empty))))))
  (((lts:rho:cost:step coin
      (rho:cost:par
        (rho:cost:purse pay
          (rho:cost:stack-cons coin
            (rho:cost:stack-cons left rho:cost:stack-empty)))
        (rho:cost:purse pay
          (rho:cost:stack-cons right rho:cost:stack-empty))
        (rho:cost:signed rho:nil done)))
    (lts:rho:cost:step coin
      (rho:cost:par
        (rho:cost:purse pay
          (rho:cost:stack-cons coin
            (rho:cost:stack-cons right rho:cost:stack-empty)))
        (rho:cost:purse pay
          (rho:cost:stack-cons left rho:cost:stack-empty))
        (rho:cost:signed rho:nil done))))))
