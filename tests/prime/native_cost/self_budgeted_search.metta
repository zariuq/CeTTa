; The producer remains in the native evaluator. This program only reads its
; cumulative work in an explicit metric and chooses another quantum.
; Both clients request totals only; neither retains cost-event chronology.
; The threshold is checked between quanta; one quantum can cross it. It is
; neither a wall-clock limit nor a bound on every primitive inside a quantum.
(= (budget:routes $n) (budget:routes (+ $n 1)))
(= (budget:routes $n) (Route $n))

(= (budget:append $items $tail)
   (if (== $items ()) $tail
     (let ($head $rest) (decons-atom $items)
       (cons-atom $head (budget:append $rest $tail)))))

(= (budget:work native-counts-v1
      (NativeCost native-counts-v1 (scope $scope) (work (Exact $n))
       (span $span) (operations $ops) (trace-status Unrecorded) (read $read))) $n)

(= (budget:work equation-admissions-v1
      (ExecutionCost (source-profile equation-admissions-v1)
       (source-domain PrimeApplicationEquations)
       (charged-operation RetainedMatchedRHS) (strategy OwnedEquationFrontier)
       (work (Exact $n)) (span $span) (native $native))) $n)

(= (budget:search $profile $limit $rounds $seen
      (Observation $values (satisfied $s) (closed $c) (residual $r)
       (work $w) (status $t) (cost $receipt)))
   (let $all (budget:append $seen $values)
     (if (or $s $c) (Finished $all $s $c $receipt $r)
       (let $spent (budget:work $profile $receipt)
         (if (>= $spent $limit) (Paused $all CostBudget $receipt $r)
           (if (== $rounds 0) (Paused $all RoundBudget $receipt $r)
             (budget:search $profile $limit (- $rounds 1) $all
               (select 7 (search-policy (control FIFO (budget 1)
                 (retain True) (costs $profile))) $r))))))))

; A small allowance yields an owned residual, which this program later resumes.
(= (budget:native-demo)
 (let (Paused $prefix CostBudget $spent $residual)
   (budget:search native-counts-v1 100 100 ()
     (select 7 (search-policy (control FIFO (budget 0)
       (retain True) (costs native-counts-v1))) (budget:routes 0)))
   (let $storage (cost:storage $residual)
     (let (Observation $rest (satisfied $s) (closed $c) (residual $r)
           (work $w) (status $t) (cost $cost))
       (select 7 (search-policy (control FIFO (budget 10000)
         (retain True) (costs native-counts-v1))) $residual)
       (BudgetedRoutes (budget:append $prefix $rest) $s $c $spent $storage $cost)))))

; The same policy can stop using source admissions while native telemetry is
; untracked. Pausing retains the frontier; completion spends its remaining
; source work once, without starting the computation over.
(= (budget:source-demo)
 (let (Paused $prefix CostBudget $spent $residual)
   (budget:search equation-admissions-v1 4 100 ()
     (select 7 (search-policy (control FIFO (budget 0)
       (retain True) (costs equation-admissions-v1)))
       (chain (println! "SOURCE-BUDGET-ONCE") $ignored (budget:routes 0))))
   (let $storage (cost:storage $residual)
     (let (Observation $rest (satisfied $s) (closed $c) (residual $r)
           (work $w) (status $t) (cost $cost))
       (select 7 (search-policy (control FIFO (budget 10000)
         (retain True) (costs equation-admissions-v1))) $residual)
       (SourceBudgetedRoutes (budget:append $prefix $rest) $s $c $spent $storage $cost)))))

; Select the native entry alone for HE/PeTTa's extended profile. The source
; admission metric describes Prime application equations specifically.
!(budget:native-demo)
!(budget:source-demo)
