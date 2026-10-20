; PolarizedNeed's value layer in the ContextualCBPV realization, against the
; Lean reading at shapes (TypeTheory/ContextualCBPVPolarizedValues):
; first-class thunks, value lambdas and native packages, with need cells.
; Terms are built eagerly, so no pending call is left in a term.
!(import! &self ../../../lib/prime/cbpv.metta)

(= (coin) (Choose (Return (Const heads)) (Return (Const tails))))

; Force the value bound last twice and pair the two answers.
(= (force-twice) (Bind (Force (Var 0)) (Bind (Force (Var 1)) (Return (Pair (Var 1) (Var 0))))))

; ---- a thunk runs again at every force: four pairs (thunkedCoin_answers)
!(let $coin (coin) (let $twice (force-twice) (cbpv:outcomes (Bind (Return (Thunk $coin)) $twice) 0)))

; ---- a need cell that a thunk captures is demanded once, however often the
; thunk is forced: two pairs (sharedInThunk_answers)
!(let $coin (coin) (let $twice (force-twice) (cbpv:outcomes (LetNeed $coin (Bind (Return (Thunk (Force (Var 0)))) $twice)) 0)))

; ---- a value lambda passes a thunk on: four pairs for a thunk of the
; choice, two for a thunk of a need cell of it (twice_thunk_answers,
; twice_need_answers)
!(let $coin (coin) (let $twice (force-twice) (cbpv:outcomes (App (Lam $twice) (Thunk $coin)) 0)))
!(let $coin (coin) (let $twice (force-twice) (cbpv:outcomes (LetNeed $coin (App (Lam $twice) (Thunk (Force (Var 0))))) 0)))

; ---- a package keeps its index with its payload (packed_answers)
!(let $coin (coin) (cbpv:outcomes (Split (Pair (Const seven) (Thunk $coin)) (Bind (Force (Var 0)) (Return (Pair (Var 2) (Var 0))))) 0))
