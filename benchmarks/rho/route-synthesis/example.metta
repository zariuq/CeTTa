!(import! &self rho)

(= (edge-forwarder (edge $src $dst))
   (rho.recv $src $msg (rho.send $dst (rho.drop $msg))))

(= (edge-signal (edge $src $dst) $payload)
   (rho.send $src (rho.send $payload rho.nil)))

(= (build-forwarders $edges)
   (foldl-atom $edges rho.nil $acc $edge
     (let $head (eval (edge-forwarder $edge))
       (rho.par $acc $head))))

(= (build-signals $edges $payload)
   (foldl-atom $edges rho.nil $acc $edge
     (let $head (eval (edge-signal $edge $payload))
       (rho.par $acc $head))))

(= (route-program $edges $payload)
   (let $forwarders (eval (build-forwarders $edges))
     (let $signals (eval (build-signals $edges $payload))
       (rho.par $forwarders $signals))))

!(let $program
  (eval (route-program
    ((edge $c0 $c1)
     (edge $c1 $c2)
     (edge $c2 $c3))
    $payload))
  $program)
