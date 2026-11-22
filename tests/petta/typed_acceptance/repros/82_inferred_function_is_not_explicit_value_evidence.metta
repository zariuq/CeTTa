; An inferred function signature is not itself an explicit first-class value
; declaration.  Returning that symbol does not manufacture positive closure
; evidence for another inferred signature.

(= (inferred-increment $value) (+ $value 1))
(= (choose-increment) inferred-increment)

(: consume-increment (-> (-> Number Number) Bool))
(= (consume-increment $function) True)

(: use-increment (-> Bool))
(= (use-increment)
   (consume-increment (choose-increment)))
