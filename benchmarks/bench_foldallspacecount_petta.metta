; PeTTa-source foldall aggregation benchmark imported from PeTTa/examples.
; This is the source-side counterpart to bench_foldallspacecount_he.metta.
; Final evaluation should produce the numeric result 3.

(foo 1)
(foo 2)
(foo 3)

(= (countitem)
   (let $x
        (match &self (foo $1) (foo $1))
        1))

(= (merge $a $b) (+ $a $b))

!(foldall merge (countitem) 0)
