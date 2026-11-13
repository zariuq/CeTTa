; HE's import! gives a module its own space and tokens.  The importing
; space reads it live together with its own atoms and the modules it
; imports, each once; listing and removal see the importer's own atoms.

; A repeated import adds nothing; a transitive import is read too.
!(import! &self mi_moda)
!(import! &self mi_moda)
!(collapse (match &self (a-fact $x) $x))
!(collapse (match &self (b-fact $x) $x))
!(collapse (get-atoms &self))
; Tokens bound by a module's code are its own.
!(+ &a-token 1)
; An own copy of an imported atom answers beside it; removal takes only the copy.
(a-fact 1)
!(collapse (match &self (a-fact $x) $x))
!(remove-atom &self (a-fact 1))
!(collapse (match &self (a-fact $x) $x))
!(remove-atom &self (a-fact 1))
!(collapse (match &self (a-fact $x) $x))

; A diamond: the shared module answers once.
!(import! &self mi_amod)
!(import! &self mi_bmod)
!(let $answers (collapse (match &self (fact $x) $x)) (sort-atom $answers))
; Updates through the module's code and through a handle to its space are
; seen by every importer.
!(add-a (new 1))
!(collapse (match &self (new $x) $x))
!(import! &am mi_amod)
!(add-atom &am (new 2))
!(let $answers (collapse (match &self (new $x) $x)) (sort-atom $answers))
!(let $answers (collapse (match &am (new $x) $x)) (sort-atom $answers))

; `&self` in a module's code is the module's space wherever it runs.
!(import! &self mi_selfmod)
!(add-here (stored 1))
!(count-here)
!(let $answers (collapse (match &self (stored $x) $x)) (sort-atom $answers))
; A module space prints by its name in the module tree.
!(here)
!(let $s &self $s)

; Functions and their declared types come from the module.
!(import! &self mi_mymod)
!(modf 5)
!(get-type modf)
(mod-fact 2)
!(let $answers (collapse (match &self (mod-fact $x) $x)) (sort-atom $answers))
!(collapse (get-atoms &self))
