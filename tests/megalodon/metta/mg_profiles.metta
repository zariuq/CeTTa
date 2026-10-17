; Profile selection.  The five upstream profiles are distinct inventories:
; primitives (syntax.ml's primname table), reserved names and the recorded
; Proofgold theory identity.  A profile name authorizes no theorem: every
; fresh theory starts with an empty inventory, and only its authority
; selects parameters and axioms.
!(import! &self megalodon)
!(let $state (mg:new-theory-state egal) (bind! &egal $state))
!(let $state (mg:new-theory-state hf) (bind! &hf $state))
!(let $state (mg:new-theory-state mizar) (bind! &mizar $state))
!(let $state (mg:new-theory-state hoas) (bind! &hoas $state))
!(let $state (mg:new-theory-state setmm) (bind! &setmm $state))
(= (E) (mg:use (get-state &egal)))

!(mg:profile (mg:use (get-state &hf)))
!(mg:profile (E))
!(mg:profile (mg:use (get-state &mizar)))
!(mg:profile (mg:use (get-state &hoas)))
; SetMM has 1291 primitives (the upstream table) and reserves wi and wal.
!(let (Megalodon.profile $name $label $primitives $reserved $id) (mg:profile (mg:use (get-state &setmm)))
   ($name (size-atom $primitives) $reserved $id))

; Nothing is known in a fresh theory of any profile.
!(mg:inventory (E))
!(mg:inventory (mg:use (get-state &mizar)))

; The same primitive index means what its profile says, or nothing.
!(mg:infer (E) (mg:empty-context) (Tm.prim 6))
!(case (mg:try-infer (mg:use (get-state &hf)) (mg:empty-context) (Tm.prim 6))
   (((Error $call $why) (refused $why)) ($type (accepted $type))))
!(mg:infer (mg:use (get-state &mizar)) (mg:empty-context) (Tm.prim 3))
!(case (mg:try-infer (mg:use (get-state &mizar)) (mg:empty-context) (Tm.prim 2))
   (((Error $call $why) (refused $why)) ($type (accepted $type))))
!(case (mg:try-infer (mg:use (get-state &hoas)) (mg:empty-context) (Tm.prim 1))
   (((Error $call $why) (refused $why)) ($type (accepted $type))))

; A reserved name cannot be declared.
!(case (mg:try-parameter (mg:authority (get-state &setmm)) "wi" (Tp.prop))
   (((Error $call $why) (refused $why)) ($done (accepted $done))))

; An Egal theorem over a primitive, with its profile recorded on it.
(= (In $x $y) (Tm.app (Tm.app (Tm.prim 1) $x) $y))
!(mg:statement
   (mg:prove-by (E) "In_refl_imp"
     (Tm.all (Tp.base 0) (Tm.imp (In (Tm.db 0) (Tm.db 0)) (In (Tm.db 0) (Tm.db 0))))
     (Tac.intros Tac.assumption)))
!(mg:theorem-info (mg:known (E) "In_refl_imp"))
; The same statement is not a known of another profile's theory.
!(case (mg:try-known (mg:use (get-state &hf)) "In_refl_imp")
   (((Error $call $why) (refused $why)) ($theorem (accepted $theorem))))
