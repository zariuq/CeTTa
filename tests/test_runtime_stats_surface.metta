!(reset-runtime-stats!)
!(bind! &zero (runtime-stats!))
!(assertEqualToResult (match &zero (runtime-counter bindings-merge $n) $n) (0))
!(assertEqualToResult (match &zero (runtime-counter bindings-constraint-add $n) $n) (0))

!(add-atom &self (= (probe A) ok))
!(probe A)
!(bind! &stats (runtime-stats!))
!(assertEqualToResult
  (match &stats (runtime-counter query-equations $n) (> $n 0))
  (True))
!(assertEqualToResult
  (match &stats (runtime-counter query-equation-candidates $n) (> $n 0))
  (True))
!(assertEqualToResult
  (match &stats (runtime-counter bindings-clone $n) (> $n 0))
  (True))
!(assertEqualToResult
  (match &stats (runtime-counter rename-vars $n) (> $n 0))
  (True))

!(add-atom &self (: typed Number))
!(remove-atom &self (= (probe A) ok))
!(remove-atom &self (: typed Number))
!(bind! &after-remove (runtime-stats!))
!(assertEqualToResult
  (match &after-remove (runtime-counter eq-index-rebuild $n) $n)
  (0))
!(assertEqualToResult
  (match &after-remove (runtime-counter ty-index-rebuild $n) $n)
  (0))
!(probe A)
!(get-type typed)
!(bind! &after-lookup (runtime-stats!))
!(assertEqualToResult
  (match &after-lookup (runtime-counter eq-index-rebuild $n) (> $n 0))
  (True))
!(assertEqualToResult
  (match &after-lookup (runtime-counter ty-index-rebuild $n) (> $n 0))
  (True))

!(reset-runtime-stats!)
!(bind! &hash (new-space hash))
!(add-atom &hash (seen alpha))
!(add-atom-nodup &hash (seen alpha))
!(match &hash (seen alpha) found)
!(bind! &hash-stats (runtime-stats!))
!(assertEqualToResult
  (match &hash-stats (runtime-counter hash-space-exact-lookup $n) (> $n 0))
  (True))
!(assertEqualToResult
  (match &hash-stats (runtime-counter hash-space-exact-hit $n) (> $n 0))
  (True))
!(assertEqualToResult
  (match &hash-stats (runtime-counter subst-query-exact-shortcut $n) (> $n 0))
  (True))
