(= (finite) 1)
(= (finite) 2)
(= (finite) 2)
(= (finite) 3)
!(let (Observation $prefix (satisfied $s) (closed $c) (residual $r) (work $w) (status $t))
   (select 2 (search-policy (control FIFO (retain True))) (finite))
   (Pair $prefix
     (select 0 (search-policy (control FIFO (demand all) (retain True))) $r)))

(= (numbers $n) (numbers (+ $n 1)))
(= (numbers $n) $n)
!(let (Observation $prefix (satisfied $s) (closed $c) (residual $r) (work $w) (status $t))
   (select 7 (search-policy (control FIFO (budget 10))) (numbers 0))
   (Pair $prefix
     (select 7 (search-policy (control PO (key (numbers $n) $n) (budget 3000))) $r)))
