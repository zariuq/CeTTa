; An IPASIR SAT solver, loaded at run time (CETTA_IPASIR_LIBRARY, or
; libcadical.so on the loader's path), behind the sat library's answer shapes.
;   (ipasir:solve CLAUSES)   (model (A ...)) for the model the solver finds, or
;                            (unsat (reference SIGNATURE)), SIGNATURE the
;                            solver's own name and version
;   (ipasir:models CLAUSES)  (models ((A ...) ...) (reference SIGNATURE)): every
;                            model once, each found blocked by one more clause
; The clauses are read as sat:solve reads them, so (import-as! &self sat ipasir)
; runs a program's sat:solve and sat:models on the loaded solver.  Without one
; loadable the library's heads are not its to answer: they stay as written,
; also under the sat library's names.
(: __cetta_lib_ipasir_solve (-> Atom Atom))
(: __cetta_lib_ipasir_models (-> Atom Atom))
(: ipasir:solve (-> Atom Expression))
(: ipasir:models (-> Atom Expression))
(= (ipasir:solve $clauses) (__cetta_lib_ipasir_solve $clauses))
(= (ipasir:models $clauses) (__cetta_lib_ipasir_models $clauses))
