; A translatePredicate goal that CeTTa has no native view of is a Prolog
; call, as in PeTTa: it binds the goal's free variables, fails with no
; answer when Prolog fails, and yields one answer per Prolog solution.

; Result in the last argument.
!(test (progn (translatePredicate (atom_string abc $S)) $S) "abc")

; Output sink in the first argument, built with Predicate.
!(test (progn (translatePredicate
                (format_time (Predicate (string $S)) "%Y" 0)) $S)
       "1970")

; Parsing succeeds, or fails with no answer.
!(test (progn (translatePredicate (number_string $N "42")) $N) 42)
!(test (collapse (translatePredicate (number_string $N "forty-two"))) ())

; Codes to atom.
!(test (progn (translatePredicate (atom_codes $A (104 105))) $A) hi)

; Every Prolog solution is an answer.
!(test (collapse (progn (translatePredicate (between 1 3 $X)) $X)) (1 2 3))
