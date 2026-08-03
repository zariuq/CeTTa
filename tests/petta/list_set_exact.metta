; Stable exact-identity list operations are distinct from alpha-set and
; answer-stream operations.
!(test (list_to_set (b a b c a)) (b a c))
!(test (exclude-item a (b a a c a)) (b c))
!(test (list_to_set (append (a b) (a c))) (a b c))
!(test (exclude-item (+ 1 1) (1 2 2 3)) (1 3))

; Repeated occurrences of one variable are identical, while a distinct
; variable with the same printed role remains a separate list element.
!(test (=alpha
    (list_to_set ((tag $x) (tag $y) (tag $x)))
    ((tag $a) (tag $b)))
  True)
!(test (=alpha
    (exclude-item (tag $x)
      ((tag $x) (tag $y) (tag $x)))
    ((tag $y)))
  True)

; A nonmatching exact item leaves the list and its order unchanged.
!(test (=alpha
    (exclude-item (tag $x)
      ((tag $y) (other $x) (tag $z)))
    ((tag $y) (other $x) (tag $z)))
  True)
