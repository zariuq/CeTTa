; Operands for which the reference's host decides, and views of the read
; algebra.  CeTTa gives the term of the reference's space operations for
; every operand that is no space:
; - an unbound operand raises instantiation_error, in =../2's context for a
;   nonempty expression.  SWI-PeTTa raises the same without its MORK bridge,
;   except that get-atoms, like a match of an unbound pattern, reads every
;   predicate there; with the bridge loaded, the bridge's &mork clause, which
;   comes first, takes an unbound operand as &mork and writes there.
; - () is an expression, no space (is-space answers false in both), and is
;   refused as any expression is.  SWI-PeTTa's () is SWI-Prolog's [], which
;   =../2 accepts as a name: there a nonempty expression is added under it and
;   read back, while any other atom is refused.
; - a view, (, A B) or (| A B), reads in the extended profile and raises the
;   reference's term in the base profile; a write to it is refused in both.
!(add-atom &a (x 1))
!(add-atom &b (x 1))
!(add-atom &b (y 2))

; An unbound operand.
!(catch (add-atom $s (z 3)))
!(catch (add-atom $s z))
!(catch (remove-atom $s (z 3)))
!(catch (remove-atom $s "t"))
!(catch (import! $s lib/space_write_atoms))
!(catch (match $s (x $v) $v))
!(catch (match $s x x))
!(catch (get-atoms $s))

; The empty expression.
!(catch (add-atom () (z 3)))
!(catch (add-atom () (= (wf $x) $x)))
!(catch (add-atom () z))
!(catch (remove-atom () (z 3)))
!(catch (import! () lib/space_write_atoms))
!(catch (match () (z $v) $v))
!(catch (match () z z))
!(catch (get-atoms ()))
!(is-space ())

; Views: a read, and the refused writes.
!(collapse (catch (match (, &a &b) (x $v) $v)))
!(collapse (catch (match (| &a &b) $p $p)))
!(collapse (catch (match (, &a &b) (, (x $v) (y $w)) $v)))
!(catch (add-atom (, &a &b) (z 3)))
!(catch (remove-atom (| &a &b) (y 2)))

; The spaces are as they were.
!(collapse (match &a $p $p))
!(collapse (match &b $p $p))
!(collapse (wf 1))
!(collapse (item-successor 1))
