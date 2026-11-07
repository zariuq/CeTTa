; clingo, loaded at run time (CETTA_CLINGO_LIBRARY, or libclingo.so.4 on the
; loader's path): its grounder, and its solver behind the asp library's
; answer shapes.
;   (clingo:ground TEXT)   the ground program clingo's grounder makes of the
;                          program TEXT, as the asp library's statements: shown
;                          atoms as terms (f(1,a) as (f 1 a), a tuple as an
;                          expression, -p as (- p)), other atoms (asp:aux N),
;                          and weight bodies as (weight-rule H L ((P W) ...)
;                          ((N W) ...)), (weight-constraint L ...) or
;                          (weight-choice (A ...) L ...)
;   (clingo:models P)      (models ((A ...) ...) (reference clingo)): every
;                          stable model clingo finds of P, given as normal
;                          rules, choices and constraints (each model over P's
;                          atoms in order of first appearance) or as a program
;                          text (each model its shown atoms)
;   (clingo:solve P)       (model (A ...)) for the first stable model clingo
;                          finds, or (unsat (reference clingo))
; The answers have the asp library's shapes, with clingo as the evidence where
; the asp library gives its certificate, so (import-as! &self asp clingo) runs
; a program's asp:solve and asp:models on clingo.  Without clingo these stay
; as they are written.
(: __cetta_lib_clingo_ground (-> Atom Atom))
(: __cetta_lib_clingo_models (-> Atom Atom))
(: __cetta_lib_clingo_solve (-> Atom Atom))
(: clingo:ground (-> Atom Expression))
(: clingo:models (-> Atom Expression))
(: clingo:solve (-> Atom Expression))
(= (clingo:ground $text) (__cetta_lib_clingo_ground $text))
(= (clingo:models $program) (__cetta_lib_clingo_models $program))
(= (clingo:solve $program) (__cetta_lib_clingo_solve $program))
