;; Row 11 — BC/FC frontier step for the HE-family execution surface.
;;
;; This is deliberately one proof-frontier expansion.  It is not recursive
;; closure disguised as a chainer.  The sibling .zero.metta file has identical
;; rules and uses Zero's authored (! subject) observation presentation.
;; The term shape follows the typed proof applications used by Nil's chainers:
;; backward expansion exposes the required premise, while forward expansion
;; constructs the conclusion proof from that premise.

(= (bc-frontier (typed $rule (arrow (typed $proof $premise) $conclusion)) (typed (apply $rule $proof) $conclusion)) (need (typed $proof $premise)))
(= (fc-frontier (typed $proof $premise) (typed $rule (arrow (typed $proof $premise) $conclusion))) (typed (apply $rule $proof) $conclusion))

!(bc-frontier (typed mp (arrow (typed a A) B)) (typed (apply mp a) B))
!(fc-frontier (typed a A) (typed mp (arrow (typed a A) B)))
