; Proof-relevant structural relations shared by Popper's native-list tasks.
; They are ordinary level-polymorphic Prime families, not privileged runtime
; operators.  A task may use any subset without changing the `list` family.

(: popper:list:empty
  (-> (element : (u $element-level))
      (list-value : (list element))
      (u $element-level)))
(: popper:list:head
  (-> (element : (u $element-level))
      (list-value : (list element))
      (head-value : element)
      (u $element-level)))
(: popper:list:tail
  (-> (element : (u $element-level))
      (list-value : (list element))
      (tail-value : (list element))
      (u $element-level)))
(: popper:list:prepend
  (-> (element : (u $element-level))
      (head-value : element)
      (tail-value : (list element))
      (result : (list element))
      (u $element-level)))
(: popper:list:append-last
  (-> (element : (u $element-level))
      (prefix : (list element))
      (last : element)
      (result : (list element))
      (u $element-level)))
(: popper:list:member
  (-> (element : (u $element-level))
      (list-value : (list element))
      (member-value : element)
      (u $element-level)))

(: popper:list:proof:empty
  (-> (element : (u $element-level))
      (popper:list:empty element (list:nil element))))
(: popper:list:proof:head
  (-> (element : (u $element-level))
      (head : element)
      (tail : (list element))
      (popper:list:head element
        (list:cons element head tail) head)))
(: popper:list:proof:tail
  (-> (element : (u $element-level))
      (head : element)
      (tail : (list element))
      (popper:list:tail element
        (list:cons element head tail) tail)))
(: popper:list:proof:prepend
  (-> (element : (u $element-level))
      (head : element)
      (tail : (list element))
      (popper:list:prepend element head tail
        (list:cons element head tail))))
(: popper:list:proof:member-here
  (-> (element : (u $element-level))
      (head : element)
      (tail : (list element))
      (popper:list:member element
        (list:cons element head tail) head)))
(: popper:list:proof:member-there
  (-> (element : (u $element-level))
      (head : element)
      (tail : (list element))
      (member-value : element)
      (tail-proof :
        (popper:list:member element tail member-value))
      (popper:list:member element
        (list:cons element head tail) member-value)))

(: popper:list:proof:append-last-empty
  (-> (element : (u $element-level))
      (last : element)
      (popper:list:append-last element
        (list:nil element)
        last
        (list:cons element last (list:nil element)))))
(: popper:list:proof:append-last-step
  (-> (element : (u $element-level))
      (head : element)
      (tail : (list element))
      (last : element)
      (tail-result : (list element))
      (tail-proof :
        (popper:list:append-last
          element tail last tail-result))
      (popper:list:append-last element
        (list:cons element head tail)
        last
        (list:cons element head tail-result))))
