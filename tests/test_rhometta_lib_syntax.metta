!(import! &self rhometta)

(= (pure:comm)
   (rho:par
     (rho:send (rho:quote rho:nil) rho:nil)
     (rho:recv (rho:quote rho:nil) $x (rho:drop $x))))

(= (eval:data)
   (rho:par
     (rho:send (rho:quote rho:nil) (rhometta:eval (+ 1 2)))
     (rho:recv (rho:quote rho:nil) $x (rho:drop $x))))

(= (eval:proc)
   (rho:par
     (rho:send (rho:quote rho:nil) (rhometta:eval rho:nil))
     (rho:recv (rho:quote rho:nil) $x (rho:drop $x))))

(= (eval:multi)
   (rho:par
     (rho:send (rho:quote rho:nil) (rhometta:eval (superpose (1 2))))
     (rho:recv (rho:quote rho:nil) $x (rho:drop $x))))

(= (eval:zero)
   (rho:par
     (rho:send (rho:quote rho:nil) (rhometta:eval (superpose ())))
     (rho:recv (rho:quote rho:nil) $x (rho:drop $x))))

(= (eval:state-local)
   (rho:par
     (rho:send (rho:quote rho:nil)
       (rhometta:eval
         (let $s (new-state 0)
           (let $_ (change-state! $s 1)
             (get-state $s)))))
     (rho:recv (rho:quote rho:nil) $x (rho:drop $x))))

!(add-atom &self (foo 1))
!(add-atom &self (foo 2))
!(add-atom &self (foo 3))

(= (eval:kb)
   (rho:par
     (rho:send (rho:quote rho:nil)
       (rhometta:eval (match &self (foo $x) (got $x))))
     (rho:recv (rho:quote rho:nil) $x (rho:drop $x))))

(= (fault-peer:program)
   (rho:par
     (rho:send
       (rho:quote (rho:send (rho:quote rho:nil) rho:nil))
       (rhometta:eval (+ 1 "S")))
     (rho:recv
       (rho:quote (rho:send (rho:quote rho:nil) rho:nil))
       $bad
       (rho:drop $bad))
     (rho:send (rho:quote rho:nil) rho:nil)
     (rho:recv (rho:quote rho:nil) $ok
       (rho:send (rho:quote rho:nil) rho:nil))))

!(assertEqualToResult
  (rhometta:run (pure:comm))
  (rho:nil))

!(assertEqual
  (collapse (rhometta:transitions (pure:comm)))
  (collapse (__cetta_lib_rhometta_transitions (pure:comm))))

!(assertEqualToResult
  (rhometta:eval (+ 1 2))
  ((rho:eval-payload (quote (+ 1 2)))))

!(assertEqualToResult
  (rhometta:eval (superpose (1 2)))
  ((rho:eval-payload (quote (superpose (1 2))))))

!(assertEqualToResult
  (rhometta:run (eval:data))
  ((rho:val 3)))

!(assertEqualToResult
  (rhometta:run (eval:proc))
  (rho:nil))

!(assertEqualToResult
  (collapse (rhometta:transitions (eval:multi)))
  (((rho:val 1) (rho:val 2))))

!(assertEqualToResult
  (collapse (rhometta:run (eval:multi)))
  (((rho:val 1) (rho:val 2))))

!(assertEqualToResult
  (collapse (rhometta:run (eval:kb)))
  (((rho:val (got 1)) (rho:val (got 2)) (rho:val (got 3)))))

!(assertAlphaEqualToResult
  (rhometta:run (eval:zero))
  ((rho:par
     (rho:recv (rho:quote rho:nil) $x (rho:drop $x))
     (rho:send (rho:quote rho:nil)
       (rho:eval-payload (quote (superpose ())))))))

!(assertEqualToResult
  (rhometta:run (eval:state-local))
  ((rho:val 1)))

!(assertAlphaEqualToResult
  (collapse (rhometta:transitions (fault-peer:program)))
  (((rho:par
      (rho:recv (rho:quote rho:nil) $ok
        (rho:send (rho:quote rho:nil) rho:nil))
      (rho:send (rho:quote rho:nil) rho:nil)
      (rho:val (Error (+ 1 "S") (BadArgType 2 Number String))))
    (rho:par
      (rho:recv
        (rho:quote (rho:send (rho:quote rho:nil) rho:nil))
        $bad
        (rho:drop $bad))
      (rho:send (rho:quote rho:nil) rho:nil)
      (rho:send
        (rho:quote (rho:send (rho:quote rho:nil) rho:nil))
        (rho:eval-payload (quote (+ 1 "S"))))))))

!(pragma! num-threads 2)

!(assertEqualToResult
  (rhometta:run (pure:comm))
  (rho:nil))

!(assertEqualToResult
  (rhometta:run (eval:data))
  ((rho:val 3)))

!(assertEqualToResult
  (collapse (rhometta:run (eval:multi)))
  (((rho:val 1) (rho:val 2))))

!(assertEqualToResult
  (collapse (rhometta:run (eval:kb)))
  (((rho:val (got 1)) (rho:val (got 2)) (rho:val (got 3)))))

!(assertAlphaEqualToResult
  (rhometta:run (eval:zero))
  ((rho:par
     (rho:recv (rho:quote rho:nil) $x (rho:drop $x))
     (rho:send (rho:quote rho:nil)
       (rho:eval-payload (quote (superpose ())))))))

!(assertEqualToResult
  (rhometta:run (eval:state-local))
  ((rho:val 1)))

!(assertEqualToResult
  (rhometta:run (fault-peer:program))
  ((rho:par
     (rho:send (rho:quote rho:nil) rho:nil)
     (rho:val (Error (+ 1 "S") (BadArgType 2 Number String))))))
