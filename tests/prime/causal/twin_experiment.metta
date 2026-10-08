; The counterfactual twin against the experiment.
;
; A unit's response type is exogenous.  The twin network reads one exogenous
; binding in the factual world and in the counterfactual one: one Need cell,
; so the two reads are correlated.  The experiment runs each regime on its own
; draw: two bindings, two cells, resampled.  The rung-3 query reads the twin;
; the rung-2 queries read the experiment
; (GSLT.Causality.ContextOperatorsControls.Retention.sat_twinQuery_iff_mem,
; GSLT.Causality.ContextOperatorsControls.Retention.sat_experimentQueries).
; They coincide exactly when the exogenous cell has at most one answer
; (GSLT.Causality.ContextBindings.twin_eq_experiment_iff,
; GSLT.Dynamics.DemandAgreement.sharedPair_eq_resampledPair_iff).
;
; Three faces.
; * Operational: the answers below.
; * Intensional: the twin's world has type context.
; * Extensional: with U the exogenous values and Y_t(u) the outcome of u
;   under treatment t, the twin is the set of pairs {(Y_1(u), Y_0(u)) | u in U},
;   which share their exogenous value; the experiment is the product
;   {Y_1(u) | u in U} x {Y_0(u) | u in U}.

(= (outcome helped $treated) $treated)
(= (outcome hurt $treated) (not $treated))
(= (outcome always $treated) True)
(= (outcome never $treated) False)

; Half helped and half hurt; half always and half never affected; one helped.
(= (mixed) (superpose (helped hurt)))
(= (fixed) (superpose (always never)))
(= (single) helped)

; ---- Mixed.  The twin: one cell, two correlated reads.
!(let $world (ctx:bind ctx:empty unit (mixed))
   (Twin (outcome (ctx:get $world unit) True) (outcome (ctx:get $world unit) False)))
; The experiment: each regime draws its own unit; all four pairs.
!(let $treated (outcome (ctx:get (ctx:bind ctx:empty unit (mixed)) unit) True)
   (let $untreated (outcome (ctx:get (ctx:bind ctx:empty unit (mixed)) unit) False)
     (Experiment $treated $untreated)))

; ---- Fixed.  A different twin, and the same four experimental pairs: the two
; populations agree at rung two and differ at rung three
; (GSLT.Causality.ContextOperatorsControls.Retention.mixed_fixed_retention,
; GSLT.Causality.Hierarchy.ResponseTypes.ladder_strict).
!(let $world (ctx:bind ctx:empty unit (fixed))
   (Twin (outcome (ctx:get $world unit) True) (outcome (ctx:get $world unit) False)))
!(let $treated (outcome (ctx:get (ctx:bind ctx:empty unit (fixed)) unit) True)
   (let $untreated (outcome (ctx:get (ctx:bind ctx:empty unit (fixed)) unit) False)
     (Experiment $treated $untreated)))

; ---- One unit: the deterministic case, where the twin is the experiment
; (GSLT.Causality.ContextOperatorsControls.Retention.twinSet_eq_experimentSet_of_card_le_one).
!(let $world (ctx:bind ctx:empty unit (single))
   (Twin (outcome (ctx:get $world unit) True) (outcome (ctx:get $world unit) False)))
!(let $treated (outcome (ctx:get (ctx:bind ctx:empty unit (single)) unit) True)
   (let $untreated (outcome (ctx:get (ctx:bind ctx:empty unit (single)) unit) False)
     (Experiment $treated $untreated)))

; ---- The experiment is the twin with its exogenous binding resampled: the
; demand bubble that commits the binding to resampling
; (GSLT.Dynamics.DemandAgreement.lazyShared_eq_resampledUses_iff).
!(demand:committed resample
   (let $unit (mixed) (Twin (outcome $unit True) (outcome $unit False))){$unit})

; ---- The intensional face.
!(get-type (ctx:bind ctx:empty unit (mixed)))
