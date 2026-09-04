; Importing an unresolved Prolog predicate with this spelling does not import
; lib_memo or grant its native controls.  It remains an ordinary PeTTa
; partial application, and the authored MeTTa equation remains callable.
!(import_prolog_function memoize)
!(memoize sq)
(= (sq $x) (* $x $x))
!(sq 3)
