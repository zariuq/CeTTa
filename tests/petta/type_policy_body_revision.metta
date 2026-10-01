; An arrow edit retranslates affected equation bodies as well as later
; calls. Adding an Atom codomain holds the body; removing it restores execution.
(: pcg-retained (-> Number %Undefined%))
(= (pcg-retained $x) (+ $x 1))
!(pcg-retained 6)
!(add-atom &self (: pcg-retained (-> Number Atom)))
!(collapse (pcg-retained 6))
!(remove-atom &self (: pcg-retained (-> Number Atom)))
!(collapse (pcg-retained 6))
