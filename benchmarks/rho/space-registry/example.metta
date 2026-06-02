!(import! &self rho)

!(bind! &rho-routes (new-space))
!(add-atom &rho-routes (service alpha serviceA))
!(add-atom &rho-routes (service beta serviceB))

(= (rho-name serviceA) $serviceA)
(= (rho-name serviceB) $serviceB)

(= (space-registered-client $key $sink $payload)
   (let $service (match &rho-routes (service $key $service) $service)
     (let $chan (rho-name $service)
       (rho.par
         (rho.recv $chan $msg (rho.send $sink (rho.drop $msg)))
         (rho.send $chan (rho.send $payload rho.nil))))))

!(let $program
  (eval (space-registered-client beta $resolved $payload))
  $program)
