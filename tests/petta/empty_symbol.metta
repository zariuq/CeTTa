; The empty name is a symbol, the identity of name concatenation.  The reader
; has no spelling for it; operations on names produce it.
!(atom_concat "" "")
!(get-metatype (atom_concat "" ""))
!(let $e (atom_concat "" "") (== $e (atom_concat "" "")))
!(let $e (atom_concat "" "") (atom_concat $e x))
!(let $e (atom_concat "" "") (atom_concat x $e))
!(atom_concat a "")
!(atom_concat "" b)
; it is stored, matched and passed on like any symbol
!(let $e (atom_concat "" "") (add-atom &self (named $e)))
!(match &self (named $n) (atom_concat $n done))
!(chain (atom_concat "" "") $e survived)
!(let $e (atom_concat "" "") (== $e x))
