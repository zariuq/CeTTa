(rho:par
  (rho:recv-persistent
    (rho:quote rho:nil)
    $message
    (rho:drop $message))
  (rho:send
    (rho:quote rho:nil)
    (rho:val first))
  (rho:send
    (rho:quote rho:nil)
    (rho:val second)))
