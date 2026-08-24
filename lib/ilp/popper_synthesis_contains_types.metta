; Prime declarations for the reachable reference-program image of Popper's
; synthesis-contains task at revision
; 5c608d3ec87e4975866042fe326e9a317277e021.

(: contains:c6
  (-> (value : popper:numeral) (u 0)))
(: contains:c9
  (-> (value : popper:numeral) (u 0)))
(: contains:target
  (-> (input : (list popper:numeral)) (u 0)))

(: contains-proof:c6 (contains:c6 popper:n6))
(: contains-proof:c9 (contains:c9 popper:n9))

(: contains-proof:at-6
  (-> (input : (list popper:numeral))
      (head : popper:numeral)
      (head-proof :
        (popper:list:head popper:numeral input head))
      (constant-proof : (contains:c6 head))
      (contains:target input)))
(: contains-proof:at-9
  (-> (input : (list popper:numeral))
      (head : popper:numeral)
      (head-proof :
        (popper:list:head popper:numeral input head))
      (constant-proof : (contains:c9 head))
      (contains:target input)))
(: contains-proof:recurse
  (-> (input : (list popper:numeral))
      (tail : (list popper:numeral))
      (tail-proof :
        (popper:list:tail popper:numeral input tail))
      (recursive-proof : (contains:target tail))
      (contains:target input)))
