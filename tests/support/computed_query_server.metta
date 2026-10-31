!(import! &self "../../examples/kb/space_computed_curriculum.metta")
; Test-only observation and authored code edits. The teaching server exposes
; only its existing ordinary Space query and publication operations.
(= (computed-test:stats $call) (get-memoize-stats))
(= (computed-test:clear $call) (invalidate-memoize space-computed:nodes))
(= (computed-test:edit $call)
  (let* (($_ (remove-atom &self (= (space-computed:leaf-weight) 1)))
         ($_ (add-atom &self (= (space-computed:leaf-weight) 2)))) ProgramEditedV1))
(= (computed-test:configuration)
  (let (ValueServiceV1 $socket $data $routes $limits) (space-computed:configuration)
    (ValueServiceV1 $socket $data
      (list:append $routes
        ((ServiceRouteV1 ComputedStats Read computed-test:stats)
         (ServiceRouteV1 ComputedClear Write computed-test:clear)
         (ServiceRouteV1 ComputedEdit Write computed-test:edit))) $limits)))
