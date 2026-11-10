; The sat library: clauses over atoms as MeTTa data.
!(import! &self sat)
; a or b, not both: two models
!(sat:models ((clause (a b) ()) (clause () (a b))))
; units force a model
!(sat:solve ((clause (p) ()) (clause (q) (p)) (clause (r) (q p))))
; a contradiction: the certificate refutes it, its last step the empty clause
!(sat:solve ((clause (x) ()) (clause () (x))))
; atoms are any ground atoms
!(sat:solve ((clause ((on a)) ()) (clause ((lit b)) ((on a)))))
; three pigeons, two holes
!(let (unsat $certificate) (sat:solve ((clause ((p 1 1) (p 1 2)) ()) (clause ((p 2 1) (p 2 2)) ()) (clause ((p 3 1) (p 3 2)) ()) (clause () ((p 1 1) (p 2 1))) (clause () ((p 1 1) (p 3 1))) (clause () ((p 2 1) (p 3 1))) (clause () ((p 1 2) (p 2 2))) (clause () ((p 1 2) (p 3 2))) (clause () ((p 2 2) (p 3 2))))) refuted)
; no clauses: one model, everything false
!(sat:models ())
; an empty clause has no model
!(sat:solve ((clause (a) ()) (clause () ())))
; clauses are ground (an error ends an HE run, so it comes last)
!(sat:solve ((clause ($x) ())))
