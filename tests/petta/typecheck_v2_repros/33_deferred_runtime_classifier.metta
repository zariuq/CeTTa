; A type ascription over an open value installs the live get-type guard.  The
; guard accepts after the value is bound to a classifier-supported atom.

(= (get-type foo) RuntimeOnly)

!(test
   (let $typed (the RuntimeOnly $value)
        (let foo $value $typed))
   foo)
