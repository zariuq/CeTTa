; v2's written eval/quote pair reactivates source, not a returned value.
!(eval (quote (+ 1 2)))
!(quote (eval (quote (+ 1 2))))
(= (keep-code $code) (eval (quote $code)))
!(keep-code (quote (+ 4 5)))

(name "bob")
(: checked-name (-> String))
(= (checked-name) (the String (match &self (name $n) $n)))
!(checked-name)
(= (bad-age) (+ (the Number (match &self (name $n) $n)) 1))
!(car-atom (catch (eval (quote (bad-age)))))

; The body below needs sequential cons-pattern translation and must not be
; taken over by the optional collection operation of the same name.
(: collect (-> (List Number) Number))
(= (collect $xs)
   (let* (($items $xs) ((cons $first $rest) $items))
      (+ $first (size-atom $rest))))
!(collect (1 2 3))
