; Experimental native snapshot-world observation. Both physical consuming
; choices remain available; the live source space is left unchanged.
!(bind! &jobs (new-space))
!(add-atom &jobs Token)
!(add-atom &jobs (exec 8 (I (take Token)) (O (+ LeftWon))))
!(add-atom &jobs (exec 2 (I (take Token)) (O (+ RightWon))))
!(select 100
   (search-policy (control WO
      (resources (Transaction worlds))
      (key (exec $priority $inputs $outputs) $priority)
      (budget 10000)))
   &jobs)
!(collapse (get-atoms &jobs))
