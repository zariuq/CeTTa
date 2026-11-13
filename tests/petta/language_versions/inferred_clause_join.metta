; The clauses of an undeclared function are joined position by position: a
; position on which they disagree carries no knowledge, and no clause is
; held against a signature read off another.
(= (g $x) (+ $x 1))
(= (f $g) ($g 1))
(= (f $g) 42)
!(test (sort-atom (collapse (f g))) (sort-atom (2 42)))
