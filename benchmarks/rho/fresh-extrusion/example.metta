!(import! &self rho)

(= (extrude-private-name $private $out)
   (rho.send $out (rho.drop $private)))

!(let $program
  (eval (extrude-private-name $private0 $out))
  $program)
