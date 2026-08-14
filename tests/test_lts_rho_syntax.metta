!(import! &self lts:rho)

(= (core:comm)
   (rho:par
     (rho:send (rho:quote rho:nil) rho:nil)
     (rho:recv (rho:quote rho:nil) $x (rho:drop $x))))

(= (branching:comm)
   (rho:par
     (rho:send (rho:quote rho:nil) rho:nil)
     (rho:send (rho:quote rho:nil)
       (rho:send (rho:quote rho:nil) rho:nil))
     (rho:recv (rho:quote rho:nil) $x
       (rho:drop $x))))

(= (closed-payload-race:program)
   (rho:par
     (rho:send (rho:quote rho:nil) rho:nil)
     (rho:send (rho:quote rho:nil)
       (rho:send (rho:quote rho:nil) rho:nil))
     (rho:recv (rho:quote rho:nil) $msg
       (rho:send
         (rho:quote (rho:send (rho:quote rho:nil) rho:nil))
         (rho:drop $msg)))))

(= (reflection-loader:program)
   (rho:par
     (rho:recv (rho:quote rho:nil) $code (rho:drop $code))
     (rho:send (rho:quote rho:nil)
       (rho:send
         (rho:quote (rho:send (rho:quote rho:nil) rho:nil))
         rho:nil))))

(= (private-session:program)
   (rho:par
     (rho:recv (rho:quote rho:nil) $x
       (rho:send
         (rho:quote (rho:send (rho:quote rho:nil) rho:nil))
         (rho:drop $x)))
     (rho:send (rho:quote rho:nil)
       (rho:send
         (rho:quote (rho:send
           (rho:quote (rho:send (rho:quote rho:nil) rho:nil))
           rho:nil))
         rho:nil))
     (rho:recv
       (rho:quote (rho:send (rho:quote rho:nil) rho:nil))
       $y
       (rho:send
         (rho:quote (rho:send
           (rho:quote (rho:send (rho:quote rho:nil) rho:nil))
           rho:nil))
         (rho:drop $y)))))

(= (two-replica:program)
   (rho:par
     (rho:recv (rho:quote rho:nil) $x
       (rho:send
         (rho:quote (rho:send (rho:quote rho:nil) rho:nil))
         (rho:drop $x)))
     (rho:recv (rho:quote rho:nil) $y
       (rho:send
         (rho:quote (rho:send
           (rho:quote (rho:send (rho:quote rho:nil) rho:nil))
           rho:nil))
         (rho:drop $y)))
     (rho:send (rho:quote rho:nil) rho:nil)
     (rho:send (rho:quote rho:nil)
       (rho:send (rho:quote rho:nil) rho:nil))))

!(assertEqualToResult
  (collapse (lts:rho:transitions (core:comm)))
  ((rho:nil)))

!(assertEqual
  (collapse (lts:rho:transitions (core:comm)))
  (collapse (lts:transitions __cetta_lib_lts_rho_transitions (core:comm))))

!(assertEqualToResult
  (collapse (lts:rho:transitions (branching:comm)))
  (((rho:par
      (rho:send (rho:quote rho:nil) rho:nil)
      (rho:send (rho:quote rho:nil) rho:nil))
    (rho:send (rho:quote rho:nil)
      (rho:send (rho:quote rho:nil) rho:nil)))))

!(assertEqualToResult
  (collapse (lts:rho:transitions (closed-payload-race:program)))
  (((rho:par
      (rho:send (rho:quote rho:nil) rho:nil)
      (rho:send
        (rho:quote (rho:send (rho:quote rho:nil) rho:nil))
        (rho:send (rho:quote rho:nil) rho:nil)))
    (rho:par
      (rho:send (rho:quote rho:nil)
        (rho:send (rho:quote rho:nil) rho:nil))
      (rho:send
        (rho:quote (rho:send (rho:quote rho:nil) rho:nil))
        rho:nil)))))

