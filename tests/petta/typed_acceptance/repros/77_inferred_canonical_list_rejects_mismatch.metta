; Canonical list publication retains its element shape, so a Number list
; cannot satisfy a String list requirement in strict mode.

(= (collected-numbers)
   (collapse (superpose (1 2))))

(: consume-strings (-> (List String) Bool))
(= (consume-strings $values) True)

(: misuse-collected-numbers (-> Bool))
(= (misuse-collected-numbers)
   (consume-strings (collected-numbers)))
