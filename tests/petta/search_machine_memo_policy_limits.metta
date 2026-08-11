!(import_prolog_function
  (superpose
    (memoize config-memoize get-memoize-stats
     clear-memoize-stats)))

!(config-memoize (float 1))
!(memoize scaled)
(= (scaled $x) (* $x 10.0))

; Both arguments have the configured 1.2 key.  The first raw call is
; evaluated; the second replays that answer without re-unifying raw inputs.
!(scaled 1.21)
!(scaled 1.24)
!(get-memoize-stats)
!(clear-memoize-stats)

!(config-memoize (answer-limit 1))
!(memoize choose-limited)
(= (choose-limited $x) $x)
(= (choose-limited $x) (+ $x 1))
(= (choose-limited $x) (+ $x 2))

!(choose-limited 5)
!(choose-limited 5)
!(get-memoize-stats)

; Retention strategy is observable through work, never through answers.
; With capacity two, LRU discards the older hot key after two newer misses.
!(clear-memoize-stats)
!(config-memoize (strategy lru) (unique-limit 2))
!(memoize retain-policy)
(= (retain-policy $x) $x)
!(retain-policy 1)
!(retain-policy 1)
!(retain-policy 2)
!(retain-policy 3)
!(retain-policy 1)
!(get-memoize-stats)

; W-TinyLFU retains the frequently used key under the same access trace.
!(clear-memoize-stats)
!(config-memoize (strategy wtinylfu) (unique-limit 2))
!(retain-policy 1)
!(retain-policy 1)
!(retain-policy 2)
!(retain-policy 3)
!(retain-policy 1)
!(get-memoize-stats)

; A one-byte global cache budget admits no entry.  Repeated calls therefore
; remain correct but both execute as misses.
!(clear-memoize-stats)
!(config-memoize
  (strategy lru)
  (unique-limit 10)
  (size-limit 0.000000001))
!(retain-policy 7)
!(retain-policy 7)
!(get-memoize-stats)
