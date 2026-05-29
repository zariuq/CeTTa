!(import! &self rho)

(= (edge-forwarder-if $required (edge $src $dst $tag))
   (if-equal $tag $required
     (rho.recv $src $msg (rho.send $dst (rho.drop $msg)))
     rho.nil))

(= (edge-signal-if $required (edge $src $dst $tag) $payload)
   (if-equal $tag $required
     (rho.send $src (rho.send $payload rho.nil))
     rho.nil))

(= (build-forwarders $required $edges)
   (foldl-atom $edges rho.nil $acc $edge
     (let $head (eval (edge-forwarder-if $required $edge))
       (rho.par $acc $head))))

(= (build-signals $required $edges $payload)
   (foldl-atom $edges rho.nil $acc $edge
     (let $head (eval (edge-signal-if $required $edge $payload))
       (rho.par $acc $head))))

!(let $program
  (eval
    (let $forwarders
      (eval (build-forwarders trusted
        ((edge $c0 $c1 trusted)
         (edge $c1 $c2 backup))))
      (let $signals
        (eval (build-signals trusted
          ((edge $c0 $c1 trusted)
           (edge $c1 $c2 backup))
          $payload))
        (rho.par $forwarders $signals))))
  (rho.step $program))
