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
(: lts:rho:cost:causal-prefix (-> Number Number Atom %Undefined%))
(: lts:rho:cost:step-cost (-> Atom Atom))
(: lts:rho:cost:step-state (-> Atom Atom))
(: lts:rho:cost:is-normal-form (-> Atom Bool))
(: lts:rho:cost:is-can-step (-> Atom Bool))

(@doc lts:rho:cost:transitions
  (@desc "Exhaustive meta-level one-step state frontier for rhocalc cost-profile states. This is the residual-state projection of lts:rho:cost:steps through lts:rho:cost:step-state: each successor is the residual state from an accounted step. Exact-cover enumeration may be combinatorial, so use lts:rho:cost:causal-prefix for bounded or untrusted execution. Incoming Error states are propagated unchanged, while malformed non-Error input is reported as Error.")
  (@params (
    (@param "Well-formed rhocalc cost-profile term")))
  (@return "Zero or more successor cost-profile states"))

(@doc lts:rho:cost:trace-2
  (@desc "Exhaustively enumerates exact two-step cost-profile rho traces as pairs of intermediate and final residual states. This requests every exact-cover witness at both steps and may be combinatorial; use lts:rho:cost:causal-prefix for bounded or untrusted execution. Incoming Error states are propagated unchanged, while malformed non-Error input is reported as Error.")
  (@params (
    (@param "Cost-profile rhocalc state to inspect for two successive accounted steps")))
  (@return "Zero or more pairs of cost-profile states"))

(@doc lts:rho:cost:steps
  (@desc "Exhaustive meta-level one-step accounted frontier for rhocalc cost-profile states. This operation is appropriate when every exact-cover witness is requested; exact-cover enumeration may be combinatorial and is never silently truncated. Each successor is paired with the consumed signature product, and lts:rho:cost:transitions is its residual-state projection through lts:rho:cost:step-state. Use lts:rho:cost:causal-prefix for bounded or untrusted execution. Incoming Error states are propagated unchanged, while malformed non-Error input is reported as Error.")
  (@params (
    (@param "Well-formed rhocalc cost-profile term")))
  (@return "Zero or more lts:rho:cost:step records"))

(@doc lts:rho:cost:causal-trace
  (@desc "Runs one terminating pure cost-rho path and returns a causal funding receipt together with its final state. The funded operational transition is authoritative; this receipt is its checked observation. With one configured worker, firings follow the canonical sequential path. With multiple configured workers, compatible firings claim disjoint endpoint and purse occurrences and execute in concurrent waves; the emitted event order is a checked linearization, not a canonical schedule. Each lts:rho:cost:event has a fresh run-local EventId, earlier producer EventIds for consumed components produced during this run, nonempty lts:rho:cost:funding records carrying the selected purse locations and head signatures, and the raw consumed signature. Repeated cause IDs are retained, while indistinguishable initial source occurrences acquire no stable wire identity. Use lts:rho:cost:causal-prefix when termination is not known.")
  (@params (
    (@param "Well-formed pure rhocalc cost-profile term")))
  (@return "(lts:rho:cost:receipt (<ordered event records>) <final cost-profile state>) or Error on malformed input"))

(@doc lts:rho:cost:causal-prefix
  (@desc "Runs within separate firing and exact-cover-search allowances, returning a checked causal funding observation plus an explicit status. The funded operational transition remains authoritative. Event IDs are local to this run and do not assign stable identities to indistinguishable initial source occurrences. One cumulative search allowance is shared across the entire trace. With multiple configured workers, compatible candidates already yielded by a bounded search may execute as a concurrent wave without materializing the rest of the frontier; this is a legal interleaving refinement, while an incomplete empty search is never treated as quiescence. The result is (lts:rho:cost:prefix STATUS RECEIPT). STATUS is lts:rho:cost:quiescent only after exhaustive search establishes that the residual has no accounted successor, lts:rho:cost:fuel-exhausted when the firing allowance ends before another frontier check, and lts:rho:cost:search-exhausted when the cumulative cover-search allowance ends while further search remains semantically relevant. Search exhaustion is never reported as quiescence. Zero firing allowance performs no cover search.")
  (@params (
    (@param "Nonnegative machine-integer firing bound")
    (@param "Nonnegative machine-integer exact-cover search-work bound")
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
  (@desc "Checks whether a cost-profile rhocalc state has an empty exhaustive accounted one-step frontier. This may enumerate a combinatorial exact-cover family; use lts:rho:cost:causal-prefix when the check must be bounded. Incoming Error states are propagated unchanged. Malformed non-Error input is reported as Error rather than being treated as quiescent.")
  (@params (
    (@param "Cost-profile rhocalc state to inspect")))
  (@return "True when the accounted one-step frontier is empty, False when at least one step exists, or Error on malformed input"))

(@doc lts:rho:cost:is-can-step
  (@desc "Checks whether a cost-profile rhocalc state has at least one transition through the exhaustive accounted frontier. This interface retains exhaustive-frontier semantics and may enumerate a combinatorial exact-cover family; use lts:rho:cost:causal-prefix for a bounded first-witness check. Incoming Error states are propagated unchanged. Malformed non-Error input is reported as Error.")
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

(= (lts:rho:cost:causal-prefix $fuel $search-fuel $state)
   (__cetta_lib_lts_rho_cost_causal_prefix $fuel $search-fuel $state))

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
