; The checked finite-source metric counts physical equation activations.
; This client admits seven-route search only if the complete unfolding fits
; its declared activation allowance. Runtime operation costs are read separately.
(= (analyzed:routes) (Route 0))
(= (analyzed:routes) (Route 1))
(= (analyzed:routes) (Route 2))
(= (analyzed:routes) (Route 3))
(= (analyzed:routes) (Route 4))
(= (analyzed:routes) (Route 5))
(= (analyzed:routes) (Route 6))
(= (analyzed:routes) (Route 7))

(= (analyzed:plan $allowance
      (CostAnalysis (metric "equation-activation") (source-profile FiniteGroundCallsUngradedRows) (result (Bound $bound))
       (input $input) (source $instance $revision) (demand CompleteUnfolding)
       (steps $steps) (residual $analysis) (analysis-cost $cost)))
   (if (<= $bound $allowance)
     (Admitted $bound
       (select 7 (search-policy (control FIFO (budget 10000)
         (retain True) (costs native-events-v1))) (analyzed:routes)))
     (Deferred $bound $analysis)))

!(analyzed:plan 8 (cost:analyze (quote (analyzed:routes)) 1000))
!(analyzed:plan 7 (cost:analyze (quote (analyzed:routes)) 1000))
