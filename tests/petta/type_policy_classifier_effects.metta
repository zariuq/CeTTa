; A failing classifier is not empty exhaustion: metatype cannot rescue it.
; Native parsing supplies the failure even without foreign arithmetic.
(= (get-type pcg-poison) (progn (println! CLASSIFIER-RAN) (parse ")")))
(: pcg-symbol (-> Symbol Atom))
(= (pcg-symbol $x) SHOULD-NOT-SUCCEED)
!(let (Error $reason $context) (catch (pcg-symbol pcg-poison)) $reason)
; A literal's intrinsic type is the first answer of its guard: the call
; commits to it, and the authored classifier after it does not run.
(= (get-type 6) (progn (println! EXACT-RAN) Number))
(: pcg-number (-> Number Atom))
(= (pcg-number $x) OK)
!(collapse (pcg-number 6))
