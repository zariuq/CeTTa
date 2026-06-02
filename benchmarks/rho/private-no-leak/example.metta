!(import! &self rho)

(= (private-no-leak $private $out $hidden)
   (rho.par
     (rho.recv $private $msg (rho.send $out rho.nil))
     (rho.send $private (rho.send $hidden rho.nil))))

!(let $program
  (eval (private-no-leak $private0 $out $hidden))
  $program)
