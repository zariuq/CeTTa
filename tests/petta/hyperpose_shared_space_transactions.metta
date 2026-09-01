(= (transactional-publish $value)
   (transaction
      (add-atom &shared-transactions (committed $value))))

(= (all-transaction-values-published)
   (match &shared-transactions (committed 0)
     (match &shared-transactions (committed 1)
       (match &shared-transactions (committed 2)
         (match &shared-transactions (committed 3)
           (match &shared-transactions (committed 4)
             (match &shared-transactions (committed 5)
               (match &shared-transactions (committed 6)
                 (match &shared-transactions (committed 7)
                   true)))))))))

(= (run-shared-transactions)
   (let $_seed
        (add-atom &shared-transactions (seed))
        (let $_workers
             (collapse
               (hyperpose
                 ((transactional-publish 0)
                  (transactional-publish 1)
                  (transactional-publish 2)
                  (transactional-publish 3)
                  (transactional-publish 4)
                  (transactional-publish 5)
                  (transactional-publish 6)
                  (transactional-publish 7))))
             (test
               (if (== (size &shared-transactions) 9)
                   (all-transaction-values-published)
                   false)
               true))))

!(run-shared-transactions)
