; transform inside a transaction answers as outside it, reading and changing
; the transaction's working spaces: the commit publishes the firings' takes
; and additions, a rollback discards them.
!(import! &self (library lib_transform))
(= (seen $x) (saw $x))
!(bind! &s (new-space))
!(add-atom &s (n 1))
!(add-atom &s (n 2))
!(read-data (transaction (sort-atom (collapse (transform &s (n $x) (got $x))))))
!(read-call (transaction (sort-atom (collapse (transform &s (n $x) (seen $x))))))
!(read-once (transaction (once (transform &s (n 1) (seen 1)))))
!(take-into (transaction (collapse (transform &s (take (n $x)) (took $x) &s))))
!(committed (sort-atom (collapse (get-atoms &s))))
!(bind! &r (new-space))
!(add-atom &r (m 1))
!(add-atom &r (m 2))
; a body that fails rolls back the takes its firings made, once or every one
!(rolled-back (collapse (transaction (let $c (transform &r (take (m $x)) (gone $x) &r) (empty)))))
!(rolled-back-once (collapse (transaction (let $c (once (transform &r (take (m $x)) (gone $x) &r)) (empty)))))
!(kept-after-rollback (sort-atom (collapse (get-atoms &r))))
; control: outside a transaction the same firing answers alike
!(outside (collapse (transform &r (take (m 2)) (out 2) &r)))
!(r-final (sort-atom (collapse (get-atoms &r))))
; Source code runs in both the proposal and firing phases. A collected
; authored implementation must carry values across the copied program too.
!(bind! &let-rule (new-space))
!(bind! &let-log (new-space))
!(add-atom &let-rule (token 7))
!(take-with-let (transaction (collapse
   (transform &let-rule
     (, (take (token $x)) (let $_written (add-atom &let-log (logged $x))))
     (taken $x) &let-rule))))
!(let-rule-left (collapse (get-atoms &let-rule)))
!(let-effects (collapse (get-atoms &let-log)))
