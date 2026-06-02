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

; lib/rho is constructor-level vocabulary only. Malformed constructor shapes
; remain inert surface syntax here rather than being validated eagerly.
!(assertEqualToResult
  (rho:send a b c)
  ((rho:send a b c)))

!(assertEqualToResult
  (rho:recv a $x)
  ((rho:recv a $x)))

!(assertEqualToResult
  (rho:drop a b)
  ((rho:drop a b)))

!(assertEqualToResult
  (rho:quote a b)
  ((rho:quote a b)))

; Dotted rho. reader sugar must preserve the same inert malformed constructor
; contract as the canonical rho: spelling.
!(assertEqualToResult
  (rho.send a b c)
  ((rho:send a b c)))

!(assertEqualToResult
  (rho.recv a $x)
  ((rho:recv a $x)))

!(assertEqualToResult
  (rho.drop a b)
  ((rho:drop a b)))

!(assertEqualToResult
  (rho.quote a b)
  ((rho:quote a b)))
