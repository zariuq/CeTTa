; A later, more expensive computation produces the cheaper answer.
(= (finite) (Route immediate 9))
(= (finite) (Route delayed (+ (+ 0 0) 0)))
!(select 1 (search-policy (control WO
   (key (Route $id $cost) (- 0 $cost))
   (best (Route $id $cost) $cost) (budget 2000))) (finite))

; A syntactically supplied bound is not a source-bound license.
(= (negative $n) (negative (- $n 1)))
(= (negative $n) (Route $n))
!(select 1 (search-policy (control WO
   (best (Route $cost) $cost) (bound (negative $n) $n)
   (budget 300))) (negative 0))

; Zero cost is valid; protected age keeps both the loop and the witness live.
(= (zero $n) (zero $n))
(= (zero $n) (Route $n))
!(select 3 (search-policy (control WO
   (key (zero $n) 0) (best (Route $cost) $cost)
   (bound (zero $n) $n) (budget 2000))) (zero 0))

; An unmatched source equation disables live certification.
(= (mixed $n) (mixed (+ $n 1)))
(= (mixed $n) (Route $n))
(= (mixed $n) (Route (- 0 $n)))
!(select 1 (search-policy (control FIFO
   (best (Route $cost) $cost) (bound (mixed $n) $n)
   (budget 300))) (mixed 0))
