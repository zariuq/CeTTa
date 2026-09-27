; Nil's learning problem with distractors: k always-true predicates D1 ... Dk
; are declared before F and G.  Depth-first proof search over the combinator
; rules types every conjunction built on a distractor before it reaches F.
; Here the learner types lambda candidates one size at a time, so a
; distractor only adds candidates at its own sizes: k + 2 atoms at size 4 and
; (k + 2)^2 conjunctions at size 8.  Every rung learns Nil's predicate with
; Nil's proof.

!(import! &self nupln_lambda_core.metta)

(= (ladder-examples) ((Example (0) ⊤) (Example (1) ⊥) (Example (2) ⊥)))

; The knowledge base of one rung: the first k distractor names.
(= (distractor-names) (D1 D2 D3 D4 D5 D6 D7 D8 D9 D10 D11 D12 D13 D14 D15 D16 D17 D18 D19 D20 D21 D22 D23 D24 D25 D26 D27 D28 D29 D30 D31 D32))

(= (add-distractors $kb $names $k)
   (if (== $k 0)
       done
       (let ($name $rest) (decons-atom $names)
         (let* (($a (add-atom $kb (: $name (-> UInt Bool))))
                ($b (add-atom $kb (: (always $name 0) (≞ @($name 0) ⊤))))
                ($c (add-atom $kb (: (always $name 1) (≞ @($name 1) ⊤))))
                ($d (add-atom $kb (: (always $name 2) (≞ @($name 2) ⊤))))
                ($next (- $k 1)))
           (add-distractors $kb $rest $next)))))

(= (rung-kb $k)
   (let $kb (new-space)
     (let* (($u1 (add-atom $kb (nu:domain UInt)))
            ($u2 (add-atom $kb (nu:bias ∧)))
            ($u3 (add-atom $kb (: 0 UInt)))
            ($u4 (add-atom $kb (: 1 UInt)))
            ($u5 (add-atom $kb (: 2 UInt)))
            ($ds (add-distractors $kb (distractor-names) $k))
            ($f (add-atom $kb (: F (-> UInt Bool))))
            ($g (add-atom $kb (: G (-> UInt Bool))))
            ($f0 (add-atom $kb (: f0t (≞ @(F 0) ⊤))))
            ($f1 (add-atom $kb (: f1f (≞ @(F 1) ⊥))))
            ($f2 (add-atom $kb (: f2t (≞ @(F 2) ⊤))))
            ($g0 (add-atom $kb (: g0t (≞ @(G 0) ⊤))))
            ($g1 (add-atom $kb (: g1t (≞ @(G 1) ⊤))))
            ($g2 (add-atom $kb (: g2f (≞ @(G 2) ⊥)))))
       $kb)))

; Each rung has its own knowledge base, bound to a name.
!(bind! &rung1 (rung-kb 1))
!(bind! &rung2 (rung-kb 2))
!(bind! &rung8 (rung-kb 8))
!(bind! &rung32 (rung-kb 32))

(= (level-size $kb) (let $c (nu:candidates $kb (-> UInt Bool) 8) (size-atom $c)))

!(Rung 1 (nu:learn-shown &rung1 (-> UInt Bool) (ladder-examples) 8 27) (CandidatesAtSize8 (level-size &rung1)))
!(Rung 2 (nu:learn-shown &rung2 (-> UInt Bool) (ladder-examples) 8 27) (CandidatesAtSize8 (level-size &rung2)))
!(Rung 8 (nu:learn-shown &rung8 (-> UInt Bool) (ladder-examples) 8 27) (CandidatesAtSize8 (level-size &rung8)))
!(Rung 32 (nu:learn-shown &rung32 (-> UInt Bool) (ladder-examples) 8 27) (CandidatesAtSize8 (level-size &rung32)))
