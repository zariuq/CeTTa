; Erasing a nominal value into an unconstrained requirement is admissible.
; The reverse direction remains forbidden: an unconstrained value is not
; evidence that a brand was introduced.

(: Count (Newtype Number))

(: accept-unknown (-> %Undefined% Number))
(= (accept-unknown $value) 1)

(: erase-count (-> Count Number))
(= (erase-count $value) (accept-unknown $value))
