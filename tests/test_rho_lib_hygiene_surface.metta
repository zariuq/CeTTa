!(import! &self rho)

; Default lib/rho printing must preserve distinct rho binder identities.
!(rho:step
  (rho:par
    (rho:recv (rho:quote rho:nil) $y
      (rho:recv (rho:quote rho:nil) $x
        (rho:drop $y)))
    (rho:send (rho:quote rho:nil) (rho:drop $x))))
