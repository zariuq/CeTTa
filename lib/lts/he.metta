!(import! &self lts)

; Public HE evaluator LTS specialization over the current one-step HE frontier.
;
; Import pattern:
;   !(import! &self lts:he)
;
; Public API family:
;   lts:he:transitions
;   lts:he:step-state
;   lts:he:trace-2
;   lts:he:is-normal-form
;   lts:he:is-can-step
;
; The current frontier exposes direct top-level one-step reductions together
; with chain/let/let*/case/switch-context progression, and emits quoted
; successors so the frontier remains stable as data instead of being eagerly
; evaluated again.

(: lts:he:transitions (-> Atom %Undefined%))
(: lts:he:step-state (-> Atom %Undefined%))
(: lts:he:trace-2 (-> Atom %Undefined%))
(: lts:he:is-normal-form (-> Atom Bool))
(: lts:he:is-can-step (-> Atom Bool))

(@doc lts:he:transitions
  (@desc "Meta-level one-step HE frontier. This is the unquoted-state front door for lts:he:step-state: it quotes the input once, emits each immediate successor as a quoted MeTTa state, emits zero results on quiescent atoms, and propagates Error outcomes.")
  (@params (
    (@param "MeTTa atom to inspect for one evaluator step")))
  (@return "Zero or more quoted immediate HE successors"))

(@doc lts:he:step-state
  (@desc "Quoted-state one-step HE frontier. This is lts:transitions bound to the raw quoted-state HE successor primitive: it accepts a frozen quoted HE state and emits quoted immediate successors, so repeated stepping composes at the MeTTa surface. Incoming Error atoms are propagated unchanged. Ordinary unquoted atoms are not implicitly frozen or stepped here, so they yield zero results.")
  (@params (
    (@param "Quoted HE state to inspect for one evaluator step")))
  (@return "Zero or more quoted immediate HE successors"))

(@doc lts:he:trace-2
  (@desc "Enumerates exact two-step HE traces as pairs of quoted intermediate and final states.")
  (@params (
    (@param "MeTTa atom to inspect for two successive HE steps")))
  (@return "Zero or more pairs of quoted HE states"))

(@doc lts:he:is-normal-form
  (@desc "Checks whether a MeTTa atom has an empty one-step HE frontier. This is lts:is-normal-form over lts:he:step-state after quoting the input once.")
  (@params (
    (@param "MeTTa atom to inspect")))
  (@return "True when no one-step successor exists, False when at least one successor exists, or Error when evaluation reports Error"))

(@doc lts:he:is-can-step
  (@desc "Checks whether a MeTTa atom has at least one one-step HE successor. This is lts:is-can-step over lts:he:step-state after quoting the input once.")
  (@params (
    (@param "MeTTa atom to inspect")))
  (@return "True when at least one successor exists, False when quiescent, or Error when evaluation reports Error"))

(= (lts:he:transitions $state)
   (__cetta_lib_lts_he_transitions (quote $state)))

(= (lts:he:step-state $qstate)
   (__cetta_lib_lts_he_transitions $qstate))

(= (lts:he:trace-2 $state)
   (lts:trace-2 lts:he:step-state (quote $state)))

(= (lts:he:is-normal-form $state)
   (lts:is-normal-form lts:he:step-state (quote $state)))

(= (lts:he:is-can-step $state)
   (lts:is-can-step lts:he:step-state (quote $state)))
