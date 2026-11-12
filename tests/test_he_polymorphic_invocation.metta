; A schematic annotation is instantiated afresh for every invocation, so
; one call does not constrain an independent later call. Within one arrow
; the scheme variable stays shared. The expected output agrees with
; Hyperon Experimental 0.2.10.
(: ident (-> $t $t))
(= (ident $x) $x)
(: same (-> $t $t Atom))
(= (same $x $y) Yes)

!(ident 7)
!(ident True)
!(Pair (ident 7) (ident True))
!(Pair (ident True) (ident 7))
; A caller variable spelled like the scheme variable is unrelated to it.
!(let $t Number (Pair (ident 7) (ident True)))

!(same 7 8)
!(same True False)
!(same 7 True)
