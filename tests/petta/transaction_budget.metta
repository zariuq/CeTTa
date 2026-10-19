; A long stretch of work inside one transaction: its heap is collected as
; outside one, so what the run holds does not grow with the work.
(= (spin $n)
   (if (== $n 0) done (let $_ (change-state! &spin (pair $n (1 2 3 4))) (spin (- $n 1)))))
!(transaction (spin 100000))
