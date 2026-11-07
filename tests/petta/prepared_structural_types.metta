; A typed head whose formal is a structural type its trusted version
; certifies runs on register code: a call from elsewhere checks its argument
; at the formal once, and the head's calls to itself keep it.
(: Tree Type)
(: Leaf Tree)
(: Node (-> Tree Tree Tree))
(: mk (-> Number Tree))
(= (mk $n) (if (== $n 0) Leaf (Node (mk (- $n 1)) (mk (- $n 1)))))
(: tsize (-> Tree Number))
(= (tsize Leaf) 1)
(= (tsize (Node $l $r)) (+ 1 (+ (tsize $l) (tsize $r))))
!(tsize (mk 6))
; An argument that is no Tree: the formal's check answers nothing.
!(collapse (tsize (Node 1 2)))
!(collapse (tsize (Node Leaf (Node Leaf 3))))
; Another head calls the typed one, which checks the argument it is given.
(= (twice $t) (+ (tsize $t) (tsize $t)))
!(twice (Node Leaf Leaf))
!(collapse (twice (Node Leaf 7)))
; A structural range no version proves keeps its check: 5 is no Tree.
(: mkbad (-> Number Tree))
(= (mkbad $n) (if (== $n 0) Leaf 5))
!(collapse (tsize (mkbad 0)))
!(collapse (tsize (mkbad 1)))
; A %Undefined% parameter beside a structural one is asked nothing.
(: tagged (-> Tree %Undefined% Number))
(= (tagged Leaf $w) 1)
(= (tagged (Node $l $r) $w) (+ (tagged $l $w) (tagged $r $w)))
!(tagged (mk 3) anything)
; A range the version does not prove, or one left unchecked, keeps the head
; off the machine; its answers are the protocol's.
(: tlabel (-> Tree Number))
(= (tlabel Leaf) "leaf")
(= (tlabel (Node $l $r)) (+ (tlabel $l) (tlabel $r)))
!(collapse (tlabel (mk 2)))
(: tleft (-> Tree %Undefined%))
(= (tleft Leaf) leaf)
(= (tleft (Node $l $r)) (tleft $l))
!(tleft (mk 4))
