; Nil Geisweiller's eleven nuPLN tests, restated over lambda predicates.
;
; The combinator form writes "P holds of 0 with value tv" as
; (≞ (→ (⊙ 0) P) tv); here it is the statement P makes about 0, obtained by
; opening P's binder at 0.  The conjunction (∧ F G) is the lambda
; (lam x (∧ (F x) (G x))).  The evidence and the proof constructors are
; Nil's: cet, cefl, cefr and the product ",".  Where a test has a proof, the
; line ends with whether it is the proof term the combinator version gives.

!(import! &self nupln_lambda_core.metta)

!(bind! &kb (new-space))
!(add-atom &kb (nu:domain UInt))
!(add-atom &kb (nu:bias ∧))
!(add-atom &kb (: 0 UInt))
!(add-atom &kb (: 1 UInt))
!(add-atom &kb (: 2 UInt))
!(add-atom &kb (: F (-> UInt Bool)))
!(add-atom &kb (: G (-> UInt Bool)))
!(add-atom &kb (: f0t (≞ @(F 0) ⊤)))
!(add-atom &kb (: f1f (≞ @(F 1) ⊥)))
!(add-atom &kb (: f2t (≞ @(F 2) ⊤)))
!(add-atom &kb (: g0t (≞ @(G 0) ⊤)))
!(add-atom &kb (: g1t (≞ @(G 1) ⊤)))
!(add-atom &kb (: g2f (≞ @(G 2) ⊥)))

(= (conj) @(lam $x (∧ (F $x) (G $x))))

; 1-2. Type check F and G.
!(Test 1 (nu:type &kb nil 1 (: @F $a)))
!(Test 2 (nu:type &kb nil 1 (: @G $a)))

; 3. Type check the conjunction predicate.
!(let (MkSized $n (: $t $type)) (nu:type &kb nil 8 (: (conj) $a))
   (Test 3 (nu:source &kb $t) $type $n))

; 4. Type check the individual 0; the combinator test types (⊙ 0), the
;    predicate that exists only to state a fact about 0.
!(Test 4 (nu:type &kb nil 1 (: @0 $a)))

; 5. Type check the application of the conjunction to 0.
!(let (MkSized $n (: (quote ($h $c)) $type)) (nu:type-app &kb 10 (conj) @0)
   (Test 5 (Application (nu:source &kb @$h) $c) $type $n))

; 6-7. Evaluate the conjunction at 0 and at 1.
!(let $s (nu:at (conj) (0))
   (let (MkSized $n (: $prf $j)) (nu:eval &kb 3 (: $prf (≞ $s $tv)))
     (Test 6 $prf $j $n (SameProof (== $prf (cet f0t g0t))))))
!(let $s (nu:at (conj) (1))
   (let (MkSized $n (: $prf $j)) (nu:eval &kb 2 (: $prf (≞ $s $tv)))
     (Test 7 $prf $j $n (SameProof (== $prf (cefl f1f))))))

; 8. F at three values.
!(let (MkSized $n (: $prf $j))
      (nu:eval &kb 5 (: $prf (, (≞ @(F 0) ⊤) (, (≞ @(F 1) ⊥) (≞ @(F 2) ⊤)))))
   (Test 8 $prf $n (SameProof (== $prf (, f0t (, f1f f2t))))))

; 9-10. The conjunction at two and at three values.
!(let $g (nu:obligations (conj) ((Example (0) ⊤) (Example (1) ⊥)))
   (let (MkSized $n (: $prf $j)) (nu:eval &kb 6 (: $prf $g))
     (Test 9 $prf $n (SameProof (== $prf (, (cet f0t g0t) (cefl f1f)))))))
!(let $g (nu:obligations (conj) ((Example (0) ⊤) (Example (1) ⊥) (Example (2) ⊥)))
   (let (MkSized $n (: $prf $j)) (nu:eval &kb 9 (: $prf $g))
     (Test 10 $prf $n
       (SameProof (== $prf (, (cet f0t g0t) (, (cefl f1f) (cefr g2f))))))))

; 11. Learn the predicate from its truth table.  The learner types lambda
;     candidates by size and returns the first that explains the examples.
!(let (Learned $h $size $cost $proof)
      (nu:learn &kb (-> UInt Bool)
        ((Example (0) ⊤) (Example (1) ⊥) (Example (2) ⊥)) 12 27)
   (Test 11 (nu:source &kb $h) (TypingSize $size) (ProofSize $cost) $proof
     (SameProof (== $proof (, (cet f0t g0t) (, (cefl f1f) (cefr g2f)))))))

; The hypotheses the learner considered, by typing size.
!(Candidates (nu:level-counts &kb (-> UInt Bool) 1 8))
