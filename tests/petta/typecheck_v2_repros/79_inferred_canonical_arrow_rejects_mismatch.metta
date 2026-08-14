; A published Number closure cannot satisfy a String closure requirement.
; This negative control ensures arrow publication carries component shapes.

(: increment (-> Number Number))
(= (increment $value) (+ $value 1))

(= (choose-increment) increment)

(: consume-string-function (-> (-> String String) Bool))
(= (consume-string-function $function) True)

(: misuse-increment (-> Bool))
(= (misuse-increment)
   (consume-string-function (choose-increment)))
