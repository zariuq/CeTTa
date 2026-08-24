; Prime typing presentation for the strictly-positive native family `list`.
; The explicit element argument is retained in intrinsic syntax.  A licensed
; runtime representation erases `list:nil A` to `()` and
; `list:cons A head tail` to the existing flat expression representation.

(: list
  (-> (element : (u $element-level))
      (u $element-level)))

(: list:nil
  (-> (element : (u $element-level))
      (list element)))

(: list:cons
  (-> (element : (u $element-level))
      (head : element)
      (tail : (list element))
      (list element)))

(: list:eliminate
  (-> (element : (u $element-level))
      (motive :
        (-> (xs : (list element))
            (u $motive-level)))
      (nil-case : (motive (list:nil element)))
      (cons-case :
        (-> (head : element)
            (tail : (list element))
            (induction : (motive tail))
            (motive (list:cons element head tail))))
      (xs : (list element))
      (motive xs)))
