!(import! &self list)
!(import! &self mork)

!(reset-runtime-stats!)
!(bind! &rows
   (collapse
     (let $xs (eval (list:range 500))
       (let $i (superpose $xs)
         (friend sam $i)))))
!(bind! &stats (runtime-stats!))
!(assertEqualToResult
  (match &stats (runtime-counter query-equation-candidates $n) (< $n 10))
  (True))
!(assertEqualToResult
  (match &stats (runtime-counter rename-vars $n) (< $n 50))
  (True))
