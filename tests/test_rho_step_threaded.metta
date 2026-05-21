!(import! &self rho)

!(pragma! rho-step-threads 1)
!(assertAlphaEqual
  (collapse
    (rho:step
      (rho:par
        (rho:recv (rho:quote rho:nil) $a (rho:drop $a))
        (rho:recv (rho:quote rho:nil) $b (rho:drop $b))
        (rho:send (rho:quote rho:nil) rho:nil)
        (rho:send (rho:quote rho:nil) rho:nil))))
  ((rho:par
     (rho:recv (rho:quote rho:nil) $left (rho:drop $left))
     (rho:send (rho:quote rho:nil) rho:nil))))

!(pragma! rho-step-threads 4)
!(assertAlphaEqual
  (collapse
    (rho:step
      (rho:par
        (rho:recv (rho:quote rho:nil) $a (rho:drop $a))
        (rho:recv (rho:quote rho:nil) $b (rho:drop $b))
        (rho:send (rho:quote rho:nil) rho:nil)
        (rho:send (rho:quote rho:nil) rho:nil))))
  ((rho:par
     (rho:recv (rho:quote rho:nil) $left (rho:drop $left))
     (rho:send (rho:quote rho:nil) rho:nil))))

!(assertAlphaEqual
  (collapse
    (rho:step
      (rho:par
        (rho:recv (rho:quote rho:nil) $y
          (rho:recv (rho:quote rho:nil) $x
            (rho:drop $y)))
        (rho:send (rho:quote rho:nil) (rho:drop $x)))))
  ((rho:recv (rho:quote rho:nil) $fresh
     (rho:drop $x))))
