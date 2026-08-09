;; Row 11 — the MeTTa Zero observation presentation of the BC/FC frontier step.
;; The rule atoms are identical to zero_bc_fc_frontier.metta.  Only the public
;; observation spelling differs: Zero authors (! subject) as an ordinary form.

(= (bc-frontier (typed $rule (arrow (typed $proof $premise) $conclusion)) (typed (apply $rule $proof) $conclusion)) (need (typed $proof $premise)))
(= (fc-frontier (typed $proof $premise) (typed $rule (arrow (typed $proof $premise) $conclusion))) (typed (apply $rule $proof) $conclusion))

(! (bc-frontier (typed mp (arrow (typed a A) B)) (typed (apply mp a) B)))
(! (fc-frontier (typed a A) (typed mp (arrow (typed a A) B))))
