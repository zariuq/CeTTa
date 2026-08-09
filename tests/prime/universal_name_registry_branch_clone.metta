; An effectful branch that mentions a structural registry reference must not be
; dispatched to a private cloned registry: doing so would discard its visible
; effects and make the result depend on the thread budget. Until an explicit
; merge contract exists, the evaluator selects the cooperative path. Prime
; Need may select that path at the earlier binding-preservation boundary, so
; the runtime witness is that no threaded run or worker task occurs.

!(bind! &@(branch-registry clone-witness) (new-space))
!(add-atom &@(branch-registry clone-witness) seed)

(= (branch-touch $label)
   (let $_
     (add-atom &@(branch-registry clone-witness) (branch $label))
     (size &@(branch-registry clone-witness))))

!(reset-runtime-stats!)
!(pragma! num-threads 2)
!(assertEqual
  (reify (hyperpose ((branch-touch left) (branch-touch right))))
  (2 3))

; Both effects remain visible in deterministic source order.
!(assertEqual (size &@(branch-registry clone-witness)) 3)
!(assertEqual
  (reify
    (match &@(branch-registry clone-witness)
           (branch $which) $which))
  (left right))

!(bind! &registry-clone-stats (runtime-stats!))
!(assertEqualToResult
  (match &registry-clone-stats
    (runtime-counter hyperpose-threaded-run $n)
    (== $n 0))
  (True))
!(assertEqualToResult
  (match &registry-clone-stats
    (runtime-counter parallel-worker-task $n)
    (== $n 0))
  (True))

!(println! (PrimeUniversalNameRegistryBranchCloneGuardSummary 5 5 0))
