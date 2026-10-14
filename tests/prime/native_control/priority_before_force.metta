(= (loop) (loop))
(= (candidates) (Candidate 0 (loop)))
(= (candidates) (Candidate 9 answer))
!(select 1
   (search-policy (control WO
      (key (Candidate $cost $payload) $cost)
      (budget 1000)))
   (candidates))
