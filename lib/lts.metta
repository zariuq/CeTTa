; Generic LTS meta surface.
;
; Import this module when you want an explicit step-parameterized API:
;   !(import! &self lts)
;   (lts:transitions $step $state)
;   (lts:trace-2 $step $state)
;
; The passed step relation must:
; - emit exactly the one-step successor frontier via nondeterministic results
; - emit zero results on quiescent states via (superpose ())
; - return Error on malformed state/input rather than silently pretending the
;   state is quiescent

(: lts:transitions (-> Atom Atom %Undefined%))
(: lts:trace-2 (-> Atom Atom %Undefined%))
(: lts:is-normal-form (-> Atom Atom Bool))
(: lts:is-can-step (-> Atom Atom Bool))

(@doc lts:transitions
  (@desc "Generic one-step LTS frontier adapter. Applies the supplied step relation to the supplied state, preserving whatever one-step successor surface that relation exposes.")
  (@params (
    (@param "One-step step relation")
    (@param "State to inspect")))
  (@return "Zero or more immediate successors, or Error if the step relation reports Error"))

(@doc lts:trace-2
  (@desc "Enumerates exact two-step traces for a supplied step relation as pairs of intermediate and final states. If the step relation reports Error at either hop, that Error is propagated unchanged.")
  (@params (
    (@param "One-step step relation")
    (@param "State to inspect for two successive steps")))
  (@return "Zero or more pairs of intermediate and final states, or Error"))

(@doc lts:is-normal-form
  (@desc "Checks whether a supplied step relation exposes an empty one-step frontier on the supplied state. This only works correctly when quiescent states are reported by zero results rather than by leaving the relation unstated.")
  (@params (
    (@param "One-step step relation")
    (@param "State to inspect")))
  (@return "True when the one-step frontier is empty, False when at least one successor exists, or Error when the step relation reports Error"))

(@doc lts:is-can-step
  (@desc "Checks whether a supplied step relation exposes at least one one-step successor on the supplied state. This is the boolean complement of lts:is-normal-form on non-Error inputs, so the same quiescence contract applies.")
  (@params (
    (@param "One-step step relation")
    (@param "State to inspect")))
  (@return "True when at least one successor exists, False when the frontier is empty, or Error when the step relation reports Error"))

(= (lts:transitions $step $state)
   ($step $state))

(= (lts:trace-2 $step $state)
   (let $mid ($step $state)
     (let $end ($step $mid)
       ($mid $end))))

(= (lts:is-normal-form $step $state)
   (case (collapse ($step $state))
     ((((Error $ctx $msg)) (Error $ctx $msg))
      (() True)
      ($_ False))))

(= (lts:is-can-step $step $state)
   (case (lts:is-normal-form $step $state)
     (((Error $ctx $msg) (Error $ctx $msg))
      (True False)
      (False True))))
