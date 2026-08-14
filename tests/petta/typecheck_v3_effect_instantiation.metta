; A direct higher-order parameter instantiates the enclosing named effect.
(: apply-effect (-[$e]-> (-[$e]-> Atom Atom) Atom Atom))

(: keep-effect (-[det]-> Atom Atom))
(= (keep-effect $value) $value)

(: maybe-effect (-[semidet]-> Atom Atom))
(= (maybe-effect $_value) (empty))

(: use-deterministic-effect (-[det]-> Atom Atom))
(= (use-deterministic-effect $value)
   (apply-effect keep-effect $value))

(: use-semideterministic-effect (-[semidet]-> Atom Atom))
(= (use-semideterministic-effect $value)
   (apply-effect maybe-effect $value))

(: use-conservative-effect (-[nondet]-> Atom Atom))
(= (use-conservative-effect $value)
   (apply-effect keep-effect $value))

(: reject-effect-overclaim (-[det]-> Atom Atom))
(= (reject-effect-overclaim $value)
   (apply-effect maybe-effect $value))
