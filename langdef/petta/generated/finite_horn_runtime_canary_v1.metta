; Generic PeTTa execution machinery for generated finite-Horn clauses.

!(import! &self (library lib_tabling))
!(tabled (horn_prove_v1 $goal))

(= (horn:prove-v1 $goal) (horn_prove_v1 $goal))

(= (horn_prove_v1 $goal)
   (match &self
     (HornClauseV1 $goal $body)
     (let $_proof (horn:prove-body-v1 $body) $goal)))

(= (horn:prove-body-v1 HornBodyNilV1) HornProvedV1)
(= (horn:prove-body-v1 (HornBodyConsV1 $goal $rest))
   (let $_goal (horn_prove_v1 $goal)
     (horn:prove-body-v1 $rest)))

(HornClauseV1 (FiniteHornPackageDigestV1 sha256-b6e5aeed9ae76ad0199e1b021a10ef41cf202f26d325e8ff9f16e9a53ebe8950) HornBodyNilV1)
(HornClauseV1 (CanaryAncestorV1 $first $last) (HornBodyConsV1 (CanaryParentV1 $first $last) HornBodyNilV1))
(HornClauseV1 (CanaryAncestorV1 $first $last) (HornBodyConsV1 (CanaryParentV1 $first $middle) (HornBodyConsV1 (CanaryAncestorV1 $middle $last) HornBodyNilV1)))
(HornClauseV1 (CanaryLoopV1 $value) (HornBodyConsV1 (CanaryLoopV1 $value) HornBodyNilV1))
(HornClauseV1 (CanaryParentV1 CanaryAliceV1 CanaryBobV1) HornBodyNilV1)
(HornClauseV1 (CanaryParentV1 CanaryBobV1 CanaryCaraV1) HornBodyNilV1)
(HornClauseV1 (CanaryParentV1 CanaryBobV1 CanaryDaraV1) HornBodyNilV1)

!(collapse
   (horn:prove-v1
     (CanaryAncestorV1 CanaryAliceV1 $descendant)))
!(collapse
   (horn:prove-v1
     (FiniteHornPackageDigestV1 $digest)))
!(collapse
   (horn:prove-v1
     (CanaryLoopV1 CanaryMissingV1)))
