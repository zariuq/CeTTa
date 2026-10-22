; Call by need, name and value in the ContextualCBPV realization, against the
; Lean semantics (TypeTheory/ContextualCBPVNeed).  (LetNeed m n) is a memo
; cell, (Thunk m) forced twice is call by name, and (Bind m n) is call by
; value.  Terms are built eagerly, so no pending call is left in a term.
!(import! &self ../../../lib/prime/cbpv.metta)

(= (coin) (Choose (Return (Const heads)) (Return (Const tails))))
(= (tick) (Read (Return (Var 0))))

; ---- a choice demanded twice: need and value give two correlated pairs,
; name gives four (coin_twice_need, coin_twice_value, coin_twice_name)
!(let $coin (coin) (cbpv:outcomes (LetNeed $coin (Bind (Force (Var 0)) (Bind (Force (Var 1)) (Return (Pair (Var 1) (Var 0)))))) 0))
!(let $coin (coin) (cbpv:outcomes (Bind $coin (Return (Pair (Var 0) (Var 0)))) 0))
!(let $coin (coin) (cbpv:outcomes (App (Lam (Bind (Force (Var 0)) (Bind (Force (Var 1)) (Return (Pair (Var 1) (Var 0)))))) (Thunk $coin)) 0))

; ---- an unused choice: one answer under need, two under value
; (coin_unused_need, coin_unused_value)
!(let $coin (coin) (cbpv:outcomes (LetNeed $coin (Return (Const one))) 0))
!(let $coin (coin) (cbpv:outcomes (Bind $coin (Return (Const one))) 0))

; ---- an unused producer with an intent: requested only under value
; (intent_unused_need, intent_unused_value)
!(cbpv:run (LetNeed (Intent (Const seven) (Return (Const v))) (Return (Const one))) 0)
!(cbpv:run (Bind (Intent (Const seven) (Return (Const v))) (Return (Const one))) 0)

; ---- a write before the demand is seen by need, not by value
; (write_before_demand)
!(let $tick (tick) (cbpv:outcomes (LetNeed $tick (Write (Const 1) (Force (Var 0)))) 0))
!(let $tick (tick) (cbpv:outcomes (Bind $tick (Write (Const 1) (Return (Var 0)))) 0))

; ---- a write between two demands is seen by name, not by need
; (write_between_demands)
!(let $tick (tick) (cbpv:outcomes (LetNeed $tick (Bind (Force (Var 0)) (Write (Const 1) (Bind (Force (Var 1)) (Return (Pair (Var 1) (Var 0))))))) 0))
!(let $tick (tick) (cbpv:outcomes (App (Lam (Bind (Force (Var 0)) (Write (Const 1) (Bind (Force (Var 1)) (Return (Pair (Var 1) (Var 0))))))) (Thunk $tick)) 0))

; ---- a choice before the demand: need runs the producer inside each
; alternative, value runs it first (choice_before_demand)
!(let $coin (coin) (cbpv:outcomes (LetNeed $coin (Choose (Bind (Force (Var 0)) (Return (Pair (Const left) (Var 0)))) (Bind (Force (Var 0)) (Return (Pair (Const right) (Var 0)))))) 0))
!(let $coin (coin) (cbpv:outcomes (Bind $coin (Choose (Return (Pair (Const left) (Var 0))) (Return (Pair (Const right) (Var 0))))) 0))

; ---- one value: need and name agree for any number of uses
; (need_name_agree_iff with rest = [])
!(cbpv:outcomes (LetNeed (Return (Const only)) (Bind (Force (Var 0)) (Bind (Force (Var 1)) (Return (Pair (Var 1) (Var 0)))))) 0)
!(cbpv:outcomes (App (Lam (Bind (Force (Var 0)) (Bind (Force (Var 1)) (Return (Pair (Var 1) (Var 0)))))) (Thunk (Return (Const only)))) 0)

; ---- the draft's own strategies on the same shapes: call arguments are
; need, `let` is value, and the demand bubbles commit to name, value or need
(= pick heads)
(= pick tails)
(= (k $x $y) $x)
(= (twice $x) (P $x $x))
!(twice pick)
!(demand:committed resample (let $c pick (P $c $c)){$c})
!(let $c pick (P $c $c))
!(k 1 pick)
!(demand:committed eager (k 1 pick))
!(let $c pick done)
!(demand:committed lazy (let $c pick done){$c})
