; foldall should accept relational generators with free variables.

(= (g 1)
   2)
(= (g 2)
   3)

(= (merge $A $B)
   (+ $A $B))

!(test (foldall merge (g $x) 0)
       5)
