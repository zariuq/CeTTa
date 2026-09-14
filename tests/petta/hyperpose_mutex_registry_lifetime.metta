!(add-atom &mutex-registry-lifetime (seed))

(= (publish-under-process-mutex $value)
   (with_mutex persistent-hyperpose-mutex
     (add-atom &mutex-registry-lifetime (value $value))))

(= (run-one-mutex-worker $value)
   (let $_workers
        (collapse
          (hyperpose
            ((publish-under-process-mutex $value)
             mutex-registry-lifetime-companion)))
        true))

!(run-one-mutex-worker first)
!(run-one-mutex-worker second)
!(test (size-atom (collapse (get-atoms &mutex-registry-lifetime))) 3)
