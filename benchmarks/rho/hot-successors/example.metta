!(import! &self rho)

(= (hot-recv $chan $out)
   (rho.recv $chan $msg (rho.send $out (rho.drop $msg))))

(= (hot-send $chan $payload)
   (rho.send $chan (rho.send $payload rho.nil)))

!(let $program
  (eval
    (rho.par
      (hot-recv $hot $out0)
      (hot-recv $hot $out1)
      (hot-send $hot $p0)
      (hot-send $hot $p1)))
  $program)
