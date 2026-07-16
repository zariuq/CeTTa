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
;   lts:rho:cost:causal-trace
;   lts:rho:cost:causal-prefix
;   lts:rho:cost:step-cost
;   lts:rho:cost:step-state
;   lts:rho:cost:is-normal-form
;   lts:rho:cost:is-can-step

(: lts:rho:cost:transitions (-> Atom %Undefined%))
(: lts:rho:cost:trace-2 (-> Atom %Undefined%))
(: lts:rho:cost:steps (-> Atom %Undefined%))
(: lts:rho:cost:causal-trace (-> Atom %Undefined%))
(: lts:rho:cost:causal-prefix (-> Number Atom %Undefined%))
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

(@doc lts:rho:cost:causal-trace
  (@desc "Runs one canonical terminating pure cost-rho path and returns an occurrence-preserving causal receipt together with its final state. Each lts:rho:cost:event record contains a fresh local EventId, a list of producer EventIds for the exact participant and purse occurrences consumed, a nonempty list of lts:rho:cost:funding records carrying actual purse surfaces and head signatures, and the raw consumed signature. Cause lists retain repeated producer IDs. Events are selected before successor-state deduplication and emitted in causal order. Use lts:rho:cost:causal-prefix when termination is not known.")
  (@params (
    (@param "Well-formed pure rhocalc cost-profile term")))
  (@return "(lts:rho:cost:receipt (<ordered event records>) <final cost-profile state>) or Error on malformed input"))

(@doc lts:rho:cost:causal-prefix
  (@desc "Runs at most the supplied number of canonical pure cost-rho firings and returns an occurrence-preserving causal receipt plus an explicit status. The result is (lts:rho:cost:prefix STATUS RECEIPT), where STATUS is lts:rho:cost:quiescent exactly when the residual has no accounted successor, and lts:rho:cost:fuel-exhausted when another successor exists but the bound has been consumed. A zero bound still checks the frontier, so exhaustion is never reported as quiescence.")
  (@params (
    (@param "Nonnegative machine-integer firing bound")
    (@param "Well-formed pure rhocalc cost-profile term")))
  (@return "(lts:rho:cost:prefix STATUS (lts:rho:cost:receipt (<ordered event records>) <residual cost-profile state>)) or Error on malformed input"))

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

(= (lts:rho:cost:causal-trace $state)
   (__cetta_lib_lts_rho_cost_causal_trace $state))

(= (lts:rho:cost:causal-prefix $fuel $state)
   (__cetta_lib_lts_rho_cost_causal_prefix $fuel $state))

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
