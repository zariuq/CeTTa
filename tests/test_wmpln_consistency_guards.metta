; lib_wmpln's consistency checks divide by the probabilities they test.  They guard
; with `if`: `and` evaluates both of its operands, so an `and` guard let an
; integer zero reach the division, which raised (Error (/ 0 0) DivisionByZero),
; while a float zero divided to an infinity and the check still answered False.
!(import! &self lib_wmpln)

; A zero probability, integer or float, is inconsistent (False) and divides nothing.
!(conditional-probability-consistency 0 1 0)
!(conditional-probability-consistency 0.0 0.6 0.0)
!(Consistency_ImplicationImplicantConjunction 0 1 1 0 0)
!(Consistency_ImplicationImplicantConjunction 1 0 1 0 0)
!(Consistency_ImplicationImplicantConjunction 0.0 0.5 0.5 0.2 0.2)
!(Consistency_ImplicationImplicantConjunction 0.5 0.0 0.5 0.2 0.2)

; The rules that ask the check: a zero middle term fails the preconditions.
!(Truth_Deduction (stv 0.5 0.7) (stv 0 0.8) (stv 0.6 0.9) (stv 0.25 0.85) (stv 0 0.95))
!(Truth_Deduction (stv 0.5 0.7) (stv 0.0 0.8) (stv 0.6 0.9) (stv 0.25 0.85) (stv 0.0 0.95))
!(collapse (simpleDeductionStrength 0.5 0 0.6 0.25 0))

; Away from zero the checks decide as before, integer and float alike.
!(conditional-probability-consistency 1 1 1)
!(conditional-probability-consistency 0.5 0.6 0.5)
!(conditional-probability-consistency 0.5 0.2 0.9)
!(Consistency_ImplicationImplicantConjunction 1 1 1 1 1)
!(Consistency_ImplicationImplicantConjunction 0.5 0.5 0.5 0.2 0.2)
!(Consistency_ImplicationImplicantConjunction 0.5 0.5 0.2 0.9 0.2)
!(simpleDeductionStrength 0.5 0.5 0.6 0.625 0.95)
