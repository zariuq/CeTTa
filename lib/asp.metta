; Ground answer-set programs.  A program is an expression of statements over
; ground atoms:
;   (rule H (P ...) (N ...))      H holds when every P holds and no N does
;   (choice A (P ...) (N ...))    A may hold when every P holds and no N does
;   (constraint (P ...) (N ...))  no stable model has every P true and every N false
;   (weight-rule H L ((P W) ...) ((N W) ...)), (weight-constraint L ...) and
;   (weight-choice (A ...) L ...)  the same with a body of weighted literals
;                                 that holds when its true literals weigh at
;                                 least L
;   (disjunction (H ...) (P ...) (N ...))  some H holds when every P holds and
;                                 no N does; a program with disjunctions is
;                                 solved when it is head-cycle-free (no two
;                                 atoms of one head depend positively on each
;                                 other), by shifting each H to hold when no
;                                 other head does, and refused otherwise
;   (minimize PRIORITY ((P W) ...) ((N W) ...))  the weights of the true
;                                 literals, P true and N false, are the cost at
;                                 PRIORITY; a program with minimize statements
;                                 asks for its optimal stable models, the costs
;                                 least lexicographically, the highest priority
;                                 first
;   (asp:solve P)   (model (A ...)) for a stable model, or
;                   (unsat (certificate STEP ...)) when there is none; with
;                   minimize statements (optimum (A ...) ((PRIORITY COST) ...)
;                   ((PRIORITY (certificate STEP ...)) ...)): an optimal model,
;                   its costs, and at each priority where a cheaper model had
;                   to be ruled out, the certificate that none exists once the
;                   higher priorities are at their optimum (found by branch and
;                   bound, each bound a weight constraint)
;   (asp:models P)  (models ((A ...) ...) (certificate STEP ...)): every stable
;                   model once, and the certificate that no further one exists;
;                   with minimize statements every optimal one
; A model lists its true atoms but the hidden atoms of a grounding, (asp:aux N)
; from clingo:ground, as the text's models show them.
; A certificate step is (rup ID (LIT ...) (HINT ...)),
; (loop ID (LIT ...) (ATOM ...) ATOM) or
; (hall ID (LIT ...) (GROUP ...) ((ATOM ...) ...)), over the program's
; numbering: its atoms in order of last occurrence, then its distinct bodies.
; A Hall step's groups are at-least-one constraints, as rule numbers, whose
; atoms outside its clause lie within fewer of its at-most-one cliques than
; there are groups.  The solver's search propagates incrementally, keeps
; Hall's condition over the program's at-least-one constraints and
; at-most-one cliques, and its certificates are checked by the verified
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
;   (asp:start P)   a session on P, searching as asp:solve does
;   (asp:step S)    one iteration of the search: (running (EVENT ...)), the events
;                   asp:trace shows for it, or (done RESULT), RESULT what asp:solve
;                   answers; a finished session keeps answering (done RESULT)
;   (asp:state S)   (state LEVEL CONFLICTS (trail ((LIT LEVEL REASON) ...))
;                   (true (A ...)) (false (A ...))): the search between steps, the
;                   trail over the certificate's numbering, REASON the implying
;                   nogood or -1 for a decision; (done RESULT) once finished
;   (asp:close S)   ends the session: closed
;   (asp:decide S A V)   the session's next decision, A true (V True) or false
;                   (False), made by the next step after it propagates:
;                   decided, or (assigned W) when A has the value W now; the
;                   search stays sound and complete whatever is decided, so a
;                   MeTTa program can steer it as its heuristic
;   (asp:nogoods S) (nogoods ((LIT ...) ...)): the nogoods the search has made
;                   (learned, loop, weight, Hall and input ones), over the
;                   trail's numbering
;   (asp:next S)    the search to its next answer: (model (A ...)), the session
;                   staying open, or the refutation (unsat (certificate ...)),
;                   which ends it
;   (asp:constrain S ((constraint (P ...) (N ...)) ...))   constraints added
;                   to the session's program after a model, the search going
;                   on with its learned nogoods: constrained.  Each is an input
;                   step of the certificate, (input ID (LIT ...)), which the
;                   checker accepts exactly for an added constraint; models are
;                   checked against every one
;   (asp:injections TERM VARS VALUES)   every instance of TERM under an
;                   injective map of the distinct variables VARS into VALUES, in
;                   lexicographic order of the values' places: the variants of
;                   a rule body under renamings of its variables
;   (asp:distinct ((KEY V ...) ...))  each group a key and the values still open
;                  to it: (sdr ((KEY V) ...)), a value for every key in group
;                  order and no value twice (distinct representatives), or
;                  (hall (KEY ...) (V ...)), keys that between them have fewer
;                  open values than keys, and those values, so no choice gives
;                  every key its own value.  A search giving keys distinct
;                  values asks it after each choice and drops a branch at a
;                  violator, before any key runs out of values; the solver
;                  keeps its at-least-one constraints apart from its
;                  at-most-one cliques by the same matching.  Data that is not
;                  ground groups stays as written.
(: __cetta_lib_asp_solve (-> Atom Atom))
(: __cetta_lib_asp_models (-> Atom Atom))
(: __cetta_lib_asp_trace (-> Atom Atom))
(: __cetta_lib_asp_distinct (-> Atom Atom))
(: __cetta_lib_asp_start (-> Atom Atom))
(: __cetta_lib_asp_step (-> Atom Atom))
(: __cetta_lib_asp_state (-> Atom Atom))
(: __cetta_lib_asp_close (-> Atom Atom))
(: __cetta_lib_asp_next (-> Atom Atom))
(: __cetta_lib_asp_constrain (-> Atom Atom Atom))
(: __cetta_lib_asp_injections (-> Atom Atom Atom Atom))
(: __cetta_lib_asp_decide (-> Atom Atom Atom Atom))
(: __cetta_lib_asp_nogoods (-> Atom Atom))
(: asp:solve (-> Atom Expression))
(: asp:models (-> Atom Expression))
(: asp:trace (-> Atom Expression))
(: asp:distinct (-> Atom Expression))
(: asp:start (-> Atom Atom))
(: asp:step (-> Atom Expression))
(: asp:state (-> Atom Expression))
(: asp:close (-> Atom Atom))
(= (asp:solve $program) (__cetta_lib_asp_solve $program))
(= (asp:models $program) (__cetta_lib_asp_models $program))
(= (asp:trace $program) (__cetta_lib_asp_trace $program))
(= (asp:distinct $groups) (__cetta_lib_asp_distinct $groups))
(= (asp:start $program) (__cetta_lib_asp_start $program))
(= (asp:step $session) (__cetta_lib_asp_step $session))
(= (asp:state $session) (__cetta_lib_asp_state $session))
(= (asp:close $session) (__cetta_lib_asp_close $session))
(= (asp:next $session) (__cetta_lib_asp_next $session))
(= (asp:constrain $session $constraints) (__cetta_lib_asp_constrain $session $constraints))
(= (asp:injections $term $vars $values) (__cetta_lib_asp_injections $term $vars $values))
(= (asp:decide $session $atom $value) (__cetta_lib_asp_decide $session $atom $value))
(= (asp:nogoods $session) (__cetta_lib_asp_nogoods $session))
