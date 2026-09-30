; A failing classifier is not empty exhaustion: metatype cannot rescue it.
(= (get-type pcg-poison) (progn (println! CLASSIFIER-RAN) (/ 1 0)))
(: pcg-symbol (-> Symbol Atom))
(= (pcg-symbol $x) SHOULD-NOT-SUCCEED)
!(let (Error $reason $context) (catch (pcg-symbol pcg-poison)) $reason)
; An intrinsic proof does not skip an authored classifier's effect or answer.
(= (get-type 6) (progn (println! EXACT-RAN) Number))
(: pcg-number (-> Number Atom))
(= (pcg-number $x) OK)
!(collapse (pcg-number 6))
