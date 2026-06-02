!(import! &self rho)

(= (replica-worker $sink)
   (rho.recv $topic $msg (rho.send $sink (rho.drop $msg))))

(= (replica-signal $payload)
   (rho.send $topic (rho.send $payload rho.nil)))

(= (build-replica-workers $sinks)
   (foldl-atom $sinks rho.nil $acc $sink
     (let $head (eval (replica-worker $sink))
       (rho.par $acc $head))))

(= (build-replica-signals $payloads)
   (foldl-atom $payloads rho.nil $acc $payload
     (let $head (eval (replica-signal $payload))
       (rho.par $acc $head))))

!(let $program
  (eval
    (let $workers
      (eval (build-replica-workers ($sink0 $sink1 $sink2)))
      (let $signals
        (eval (build-replica-signals ($payload0 $payload1 $payload2)))
        (rho.par $workers $signals))))
  $program)
