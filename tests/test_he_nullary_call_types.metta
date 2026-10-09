; The type of a call without arguments comes from its operator's function type.
(: zero (-> Number))
(= (zero) 5)
!(get-type (zero))
!(zero)

; Each function type of the operator gives a type.
(: either (-> Number))
(: either (-> String))
!(get-type (either))

; A built-in operation without arguments.
!(get-type (print-mods!))

; An operator that needs an argument gives no type when called without one,
; and neither does a call with an argument too many.
(: one (-> Number Number))
!(collapse (get-type (one)))
!(collapse (get-type (zero 1)))

; An operator whose type is not a function type makes a tuple.
(: plain Number)
!(get-type (plain))

; An undeclared operator.
!(get-type (nobody))

; A call in argument position is typed without being evaluated.
(: loud (-> Number))
(= (loud) (chain (println! "evaluated") $_ 7))
(: wants-string (-> String String))
!(wants-string (loud))
