(= (loop) (loop))
(= (outer) (select 1 (loop)))
(= (outer) 42)
!(select 1 (search-policy (control FIFO (budget 1000))) (outer))
