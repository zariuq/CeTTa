; Propositional satisfiability over atoms, on the asp library's solver.
;   (sat:solve CLAUSES)   CLAUSES a list of (clause (P ...) (N ...)), each
;                         holding when some atom P is true or some atom N false:
;                         (model (A ...)), the atoms true in a model in order of
;                         first occurrence (every other atom false), or
;                         (unsat (certificate STEP ...)) when there is none
;   (sat:models CLAUSES)  (models ((A ...) ...) (certificate STEP ...)): every
;                         model once, and the refutation of the clauses with each
;                         model found blocked by the clause that its assignment
;                         does not hold
; A certificate step is (rup ID (LIT ...) (HINT ...)), over the atoms numbered from
; 1 in order of first occurrence, each clause's P before its N (a literal is i or
; -i), and the clauses named from
; 1 in order, the blocking clauses after them in the order their models were
; found: each step's clause follows by unit propagation over its hints in turn,
; so the steps are an LRAT proof.  Every answer is checked before it is given: a
; model against every clause, a refutation by the certificate checker.
(: __cetta_lib_sat_solve (-> Atom Atom))
(: __cetta_lib_sat_models (-> Atom Atom))
(: sat:solve (-> Atom Expression))
(: sat:models (-> Atom Expression))
(= (sat:solve $clauses) (__cetta_lib_sat_solve $clauses))
(= (sat:models $clauses) (__cetta_lib_sat_models $clauses))
