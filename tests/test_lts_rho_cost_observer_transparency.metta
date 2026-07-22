!(import! &self lts:rho:cost)

; Project only the funded residual from an explicitly requested receipt.  This
; fixture has one uniquely funded COMM, so observer-on and observer-off runs
; select the same wave without assuming confluence for arbitrary rho programs.
(= (test:receipt-residual
     (lts:rho:cost:receipt $events $residual))
   $residual)

!(test:receipt-residual
  (lts:rho:cost:causal-trace
    (rho:cost:par
      (rho:cost:signed
        (rho:recv pay $message
          (rho:cost:signed rho:nil cont))
        alice)
      (rho:cost:signed
        (rho:send pay
          (rho:cost:signed rho:nil payload))
        bob)
      (rho:cost:purse pay
        (rho:cost:stack-cons alice rho:cost:stack-empty))
      (rho:cost:purse pay
        (rho:cost:stack-cons bob rho:cost:stack-empty)))))
