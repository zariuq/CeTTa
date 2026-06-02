!(import! &self rho)

(= (count-valid-item $acc (cert valid $payload))
   (+ $acc 1))

(= (count-valid-item $acc (cert stale $payload))
   $acc)

(= (count-valid $certs)
   (foldl-atom $certs 0 $acc $cert
     (eval (count-valid-item $acc $cert))))

(= (quorum-signal $certs)
   (let $count (eval (count-valid $certs))
     (if (<= 2 $count)
       (rho.send $approved (rho.send $quorum rho.nil))
       rho.nil)))

!(let $program
  (eval (quorum-signal
    ((cert valid cert0)
     (cert stale cert1)
     (cert valid cert2))))
  $program)
