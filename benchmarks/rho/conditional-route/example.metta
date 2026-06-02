!(import! &self rho)

(= (route-destination priority)
   $fast)

(= (route-destination bulk)
   $slow)

(= (route-send $class $payload)
   (let $dst (route-destination $class)
     (rho.send $dst (rho.send $payload rho.nil))))

!(let $program
  (eval (route-send priority $payload))
  $program)