!(assertEqualToResult
  (collapse (lts:rho:transitions (reflection-loader:program)))
  (((rho:send
      (rho:quote (rho:send (rho:quote rho:nil) rho:nil))
      rho:nil))))

!(assertAlphaEqualToResult
  (collapse (lts:rho:trace-2 (private-session:program)))
  ((((rho:par
       (rho:recv
         (rho:quote (rho:send (rho:quote rho:nil) rho:nil))
         $y
         (rho:send
           (rho:quote (rho:send
             (rho:quote (rho:send (rho:quote rho:nil) rho:nil))
             rho:nil))
           (rho:drop $y)))
       (rho:send
         (rho:quote (rho:send (rho:quote rho:nil) rho:nil))
         (rho:send
           (rho:quote (rho:send
             (rho:quote (rho:send (rho:quote rho:nil) rho:nil))
             rho:nil))
           rho:nil)))
      (rho:send
        (rho:quote (rho:send
          (rho:quote (rho:send (rho:quote rho:nil) rho:nil))
          rho:nil))
        (rho:send
          (rho:quote (rho:send
            (rho:quote (rho:send (rho:quote rho:nil) rho:nil))
            rho:nil))
          rho:nil))))))

!(assertAlphaEqualToResult
  (collapse (lts:rho:transitions (two-replica:program)))
  (((rho:par
      (rho:recv (rho:quote rho:nil) $x
        (rho:send
          (rho:quote (rho:send (rho:quote rho:nil) rho:nil))
          (rho:drop $x)))
      (rho:send (rho:quote rho:nil) rho:nil)
      (rho:send
        (rho:quote (rho:send
          (rho:quote (rho:send (rho:quote rho:nil) rho:nil))
          rho:nil))
        (rho:send (rho:quote rho:nil) rho:nil)))
    (rho:par
      (rho:recv (rho:quote rho:nil) $x
        (rho:send
          (rho:quote (rho:send (rho:quote rho:nil) rho:nil))
          (rho:drop $x)))
      (rho:send (rho:quote rho:nil)
        (rho:send (rho:quote rho:nil) rho:nil))
      (rho:send
        (rho:quote (rho:send
          (rho:quote (rho:send (rho:quote rho:nil) rho:nil))
          rho:nil))
        rho:nil))
    (rho:par
      (rho:recv (rho:quote rho:nil) $y
        (rho:send
          (rho:quote (rho:send
            (rho:quote (rho:send (rho:quote rho:nil) rho:nil))
            rho:nil))
          (rho:drop $y)))
      (rho:send (rho:quote rho:nil) rho:nil)
      (rho:send
        (rho:quote (rho:send (rho:quote rho:nil) rho:nil))
        (rho:send (rho:quote rho:nil) rho:nil)))
    (rho:par
      (rho:recv (rho:quote rho:nil) $y
        (rho:send
          (rho:quote (rho:send
            (rho:quote (rho:send (rho:quote rho:nil) rho:nil))
            rho:nil))
          (rho:drop $y)))
      (rho:send (rho:quote rho:nil)
        (rho:send (rho:quote rho:nil) rho:nil))
      (rho:send
        (rho:quote (rho:send (rho:quote rho:nil) rho:nil))
        rho:nil)))))

!(assertEqualToResult
  (collapse (lts:rho:transitions rho:nil))
  (()))

!(assertEqual
  (collapse (lts:rho:transitions rho:nil))
  (collapse (lts:transitions __cetta_lib_lts_rho_transitions rho:nil)))

!(assertEqualToResult
  (lts:rho:is-normal-form rho:nil)
  (True))

!(assertEqual
  (lts:rho:is-normal-form rho:nil)
  (lts:is-normal-form lts:rho:transitions rho:nil))

!(assertEqualToResult
  (lts:rho:is-normal-form (core:comm))
  (False))

!(assertEqual
  (lts:rho:is-normal-form (core:comm))
  (lts:is-normal-form lts:rho:transitions (core:comm)))

