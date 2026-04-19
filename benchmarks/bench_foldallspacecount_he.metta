; HE-source fold/collapse aggregation benchmark.
; Imported from the PeTTa foldallspacecount workload as a benchmark witness.
; Final evaluation should produce the numeric result 3.

(foo 1)
(foo 2)
(foo 3)

(= (countitem)
   (let $x
        (match &self (foo $1) (foo $1))
        1))

(= (merge $a $b) (+ $a $b))

!(let $items
      (collapse (countitem))
      (foldl-atom $items 0 $acc $item
        (eval (merge $acc $item))))
