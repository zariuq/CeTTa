; Circulant dia count. Node i links to the next 2 nodes.
; Nodes 4.

(edge n0 n1)
(edge n0 n2)
(edge n1 n2)
(edge n1 n3)
(edge n2 n3)
(edge n2 n0)
(edge n3 n0)
(edge n3 n1)

!(foldall + (match &self (, (edge $s $a) (edge $s $b) (edge $a $t) (edge $b $t)) 1) 0)
