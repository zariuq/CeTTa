; HE conformance: cons-atom and decons-atom
; Tests structural construction and deconstruction

; cons-atom: prepend head to tail
!(assertEqual (cons-atom a (b c)) (a b c))

; cons-atom: prepend to empty
!(assertEqual (cons-atom a ()) (a))

; decons-atom: split into head and tail
!(assertEqual (decons-atom (a b c)) (a (b c)))

; decons-atom: singleton
!(assertEqual (decons-atom (a)) (a ()))
