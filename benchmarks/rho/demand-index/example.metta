!(import! &self rho)

(= (watch-proc (watch $topic $sink))
   (rho.recv $topic $msg (rho.send $sink (rho.drop $msg))))

(= (fact-proc (fact $topic $payload))
   (rho.send $topic (rho.send $payload rho.nil)))

(= (build-watches $watches)
   (foldl-atom $watches rho.nil $acc $watch
     (let $head (eval (watch-proc $watch))
       (rho.par $acc $head))))

(= (build-facts $facts)
   (foldl-atom $facts rho.nil $acc $fact
     (let $head (eval (fact-proc $fact))
       (rho.par $acc $head))))

(= (demand-program $watches $facts)
   (let $watch-procs (eval (build-watches $watches))
     (let $fact-procs (eval (build-facts $facts))
       (rho.par $watch-procs $fact-procs))))

!(let $program
  (eval (demand-program
    ((watch $topic0 $sink0)
     (watch $topic0 $sink1)
     (watch $topic1 $sink2))
    ((fact $topic0 $payload0)
     (fact $topic1 $payload1))))
  (rho.step $program))
