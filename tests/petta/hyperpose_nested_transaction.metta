(= (nested-transaction-publish)
   (transaction
     (let $_inner
          (collapse
            (hyperpose
              ((add-atom &nested-transaction-space (inner 0))
               (add-atom &nested-transaction-space (inner 1)))))
          (add-atom &nested-transaction-space
                    (transaction-complete)))))

(= (run-nested-transaction)
   (let $_seed
        (add-atom &nested-transaction-space (seed))
        (let $_outer
             (collapse
               (hyperpose
                 ((nested-transaction-publish)
                  (add-atom &nested-transaction-space (sibling)))))
             (test (size &nested-transaction-space) 5))))

!(run-nested-transaction)
