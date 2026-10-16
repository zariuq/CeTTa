;; Strings, bigints and rationals key the substitution index by content.
;; Neither filing such a value in an index nor looking one up may make its
;; text a permanent symbol, so transient keys leave the symbol table's owned
;; count and bytes where they were. Before content keys, each loop below
;; added one owned symbol per iteration.
!(bind! &facts (new-space))
(= (pick "a") 1)
(= (pick "b") 2)
(= (pick "c") 3)
(= (pick "d") 4)
(= (pick "e") 5)
(= (pick 100000000000000000000000) 6)
(= (owned-count) (match (runtime-stats!) (runtime-counter symbol-owned-count $n) $n))
(= (owned-bytes) (match (runtime-stats!) (runtime-counter symbol-owned-bytes $n) $n))
(= (key $n) (repr (transient-index-key $n)))
(= (bounded $count $bytes)
   ((< (- (owned-count) $count) 64) (< (- (owned-bytes) $bytes) 2048)))
;; Calls select among more than four clauses through the equation index.
(= (query-loop $n)
   (if (== $n 0) done
       (let* (($r (collapse (pick (key $n))))
              ($b (collapse (pick (+ 100000000000000000000000 $n)))))
         (query-loop (- $n 1)))))
;; Equations and facts filed under fresh keys, read and removed again.
(= (churn-loop $n)
   (if (== $n 0) done
       (let $s (key $n)
         (let* (($a1 (add-atom &self (= (pick $s) churned)))
                ($r1 (collapse (pick $s)))
                ($a2 (remove-atom &self (= (pick $s) churned)))
                ($a3 (add-atom &facts (row $s churned)))
                ($r2 (collapse (match &facts (row $s $v) $v)))
                ($a4 (remove-atom &facts (row $s churned))))
           (churn-loop (- $n 1))))))
;; Reads of a space large enough to keep a substitution index; its own
;; keys are filed when the index is built.
(= (add-facts $n)
   (if (== $n 0) done
       (let* (($k (repr (fact-key $n))) ($_ (add-atom &facts (row $k $n))))
         (add-facts (- $n 1)))))
(= (fact-loop $n)
   (if (== $n 0) done
       (let $r (collapse (match &facts (row (key $n) $v) $v))
         (fact-loop (- $n 1)))))
!(let $_ (owned-count) settled)
!(let* (($c (owned-count)) ($b (owned-bytes)) ($_ (query-loop 1000)))
   (queries (bounded $c $b)))
!(let* (($c (owned-count)) ($b (owned-bytes)) ($_ (churn-loop 1000)))
   (churn (bounded $c $b)))
!(let* (($c (owned-count)) ($b (owned-bytes))
        ($added (add-facts 200)) ($read (fact-loop 1000)))
   (facts (bounded $c $b)))
!(collapse (pick "c"))
!(collapse (pick 100000000000000000000000))
!(collapse (match &facts (row "(fact-key 7)" $v) $v))
