; Feedback: two mutually defined cells, outside the recursive bubble.
;
; A recursive model has exactly one solution for every setting
; (GSLT.Causality.StructuralModels.exists_unique_solution).  Two cells defined
; by each other are not recursive: copying (x := y, y := x) has two solutions
; and negating (x := not y, y := x) has none
; (GSLT.Causality.StructuralModels.copyLoop_two_solutions,
; GSLT.Causality.StructuralModels.negLoop_no_solution,
; GSLT.Causality.StructuralModels.copyLoop_not_recursive,
; GSLT.Causality.StructuralModels.negLoop_not_recursive).  Both are programs of
; the same language as every recursive model.
;
; Three faces.
; * Operational: demanding a cell of the loop meets the cycle; the Need machine
;   reports it instead of choosing a value.
; * Intensional: the cells are untyped equations; nothing in the types marks
;   the loop.
; * Extensional: the solutions are the stores every mechanism reproduces; they
;   are enumerated below over {True, False}^2.

; ---- The solutions, enumerated.  Copying: two.
!(let $x (superpose (True False))
   (let $y (superpose (True False))
     (if (and (== $x $y) (== $y $x)) (Solution $x $y) (empty))))
; Negating: none.  A query with no answer prints nothing, so its answers are
; collected into one list, which is empty.
!(collapse
   (let $x (superpose (True False))
     (let $y (superpose (True False))
       (if (and (== $x (not $y)) (== $y $x)) (Solution $x $y) (empty)))))

; ---- The program.  Demanding a cell of the copying loop meets the cycle.
(= copy-x copy-y)
(= copy-y copy-x)
(= (read-cell $cell) $cell)
!(demand:committed eager (read-cell copy-x))
; Not demanding it, nothing runs.
(= (ignore $cell) unread)
!(ignore copy-x)
