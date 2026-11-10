; Satisfiability modulo equality with uninterpreted functions and linear real
; arithmetic; see lib/smt.metta.
(= (smt:solve $clauses) (__cetta_lib_smt_solve $clauses))
(= (smt:solve-text $text) (__cetta_lib_smt_solve_text $text))
(= (smt:minimize $clauses $term) (__cetta_lib_smt_minimize $clauses $term))
(= (smt:maximize $clauses $term) (__cetta_lib_smt_maximize $clauses $term))
