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

!(pragma! num-threads 1)

!(bind! &pure-step-seq (collapse (rhometta:transitions (pure:comm))))
!(bind! &data-step-seq (collapse (rhometta:transitions (eval:data))))
!(bind! &proc-step-seq (collapse (rhometta:transitions (eval:proc))))
!(bind! &multi-step-seq (collapse (rhometta:transitions (eval:multi))))
!(bind! &zero-step-seq (collapse (rhometta:transitions (eval:zero))))
!(bind! &state-local-step-seq (collapse (rhometta:transitions (eval:state-local))))
!(bind! &fault-step-seq (collapse (rhometta:transitions (fault-peer:program))))
!(bind! &kb-run-seq (collapse (rhometta:run (eval:kb))))

!(bind! &pure-run-seq (collapse (rhometta:run (pure:comm))))
!(bind! &data-run-seq (collapse (rhometta:run (eval:data))))
!(bind! &proc-run-seq (collapse (rhometta:run (eval:proc))))
!(bind! &multi-run-seq (collapse (rhometta:run (eval:multi))))
!(bind! &zero-run-seq (collapse (rhometta:run (eval:zero))))
!(bind! &state-local-run-seq (collapse (rhometta:run (eval:state-local))))
!(bind! &fault-run-seq (collapse (rhometta:run (fault-peer:program))))

!(pragma! num-threads 2)

!(bind! &pure-step-threaded (collapse (rhometta:transitions (pure:comm))))
!(bind! &data-step-threaded (collapse (rhometta:transitions (eval:data))))
!(bind! &proc-step-threaded (collapse (rhometta:transitions (eval:proc))))
!(bind! &multi-step-threaded (collapse (rhometta:transitions (eval:multi))))
!(bind! &zero-step-threaded (collapse (rhometta:transitions (eval:zero))))
!(bind! &state-local-step-threaded (collapse (rhometta:transitions (eval:state-local))))
!(bind! &fault-step-threaded (collapse (rhometta:transitions (fault-peer:program))))
!(bind! &kb-run-threaded (collapse (rhometta:run (eval:kb))))

!(bind! &pure-run-threaded (collapse (rhometta:run (pure:comm))))
!(bind! &data-run-threaded (collapse (rhometta:run (eval:data))))
!(bind! &proc-run-threaded (collapse (rhometta:run (eval:proc))))
!(bind! &multi-run-threaded (collapse (rhometta:run (eval:multi))))
!(bind! &zero-run-threaded (collapse (rhometta:run (eval:zero))))
!(bind! &state-local-run-threaded (collapse (rhometta:run (eval:state-local))))
!(bind! &fault-run-threaded (collapse (rhometta:run (fault-peer:program))))

!(assertAlphaEqual &pure-step-seq &pure-step-threaded)
!(assertAlphaEqual &data-step-seq &data-step-threaded)
!(assertAlphaEqual &proc-step-seq &proc-step-threaded)
!(assertAlphaEqual &multi-step-seq &multi-step-threaded)
!(assertAlphaEqual &zero-step-seq &zero-step-threaded)
!(assertAlphaEqual &state-local-step-seq &state-local-step-threaded)
!(assertAlphaEqual &fault-step-seq &fault-step-threaded)

!(assertAlphaEqual &pure-run-seq &pure-run-threaded)
!(assertAlphaEqual &data-run-seq &data-run-threaded)
!(assertAlphaEqual &proc-run-seq &proc-run-threaded)
!(assertAlphaEqual &multi-run-seq &multi-run-threaded)
!(assertAlphaEqual &zero-run-seq &zero-run-threaded)
!(assertAlphaEqual &state-local-run-seq &state-local-run-threaded)
!(assertAlphaEqual &fault-run-seq &fault-run-threaded)
!(assertAlphaEqual &kb-run-seq &kb-run-threaded)
