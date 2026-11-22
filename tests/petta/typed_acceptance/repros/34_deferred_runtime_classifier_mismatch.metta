; Negative mirror: an open ascription may not become an established receipt
; merely because its rigid value is outside the native checker's knowledge.

(= (get-type foo) RuntimeOnly)

!(let $typed (the RuntimeOnly $value)
      (let bar $value $typed))
