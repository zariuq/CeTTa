!(import! &self rho)
!(import! &self lts)

; Public rho LTS specialization over strict-core rho.
; This keeps reduction observation out of the rho: object-language namespace
; while exposing a programmable MeTTa-level frontier surface.
;
; Import pattern:
;   !(import! &self lts:rho)
;
; Public API family:
;   lts:rho:transitions
;   lts:rho:trace-2
;   lts:rho:is-normal-form
;   lts:rho:is-can-step
;
; This specialization binds generic lts:* to strict-core rho by binding the
; frontier relation once to the rho successor primitive. Since it imports lts,
; callers also get the generic lts:* operators in scope.

(: lts:rho:transitions (-> Atom %Undefined%))
(: lts:rho:trace-2 (-> Atom %Undefined%))
(: lts:rho:is-normal-form (-> Atom Bool))
(: lts:rho:is-can-step (-> Atom Bool))

(@doc lts:rho:transitions
  (@desc "Meta-level one-step strict-core rho successor frontier. This is lts:transitions bound to the strict-core rho successor primitive: it emits each COMM successor as an ordinary MeTTa result, emits zero results on quiescent processes, propagates incoming Error states unchanged, and returns Error on malformed non-Error rho input.")
  (@params (
    (@param "Well-formed strict-core rho process")))
  (@return "Zero or more successor rho processes"))

(@doc lts:rho:trace-2
  (@desc "Enumerates exact two-step strict-core rho traces as pairs of intermediate and final rho states. Incoming Error states are propagated unchanged, while malformed non-Error rho input is reported as Error.")
  (@params (
    (@param "Strict-core rho process to inspect for two successive COMM steps")))
  (@return "Zero or more pairs of rho states"))

(@doc lts:rho:is-normal-form
  (@desc "Checks whether a strict-core rho process has an empty one-step COMM frontier. This is lts:is-normal-form over lts:rho:transitions, so Incoming Error states are propagated unchanged and malformed non-Error rho input is reported as Error rather than being treated as quiescent.")
  (@params (
    (@param "Strict-core rho process to inspect")))
  (@return "True when the one-step frontier is empty, False when at least one successor exists, or Error on malformed input"))

(@doc lts:rho:is-can-step
  (@desc "Checks whether a strict-core rho process has at least one enabled one-step COMM reduction. This is lts:is-can-step over lts:rho:transitions, so incoming Error states are propagated unchanged and malformed non-Error rho input is reported as Error.")
  (@params (
    (@param "Strict-core rho process to inspect")))
  (@return "True when at least one successor exists, False when quiescent, or Error on malformed input"))

(= (lts:rho:transitions $state)
   (lts:transitions __cetta_lib_lts_rho_transitions $state))

(= (lts:rho:trace-2 $state)
   (lts:trace-2 lts:rho:transitions $state))

(= (lts:rho:is-normal-form $state)
   (lts:is-normal-form lts:rho:transitions $state))

(= (lts:rho:is-can-step $state)
   (lts:is-can-step lts:rho:transitions $state))
