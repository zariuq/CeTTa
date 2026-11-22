; The intrinsic classifier and an earlier explicit alternative may fail
; before a later explicit classifier proves the open obligation.

(= (get-type $value) Other)
(= (get-type foo) RuntimeOnly)

!(test
   (let $typed (the RuntimeOnly $value)
        (let foo $value $typed))
   foo)
