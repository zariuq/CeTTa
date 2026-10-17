; Under PeTTa 1.1.c a declared argument type is a contract checked before an
; equation is selected.  A closed value the contract refuses raises its
; error with the decons-atom entry guard as without it: the guard skips an
; equation only where trying it could neither answer nor raise.
(: cnt (-> Expression Number))
(= (cnt ()) 0)
(= (cnt $xs) (let ($h $t) (decons-atom $xs) (+ 1 (cnt $t))))
!(println! (collapse (catch (cnt foo))))
!(println! (collapse (catch (cnt 5))))
!(println! (collapse (catch (cnt "abc"))))
!(println! (collapse (catch (cnt ()))))
!(println! (collapse (catch (cnt (a b)))))
