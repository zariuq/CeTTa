; A newtype may be eliminated only into a context accepted by its concrete
; representation.  Number therefore cannot be laundered into String merely
; by passing through the Count brand.

(: Count (Newtype Number))

(: need-string (-> String Number))
(= (need-string $value) 1)

(: invalid-elimination (-> Count Number))
(= (invalid-elimination $value) (need-string $value))
