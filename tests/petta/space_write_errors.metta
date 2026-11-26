; A space operation whose space operand is no space raises the reference's
; term.  The reference keeps the atoms of a space under predicates named
; after the space, so an operand that is no symbol fails the first check the
; operation reaches: =../2's for a nonempty expression (a function definition
; and a declaration included), must_be/2's for any other atom, and
; current_predicate/1's for get-atoms and a match of an unbound pattern.
; Each caught term is compared in full, and the spaces stay as they were.
; The answers are SWI-PeTTa's.
!(add-atom &a (x 1))
!(add-atom &b (x 1))
!(add-atom &b (y 2))
(= (put $s $x) (add-atom $s $x))
(= (take $s $x) (remove-atom $s $x))
(= (num) 42)

; Writes to spaces land: directly, through a function, and into the space a
; symbol names, made by its first write.
!(add-atom &a (w 0))
!(remove-atom &a (w 0))
!(put &a (v 9))
!(take &a (v 9))
!(add-atom named-on-write (f 1))
!(collapse (match named-on-write (f $x) $x))
!(remove-atom named-on-write (f 1))
!(remove-atom never-written (f 1))

; add-atom of an expression: =../2's term, whatever the operand that is no
; symbol (a number, a string, an expression, a view of the read algebra, a
; function's value, a value bound by let or passed to a function).
!(catch (add-atom 42 (z 3)))
!(catch (add-atom 1.5 (z 3)))
!(catch (add-atom "str" (z 3)))
!(catch (add-atom (foo bar) (z 3)))
!(catch (add-atom (, &a &b) (z 3)))
!(catch (add-atom (| &a &b) (z 3)))
!(catch (add-atom (, (| &a &b) &a) (z 3)))
!(catch (add-atom (, &a) (z 3)))
!(catch (add-atom (num) (z 3)))
!(catch (let $v (, &a &b) (add-atom $v (q 5))))
!(catch (put (| &a &b) (z 3)))
; A function definition and a declaration are expressions too.
!(catch (add-atom 42 (= (wf $x) $x)))
!(catch (add-atom (, &a &b) (: wd Number)))
; Any other atom: must_be/2's term.
!(catch (add-atom 42 z))
!(catch (add-atom "str" 7))
!(catch (add-atom (foo bar) "t"))
!(catch (add-atom (| &a &b) ()))
; An unbound atom is refused before the space is read.
!(catch (add-atom 42 $x))
!(catch (add-atom &a $x))

; remove-atom: the same terms.
!(catch (remove-atom 42 (x 1)))
!(catch (remove-atom (, &a &b) (x 1)))
!(catch (remove-atom (| &a &b) (y 2)))
!(catch (remove-atom (foo bar) (= (wf $x) $x)))
!(catch (take (, &a &b) (x 1)))
!(catch (remove-atom 1.5 x))
!(catch (remove-atom (num) "t"))
; An unbound atom, or an equation whose function name is unbound, is refused
; before the space is read.
!(catch (remove-atom 42 $x))
!(catch (remove-atom (, &a &b) (= ($f) 1)))

; import! adds a document's atoms as add-atom adds one: an operand that is no
; space raises add-atom's term for the document's first atom, and the
; function the document defines stays undefined.  A document that is not
; found raises its own error first.
!(catch (import! 42 lib/space_write_atoms))
!(catch (import! (, &a &b) lib/space_write_atoms))
!(catch (import! "str" lib/space_write_atoms))
!(catch (import! 42 lib/no_such_document))
!(collapse (item-successor 1))

; Reads raise the same terms: match by its pattern, a conjunction by its
; first; get-atoms, and a match of an unbound pattern, by
; current_predicate/1's.  An empty conjunction reads nothing.
!(catch (match 42 (x $v) $v))
!(catch (match "str" x x))
!(catch (match (foo bar) $v $v))
!(catch (match 1.5 (, (x $v) (y $w)) $v))
!(catch (match 42 (, $p (x $v)) $v))
!(catch (match (foo bar) (, x (x $v)) $v))
!(catch (get-atoms 42))
!(catch (get-atoms (foo bar)))
!(match 42 (,) empty-conjunction)
; A template that is a dispatch decided at run time does not take the read's
; error: it is the template's handler, not the match's.
!(catch (match (foo bar) (x $v) ((q $v))))
!(catch (match "str" (x $v) ((q $v))))
!(collapse (match &a (x $v) ((q $v))))
; A space operation reached through a variable is a dispatch decided at run
; time: its error fails that branch alone.
!(collapse (let $f add-atom ($f 42 (z 1))))
!(collapse (let $f remove-atom ($f (, &a &b) (x 1))))
!(collapse (let $f match ($f (foo bar) (x $v) ((q $v)))))

; Library functions write through add-atom and remove-atom and read through
; match: each raises the term of the operation that meets the operand first.
!(import! &self (library lib_spaces))
!(import! &self (library lib_datastructures))
!(catch (migrateAtoms &a 42 (x $v)))
!(catch (migrateAtoms &a (, &a &b) (x $v)))
!(catch (migrateAtoms 42 &a (x $v)))
!(catch (remove-all-atoms 42))
!(catch (remove-all-atoms (foo bar)))
!(catch (add-unique-or-fail 42 foo))
!(catch (add-unique-or-fail (| &a &b) foo))

; The spaces are as they were.
!(collapse (match &a $p $p))
!(collapse (match &b $p $p))
!(collapse (wf 1))
