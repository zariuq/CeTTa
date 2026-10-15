; Earned structures survive storage. Returned code stays inert until eval,
; and pending arithmetic still runs when its value is explicitly demanded.
(= (stored-ready:effect) (println! STORED_READY_EFFECT))
(= (stored-ready:computed) (let $_ (println! STORED_READY_COMPUTED) (Payload 7)))
!(let $v (parse "(stored-ready:effect)") (add-atom &self (SavedReadyCode $v)))
!(match &self (SavedReadyCode $v) $v)
!(println! BEFORE_READY_ACTIVATION)
!(match &self (SavedReadyCode $v) (eval $v))
!(let $v (stored-ready:computed) (add-atom &self (SavedReadyValue $v)))
!(match &self (SavedReadyValue (Payload 7)) matched)
!(match &self (SavedReadyValue $v) $v)
(= (stored-ready:save $v) (add-atom &self (SavedPending $v)))
!(stored-ready:save (+ 1 2))
!(match &self (SavedPending $v) $v)
