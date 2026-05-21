!(import! &self rho)

; rho:step must accept MeTTa-built rho processes and names without reducing
; through explicit wrapper helpers, while keeping rho:quote opaque.
(= (mk-proc $payload)
   (rho:par
     (rho:recv (rho:quote rho:nil) $x (rho:drop $x))
     (rho:send (rho:quote rho:nil) $payload)))

(= (mk-name)
   (rho:quote rho:nil))

(= (hot-recv $chan $out)
   (rho.recv $chan $msg (rho.send $out (rho.drop $msg))))

(= (hot-send $chan $payload)
   (rho.send $chan (rho.send $payload rho.nil)))

(= (host-proc)
   rho:nil)

!(assertEqualToResult
  (rho:step (mk-proc rho:nil))
  (rho:nil))

!(assertEqualToResult
  (rho:step
    (rho:par
      (rho:recv (mk-name) $x (rho:drop $x))
      (rho:send (mk-name) rho:nil)))
  (rho:nil))

!(assertEqualToResult
  (case (rho:step
          (rho.par
            (hot-recv $hot $out0)
            (hot-send $hot $p0)))
    (((Error $call $msg) error)
     ($x success)))
  (success))

!(assertEqualToResult
  (case (rho:step (rho:send (rho:quote (host-proc)) rho:nil))
    (((Error $call $msg) error)
     ($x success)))
  (error))

!(assertEqualToResult
  (collapse (rho:step (rho:step (mk-proc rho:nil))))
  (()))
