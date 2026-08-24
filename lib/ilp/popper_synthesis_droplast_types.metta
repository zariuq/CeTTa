; Prime declarations for Popper's synthesis-droplast task at revision
; 5c608d3ec87e4975866042fe326e9a317277e021.

(: droplast:target
  (-> (input : (list popper:numeral))
      (output : (list popper:numeral))
      (u 0)))

(: droplast-proof:base
  (-> (input : (list popper:numeral))
      (output : (list popper:numeral))
      (output-empty :
        (popper:list:empty popper:numeral output))
      (tail-proof :
        (popper:list:tail popper:numeral input output))
      (droplast:target input output)))

(: droplast-proof:step
  (-> (input : (list popper:numeral))
      (head : popper:numeral)
      (tail : (list popper:numeral))
      (tail-output : (list popper:numeral))
      (output : (list popper:numeral))
      (head-proof :
        (popper:list:head popper:numeral input head))
      (tail-proof :
        (popper:list:tail popper:numeral input tail))
      (recursive-proof : (droplast:target tail tail-output))
      (prepend-proof :
        (popper:list:prepend
          popper:numeral head tail-output output))
      (droplast:target input output)))
