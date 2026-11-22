; The same symbol has two distinct first-class value candidates.  Inference
; must not select one arbitrarily: the producer has no singleton output type,
; so strict mode rejects its attempted use as positive Number evidence.

(: dual Number)
(: dual (-> Number Number))
(= (dual $value) $value)

(= (choose-dual) dual)

(: consume-number (-> Number Bool))
(= (consume-number $value) True)

(: use-dual (-> Bool))
(= (use-dual)
   (consume-number (choose-dual)))
