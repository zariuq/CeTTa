; Regression: counting a collapsed native-space match should count bindings
; directly instead of materializing and re-evaluating a large result tuple.

!(import! &self list)

!(bind! &native-count (new-space native))
!(let $ignored
  (collapse
    (let $xs (eval (list:range 512))
      (let $x (superpose $xs)
        (add-atom &native-count (friend sam $x)))))
  ())

!(reset-runtime-stats!)
!(assertEqualToResult
  (size (collapse (match &native-count (friend $y $x) hit)))
  (512))
!(assertEqualToResult
  (size-atom (collapse (match &native-count (friend $y $x) hit)))
  (512))
!(bind! &stats (runtime-stats!))
!(assertEqualToResult
  (match &stats
         (runtime-counter eval-c-stack-guard-depth-peak $n)
         (< $n 64))
  (True))
