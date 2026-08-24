; Prime declarations for Popper's synthesis-length task at
; logic-and-learning-lab/Popper revision
; 5c608d3ec87e4975866042fe326e9a317277e021.

; Popper writes list elements and integers as Prolog numerals.  Their authored
; types remain distinct here: eN is an element and nN is an integer.
(: element (u 0))
(: integer (u 0))
(: e1 element)
(: e2 element)
(: e3 element)
(: e4 element)
(: e5 element)
(: e7 element)
(: n0 integer)
(: n1 integer)
(: n2 integer)
(: n3 integer)
(: n4 integer)
(: n5 integer)
(: n6 integer)
(: n7 integer)
(: n8 integer)
(: n9 integer)
(: n10 integer)

; Background and target predicates are proof-relevant indexed families.
(: bk:empty
  (-> (list-value : (list element)) (u 0)))
(: bk:tail
  (-> (list-value : (list element))
      (tail-value : (list element))
      (u 0)))
(: bk:zero
  (-> (value : integer) (u 0)))
(: bk:succ
  (-> (earlier : integer) (later : integer) (u 0)))
(: target:length
  (-> (list-value : (list element))
      (length-value : integer)
      (u 0)))

(: length-proof:empty
  (bk:empty (list:nil element)))
(: length-proof:tail
  (-> (head : element)
      (tail : (list element))
      (bk:tail (list:cons element head tail) tail)))
(: length-proof:zero (bk:zero n0))
(: length-proof:succ-0-1 (bk:succ n0 n1))
(: length-proof:succ-1-2 (bk:succ n1 n2))
(: length-proof:succ-2-3 (bk:succ n2 n3))
(: length-proof:succ-3-4 (bk:succ n3 n4))
(: length-proof:succ-4-5 (bk:succ n4 n5))
(: length-proof:succ-5-6 (bk:succ n5 n6))
(: length-proof:succ-6-7 (bk:succ n6 n7))
(: length-proof:succ-7-8 (bk:succ n7 n8))
(: length-proof:succ-8-9 (bk:succ n8 n9))
(: length-proof:succ-9-10 (bk:succ n9 n10))

(: length-proof:base
  (-> (list-value : (list element))
      (length-value : integer)
      (zero-proof : (bk:zero length-value))
      (empty-proof : (bk:empty list-value))
      (target:length list-value length-value)))

(: length-proof:step
  (-> (list-value : (list element))
      (tail-value : (list element))
      (previous : integer)
      (length-value : integer)
      (tail-proof : (bk:tail list-value tail-value))
      (recursive-proof : (target:length tail-value previous))
      (successor-proof : (bk:succ previous length-value))
      (target:length list-value length-value)))
