; Run with cettap --no-results examples/prime/math_spaces.metta.
!(import! &self math_space_store)
!(bind! &math (math:new "arithmetic"))
!(math:declare &math (quote (hol:inductive Nat (u 0) (: Z Nat) (: S (-> Nat Nat)))))
!(math:declare &math (quote (hol:define add (-> Nat Nat Nat)
   (= (add $n Z) $n) (= (add $n (S $m)) (S (add $n $m))))))
!(let $value (math:run &math (quote (add (S Z) (S Z)))) (println! (Value $value)))

; Statements and strategies are ordinary functions returning syntax as data.
(= (right-unit-statement) (quote (-> (n : Nat) (id Nat (add n Z) n))))
(= (right-unit-proof) (quote (lam n (refl n))))
!(math:goal &math "right-unit" type (right-unit-statement) "Addition by zero preserves its input.")
!(math:propose &math "wrong" "right-unit" "reflexivity" (quote (lam n (refl (S n)))))
!(chain (math:check &math "wrong" 100000) $attempt
   (let $status (math:attempt-status &math "wrong") (println! (Evidence $status))))
!(let $status (math:goal-status &math "right-unit" 100000) (println! $status))
!(math:propose &math "right" "right-unit" "reflexivity" (right-unit-proof))
!(chain (math:check &math "right" 100000) $attempt
   (let $status (math:attempt-status &math "right") (println! (Evidence $status))))
!(let $publication (math:publish &math "right" right-unit 100000) (println! $publication))

; Restoring allocates a fresh theory and checks the retained proof again.
!(let $snapshot (math:serialize &math)
   (case (math:restore $snapshot 100000)
     (((Math:Restored $restored)
        (let $status (math:goal-status $restored "right-unit" 100000) (println! (Restored $status))))
      ($other (println! $other)))))
