!(import! &self rho)
!(import! &self lts)

; Rhometta surface over the real rhocalc reducer.
;
; Import pattern:
;   !(import! &self rhometta)
;
; This library keeps the user-facing surface in MeTTa while delegating actual
; reduction and one-step successor construction to the native rhocalc engine.

(: rhometta:transitions (-> Atom %Undefined%))
(: rhometta:run (-> Atom %Undefined%))
(: rhometta:eval (-> Atom Atom))
(: rhometta:trace-2 (-> Atom %Undefined%))
(: rhometta:is-normal-form (-> Atom Bool))
(: rhometta:is-can-step (-> Atom Bool))

(@doc rhometta:transitions
  (@desc "One-step Rhometta successor frontier backed by the real rhocalc reducer seam. It emits one successor per successful COMM outcome, including zero, one, or many outcomes from sibling-isolated transactional deferred-payload evaluation.")
  (@params (
    (@param "Rhometta process to inspect")))
  (@return "Zero or more Rhometta successor processes"))

(@doc rhometta:run
  (@desc "Enumerates the quiescent Rhometta outcomes reachable through the real rhocalc reducer seam. Deferred payloads evaluate against a frozen shared base with payload-local write-forward scratch; shared mutable resources stay unchanged, and mutated resources escape only as ordinary owned results.")
  (@params (
    (@param "Rhometta process to reduce")))
  (@return "One or more quiescent Rhometta residuals"))

(@doc rhometta:eval
  (@desc "Builds an explicit deferred Rhometta payload without eagerly evaluating the enclosed MeTTa term at the surface.")
  (@params (
    (@param "MeTTa term to defer until COMM time")))
  (@return "Explicit rho:eval-payload wrapper carrying quoted syntax"))

(= (rhometta:eval $payload)
   (rho:eval-payload (quote $payload)))

(= (rhometta:transitions $state)
   (__cetta_lib_rhometta_transitions $state))

(= (rhometta:run $state)
   (__cetta_lib_rhometta_run $state))

(= (rhometta:trace-2 $state)
   (lts:trace-2 rhometta:transitions $state))

(= (rhometta:is-normal-form $state)
   (lts:is-normal-form rhometta:transitions $state))

(= (rhometta:is-can-step $state)
   (lts:is-can-step rhometta:transitions $state))
