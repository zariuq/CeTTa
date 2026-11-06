!(import! &self (library py))
!(bind! &math (py:import "math"))
!(py:bind! &self math:sqrt (py:get &math "sqrt"))
!(math:sqrt 81)

!(let $round (py:get (py:import "builtins") "round")
   ($round 1.234 (py:kwargs ("ndigits" 2))))

!(bind! &values (py:list 2 3 5))
!(py:call (py:get &values "append") 7)
!(py:to-tuple &values)

(= (read-lines $file) (py:iter $file))
!(py:with
   (py:call (py:get (py:import "io") "StringIO") "first\nsecond\n")
   read-lines)
