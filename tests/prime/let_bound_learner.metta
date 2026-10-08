; A computation whose working memory reaches a collection point the
; collector cannot account for goes on without collecting: it keeps all its
; answers.  Here a learner's result, bound with let, is the same result the
; direct call gives.  The let-bound query runs first, in a fresh process.
!(import! &self ../../examples/prime/nupln_lambda/nupln_lambda_core.metta)
(= (examples) ((Example (0) ⊤) (Example (1) ⊥) (Example (2) ⊥)))
(= (names) (D1 D2 D3 D4 D5 D6 D7 D8))
(= (add-ds $kb $names)
   (if (== $names ()) done
       (let (quote ($n $rest)) (lift (decons-atom $names))
         (let* (($a (add-atom $kb (: $n (-> UInt Bool))))
                ($b (add-atom $kb (: (always $n 0) (≞ @($n 0) ⊤))))
                ($c (add-atom $kb (: (always $n 1) (≞ @($n 1) ⊤))))
                ($d (add-atom $kb (: (always $n 2) (≞ @($n 2) ⊤)))))
           (add-ds $kb $rest)){$a $b $c $d}){$n $rest}))
(= (kb8)
   (let $kb (new-space)
     (let* (($u1 (add-atom $kb (nu:domain UInt))) ($u2 (add-atom $kb (nu:bias ∧)))
            ($u3 (add-atom $kb (: 0 UInt))) ($u4 (add-atom $kb (: 1 UInt))) ($u5 (add-atom $kb (: 2 UInt)))
            ($ds (add-ds $kb (names)))
            ($f (add-atom $kb (: F (-> UInt Bool)))) ($g (add-atom $kb (: G (-> UInt Bool))))
            ($f0 (add-atom $kb (: f0t (≞ @(F 0) ⊤)))) ($f1 (add-atom $kb (: f1f (≞ @(F 1) ⊥))))
            ($f2 (add-atom $kb (: f2t (≞ @(F 2) ⊤)))) ($g0 (add-atom $kb (: g0t (≞ @(G 0) ⊤))))
            ($g1 (add-atom $kb (: g1t (≞ @(G 1) ⊤)))) ($g2 (add-atom $kb (: g2f (≞ @(G 2) ⊥)))))
       $kb)))
!(let $kb (kb8) (let $r (nu:learn $kb (-> UInt Bool) (examples) 8 27)
   (let (Learned $code $n $m $evidence) $r (learned $n $m $evidence))))
!(let $kb (kb8) (let (Learned $code $n $m $evidence) (nu:learn $kb (-> UInt Bool) (examples) 8 27)
   (learned $n $m $evidence)))
