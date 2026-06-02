!(import! &self rho)

(= (subscription-proc (sub $topic $sink $payload))
   (rho.par
     (rho.recv $topic $msg (rho.send $sink (rho.drop $msg)))
     (rho.send $topic (rho.send $payload rho.nil))))

(= (build-subscriptions $subs)
   (foldl-atom $subs rho.nil $acc $sub
     (let $head (eval (subscription-proc $sub))
       (rho.par $acc $head))))

!(let $program
  (eval (build-subscriptions
    ((sub $topic0 $sink0 $payload0)
     (sub $topic1 $sink1 $payload1)
     (sub $topic2 $sink2 $payload2))))
  $program)
