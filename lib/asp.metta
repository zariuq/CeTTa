; Ground answer-set programs.  A program is an expression of statements over
; ground atoms:
;   (rule H (P ...) (N ...))      H holds when every P holds and no N does
;   (choice A (P ...) (N ...))    A may hold when every P holds and no N does
;   (constraint (P ...) (N ...))  no stable model has every P true and every N false
;   (weight-rule H L ((P W) ...) ((N W) ...)), (weight-constraint L ...) and
;   (weight-choice (A ...) L ...)  the same with a body of weighted literals
;                                 that holds when its true literals weigh at
;                                 least L
;   (asp:solve P)   (model (A ...)) for a stable model, or
;                   (unsat (certificate STEP ...)) when there is none
;   (asp:models P)  (models ((A ...) ...) (certificate STEP ...)): every stable
;                   model once, and the certificate that no further one exists
; A certificate step is (rup ID (LIT ...) (HINT ...)) or
; (loop ID (LIT ...) (ATOM ...) ATOM), over the program's numbering: its atoms
; in order of last occurrence, then its distinct bodies.  The solver's search
; propagates incrementally and its certificates are checked by the verified
; checker; its stable models and verdicts are those of the verified driver
; (Driver.lean), whose own steps the reference search takes one for one.
;   (asp:trace P)  (trace (atoms A ...) (EVENT ...) RESULT): the solver's steps
;                  on P, over the certificate's numbering (variable i is the
;                  i-th of the atoms, then the bodies; a literal is i or -i):
;                  (decide L LEVEL), (imply L LEVEL NOGOOD), (conflict NOGOOD),
;                  (learn NOGOOD (L ...) LEVEL) and (unfounded (V ...)), the
;                  atoms an unfounded-set check leaves unsupported (a program
;                  without positive loops needs no such check), the nogoods
;                  numbered from 0, the completion's first; RESULT is what
;                  asp:solve answers
; The clingo library grounds program texts into these statements and answers
; asp:solve's and asp:models' shapes by clingo; see lib/clingo.metta.
(: __cetta_lib_asp_solve (-> Atom Atom))
(: __cetta_lib_asp_models (-> Atom Atom))
(: __cetta_lib_asp_trace (-> Atom Atom))
(: asp:solve (-> Atom Expression))
(: asp:models (-> Atom Expression))
(: asp:trace (-> Atom Expression))
(= (asp:solve $program) (__cetta_lib_asp_solve $program))
(= (asp:models $program) (__cetta_lib_asp_models $program))
(= (asp:trace $program) (__cetta_lib_asp_trace $program))
