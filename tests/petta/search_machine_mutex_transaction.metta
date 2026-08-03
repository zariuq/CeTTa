!(add-atom &mutex-space (cnt 37))

(= (mutexinc)
   (with_mutex petta-counter
     (match &mutex-space (cnt $n)
       ((remove-atom &mutex-space (cnt $n))
        (let $next (+ $n 1)
          (add-atom &mutex-space (cnt $next)))))))

!(hyperpose
   ((mutexinc)
    (mutexinc)
    (mutexinc)
    (mutexinc)
    (mutexinc)))
!(collapse (get-atoms &mutex-space))

!(transaction
   (match &mutex-space (cnt $n)
     ((remove-atom &mutex-space (cnt $n))
      (add-atom &mutex-space (cnt 999))
      (empty))))
!(collapse (get-atoms &mutex-space))
