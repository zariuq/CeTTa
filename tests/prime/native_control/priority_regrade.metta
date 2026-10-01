(= (items) (Item a 4))
(= (items) (Item b 1))
(= (items) (Item c 3))
(= (items) (Item d 2))
!(let (Observation $prefix (satisfied $s) (closed $c) (residual $r) (work $w) (status $t))
   (select 4 (search-policy (control WO (key (Item $id $cost) $cost) (budget 12))) (items))
   (Pair $prefix
     (select 4 (search-policy (control WO (key (Item $id $cost) (- 0 $cost)) (budget 1000))) $r)))
