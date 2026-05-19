!(import! &self rho)

(= (fwd $src $dst)
   (rho.recv $src $msg
     (rho.send $dst (rho.drop $msg))))

!(assertEqualToResult
  (rho:step
    (rho:par
      (rho:recv (rho:quote rho:nil) $x (rho:drop $x))
      (rho:send (rho:quote rho:nil) rho:nil)))
  ((rho:steps rho:nil)))

!(assertEqualToResult
  (rho.step
    (rho.par
      (rho.recv (rho.quote rho.nil) $x (rho.drop $x))
      (rho.send (rho.quote rho.nil) rho.nil)))
  ((rho:steps rho:nil)))

!(assertEqualToResult
  (rho.step
    (rho.send (rho.quote rho.nil) rho.nil))
  ((rho:steps)))

!(assertEqualToResult
  (rho.step
    (rho.par
      (fwd (rho.quote rho.nil)
           (rho.quote (rho.send (rho.quote rho.nil) rho.nil)))
      (rho.send (rho.quote rho.nil) rho.nil)))
  ((rho:steps
     (rho:send
       (rho:quote (rho:send (rho:quote rho:nil) rho:nil))
       rho:nil))))

!(assertEqualToResult
  (case (rho:step (rho:fresh $bad rho:nil))
    (((Error $call $message) ok)))
  (ok))

!(assertEqualToResult
  (case
    (rho:step
      (rho:par
        (rho:recv (rho:quote rho:nil) $y
          (rho:recv (rho:quote rho:nil) $x
            (rho:drop $y)))
        (rho:send (rho:quote rho:nil) (rho:drop $x))))
    (((rho:steps $p)
      (rho:step
        (rho:par
          $p
          (rho:send (rho:quote rho:nil) rho:nil))))))
  ((rho:steps (rho:drop $x))))
