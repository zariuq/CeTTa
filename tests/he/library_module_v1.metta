; The standard library is a module that &self imports.  Its atoms answer
; queries after the program's own atoms; listing and removal through &self
; see only the program's own atoms.
!(get-type eval)
(: eval (-> Atom %Undefined%))
!(get-type eval)
!(eval (superpose ((+ 1 2))))
!(remove-atom &self (: eval (-> Atom %Undefined%)))
!(remove-atom &self (: eval (-> Atom Atom)))
!(get-type eval)
!(eval (superpose ((+ 1 2))))
!(match &self (: eval $t) $t)
!(collapse (get-atoms &self))
(: own-f (-> Atom Atom))
!(collapse (get-atoms &self))
; An own copy of a library declaration is removable; the library's stays.
(: eval (-> Atom Atom))
!(collapse (get-type eval))
!(remove-atom &self (: eval (-> Atom Atom)))
!(collapse (get-type eval))
!(collapse (get-atoms &self))
; Own equations and declarations answer before the library's.
(= (id $x) (own-id $x))
!(collapse (id 5))
!(collapse (match &self (= (id 7) $r) $r))
(: if (-> Number Number Number Number))
!(collapse (match &self (: if (-> Number $a $b $c)) $a))
!(if True ok no)
; An own equation answers before an imported one, wildcard head or not.
(= ($f 5) (wild $f))
!(collapse (id 5))
!(collapse (match &self (= (id 5) $r) $r))
