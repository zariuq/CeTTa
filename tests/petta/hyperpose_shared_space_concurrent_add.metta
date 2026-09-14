(= (run-shared-concurrent-add)
   (let $_seed
        (add-atom &shared-concurrent-add (seed))
        (let $_workers
             (collapse
               (hyperpose
                 ((add-atom &shared-concurrent-add (value 0))
                  (add-atom &shared-concurrent-add (value 1))
                  (add-atom &shared-concurrent-add (value 2))
                  (add-atom &shared-concurrent-add (value 3))
                  (add-atom &shared-concurrent-add (value 4))
                  (add-atom &shared-concurrent-add (value 5))
                  (add-atom &shared-concurrent-add (value 6))
                  (add-atom &shared-concurrent-add (value 7)))))
             (test
               (size-atom
                 (collapse (get-atoms &shared-concurrent-add)))
               9))))

!(run-shared-concurrent-add)
