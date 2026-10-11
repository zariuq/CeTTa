; Disabled builtin spellings remain data and may have authored definitions.
!(get-type once)
! once
(: once ProfileData)
!(get-type once)
! once
(: once (-> Atom Atom))
(= (once $x) (Authored $x))
!(once (+ 1 2))
!(get-type collect)
! collect
