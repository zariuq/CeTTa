(= (pause-grade $n $value)
   (if (== $n 0) $value (pause-grade (- $n 1) $value)))
(=w (rows) A (pause-grade 6 2))
(=w (rows) A (pause-grade 6 2))
(= (rows) B (where 3))

!(select 7 (search-policy (control FIFO (budget 2000) (workers 2)))
   (weight:read (algebra 0 1 + *) (rows)))

; The second observation continues the residual returned by the first.
!(let (Observation $prefix (satisfied $s) (closed $c)
                   (residual $remaining) (work $work) (status $status))
      (select 7 (search-policy (control FIFO (budget 37) (workers 2)))
         (weight:read (algebra 0 1 + *) (rows)))
   (Split $prefix $work
      (select 7 (search-policy (control FIFO (budget 1963) (workers 2)))
         $remaining)))
