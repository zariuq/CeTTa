!(import! &self rho)

; Default lib/rho printing must preserve distinct rho binder identities.
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

; Importing lib/rho must not smuggle evaluator or relation helpers into the
; rho: namespace.
(= (comm-redex)
   (rho.par
     (rho.recv (rho.quote rho.nil) $msg (rho.drop $msg))
     (rho.send (rho.quote rho.nil) rho.nil)))

!(assertAlphaEqualToResult
  (rho:step (comm-redex))
  ((rho:step
     (rho:par
       (rho:recv (rho:quote rho:nil) $msg (rho:drop $msg))
       (rho:send (rho:quote rho:nil) rho:nil)))))

!(assertAlphaEqualToResult
  (rho:frontier (comm-redex))
  ((rho:frontier
     (rho:par
       (rho:recv (rho:quote rho:nil) $msg (rho:drop $msg))
       (rho:send (rho:quote rho:nil) rho:nil)))))

!(assertEqualToResult
  (rho:reduce rho:nil)
  ((rho:reduce rho:nil)))

!(assertEqualToResult
  (rho:eval rho:nil)
  ((rho:eval rho:nil)))

; Dotted rho. reader sugar must not smuggle the same helper names into scope.
!(assertAlphaEqualToResult
  (rho.step (comm-redex))
  ((rho:step
     (rho:par
       (rho:recv (rho:quote rho:nil) $msg (rho:drop $msg))
       (rho:send (rho:quote rho:nil) rho:nil)))))

!(assertAlphaEqualToResult
  (rho.frontier (comm-redex))
  ((rho:frontier
     (rho:par
       (rho:recv (rho:quote rho:nil) $msg (rho:drop $msg))
       (rho:send (rho:quote rho:nil) rho:nil)))))

!(assertEqualToResult
  (rho.reduce rho.nil)
  ((rho:reduce rho:nil)))

!(assertEqualToResult
  (rho.eval rho.nil)
  ((rho:eval rho:nil)))
