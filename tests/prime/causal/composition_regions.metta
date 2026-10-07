; Composing interventions, and interventions bounded by a region.
;
; An intervention is a context; composing two is binding one after the other,
; and the newest binding wins.  Interventions on different keys commute;
; interventions on one key do not
; (GSLT.Causality.ContextBindings.OverrideAction.commute_of_disjoint,
; GSLT.Causality.ContextOperatorsControls.Store.same_key_not_commute).
;
; A region is the set of keys an intervention may bind: authority is a
; declared class of contexts.  The region's contexts cannot tell apart two
; worlds that differ only in a secret the law reads through a guard the region
; cannot bind; a context outside the region binds the guard and tells them
; apart (GSLT.Causality.ContextOperatorsControls.OneShot.masked_noninterference,
; GSLT.Causality.ContextOperatorsControls.OneShot.region_witness).
; The draft has no native region guard on contexts.  `region:bind` below is an
; equation-level prototype of the guard; the native refusal is stated, pending,
; in region_guard.pending.metta.
;
; Three faces.
; * Operational: the answers below.
; * Intensional: an intervention has type (-> context Name Atom context); the
;   prototype guard is an ordinary equation, so it has no declared type.
; * Extensional: an intervention is a map on worlds (assignments of values to
;   keys); composing two is composing the maps; maps on different keys commute,
;   two maps on one key compose to the outer one.  A region's interventions
;   are the maps that change only the region's keys.

(= (show-both $world) (Pair (ctx:get $world x) (ctx:get $world y)))

; ---- Different keys, either order: one world.
!(show-both (ctx:bind (ctx:bind ctx:empty x 1) y 2))
!(show-both (ctx:bind (ctx:bind ctx:empty y 2) x 1))
; ---- One key, either order: the newer binding is read.
!(ctx:get (ctx:bind (ctx:bind ctx:empty x 1) x 2) x)
!(ctx:get (ctx:bind (ctx:bind ctx:empty x 2) x 1) x)

; ---- Interaction.  The outcome is true when both keys are.  Setting either
; key alone leaves it false; setting both makes it true.  Measured passively,
; impact is not subadditive; a class that may set the other key sees each
; setting's impact
; (GSLT.Causality.ContextOperatorsControls.Interaction.passive_impact_not_subadditive,
; GSLT.Causality.ContextOperatorsControls.Interaction.impact_sees_interaction,
; GSLT.Causality.ContextOperators.impact_compose_le).
(= (both $world) (and (ctx:get $world first) (ctx:get $world second)))
(= (base) (ctx:bind (ctx:bind ctx:empty first False) second False))
!(both (base))
!(both (ctx:bind (base) first True))
!(both (ctx:bind (base) second True))
!(both (ctx:bind (ctx:bind (base) first True) second True))

; ---- Regions.  The law reads the secret through the guard.
(= (gated $world) (and (ctx:get $world guard) (ctx:get $world secret)))
(= (secret-on)
   (ctx:bind (ctx:bind (ctx:bind ctx:empty open False) guard False) secret True))
(= (secret-off)
   (ctx:bind (ctx:bind (ctx:bind ctx:empty open False) guard False) secret False))

; The prototype guard: bind a key only when the region holds it.
(= (region:member $key $region)
   (if (== $region ())
       False
       (let ($head $tail) (decons-atom $region)
         (if (== $head $key) True (region:member $key $tail)))))
(= (region:bind $region $world $key $value)
   (if (region:member $key $region)
       (ctx:bind $world $key $value)
       (Refused (outside-region $key))))

; Every intervention of the region (open) leaves the two worlds alike.
!(Pair (gated (region:bind (open) (secret-on) open True))
       (gated (region:bind (open) (secret-off) open True)))
!(Pair (gated (region:bind (open) (secret-on) open False))
       (gated (region:bind (open) (secret-off) open False)))
; A context outside the region binds the guard and separates them.
!(Pair (gated (ctx:bind (secret-on) guard True)) (gated (ctx:bind (secret-off) guard True)))
; The region refuses the guard.
!(region:bind (open) (secret-on) guard True)

; ---- The intensional face.
!(get-type ctx:bind)
