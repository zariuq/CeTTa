; The ContextualCBPV curriculum under --lang prime, against the Lean
; semantics.  Model queries name the Lean theorem that fixes their answer
; (TypeTheory/ContextualCBPVEquations, Languages/MeTTa/Experimental/
; NoValueCBPV).  A world is (World branch outcome state intents); a branch
; lists the most recent choice first.
!(import! &self ../../../lib/prime/cbpv.metta)

; Terms are built eagerly (`let`), so no pending call is left inside a term.
(= (coin) (Choose (Return (Const heads)) (Return (Const tails))))

; ---- eager `to`: an unused binding of a two-way choice still gives two
; worlds (eagerLet_worlds, eager_let_two_occurrences)
!(let $coin (coin) (cbpv:run (Bind $coin (Return (Const done))) 0))

; ---- thunked arguments: an unused thunk is never forced
; (thunked_argument_one_occurrence); the call-by-value translation runs it
; (eager_argument_two_occurrences)
!(let $coin (coin) (cbpv:outcomes (App (App (Lam (Lam (Force (Var 1)))) (Thunk (Return (Const one)))) (Thunk $coin)) 0))
!(let $coin (coin) (cbpv:outcomes (Bind $coin (App (Lam (Return (Const one))) (Var 0))) 0))

; ---- an argument used twice: re-forced, four pairs
; (reforced_argument_four_pairs); evaluated once and shared, two correlated
; pairs (shared_argument_two_pairs)
!(let $coin (coin) (cbpv:outcomes (App (Lam (Bind (Force (Var 0)) (Bind (Force (Var 1)) (Return (Pair (Var 1) (Var 0)))))) (Thunk $coin)) 0))
!(let $coin (coin) (cbpv:outcomes (Bind $coin (App (Lam (Return (Pair (Var 0) (Var 0)))) (Var 0))) 0))

; ---- independent and correlated choice: four and two occurrences
; (independent_choice_four, correlated_choice_two)
!(let $coin (coin) (cbpv:outcomes (Bind $coin (Bind $coin (Return (Pair (Var 1) (Var 0))))) 0))
!(let $coin (coin) (cbpv:outcomes (Bind $coin (Return (Pair (Var 0) (Var 0)))) 0))

; ---- state and intents: a tick reads 0 and leaves 1 (tick_worlds); each
; alternative keeps its own intent (intentChoice_worlds)
!(cbpv:run (Read (Write (Prim succ (Var 0)) (Return (Var 0)))) 0)
!(cbpv:run (Choose (Intent (Const left) (Return (Const one))) (Intent (Const right) (Return (Const two)))) 0)

; ---- raise and handle: a raise is not discarded by `to`
; (raise_not_discardable); a handler catches the raising alternative only
; (handledChoice_answers)
!(cbpv:outcomes (Bind (Raise ErrorKind (Const boom)) (Return (Const after))) 0)
!(cbpv:outcomes (Handle (Choose (Raise ErrorKind (Const boom)) (Return (Const fine))) ErrorKind (Return (Var 0)) (Return (Const caught))) 0)

; ---- the runtime no-value carrier (NoValueCBPV): error and inert are two
; kinds of one carrier (raise_error_outcomes, raise_inert_outcomes);
; retention answers an inert call itself and lets an error through
; (retain_inert, retain_does_not_catch_error); an error propagates through
; `to` and a forced argument, and an unforced one raises nothing
; (error_propagates, unforced_error_not_raised)
!(cbpv:outcomes (Raise ErrorKind (Const empty-list)) 0)
!(cbpv:outcomes (Raise InertKind (Const (head-of Nil))) 0)
!(cbpv:outcomes (cbpv:retain-inert (Raise InertKind (Const (head-of Nil)))) 0)
!(cbpv:outcomes (cbpv:retain-inert (Raise ErrorKind (Const empty-list))) 0)
!(cbpv:outcomes (Bind (Raise ErrorKind (Const empty-list)) (Return (Const after))) 0)
!(cbpv:outcomes (App (Lam (Force (Var 0))) (Thunk (Raise ErrorKind (Const empty-list)))) 0)
!(cbpv:outcomes (App (Lam (Return (Const wrapped))) (Thunk (Raise ErrorKind (Const empty-list)))) 0)

; ---- Prime's own strategy on the same programs: `let` is the eager `to`,
; call arguments are shared thunks.
(= (pick) heads)
(= (pick) tails)
(= (k $a $b) $a)
(= (twice $x) (P $x $x))
!(let $c (pick) done)
!(k one (pick))
!(twice (pick))

; ---- native bounded observation: the first report is already available.
; Asking for it must not request a sensor used only by a later rule.
; Asking for one more report requests A, leaving B and C untouched.
(= (report-input) (Box A (report-sensor A)))
(= (report-input) (Box B (report-sensor B)))
(= (report-input) (Box C (report-sensor C)))
(= (report-sensor $tag) (chain (println! (SensorRequested $tag)) $effect 7))
(= (native-report (Box $tag $reading)) Ready)
(= (native-report (Box $tag 7)) $tag)
!(once (native-report (report-input)))
!(select 4 (native-report (report-input)))
