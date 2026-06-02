!(import! &self rho)

(= (private-session $req $reply $out $payload)
   (rho.par
     (rho.recv $req $msg (rho.send $reply (rho.drop $msg)))
     (rho.send $req (rho.send $payload rho.nil))
     (rho.recv $reply $msg (rho.send $out (rho.drop $msg)))))

!(let $program
  (eval (private-session $req0 $reply0 $out $payload))
  $program)
