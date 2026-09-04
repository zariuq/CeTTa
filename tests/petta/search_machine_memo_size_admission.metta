!(import! &self (library lib_memo))

(= (memo-size-boundary $value) (empty))
!(memoize memo-size-boundary)

; The public size-limit policy is term_size(Arguments) * 8.  A tagged
; integer therefore fits in this budget and begins one cache entry.
!(config-memoize (size-limit 0.00000003))
!(memo-size-boundary 72057594037927935)
!(memo-size-boundary 72057594037927935)
!(get-memoize-stats)

; The adjacent integer crosses SWI's tagged representation boundary.  Its
; indirect payload exceeds the same budget, so evaluation bypasses the cache.
!(clear-memoize)
!(memo-size-boundary 72057594037927936)
!(memo-size-boundary 72057594037927936)
!(get-memoize-stats)

; Size is determined by binary limbs rather than printed decimal length.  This
; two-limb integer fits in a 72-byte argument budget and begins an entry.
!(clear-memoize)
!(config-memoize (size-limit 0.000000067))
!(memo-size-boundary 1267650600228229401496703205377)
!(memo-size-boundary 1267650600228229401496703205377)
!(get-memoize-stats)

; Retention charges the canonical key plus the answer-list and answer/1
; constructors.  This complete ground entry is exactly 64 bytes.
!(clear-memoize)
(= (memo-ground-retention $value) $value)
!(memoize memo-ground-retention)
!(config-memoize
  (aggregate none)
  (size-limit 0.000000059604644775390625))
!(memo-ground-retention 7)
!(memo-ground-retention 7)
!(get-memoize-stats)

; A variant entry retains solved argument lists in answer/2 records.  Two
; answers plus its numbered-variable key occupy exactly 184 bytes.
!(clear-memoize)
(= (memo-variant-retention a) ok)
(= (memo-variant-retention b) ok)
!(memoize memo-variant-retention)
!(config-memoize
  (aggregate none)
  (size-limit 0.000000171363353729248046875))
!(collapse (memo-variant-retention $left))
!(collapse (memo-variant-retention $right))
!(get-memoize-stats)

; Aggregation retains one answer/1 result, not the unaggregated answer bag.
!(clear-memoize)
(= (memo-aggregate-retention $value) $value)
(= (memo-aggregate-retention $value) (+ $value 1))
!(memoize memo-aggregate-retention)
!(config-memoize
  (aggregate count)
  (size-limit 0.000000059604644775390625))
!(memo-aggregate-retention 7)
!(memo-aggregate-retention 7)
!(get-memoize-stats)
