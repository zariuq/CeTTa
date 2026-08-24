; Prime declarations for Popper's synthesis-reverse task at revision
; 5c608d3ec87e4975866042fe326e9a317277e021.
;
; The background relations are structural over native `list` values.  Their
; proof constructors therefore cover every finite native list, rather than
; only the examples in the pinned corpus.

(: reverse:element (u 0))
(: reverse:a reverse:element)
(: reverse:b reverse:element)
(: reverse:c reverse:element)
(: reverse:d reverse:element)
(: reverse:e reverse:element)
(: reverse:f reverse:element)
(: reverse:n1 reverse:element)
(: reverse:n2 reverse:element)
(: reverse:n3 reverse:element)
(: reverse:n4 reverse:element)

(: reverse:target
  (-> (input : (list reverse:element))
      (output : (list reverse:element))
      (u 0)))

(: reverse-proof:base
  (-> (input : (list reverse:element))
      (output : (list reverse:element))
      (output-empty :
        (popper:list:empty reverse:element output))
      (input-empty :
        (popper:list:empty reverse:element input))
      (reverse:target input output)))
(: reverse-proof:step
  (-> (input : (list reverse:element))
      (head : reverse:element)
      (tail : (list reverse:element))
      (tail-reversed : (list reverse:element))
      (output : (list reverse:element))
      (head-proof :
        (popper:list:head reverse:element input head))
      (tail-proof :
        (popper:list:tail reverse:element input tail))
      (recursive-proof : (reverse:target tail tail-reversed))
      (append-proof :
        (popper:list:append-last
          reverse:element tail-reversed head output))
      (reverse:target input output)))
