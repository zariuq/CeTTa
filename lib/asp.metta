; Ground answer-set programs.  A program is an expression of statements over
; ground atoms:
;   (rule H (P ...) (N ...))      H holds when every P holds and no N does
;   (choice A (P ...) (N ...))    A may hold when every P holds and no N does
;   (constraint (P ...) (N ...))  no stable model has every P true and every N false
;   (asp:solve P)   (model (A ...)) for a stable model, or
;                   (unsat (certificate STEP ...)) when there is none
;   (asp:models P)  (models ((A ...) ...) (certificate STEP ...)): every stable
;                   model once, and the certificate that no further one exists
; A certificate step is (rup ID (LIT ...) (HINT ...)) or
; (loop ID (LIT ...) (ATOM ...) ATOM), over the program's numbering: its atoms
; in order of last occurrence, then its distinct bodies.
; With clingo loadable (CETTA_CLINGO_LIBRARY, or libclingo.so.4 on the
; loader's path), clingo serves as a reference:
;   (asp:ground TEXT)        the ground program clingo's grounder makes of the
;                            program TEXT, as statements: shown atoms as terms
;                            (f(1,a) as (f 1 a), a tuple as an expression,
;                            -p as (- p)), other atoms (asp:aux N), and a
;                            weight rule as (weight-rule H L ((P W) ...)
;                            ((N W) ...)), (weight-constraint L ...) or
;                            (weight-choice (A ...) L ...)
;   (asp:clingo-models P)    (clingo-models ((A ...) ...)): every stable model
;                            of the program data P, as clingo's solver finds
;                            them, each over P's atoms in order of appearance
; Without clingo these stay as they are written.
;   (asp:trace P)  (trace (atoms A ...) (EVENT ...) RESULT): the solver's steps
;                  on P, over the certificate's numbering (variable i is the
;                  i-th of the atoms, then the bodies; a literal is i or -i):
;                  (decide L LEVEL), (imply L LEVEL NOGOOD), (conflict NOGOOD),
;                  (learn NOGOOD (L ...) LEVEL) and (unfounded (V ...)), the
;                  atoms an unfounded-set check leaves unsupported (a program
;                  without positive loops needs no such check), the nogoods
;                  numbered from 0, the completion's first; RESULT is what
;                  asp:solve answers
(: __cetta_lib_asp_solve (-> Atom Atom))
(: __cetta_lib_asp_models (-> Atom Atom))
(: __cetta_lib_asp_ground (-> Atom Atom))
(: __cetta_lib_asp_clingo_models (-> Atom Atom))
(: __cetta_lib_asp_trace (-> Atom Atom))
(: asp:solve (-> Atom Expression))
(: asp:models (-> Atom Expression))
(: asp:ground (-> Atom Expression))
(: asp:clingo-models (-> Atom Expression))
(: asp:trace (-> Atom Expression))
(= (asp:solve $program) (__cetta_lib_asp_solve $program))
(= (asp:models $program) (__cetta_lib_asp_models $program))
(= (asp:ground $text) (__cetta_lib_asp_ground $text))
(= (asp:clingo-models $program) (__cetta_lib_asp_clingo_models $program))
(= (asp:trace $program) (__cetta_lib_asp_trace $program))
