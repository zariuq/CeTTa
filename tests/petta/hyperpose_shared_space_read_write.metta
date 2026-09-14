(= (shared-write-loop $n)
   (if (== $n 0)
       true
       ((add-atom &shared-read-write (value $n))
        (shared-write-loop (- $n 1)))))

(= (shared-read-loop $n)
   (if (== $n 0)
       true
       (let $snapshot (collapse (get-atoms &shared-read-write))
            (let $count (size-atom $snapshot)
                 (let $size
                      (size-atom
                        (collapse (get-atoms &shared-read-write)))
                      (shared-read-loop (- $n 1)))))))

(= (run-shared-read-write)
   (let $_seed
        (add-atom &shared-read-write (seed))
        (let $_workers
             (collapse
               (hyperpose
                 ((shared-write-loop 100)
                  (shared-read-loop 100)
                  (add-atom &shared-read-write (value 0))
                  (add-atom &shared-read-write (value 1))
                  (add-atom &shared-read-write (value 2))
                  (add-atom &shared-read-write (value 3)))))
             (test
               (size-atom
                 (collapse (get-atoms &shared-read-write)))
               105))))

!(run-shared-read-write)
