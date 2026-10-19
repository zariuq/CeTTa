; The checked finite-source metric counts source equation activations.
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

; One graph pass computes both source measures. The choice below is a policy,
; not a proof that two workers improve elapsed time or are always independent.
; Execution reports retained application-equation admissions separately from
; native telemetry. This policy leaves native counters untracked; requesting
; (costs (equation-admissions-v1 native-counts-v1)) enables both explicitly.
; Scheduled execution span is distinct from graph dependency depth.
(= (analyzed:workers
      (CostAnalysis (metric "equation-work-depth") (source-profile FiniteGroundCallsUngradedRows)
       (result (Bound (WorkDepth $work $depth))) (input $input)
       (source $instance $revision) (demand CompleteUnfolding) (steps $steps)
       (residual $analysis) (analysis-cost $analysis-cost)))
   (let $workers (if (> $work $depth) 2 1)
     (Planned $workers $work $depth
       (select 7 (search-policy (control FIFO (budget 10000) (workers $workers)
         (retain True) (costs equation-admissions-v1))) (analyzed:routes)))))

!(analyzed:workers
   (cost:analyze (quote (analyzed:routes)) 1000 equation-work-depth native-counts-v1))
