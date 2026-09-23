; Search discovers the proof term; native replay earns its dependent use.
; This closed-premise arrow fragment is supported by both current engines.
!(bind! &thy (new-space))
!(add-atom &thy (: A (u 0)))
!(add-atom &thy (: a A))
!(add-atom &thy (: b A))
!(add-atom &thy (: f (-> A A)))
!(add-atom &thy (: P (-> A (u 0))))
!(add-atom &thy (: h (P a)))
!(add-atom &thy (: next (-> (P a) (P (f a)))))
!(add-atom &thy (chaining-rule next))
!(set:define &thy retainIndexed
   (-> (A : (u 0)) (x : A) (P : (-> (z : A) (u 0))) (w : (P x)) (P x))
   (= (retainIndexed $A $x $P $w) $w))
!(search-first-inhabitant &thy (P (f a)) 2)
!(let (he-accept (typed-answer-v2 (: $proof $type) $substitution))
      (search-first-inhabitant &thy (P (f a)) 2)
      (type:check &thy (retainIndexed A (f a) P $proof) (P (f a))))
!(let (he-accept (typed-answer-v2 (: $proof $type) $substitution))
      (search-first-inhabitant &thy (P (f a)) 2)
      (type:eq &thy (retainIndexed A (f a) P $proof) $proof))
; The same discovered proof cannot be moved to a different dependent index.
!(let (he-accept (typed-answer-v2 (: $proof $type) $substitution))
      (search-first-inhabitant &thy (P (f a)) 2)
      (type:check &thy (retainIndexed A (f b) P $proof) (P (f b))))
; Bounded exhaustion and absence at the selected depth are not refutations.
!(search-first-inhabitant &thy (P (f a)) 2 1)
!(search-first-inhabitant &thy (P (f a)) 0)
!(type:check &thy (retainIndexed A (f a) P unknownEvidence) (P (f a)))
; A checked search proof feeds a continuation producing different evidence.
!(set:define &thy feedEvidence
   (-> (A : (u 0)) (x : A) (P : (-> (z : A) (u 0))) (w : (P x))
       (Q : (u 0)) (k : (-> (p : (P x)) Q)) Q)
   (= (feedEvidence $A $x $P $w $Q $k) ($k $w)))
!(let (he-accept (typed-answer-v2 (: $proof $type) $substitution))
      (search-first-inhabitant &thy (P (f a)) 2)
      (type:check &thy
        (feedEvidence A (f a) P $proof (id A b b) (lam p (refl b))) (id A b b)))
!(let (he-accept (typed-answer-v2 (: $proof $type) $substitution))
      (search-first-inhabitant &thy (P (f a)) 2)
      (type:eq &thy
        (feedEvidence A (f a) P $proof (id A b b) (lam p (refl b))) (refl b)))
!(let (he-accept (typed-answer-v2 (: $proof $type) $substitution))
      (search-first-inhabitant &thy (P (f a)) 2)
      (type:check &thy
        (feedEvidence A (f a) P $proof (id A b b) (lam p (refl b))) (id A a a)))
!(let (he-accept (typed-answer-v2 (: $proof $type) $substitution))
      (search-first-inhabitant &thy (P (f a)) 2)
      (type:check &thy
        (feedEvidence A (f a) P $proof (id A b b) (lam p (refl a))) (id A b b)))
; Replay is relative to the current hypotheses, not to saved answer syntax.
!(let (he-accept (typed-answer-v2 (: $proof $type) $substitution))
      (search-first-inhabitant &thy (P (f a)) 2)
      (let $removed (remove-atom &thy (: h (P a)))
        (let $result (try (type:check &thy $proof (P (f a))))
          (car-atom $result))))
