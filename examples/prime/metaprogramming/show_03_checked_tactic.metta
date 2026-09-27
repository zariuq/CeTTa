; Why it matters: a search procedure (hand-written, learned or generated) may
; propose proofs, but only the small checker decides what is proved, so a
; wrong tactic can waste time and never publish a false theorem.
;
; `tac:search` is ordinary MeTTa: backward proof search for implicational
; goals, and goals quantified over propositions, that builds a pf: proof term.
; Its output is submitted to set:proves, published with set:theorem and
; compiled by set:native-proof into a kernel term.  A sloppy tactic's
; candidate is refuted with the checker's reason.

(: A prop)
(: B prop)
(: C prop)

; Hypotheses are a list, innermost first; (pf:hyp i) names the i-th one.
(= (tac:hyps $hyps $i)
   (if (== $hyps ()) (empty)
       (let $h (car-atom $hyps)
         (let $t (cdr-atom $hyps)
           (superpose ((Hyp $i $h) (tac:hyps $t (+ $i 1))))))))

; Use a hypothesis of type (imp c1 (imp c2 ... goal)): prove each ci.
(= (tac:apply $proof $type $goal $hyps $d)
   (if (== $type $goal) $proof
       (unify $type (imp $c $rest)
         (let $sub (tac:search $hyps $c $d)
           (tac:apply (pf:imp-elim $proof $sub) $rest $goal $hyps $d))
         (empty))))

; Introduction first (it is invertible), then use a hypothesis.
(= (tac:search $hyps $goal $d)
   (if (<= $d 0) (empty)
     (let $d1 (- $d 1)
       (unify $goal (imp $a $b)
         (let $body (tac:search (cons-atom $a $hyps) $b $d1) (pf:imp-intro $body))
         (unify $goal (all prop (lam $p $inner))
           (let $body (tac:search $hyps $inner $d1) (pf:all-intro $body))
           (let (Hyp $i $h) (tac:hyps $hyps 0)
             (tac:apply (pf:hyp $i) $h $goal $hyps $d1)))))))

; Iterative deepening returns a smallest-depth proof, or NoProof.
(= (tac:prove $goal $d $max)
   (if (> $d $max) (NoProof $goal)
       (let $found (collapse (tac:search () $goal $d))
         (if (== $found ())
             (tac:prove $goal (+ $d 1) $max)
             (car-atom $found)))))

(= (tac:check $goal)
   (let $p (tac:prove $goal 1 6)
     (unify $p (NoProof $g) $p
       (let $ok (set:proves $p $goal) (Proved $goal $p $ok)))))

; Tautologies: identity, K, composition, S, and a quantified identity.
!(tac:check (imp A A))
!(tac:check (imp A (imp B A)))
!(tac:check (imp (imp A B) (imp (imp B C) (imp A C))))
!(tac:check (imp (imp A (imp B C)) (imp (imp A B) (imp A C))))
!(tac:check (all prop (lam p (imp p p))))

; Non-theorems: no proof is found, so nothing is claimed.  Peirce's law is
; classically valid but has no proof in this implicational calculus.
!(tac:check (imp A B))
!(tac:check (imp (imp (imp A B) A) A))

; Publication: the checker re-checks the tactic's term, records it, and the
; published name is then usable as a known fact.
!(let $goal (imp (imp A B) (imp (imp B C) (imp A C)))
   (let $p (tac:prove $goal 1 6)
     (set:theorem composition $goal $p)))
!(set:axiom a-to-b (imp A B))
!(set:axiom b-to-c (imp B C))
!(set:proves (pf:imp-elim (pf:imp-elim (pf:known composition) (pf:known a-to-b))
                          (pf:known b-to-c))
             (imp A C))
; The published proof compiles to a kernel lambda term.
!(let (SetNativeProofV1 $name $prop $proof $term $type $context $rules $assumptions $digest)
      (set:native-proof composition)
      (Compiled $name $term))

; A sloppy tactic that always answers with the newest hypothesis.
(= (tac:sloppy (imp $a $b)) (pf:imp-intro (tac:sloppy $b)))
(= (tac:sloppy $atom) (if (== (get-metatype $atom) Symbol) (pf:hyp 0) (empty)))
!(let $p (tac:sloppy (imp A (imp B A))) (Candidate $p))
!(let $p (tac:sloppy (imp A (imp B A))) (try (set:proves $p (imp A (imp B A)))))
!(let $p (tac:sloppy (imp A (imp B A))) (try (set:theorem sloppy-k (imp A (imp B A)) $p)))
!(try (set:proves (pf:known sloppy-k) (imp A (imp B A))))
