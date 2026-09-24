; Circulant tri count. Node i links to the next 2 nodes.
; Nodes 7.

(edge n0 n1)
(edge n0 n2)
(edge n1 n2)
(edge n1 n3)
(edge n2 n3)
(edge n2 n4)
(edge n3 n4)
(edge n3 n5)
(edge n4 n5)
(edge n4 n6)
(edge n5 n6)
(edge n5 n0)
(edge n6 n0)
(edge n6 n1)

!(foldall + (match &self (, (edge $a $b) (edge $b $c) (edge $c $a)) 1) 0)
