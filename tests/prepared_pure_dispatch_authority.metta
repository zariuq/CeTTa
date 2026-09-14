; Equation names cannot change a dialect-owned operation's interpretation
; merely because the call enters through a compiled producer.
(= (length $items) 77)
(= (last $items) 88)
(= (id $value) 99)
!(length (a b b))
!(let $items (a b b) (length $items))
!(last (a b b))
!(let $items (a b b) (last $items))
!(id token)
!(let $value token (id $value))

; Ordinary definitions, including their duplicate answers, stay relational.
(= (dispatch-count $items) (size-atom $items))
!(dispatch-count (a b b))
(= (dispatch-pick $items) same)
(= (dispatch-pick $items) same)
!(collapse (dispatch-pick (a b b)))
