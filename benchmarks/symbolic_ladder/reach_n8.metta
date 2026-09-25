; Recursive reaching definitions as an ordered bag. Parallel edges
; from 1 to 2 keep duplicate derivations. The kill is observed and is
; not subtracted. Nodes 8, copies 2.

(gen 1 x)
(edge 1 2)
(edge 1 2)
(edge 2 3)
(edge 3 4)
(edge 4 5)
(edge 5 6)
(edge 6 7)
(edge 7 8)
(kill 8 x)

(= (live $n $v) (match &self (gen $n $v) (hit $n $v)))
(= (live $n $v)
   (match &self (edge $s $n)
     (let (hit $s $v) (live $s $v) (hit $n $v))))

!(live 1 $v)
!(live 2 $v)
!(live 3 $v)
!(live 4 $v)
!(live 5 $v)
!(live 6 $v)
!(live 7 $v)
!(live 8 $v)
!(live 8 y)
!(match &self (kill $k $v) (killed $k $v))
