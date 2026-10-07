; Rain, sprinkler and wet grass as a Prime program.
;
; A world is a first-class context.  The exogenous keys u-rain and u-sprinkler
; are bound to choices; each binding is one lazy Need cell, so every read of a
; key in one world sees the same choice.  A mechanism reads the world: when the
; world binds the key itself, that binding wins (an intervention); otherwise
; the mechanism computes the key from other keys.  `do(key := v)` is
; `(ctx:bind world key v)`: an intervention is a context, and the newest
; binding wins.
;
; The model is recursive: u-rain and u-sprinkler, then rain, then sprinkler,
; then wet.  A recursive model has exactly one solution for every exogenous
; setting, before and after every intervention
; (GSLT.Causality.StructuralModels.exists_unique_solution,
; GSLT.Causality.StructuralModels.surgery_exists_unique_solution).
;
; Three faces.
; * Operational: the answers below, one per world the run demands.
; * Intensional: an intervention has type (-> context Name Atom context), and
;   an intervened world has type context.
; * Extensional: the worlds are the exogenous settings {True, False}^2; the
;   observation maps each setting to its solution (rain, sprinkler, wet);
;   do(sprinkler := True) maps each setting to the solution of the surgered
;   model.  Only the settings the run demands appear: when it rains the
;   sprinkler's exogenous cell is never read, so that world is one answer, not
;   two.

(= (value $world $key $mechanism)
   (case (ctx:get $world $key)
     (((ctx:missing $missing) $mechanism)
      ($bound $bound))))

(= (rain $world) (value $world rain (ctx:get $world u-rain)))
(= (sprinkler $world)
   (value $world sprinkler (if (rain $world) False (ctx:get $world u-sprinkler))))
(= (wet $world) (value $world wet (or (rain $world) (sprinkler $world))))

(= (world)
   (ctx:bind (ctx:bind ctx:empty u-rain (superpose (True False)))
     u-sprinkler (superpose (True False))))

(= (observe $world) (Triple (rain $world) (sprinkler $world) (wet $world)))

; ---- Seeing: the passive run.  Three worlds: rain (the sprinkler's cell is
; not read), dry with the sprinkler on, dry with it off.
!(let $world (world) (observe $world))

; ---- Doing.  do(sprinkler := True): the sprinkler's mechanism is replaced, so
; its exogenous cell is never read.
!(let $world (ctx:bind (world) sprinkler True) (observe $world))
; do(rain := False).
!(let $world (ctx:bind (world) rain False) (observe $world))
; Override: do(rain := True) then do(rain := False) is do(rain := False)
; (GSLT.Causality.ContextBindings.OverrideAction.do_override).
!(let $world (ctx:bind (ctx:bind (world) rain True) rain False) (observe $world))
; The sprinkler's effect on the grass in a dry world: two regimes read off one
; world.  Neither exogenous cell is read.
!(let $world (ctx:bind (world) rain False)
   (Pair (wet (ctx:bind $world sprinkler True)) (wet (ctx:bind $world sprinkler False))))

; ---- Explicit do against derived do.  `ctx:bind` binds its value lazily in
; one Need cell (derived do).  An explicit do takes a value, so a computation
; given to it is evaluated where the intervention is made: written here as a
; `let`, which Prime evaluates eagerly.  They agree when the binding is read or
; has one answer, and differ on an unread binding with two answers
; (GSLT.Causality.ContextBindings.explicit_eq_derived_iff,
; GSLT.Dynamics.DemandAgreement.eagerShared_eq_lazyShared_iff).
; Derived: the unread coin is never drawn; the three worlds.
!(let $world (ctx:bind (world) unread (superpose (heads tails))) (observe $world))
; Explicit: the coin is drawn first and each face carries the three worlds.
!(let $face (superpose (heads tails))
   (let $world (ctx:bind (world) unread $face) (observe $world)))
; The override law holds for derived do and fails for explicit do when the
; shadowed computation has two answers: a shadowed explicit intervention still
; runs (GSLT.Causality.ContextBindings.derived_override,
; GSLT.Causality.ContextBindings.explicit_override_iff).
!(let $world (ctx:bind (ctx:bind (world) rain (superpose (True False))) rain False)
   (observe $world))
!(let $shadowed (superpose (True False))
   (let $world (ctx:bind (ctx:bind (world) rain $shadowed) rain False) (observe $world)))

; ---- The intensional face.
!(get-type ctx:bind)
!(get-type (ctx:bind (world) sprinkler True))
