!(import! &self rho)

(= (registry alpha)
   $serviceA)

(= (registry beta)
   $serviceB)

(= (registered-client $key $sink $payload)
   (let $chan (registry $key)
     (rho.par
       (rho.recv $chan $msg (rho.send $sink (rho.drop $msg)))
       (rho.send $chan (rho.send $payload rho.nil)))))

!(let $program
  (eval (registered-client beta $resolved $payload))
  $program)
