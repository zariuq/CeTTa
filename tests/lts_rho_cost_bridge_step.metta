!(import! &self lts:rho:cost)

!(collapse
  (lts:rho:cost:steps
    (rho:cost:par
      (rho:cost:signed
        (rho:par
          (rho:recv pay $m (rho:cost:signed rho:nil cont))
          (rho:send pay (rho:cost:signed rho:nil payload)))
        (rho:cost:sig-mul bob alice))
      (rho:cost:stack-cons alice rho:cost:stack-empty)
      (rho:cost:stack-cons bob rho:cost:stack-empty))))
