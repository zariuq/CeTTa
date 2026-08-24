; Prime declarations for Popper's synthesis-next task at revision
; 5c608d3ec87e4975866042fe326e9a317277e021.

(: next:element (u 0))
(: next:a next:element)
(: next:b next:element)
(: next:c next:element)
(: next:d next:element)
(: next:e next:element)
(: next:f next:element)
(: next:g next:element)
(: next:h next:element)
(: next:i next:element)
(: next:j next:element)
(: next:k next:element)
(: next:x next:element)

; The pinned background exposes eleven distinct unary predicates.  Keeping
; them distinct preserves the authored hypothesis space even though the
; learned program below uses only `next:is-x`.
(: next:is-a (-> (value : next:element) (u 0)))
(: next:is-b (-> (value : next:element) (u 0)))
(: next:is-c (-> (value : next:element) (u 0)))
(: next:is-d (-> (value : next:element) (u 0)))
(: next:is-e (-> (value : next:element) (u 0)))
(: next:is-f (-> (value : next:element) (u 0)))
(: next:is-g (-> (value : next:element) (u 0)))
(: next:is-h (-> (value : next:element) (u 0)))
(: next:is-i (-> (value : next:element) (u 0)))
(: next:is-j (-> (value : next:element) (u 0)))
(: next:is-x (-> (value : next:element) (u 0)))

(: next-proof:is-a (next:is-a next:a))
(: next-proof:is-b (next:is-b next:b))
(: next-proof:is-c (next:is-c next:c))
(: next-proof:is-d (next:is-d next:d))
(: next-proof:is-e (next:is-e next:e))
(: next-proof:is-f (next:is-f next:f))
(: next-proof:is-g (next:is-g next:g))
(: next-proof:is-h (next:is-h next:h))
(: next-proof:is-i (next:is-i next:i))
(: next-proof:is-j (next:is-j next:j))
(: next-proof:is-x (next:is-x next:x))

(: next:target
  (-> (input : (list next:element))
      (following : next:element)
      (u 0)))

(: next-proof:adjacent
  (-> (input : (list next:element))
      (marker : next:element)
      (tail : (list next:element))
      (following : next:element)
      (marker-head :
        (popper:list:head next:element input marker))
      (marker-is-x : (next:is-x marker))
      (tail-proof :
        (popper:list:tail next:element input tail))
      (following-head :
        (popper:list:head next:element tail following))
      (next:target input following)))

(: next-proof:scan
  (-> (input : (list next:element))
      (tail : (list next:element))
      (following : next:element)
      (tail-proof :
        (popper:list:tail next:element input tail))
      (recursive-proof : (next:target tail following))
      (next:target input following)))
