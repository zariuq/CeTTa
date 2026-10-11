; The producer remains in the native evaluator. This program only reads its
; cumulative declared work and chooses whether to grant another quantum.
; The threshold is checked between quanta; one quantum can cross it. It is
; neither a wall-clock limit nor a bound on every primitive inside a quantum.
(= (budget:routes $n) (budget:routes (+ $n 1)))
(= (budget:routes $n) (Route $n))

(= (budget:append $items $tail)
   (if (== $items ()) $tail
     (let ($head $rest) (decons-atom $items)
       (cons-atom $head (budget:append $rest $tail)))))

(= (budget:work
      (NativeCost native-events-v1 (scope $scope) (work (Exact $n))
       (span $span) (operations $ops) (trace-status Complete) (read $read))) $n)

(= (budget:search $limit $rounds $seen
      (Observation $values (satisfied $s) (closed $c) (residual $r)
       (work $w) (status $t) (cost $receipt)))
   (let $all (budget:append $seen $values)
     (if (or $s $c) (Finished $all $s $c $receipt $r)
       (let $spent (budget:work $receipt)
         (if (>= $spent $limit) (Paused $all CostBudget $receipt $r)
           (if (== $rounds 0) (Paused $all RoundBudget $receipt $r)
             (budget:search $limit (- $rounds 1) $all
               (select 7 (search-policy (control FIFO (budget 1)
                 (retain True) (costs native-events-v1))) $r))))))))

; A small allowance yields an owned residual, which this program later resumes.
!(let (Paused $prefix CostBudget $spent $residual)
   (budget:search 100 100 ()
     (select 7 (search-policy (control FIFO (budget 0)
       (retain True) (costs native-events-v1))) (budget:routes 0)))
   (let $storage (cost:storage $residual)
     (let (Observation $rest (satisfied $s) (closed $c) (residual $r)
           (work $w) (status $t) (cost $cost))
       (select 7 (search-policy (control FIFO (budget 10000)
         (retain True) (costs native-events-v1))) $residual)
       (BudgetedRoutes (budget:append $prefix $rest) $s $c $spent $storage $cost))))
