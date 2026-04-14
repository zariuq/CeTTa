!(import! &self list)

!(reset-runtime-stats!)
!(bind! &rows
   (collapse
     (let $xs (eval (list:range 5000))
       (let $i (superpose $xs)
         (+ $i 1)))))
!(bind! &stats (runtime-stats!))
!(assertEqualToResult
  (match &stats (runtime-counter rename-vars $n) (< $n 50))
  (True))
!(assertEqualToResult
  (match &stats (runtime-counter bindings-merge $n) (< $n 20))
  (True))
!(assertEqualToResult
  (match &stats (runtime-counter bindings-clone $n) (< $n 20000))
  (True))
!(assertEqualToResult
  (match &stats (runtime-counter bindings-apply $n) (< $n 15000))
  (True))
!(println! ok)
