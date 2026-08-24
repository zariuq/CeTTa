; Prime declarations for Popper's synthesis-finddupl task at revision
; 5c608d3ec87e4975866042fe326e9a317277e021.

(: finddupl:target
  (-> (input : (list popper:numeral))
      (duplicate : popper:numeral)
      (u 0)))

(: finddupl-proof:direct
  (-> (input : (list popper:numeral))
      (tail : (list popper:numeral))
      (duplicate : popper:numeral)
      (tail-proof :
        (popper:list:tail popper:numeral input tail))
      (member-proof :
        (popper:list:member popper:numeral tail duplicate))
      (head-proof :
        (popper:list:head popper:numeral input duplicate))
      (finddupl:target input duplicate)))

(: finddupl-proof:recurse
  (-> (input : (list popper:numeral))
      (tail : (list popper:numeral))
      (duplicate : popper:numeral)
      (tail-proof :
        (popper:list:tail popper:numeral input tail))
      (recursive-proof : (finddupl:target tail duplicate))
      (finddupl:target input duplicate)))
