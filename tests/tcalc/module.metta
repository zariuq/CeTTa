; tcalc as a module of MeTTa (lib/tcalc.metta).  Each answer is what
; cetta --lang tcalc prints, as atoms.
!(import! &self tcalc)

; a program written in MeTTa: two rules race for one token
!(tcalc:outcomes ((step one) (token 1) (rule a (take (token $x)) (make (got a $x))) (rule b (take (token $x)) (make (got b $x)))) "")

; one step from a given state
!(tcalc:step ((rule a (take (token $x)) (make (got a $x)))) "step=one" ((token 1) (token 2)))

; a program MeTTa builds keeps its variables apart: the let renames $x and
; $y, and they stay two variables of the rule
!(let $p ((pair 1 2) (rule second (read (pair $x $y)) (make (got $y)))) (tcalc:outcomes $p "step=one;store=set"))

; a state with an unknown: reading (row a) decides it
!(tcalc:step ((rule pick (read (row a)) (make (chosen a)))) "step=one;store=set" ((row $u)))

; a state is rows: (step one) here is a row, not a declaration
!(tcalc:step ((rule a (take (t)) (make (u)))) "step=one" ((step one) (t)))

; in each outcome, the rows a read of the pattern can select
!(tcalc:query ((step one) (store set) (row $u) (rule pick-a (read (row a)) (make (chosen a))) (rule pick-b (read (row b)) (make (chosen b)))) "" (chosen $x))

; programs loaded from files in the three notations: the same race
!(let $p (tcalc:load "tests/tcalc/chr_token_race.chr") (tcalc:outcomes $p "step=one"))
!(let $p (tcalc:load "tests/tcalc/rho_token_race.rho") (tcalc:outcomes $p "step=one"))
!(tcalc:load "tests/tcalc/chr_simpagation.chr")

; refusals are Errors with the runner's message (caught here, since an
; Error at the top level ends the file)
!(case (tcalc:outcomes ((token 1)) "step=sideways") (((Error $s $m) (refused $m)) ($x $x)))
!(case (tcalc:load "tests/tcalc/refused_rho_new.rho") (((Error $s $m) (refused $m)) ($x $x)))
!(case (tcalc:step ((rule a (take (t)) (make (u)))) "step=one" token) (((Error $s $m) (refused $m)) ($x $x)))
!(case (tcalc:outcomes program "") (((Error $s $m) (refused $m)) ($x $x)))
