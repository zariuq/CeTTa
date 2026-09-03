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

(HornClauseV1 (FiniteHornPackageDigestV1 sha256-9cff63946a86ec178c28459190ba0b9bc7e329bad4569859d087fa5b2779bea5) HornBodyNilV1)
(HornClauseV1 (GroundAppend (ground-resolution:cons $head $xs) $ys (ground-resolution:cons $head $zs)) (HornBodyConsV1 (GroundAppend $xs $ys $zs) HornBodyNilV1))
(HornClauseV1 (GroundAppend ground-resolution:nil $ys $ys) HornBodyNilV1)
(HornClauseV1 (GroundLiterals (ground-resolution:cons (ground-resolution:negative $pivot) $xs)) (HornBodyConsV1 (GroundLiterals $xs) HornBodyNilV1))
(HornClauseV1 (GroundLiterals ground-resolution:nil) HornBodyNilV1)
(HornClauseV1 (GroundLiterals (ground-resolution:cons (ground-resolution:positive $pivot) $xs)) (HornBodyConsV1 (GroundLiterals $xs) HornBodyNilV1))
(HornClauseV1 (GroundRemoveNegative $pivot (ground-resolution:cons (ground-resolution:negative $pivot) $xs) $xs) HornBodyNilV1)
(HornClauseV1 (GroundRemoveNegative $pivot (ground-resolution:cons $head $xs) (ground-resolution:cons $head $ys)) (HornBodyConsV1 (GroundRemoveNegative $pivot $xs $ys) HornBodyNilV1))
(HornClauseV1 (GroundRemovePositive $pivot (ground-resolution:cons (ground-resolution:positive $pivot) $xs) $xs) HornBodyNilV1)
(HornClauseV1 (GroundRemovePositive $pivot (ground-resolution:cons $head $xs) (ground-resolution:cons $head $ys)) (HornBodyConsV1 (GroundRemovePositive $pivot $xs $ys) HornBodyNilV1))
(HornClauseV1 (GroundResolve ground-resolution:positive-left $pivot $left $right $result) (HornBodyConsV1 (GroundRemovePositive $pivot $left $leftRest) (HornBodyConsV1 (GroundRemoveNegative $pivot $right $rightRest) (HornBodyConsV1 (GroundAppend $leftRest $rightRest $result) (HornBodyConsV1 (GroundLiterals $left) (HornBodyConsV1 (GroundLiterals $right) (HornBodyConsV1 (GroundLiterals $result) HornBodyNilV1)))))))
(HornClauseV1 (GroundResolve ground-resolution:positive-right $pivot $left $right $result) (HornBodyConsV1 (GroundRemoveNegative $pivot $left $leftRest) (HornBodyConsV1 (GroundRemovePositive $pivot $right $rightRest) (HornBodyConsV1 (GroundAppend $leftRest $rightRest $result) (HornBodyConsV1 (GroundLiterals $left) (HornBodyConsV1 (GroundLiterals $right) (HornBodyConsV1 (GroundLiterals $result) HornBodyNilV1)))))))

!(collapse
   (horn:prove-v1
     (GroundResolve
       ground-resolution:positive-left
       p
       (ground-resolution:cons
         (ground-resolution:positive p)
         ground-resolution:nil)
       (ground-resolution:cons
         (ground-resolution:negative p)
         ground-resolution:nil)
       ground-resolution:nil)))
