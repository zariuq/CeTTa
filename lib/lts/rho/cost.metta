!(import! &self lts:rho)

; Public accounted frontier over rhocalc cost-profile states.
;
; Import pattern:
;   !(import! &self lts:rho:cost)
;
; Public API family:
;   lts:rho:cost:transitions
;   lts:rho:cost:trace-2
;   lts:rho:cost:steps
;   lts:rho:cost:step-cost
;   lts:rho:cost:step-state
;   lts:rho:cost:is-normal-form
;   lts:rho:cost:is-can-step

(: lts:rho:cost:transitions (-> Atom %Undefined%))
(: lts:rho:cost:trace-2 (-> Atom %Undefined%))
(: lts:rho:cost:steps (-> Atom %Undefined%))
(: lts:rho:cost:step-cost (-> Atom Atom))
(: lts:rho:cost:step-state (-> Atom Atom))
(: lts:rho:cost:is-normal-form (-> Atom Bool))
(: lts:rho:cost:is-can-step (-> Atom Bool))

(@doc lts:rho:cost:transitions
  (@desc "Meta-level one-step state frontier for rhocalc cost-profile states. This is the residual-state projection of lts:rho:cost:steps through lts:rho:cost:step-state: each successor is the residual state from an accounted step. Incoming Error states are propagated unchanged, while malformed non-Error input is reported as Error.")
  (@params (
    (@param "Well-formed rhocalc cost-profile term")))
  (@return "Zero or more successor cost-profile states"))

(@doc lts:rho:cost:trace-2
  (@desc "Enumerates exact two-step cost-profile rho traces as pairs of intermediate and final residual states. Incoming Error states are propagated unchanged, while malformed non-Error input is reported as Error.")
  (@params (
    (@param "Cost-profile rhocalc state to inspect for two successive accounted steps")))
  (@return "Zero or more pairs of cost-profile states"))

(@doc lts:rho:cost:steps
  (@desc "Meta-level one-step accounted frontier for rhocalc cost-profile states. This is the public front door for the accounted-step relation; each successor is paired with the consumed signature product for that step, and lts:rho:cost:transitions is its residual-state projection through lts:rho:cost:step-state. Incoming Error states are propagated unchanged, while malformed non-Error input is reported as Error.")
  (@params (
    (@param "Well-formed rhocalc cost-profile term")))
  (@return "Zero or more lts:rho:cost:step records"))

(@doc lts:rho:cost:step-cost
  (@desc "Extracts the consumed signature product from an accounted cost step. This is the cost-field projection for records emitted by lts:rho:cost:steps. Any non-step atom, including an Error atom, is rejected with an explicit \"expected lts:rho:cost:step record\" error rather than being propagated unchanged.")
  (@params (
    (@param "lts:rho:cost:step record")))
  (@return "Consumed signature product, or Error on malformed non-step input"))

(@doc lts:rho:cost:step-state
  (@desc "Extracts the residual state from an accounted cost step. This is the state-field projection for records emitted by lts:rho:cost:steps and used by lts:rho:cost:transitions. Any non-step atom, including an Error atom, is rejected with an explicit \"expected lts:rho:cost:step record\" error rather than being propagated unchanged.")
  (@params (
    (@param "lts:rho:cost:step record")))
  (@return "Residual cost-profile state, or Error on malformed non-step input"))

(@doc lts:rho:cost:is-normal-form
  (@desc "Checks whether a cost-profile rhocalc state has an empty accounted one-step frontier. Incoming Error states are propagated unchanged. Malformed non-Error input is reported as Error rather than being treated as quiescent.")
  (@params (
    (@param "Cost-profile rhocalc state to inspect")))
  (@return "True when the accounted one-step frontier is empty, False when at least one step exists, or Error on malformed input"))

(@doc lts:rho:cost:is-can-step
  (@desc "Checks whether a cost-profile rhocalc state has at least one accounted one-step transition. Incoming Error states are propagated unchanged. Malformed non-Error input is reported as Error.")
  (@params (
    (@param "Cost-profile rhocalc state to inspect")))
  (@return "True when at least one step exists, False when quiescent, or Error on malformed input"))

(= (lts:rho:cost:transitions $state)
   (let $step (lts:rho:cost:steps $state)
     (lts:rho:cost:step-state $step)))

(= (lts:rho:cost:trace-2 $state)
   (lts:trace-2 lts:rho:cost:transitions $state))

(= (lts:rho:cost:steps $state)
   (__cetta_lib_lts_rho_cost_steps $state))

(= (lts:rho:cost:step-cost (lts:rho:cost:step $cost $state))
   $cost)

(= (lts:rho:cost:step-cost $bad)
   (Error (lts:rho:cost:step-cost $bad)
     "expected lts:rho:cost:step record"))

(= (lts:rho:cost:step-state (lts:rho:cost:step $cost $state))
   $state)

(= (lts:rho:cost:step-state $bad)
   (Error (lts:rho:cost:step-state $bad)
     "expected lts:rho:cost:step record"))

(= (lts:rho:cost:is-normal-form $state)
   (lts:is-normal-form lts:rho:cost:transitions $state))

(= (lts:rho:cost:is-can-step $state)
   (lts:is-can-step lts:rho:cost:transitions $state))
