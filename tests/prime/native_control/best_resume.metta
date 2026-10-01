(= (routes $n) (routes (+ $n 1)))
(= (routes $n) (Route $n))
!(let (Observation $first (satisfied $s) (closed $c) (residual $r) (work $w) (status $t))
   (select 3 (search-policy (control FIFO
      (best (Route $cost) $cost) (bound (routes $n) $n) (budget 3000)))
     (routes 0))
   (Pair $first
     (select 7 (search-policy (control PO (key (routes $n) $n)
        (best (Route $cost) $cost) (budget 5000))) $r)))

(= (items) (Item a 8))
(= (items) (Item b 2))
(= (items) (Item c 3))
!(let (Observation $first (satisfied $s) (closed $c) (residual $r) (work $w) (status $t))
   (select 1 (search-policy (control WO (best (Item $id $cost) $cost) (retain True)))
     (items))
   (Pair $first
     (select 0 (search-policy (control FIFO (demand all) (retain True))) $r)))

; Complete ordered observation is distinct from the empty best prefix.
!(select 0 (search-policy (control FIFO (best (Item $id $cost) $cost)
   (demand all) (retain True))) (items))
