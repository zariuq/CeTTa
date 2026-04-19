; HE-source fold/match aggregation benchmark.
; Imported from PeTTa/examples/foldallmatch.metta.
; Final evaluation should produce the numeric result 5.

(kb 1)
(kb 2)

!(let $items
      (collapse (match &self (kb $n) (+ $n 1)))
      (foldl-atom $items 0 $acc $item
        (eval (+ $acc $item))))
