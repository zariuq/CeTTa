!(import! &self rho)

(= (with-private-channel $private $public $payload)
   (rho.par
     (rho.recv $private $msg
       (rho.send $public (rho.drop $msg)))
     (rho.send $private
       (rho.send $payload rho.nil))))

!(let $program
  (eval (with-private-channel $private0 $out $payload))
  $program)
