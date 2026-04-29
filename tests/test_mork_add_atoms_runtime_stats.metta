!(import! &self mork)
!(bind! &bulk (mork:new-space))
!(reset-runtime-stats!)
!(mork:add-atoms &bulk (collapse (superpose ((edge c d) (edge e f) (edge g h)))))
!(bind! &bulk-stats (runtime-stats!))
!(assertEqualToResult
  (match &bulk-stats (runtime-counter mork-add-batch-call $n) $n)
  (1))
!(assertEqualToResult
  (match &bulk-stats (runtime-counter mork-add-batch-items $n) $n)
  (3))
!(assertEqualToResult
  (match &bulk-stats (runtime-counter mork-add-call $n) $n)
  (0))
!(assertEqualToResult (mork:size &bulk) (3))
