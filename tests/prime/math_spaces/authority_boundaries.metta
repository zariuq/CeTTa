!(import! &self math_space)
!(bind! &research (math:new "boundaries"))
!(math:declare &research (quote (hol:inductive Nat (u 0) (: Z Nat) (: S (-> Nat Nat)))))
!(math:goal &research "identity" type (quote (-> (n : Nat) (id Nat n n))) "Identity by reflexivity.")
!(math:goal &research "impossible" type (quote (id Nat Z (S Z))) "A deliberately false endpoint.")

; A note or a forged checked record cannot publish an invalid proof.
!(math:note &research "claim" "This has been proved." ("llm-response"))
!(assertEqual (math:goal-status &research "impossible" 100000)
   (Math:GoalStatus "impossible" open no-current-publication))
!(let* (($records (math:records &research)) ($key (math:task-key &research "impossible")))
   (add-atom $records
     (Math:Attempt "forged" "impossible" "claimed-success" $key
       (quote (refl Z)) checked (Established fabricated))))
!(assertEqual
   (case (math:publish &research "forged" forged 100000)
     (((Math:PublicationFailed $name $reason) Refused) ($other Wrong))) Refused)
!(assertEqual
   (case (math:check-native &research type (quote (id Nat Z (S Z))) (quote forged) 100000)
     (((Undetermined $reason) NoTheorem) ($other Wrong))) NoTheorem)

; The proposed proof is still data until the explicit checking call.
!(math:propose &research "valid" "identity" "reflexivity" (quote (lam n (refl n))))
!(assertEqual (math:attempt-entry &research "valid") (Math:Missing attempt "valid"))
!(let $_ (math:check &research "valid" 100000)
   (assertEqual (math:attempt-status &research "valid") checked))
!(assertEqual
   (math:attempt &research "valid" "identity" "changed-request" (quote (lam n (refl (S n)))) 100000)
   (Math:Conflict attempt "valid"))

; A changed statement invalidates its old candidate and checking contract.
!(let $records (math:records &research)
   (remove-atom $records
     (Math:Goal "identity" type (quote (-> (n : Nat) (id Nat n n))) "Identity by reflexivity.")))
!(math:goal &research "identity" type (quote (-> (n : Nat) (id Nat n (S n)))) "Changed endpoint.")
!(assertEqual (math:check &research "valid" 100000) (Math:Stale "valid" task-changed))
!(assertEqual (math:publish &research "valid" identity 100000) (Math:Stale "valid" task-changed))

; A changed declaration view also invalidates a pending candidate.
!(math:propose &research "pending" "impossible" "reflexivity" (quote (refl Z)))
!(math:declare &research (quote (hol:define identity-fn (-> Nat Nat) (= (identity-fn $x) $x))))
!(assertEqual (math:check &research "pending" 100000) (Math:Stale "pending" task-changed))

; Research notes may contain quoted code, which is never activated by reading.
!(bind! &note-counter (new-state 0))
!(math:note &research "quoted" (quote (change-state! &note-counter 1)) ())
!(assertEqual (math:notes &research)
   ((Math:Note "claim" "This has been proved." ("llm-response"))
    (Math:Note "quoted" (quote (change-state! &note-counter 1)) ())))
!(assertEqual (get-state &note-counter) 0)
