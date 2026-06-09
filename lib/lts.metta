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
(: lts:box (-> Atom Atom Atom Bool))
(: lts:diamond (-> Atom Atom Atom Bool))

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

(@doc lts:box
  (@desc "Checks whether every one-step successor exposed by the supplied step relation satisfies the supplied unary predicate symbol. A pure singleton Error frontier propagates unchanged, matching lts:is-normal-form. An empty frontier makes lts:box vacuously True. Mixed frontiers that contain Error among ordinary successors are folded as ordinary frontiers rather than being promoted to Error.")
  (@params (
    (@param "One-step step relation")
    (@param "Unary Bool-valued predicate symbol over successor states")
    (@param "State to inspect")))
  (@return "True when every successor satisfies the predicate, False when at least one successor fails it, or Error when the step relation reports a pure singleton Error frontier"))

(@doc lts:diamond
  (@desc "Checks whether some one-step successor exposed by the supplied step relation satisfies the supplied unary predicate symbol. A pure singleton Error frontier propagates unchanged, matching lts:is-can-step. An empty frontier makes lts:diamond False. Mixed frontiers that contain Error among ordinary successors are folded as ordinary frontiers rather than being promoted to Error.")
  (@params (
    (@param "One-step step relation")
    (@param "Unary Bool-valued predicate symbol over successor states")
    (@param "State to inspect")))
  (@return "True when some successor satisfies the predicate, False when no successor satisfies it, or Error when the step relation reports a pure singleton Error frontier"))

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

(= (lts:box $step $phi $state)
   (case (collapse ($step $state))
     ((((Error $ctx $msg)) (Error $ctx $msg))
      (() True)
      ($frontier
       (foldl-atom $frontier True $acc $q
         (if $acc ($phi $q) False))))))

(= (lts:diamond $step $phi $state)
   (case (collapse ($step $state))
     ((((Error $ctx $msg)) (Error $ctx $msg))
      (() False)
      ($frontier
       (foldl-atom $frontier False $acc $q
         (if $acc True ($phi $q)))))))
