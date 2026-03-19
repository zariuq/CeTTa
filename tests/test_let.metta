; Let bindings (sugar for case)
!(assertEqual (let $x 5 (+ $x 1)) 6)
!(assertEqual (let $x (+ 2 3) (* $x 2)) 10)

; Note: let with tuple pattern !(let ($a $b) ...) does not work in HE 0.2.10
; for destructuring — only variable patterns are supported by let
