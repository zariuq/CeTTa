; A compiled caller must observe edits to its standard dependencies.
(= (plus-three $x) (+ $x 3))
!(plus-three 2)
!(add-atom &self (= (+ 2 3) 99))
!(collapse (plus-three 2))
!(std:+ 2 3)
!(let $definition (std:definitions +) (remove-atom &self $definition))
!(plus-three 2)
!(+ 1 3)
!(let $definition (std:definitions +) (add-atom &self $definition))
!(collapse (plus-three 2))
!(+ 1 3)
!(remove-atom &self (= (+ 2 3) 99))
!(plus-three 2)

; A public checker can be extended as a program. Its answers do not replace
; the native checking step used to publish a theorem.
(hol:profile base)
(= (type:check $proof $type) True)
!(collapse (type:check (refl (u 0)) (id (u 1) (u 0) (u 1))))
!(std:type:check (refl (u 0)) (id (u 1) (u 0) (u 1)))
!(type:theorem false-equality (id (u 1) (u 0) (u 1)) (refl (u 0)))

; Recovery remains possible after removing the mutation API itself.
!(std:let $definition (std:definitions add-atom)
   (std:remove-atom &self $definition))
!(let $result (add-atom &self (api-marker ignored))
   (match &self (api-marker $value) $value))
!(std:let $definition (std:definitions add-atom)
   (std:add-atom &self $definition))
!(add-atom &self (api-marker restored))
!(match &self (api-marker $value) $value)
