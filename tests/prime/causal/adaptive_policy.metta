; A program that chooses its intervention from data it observes.
;
; Each unit has a natural treatment, the covariate the program observes, and a
; response type.  The adaptive program reads the covariate of the drawn unit,
; builds the intervention from it as a context value, and runs the unit under
; that context: a first-class context computed from the run's own data.  A
; meta-level intervention is one context for every unit.  A constant policy
; is the meta-level intervention
; (GSLT.Causality.AdaptiveContexts.internal_do_eq_external), and under it the
; regime does not depend on the unit
; (GSLT.Causality.AdaptiveContexts.constant_policy_regime_independent).
; On these two units no single regime reaches both records the adaptive
; program reaches (GSLT.Causality.AdaptiveContexts.adaptive_not_meta); a
; different population, whose natural treatments are the policy's choices,
; does (GSLT.Causality.AdaptiveContexts.adaptive_reencoded).
;
; Three faces.
; * Operational: the records below.
; * Intensional: the policy's result is a context.
; * Extensional: a policy is a map from covariates to interventions; the
;   adaptive run is the image of the units under unit -> respond(unit, policy(cov
;   unit)); a meta-level intervention is a constant policy.

(= (outcome always $treated) True)
(= (outcome never $treated) False)

; An always-affected unit, naturally untreated, and a never-affected unit,
; naturally treated.
(= (units) (superpose ((Unit False always) (Unit True never))))

; Treat exactly the naturally untreated.
(= (flip $natural) (not $natural))
(= (policy $natural) (ctx:bind ctx:empty treat (flip $natural)))

(= (respond $type $regime)
   (Record (ctx:get $regime treat) (outcome $type (ctx:get $regime treat))))

; ---- Adaptive: the regime is computed from the observed covariate.
!(let (Unit $natural $type) (units) (respond $type (policy $natural)))

; ---- Meta level: one regime for every unit.  Treat everyone; treat no one.
!(let $regime (ctx:bind ctx:empty treat True)
   (let (Unit $natural $type) (units) (respond $type $regime)))
!(let $regime (ctx:bind ctx:empty treat False)
   (let (Unit $natural $type) (units) (respond $type $regime)))

; ---- Left alone, each unit takes its natural treatment.
!(let (Unit $natural $type) (units)
   (respond $type (ctx:bind ctx:empty treat $natural)))

; ---- The intensional face.
!(get-type (policy False))