!(assertEqualToResult
  (lts:rho:is-can-step rho:nil)
  (False))

!(assertEqual
  (lts:rho:is-can-step rho:nil)
  (lts:is-can-step lts:rho:transitions rho:nil))

!(assertEqualToResult
  (lts:rho:is-can-step (core:comm))
  (True))

!(assertEqual
  (lts:rho:is-can-step (core:comm))
  (lts:is-can-step lts:rho:transitions (core:comm)))

!(assertEqualToResult
  (collapse (lts:trace-2 lts:rho:transitions (core:comm)))
  (()))

!(assertEqualToResult
  (collapse (lts:rho:trace-2 (core:comm)))
  (()))

!(assertEqual
  (collapse (lts:rho:trace-2 (core:comm)))
  (collapse (lts:trace-2 lts:rho:transitions (core:comm))))

!(assertEqualToResult
  (lts:rho:transitions rho:poop)
  ((Error (lts:rho:transitions rho:poop) "unsupported rho process atom")))

!(assertEqualToResult
  (lts:trace-2 lts:rho:transitions rho:poop)
  ((Error (lts:rho:transitions rho:poop) "unsupported rho process atom")))

!(assertEqualToResult
  (lts:rho:trace-2 rho:poop)
  ((Error (lts:rho:transitions rho:poop) "unsupported rho process atom")))

!(assertEqual
  (lts:rho:trace-2 rho:poop)
  (lts:trace-2 lts:rho:transitions rho:poop))

!(assertEqualToResult
  (lts:rho:is-normal-form rho:poop)
  ((Error (lts:rho:transitions rho:poop) "unsupported rho process atom")))

!(assertEqual
  (lts:rho:is-normal-form rho:poop)
  (lts:is-normal-form lts:rho:transitions rho:poop))

!(assertEqualToResult
  (lts:rho:is-can-step rho:poop)
  ((Error (lts:rho:transitions rho:poop) "unsupported rho process atom")))

!(assertEqual
  (lts:rho:is-can-step rho:poop)
  (lts:is-can-step lts:rho:transitions rho:poop))

!(assertEqualToResult
  (lts:rho:transitions (Error boom msg))
  ((Error boom msg)))

!(assertEqual
  (lts:rho:transitions (Error boom msg))
  (lts:transitions __cetta_lib_lts_rho_transitions (Error boom msg)))

!(assertEqualToResult
  (lts:trace-2 lts:rho:transitions (Error boom msg))
  ((Error boom msg)))

!(assertEqualToResult
  (lts:rho:trace-2 (Error boom msg))
  ((Error boom msg)))

!(assertEqual
  (lts:rho:trace-2 (Error boom msg))
  (lts:trace-2 lts:rho:transitions (Error boom msg)))

!(assertEqualToResult
  (lts:rho:is-normal-form (Error boom msg))
  ((Error boom msg)))

!(assertEqual
  (lts:rho:is-normal-form (Error boom msg))
  (lts:is-normal-form lts:rho:transitions (Error boom msg)))

!(assertEqualToResult
  (lts:rho:is-can-step (Error boom msg))
  ((Error boom msg)))

!(assertEqual
  (lts:rho:is-can-step (Error boom msg))
  (lts:is-can-step lts:rho:transitions (Error boom msg)))

!(assertEqualToResult
  (lts:rho:transitions)
  ((Error (lts:rho:transitions) IncorrectNumberOfArguments)))

!(assertEqualToResult
  (lts:rho:trace-2)
  ((Error (lts:rho:trace-2) IncorrectNumberOfArguments)))

!(assertEqualToResult
  (lts:rho:is-normal-form)
  ((Error (lts:rho:is-normal-form) IncorrectNumberOfArguments)))

!(assertEqualToResult
  (lts:rho:is-can-step)
  ((Error (lts:rho:is-can-step) IncorrectNumberOfArguments)))
