!(import! &self list)

!(reset-runtime-stats!)
!(bind! &small-rows
   (collapse
     (let $xs (list:range 50)
       (let $i (superpose $xs)
         (+ $i 1)))))
!(bind! &small-stats (runtime-stats!))

!(reset-runtime-stats!)
!(bind! &rows
   (collapse
     (let $xs (list:range 5000)
       (let $i (superpose $xs)
         (+ $i 1)))))
!(bind! &stats (runtime-stats!))
!(assertEqualToResult (size-atom &small-rows) (50))
!(assertEqualToResult (size-atom &rows) (5000))
!(assertEqualToResult
  (match &stats (runtime-counter rename-vars $n) (< $n 50))
  (True))
!(assertEqualToResult
  ; Bound per-item work and fixed setup separately. Both streams permit
  ; nine merges per answer plus at most 64 for setup and observation.
  (match &small-stats (runtime-counter bindings-merge $n) (< $n (+ (* 9 50) 64)))
  (True))
!(assertEqualToResult
  (match &stats (runtime-counter bindings-merge $n) (< $n (+ (* 9 5000) 64)))
  (True))
!(assertEqualToResult
  ; Scaling by one hundred must not cost more than one hundred times the
  ; work. The size checks above also reject a cheap residual expression.
  (let $small (match &small-stats (runtime-counter bindings-merge $n) $n)
    (match &stats (runtime-counter bindings-merge $large) (<= $large (* 100 $small))))
  (True))
!(assertEqualToResult
  (match &stats (runtime-counter bindings-clone $n) (< $n 275000))
  (True))
!(assertEqualToResult
  (match &stats (runtime-counter bindings-apply $n) (< $n 15000))
  (True))
!(println! ok)
