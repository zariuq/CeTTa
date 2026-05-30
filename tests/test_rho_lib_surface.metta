!(import! &self rho)

(= (fwd $src $dst)
   (rho.recv $src $msg
     (rho.send $dst (rho.drop $msg))))

!(assertEqualToResult
  rho:nil
  (rho:nil))

!(assertEqualToResult
  (rho:par
    (rho:recv (rho:quote rho:nil) $x (rho:drop $x))
    (rho:send (rho:quote rho:nil) rho:nil))
  ((rho:par
     (rho:recv (rho:quote rho:nil) $x (rho:drop $x))
     (rho:send (rho:quote rho:nil) rho:nil))))

!(assertEqualToResult
  (rho.par
    (rho.recv (rho.quote rho.nil) $x (rho.drop $x))
    (rho.send (rho.quote rho.nil) rho.nil))
  ((rho:par
     (rho:recv (rho:quote rho:nil) $x (rho:drop $x))
     (rho:send (rho:quote rho:nil) rho:nil))))

!(assertAlphaEqualToResult
  (fwd $src $dst)
  ((rho:recv $src $msg
     (rho:send $dst (rho:drop $msg)))))

!(assertAlphaEqualToResult
  (rho.par
    (rho.recv (rho.quote rho.nil) $y
      (rho.recv (rho.quote rho.nil) $x
        (rho.drop $y)))
    (rho.send (rho.quote rho.nil) (rho.drop $x)))
  ((rho:par
     (rho:recv (rho:quote rho:nil) $y
       (rho:recv (rho:quote rho:nil) $x
         (rho:drop $y)))
     (rho:send (rho:quote rho:nil) (rho:drop $x)))))
