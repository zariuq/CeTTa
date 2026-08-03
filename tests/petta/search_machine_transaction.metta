!(add-atom &tx-success (counter 1))
!(transaction
   (match &tx-success (counter $n)
     ((remove-atom &tx-success (counter $n))
      (let $next (+ $n 1)
        (add-atom &tx-success (counter $next))))))
!(collapse (get-atoms &tx-success))

!(add-atom &tx-once (item a))
!(add-atom &tx-once (item b))
!(transaction
   (match &tx-once (item $x)
     (remove-atom &tx-once (item $x))))
!(collapse (get-atoms &tx-once))

!(transaction
   (transaction
     (add-atom &tx-success (nested yes))))
!(collapse (get-atoms &tx-success))

!(bind! tx-state (new-state start))
!(transaction (change-state! tx-state committed))
!(get-state tx-state)
!(transaction
   ((change-state! tx-state rolled-back)
    (empty)))
!(get-state tx-state)
