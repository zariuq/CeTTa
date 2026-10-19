;; Inside a transaction, a function of the reference library that SWI
;; implements without effects is a call, as outside one. Each line shows the
;; answers inside a transaction, then outside one.
!(inequality (collapse (transaction (!= 1 2))) (collapse (!= 1 2)))
!(implication (collapse (transaction (implies True False)))
              (collapse (implies True False)))
!(concatenation (collapse (transaction (atom_concat txn_ name)))
                (collapse (atom_concat txn_ name)))
!(characters (collapse (transaction (atom_chars abc)))
             (collapse (atom_chars abc)))
!(hash-agrees (== (collapse (transaction (term_hash (f a 1))))
                  (collapse (term_hash (f a 1)))))
!(sorting (collapse (transaction (sort (3 1 2 1))))
          (collapse (sort (3 1 2 1))))
!(last-element (collapse (transaction (last (1 2 3))))
               (collapse (last (1 2 3))))
!(constraint (collapse (transaction (#< 1 2))) (collapse (#< 1 2)))
!(constraint-arithmetic (collapse (transaction (#- 3 1)))
                        (collapse (#- 3 1)))
;; A condition computed by one decides the branch.
!(branch (collapse (transaction (if (!= 1 2) differ same)))
         (collapse (if (!= 1 2) differ same)))
!(nested (collapse (transaction (transaction (atom_concat in ner))))
         (collapse (atom_concat in ner)))
