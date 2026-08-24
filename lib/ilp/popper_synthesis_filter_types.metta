; Prime declarations for Popper's synthesis-filter task at revision
; 5c608d3ec87e4975866042fe326e9a317277e021.
;
; The pinned background defines parity by ten explicit facts.  These proof
; constants reproduce that authored relation exactly; no arithmetic oracle is
; introduced by the conversion.

(: filter:element (u 0))
(: filter:n1 filter:element)
(: filter:n2 filter:element)
(: filter:n3 filter:element)
(: filter:n4 filter:element)
(: filter:n5 filter:element)
(: filter:n6 filter:element)
(: filter:n7 filter:element)
(: filter:n8 filter:element)
(: filter:n9 filter:element)
(: filter:n10 filter:element)

(: filter:odd
  (-> (value : filter:element) (u 0)))
(: filter:even
  (-> (value : filter:element) (u 0)))
(: filter:target
  (-> (input : (list filter:element))
      (output : (list filter:element))
      (u 0)))

(: filter-proof:odd-1 (filter:odd filter:n1))
(: filter-proof:odd-3 (filter:odd filter:n3))
(: filter-proof:odd-5 (filter:odd filter:n5))
(: filter-proof:odd-7 (filter:odd filter:n7))
(: filter-proof:odd-9 (filter:odd filter:n9))
(: filter-proof:even-2 (filter:even filter:n2))
(: filter-proof:even-4 (filter:even filter:n4))
(: filter-proof:even-6 (filter:even filter:n6))
(: filter-proof:even-8 (filter:even filter:n8))
(: filter-proof:even-10 (filter:even filter:n10))

(: filter-proof:base
  (-> (input : (list filter:element))
      (output : (list filter:element))
      (input-empty :
        (popper:list:empty filter:element input))
      (output-empty :
        (popper:list:empty filter:element output))
      (filter:target input output)))
(: filter-proof:odd-step
  (-> (input : (list filter:element))
      (head : filter:element)
      (tail : (list filter:element))
      (output : (list filter:element))
      (head-proof :
        (popper:list:head filter:element input head))
      (odd-proof : (filter:odd head))
      (tail-proof :
        (popper:list:tail filter:element input tail))
      (recursive-proof : (filter:target tail output))
      (filter:target input output)))
(: filter-proof:even-step
  (-> (input : (list filter:element))
      (head : filter:element)
      (tail : (list filter:element))
      (tail-output : (list filter:element))
      (output : (list filter:element))
      (tail-proof :
        (popper:list:tail filter:element input tail))
      (head-proof :
        (popper:list:head filter:element input head))
      (even-proof : (filter:even head))
      (recursive-proof : (filter:target tail tail-output))
      (prepend-proof :
        (popper:list:prepend
          filter:element head tail-output output))
      (filter:target input output)))
