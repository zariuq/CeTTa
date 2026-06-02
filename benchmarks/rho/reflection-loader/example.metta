!(import! &self rho)

(= (reflection-loader $loader $out $payload)
   (rho.par
     (rho.recv $loader $code (rho.drop $code))
     (rho.send $loader (rho.send $out (rho.send $payload rho.nil)))))

!(let $program
  (eval (reflection-loader $loader $out $payload))
  $program)
