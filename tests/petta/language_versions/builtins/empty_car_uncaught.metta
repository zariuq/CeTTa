(: head-number (-> (List Number) Number))
(= (head-number $xs) (+ 1 (car-atom $xs)))
!(head-number ())
!(after-uncaught-error)
