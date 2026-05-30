!(import! &self rho)

(= (watch-proc $target (watch $topic-tag $topic-name $sink))
   (if-equal $topic-tag $target
     (rho.recv $topic-name $msg (rho.send $sink (rho.drop $msg)))
     rho.nil))

(= (fact-proc $target (fact $topic-tag $topic-name $payload))
   (if-equal $topic-tag $target
     (rho.send $topic-name (rho.send $payload rho.nil))
     rho.nil))

(= (build-watches $target $watches)
   (foldl-atom $watches rho.nil $acc $watch
     (let $head (eval (watch-proc $target $watch))
       (rho.par $acc $head))))

(= (build-facts $target $facts)
   (foldl-atom $facts rho.nil $acc $fact
     (let $head (eval (fact-proc $target $fact))
       (rho.par $acc $head))))

!(let $program
  (eval
    (let $watch-procs
      (eval (build-watches topic0
        ((watch topic0 $topic0 $sink0)
         (watch topic1 $topic1 $sink1))))
      (let $fact-procs
        (eval (build-facts topic0
          ((fact topic0 $topic0 $payload0)
           (fact topic1 $topic1 $payload1))))
        (rho.par $watch-procs $fact-procs))))
  $program)
