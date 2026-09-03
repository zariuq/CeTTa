(= (publish $value)
   (add-atom &shared-indirect-add (value $value)))

(= (run-shared-indirect-add)
   (let $_seed
        (add-atom &shared-indirect-add (seed))
        (let $_workers
             (collapse
               (hyperpose
                 ((publish 0)
                  (publish 1)
                  (publish 2)
                  (publish 3)
                  (publish 4)
                  (publish 5)
                  (publish 6)
                  (publish 7))))
             (test
               (size-atom
                 (collapse (get-atoms &shared-indirect-add)))
               9))))

!(run-shared-indirect-add)
