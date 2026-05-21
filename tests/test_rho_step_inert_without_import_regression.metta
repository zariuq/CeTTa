!(assertEqualToResult
  (rho:step
    (rho:par
      (rho:recv (rho:quote rho:nil) $x (rho:drop $x))
      (rho:send (rho:quote rho:nil) rho:nil)))
  ((rho:step
    (rho:par
      (rho:recv (rho:quote rho:nil) $x (rho:drop $x))
      (rho:send (rho:quote rho:nil) rho:nil)))))
