; Deep tail-path coverage for single-continuation special forms.

(= (countdown-unify $n)
   (unify $n 0 done (countdown-unify (- $n 1))))
!(assertEqual (countdown-unify 5000) done)

(= (countdown-switch $n)
   (switch $n
     ((0 done)
      ($_ (countdown-switch (- $n 1))))))
!(assertEqual (countdown-switch 5000) done)

!(bind! &tail-snapshot (new-space))
!(add-atom &tail-snapshot (tick ok))
(= (countdown-snapshot $n)
   (if (<= $n 0) done
     (with-space-snapshot $snap &tail-snapshot
       (let $_ (select (match $snap (tick $x) $x))
         (countdown-snapshot (- $n 1))))))
!(assertEqual (countdown-snapshot 5000) done)
