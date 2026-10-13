; A judgment cached for one declaration revision must not answer for the next.
(= (k $x) ok)
(= (fact $x) (box $x))
(fact 1)
!(collapse (k "s"))
!(get-type (k "s"))
; Data mutation between calls: the program (declarations) is unchanged.
!(add-atom &self (fact 2))
!(collapse (k "s"))
; A declaration added at run time changes the judgment.
!(add-atom &self (: k (-> Number Symbol)))
!(get-type (k 1))
!(collapse (k "s"))
!(collapse (k 1))
; A declaration removed at run time changes it back.
!(remove-atom &self (: k (-> Number Symbol)))
!(collapse (k "s"))
!(get-type (k "s"))
; A declaration in a module space the program imports.
!(import! &self declaration_revision_lib)
!(collapse (k "s"))
!(collapse (k 1))
