; An error is raised where it occurs.  catch makes it a value, the
; reference's error term; a dispatch decided at run time fails its own
; branch (swi_differences, entries 1-2); an Error written as data is a
; value.  The answers are SWI-PeTTa's.

; The list builtins raise type_error(list, X) for a non-list and
; instantiation_error for an unbound or partial list, in their predicate's
; context.
!(catch (length a))
!(catch (length "abc"))
!(catch (msort a))
!(catch (msort $x))
!(catch (sort-atom $x))
!(sort-atom a)
!(catch (let $l (cons-atom 1 $t) (msort $l)))
!(catch (list_to_set 5))
!(catch (list_to_set $x))
!(size-atom a)

; sread and parse read a string, a symbol or a number, and raise a syntax
; error for text they cannot read.
!(catch (sread "(a b"))
!(catch (parse ")"))
!(catch (sread $x))
!(sread 5)
!(sread abc)

; Arithmetic raises is/2's errors.
!(catch (pow-math 0 -1))
!(catch (asin-math 2.0))
!(catch (sqrt-math -1.0))
!(catch (+ a 1))

; A raise leaves collapse and is caught outside it; caught inside, it is
; one of the collected values.
!(catch (collapse (sqrt-math -1.0)))
!(collapse (catch (sqrt-math -1.0)))

; A dispatch decided at run time fails the branch that raised.
(= (boom $x) (sqrt-math $x))
!(collapse (let $f boom ($f -1.0)))
!(let $f boom ($f 4.0))
!(map-atom (4.0 -1.0 9.0) sqrt-math)
!(collapse (map-atom (4.0 9.0) sqrt-math))

; foldall applies its step and runs its generator through reduce: a raising
; step leaves the state, and a raising generator ends it.
(edge a b)
(edge b c)
(edge b d)
!(foldall % (match &self (edge $x $y) 4) 23)
(= (explode $x) (superpose 1 2))
!(foldall + (match &self (edge $x $y) (explode $x)) 0)

; A value is not run again: an observer of a matched value that holds a
; call raises nothing.
(Rule (And (Rectangle $r) (CPU F (* $l $w) $a)))
!(match &self (Rule $t) (is-var $t))
!(match &self (Rule $t) (is-ground $t))
!(match &self (Rule $t) (is-expr $t))

; An Error written as data is a value, and the run goes on.
!(let $e (Error foo bar) $e)
!(+ 1 1)
