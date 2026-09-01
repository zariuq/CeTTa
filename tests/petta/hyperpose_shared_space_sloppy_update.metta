(= (sloppy-increment)
   (match &shared-sloppy-update (count $value)
          ((remove-atom &shared-sloppy-update (count $value))
           (let $next (+ $value 1)
                (add-atom &shared-sloppy-update (count $next))))))

(= (run-shared-sloppy-update)
   (let $_seed
        (add-atom &shared-sloppy-update (count 37))
        (let $_workers
             (collapse
               (hyperpose
                 ((sloppy-increment)
                  (sloppy-increment)
                  (sloppy-increment)
                  (sloppy-increment)
                  (sloppy-increment))))
             (test (< 0 (size &shared-sloppy-update)) true))))

!(run-shared-sloppy-update)
