(= (before-install) (late 4))

(= (install-late)
   (let $_
        (add-atom &self (= (late $x) (+ $x 1)))
        installed))

!(install-late)

(= (after-install) (late 4))

!(before-install)
!(after-install)
!(eval (before-install))

(= (forward-left) (forward-right))
(= (forward-right) 42)

!(forward-left)

; Removing the final live clause unregisters the callable.  A source
; occurrence compiled from the document's predeclared head must then return
; to inert-data behavior rather than becoming search failure.
(= (removed-head) present)
!(remove-atom &self (= (removed-head) present))
!(removed-head)
