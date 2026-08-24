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
