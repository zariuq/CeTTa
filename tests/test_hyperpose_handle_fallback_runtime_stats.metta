; Worker-local handles must not escape through threaded hyperpose. A branch
; that can produce a fresh space forces the cooperative fallback path.

!(reset-runtime-stats!)
!(pragma! hyperpose-threads 2)
!(case (collapse (hyperpose ((new-space) ok)))
  ((($s ok) ())
   ($_ (Error hyperpose unexpected-handle-frontier))))
!(bind! &hyperpose-handle-stats (runtime-stats!))
!(assertEqualToResult
  (match &hyperpose-handle-stats
    (runtime-counter hyperpose-cooperative-fallback $n)
    (> $n 0))
  (True))
