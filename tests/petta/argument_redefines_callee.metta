; The argument changes the definition before the enclosing dispatch.
; Its observable effect happens once across prepared/canonical handoff.
(= (redefined-head 7) Before)
(= (change-head)
   (progn
     (remove-atom &self (= (redefined-head 7) Before))
     (add-atom &self (= (redefined-head 7) After))
     (println! argument-evaluated)
     7))
!(redefined-head (change-head))
!(match &self (= (redefined-head 7) $body) $body)
