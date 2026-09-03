!(reset-runtime-stats!)
!(bind! &zero (runtime-stats!))
!(assertEqualToResult (match &zero (runtime-counter bindings-merge $n) $n) (0))
!(assertEqualToResult (match &zero (runtime-counter bindings-constraint-add $n) $n) (0))
