; A uniquely declared function is a first-class value candidate.  Returning
; it from an undeclared producer publishes its closed arrow type.

(: increment (-> Number Number))
(= (increment $value) (+ $value 1))

(= (choose-increment) increment)

(: consume-increment (-> (-> Number Number) Bool))
(= (consume-increment $function) True)

(: use-increment (-> Bool))
(= (use-increment)
   (consume-increment (choose-increment)))
