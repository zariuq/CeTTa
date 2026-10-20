; Zero answers in the ContextualCBPV realization, against the Lean semantics
; (TypeTheory/ContextualCBPVZero).  `Zero` is the nullary choice; `collapse`
; shows its observation, which has no world.
!(import! &self ../../../lib/prime/cbpv.metta)

; no world (empty_observed)
!(collapse (cbpv:run Zero 0))
; absorbs `to`, application and a handler of a user kind
; (empty_bind, empty_app, empty_handle)
!(collapse (cbpv:outcomes (Bind Zero (Return (Const after))) 0))
!(collapse (cbpv:outcomes (App Zero (Const x)) 0))
!(collapse (cbpv:outcomes (Handle Zero ErrorKind (Return (Var 0)) (Return (Const caught))) 0))
; beside an alternative it contributes only the branch step
; (empty_choose_observed)
!(cbpv:run (Choose Zero (Return (Const one))) 0)
; zero answers, a returned unit and a raised error are three observations
; (zero_unit_error_apart)
!(collapse (cbpv:outcomes Zero 0))
!(collapse (cbpv:outcomes (Return Unit) 0))
!(collapse (cbpv:outcomes (Raise ErrorKind (Const boom)) 0))

; ---- Prime's own (empty) on the same shapes
!(collapse (empty))
!(collapse (let $x (empty) (Got $x)))
!(collapse (superpose ((empty) one)))
