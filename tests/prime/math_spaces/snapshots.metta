!(import! &self math_space_store)
!(bind! &project (math:new "snapshot"))
!(math:declare &project (quote (hol:inductive Nat (u 0) (: Z Nat) (: S (-> Nat Nat)))))
!(math:declare &project (quote (hol:define add (-> Nat Nat Nat)
   (= (add $n Z) $n) (= (add $n (S $m)) (S (add $n $m))))))
!(math:goal &project "right-zero" type (quote (-> (n : Nat) (id Nat (add n Z) n))) "Right identity.")
!(chain (math:attempt &project "proof" "right-zero" "reflexivity" (quote (lam n (refl n))) 100000) $checked
   (assertEqual (math:attempt-status &project "proof") checked))
!(math:publish &project "proof" right-zero 100000)

; All uses of a shared variable stay related, including across context rows.
!(let (quote $left) (quote ((D a $x) (D b $x)))
   (let (quote $right) (quote ((D b $y) (D a $y)))
     (assertEqual (math:same-rows $left $right) True)))
!(let (quote $left) (quote ((D a $x) (D b $x)))
   (let (quote $right) (quote ((D b $y) (D a $z)))
     (assertEqual (math:same-rows $left $right) False)))
!(assertEqual (math:same-rows ((D a) (D a)) ((D a))) False)
!(assertEqual (math:same-rows ((D a) (D a)) ((D a) (D a))) True)
!(let (quote $research) (quote ((Goal first (P $x)) (Goal second (Q $x))))
   (case (math:decode-artifact (math:encode-artifact $research))
     (((Math:Decoded $restored) (assertEqual (=alpha $research $restored) True)))))
!(assertEqual (std:get-type (math:serialize &project)) String)

; Restoring rebuilds the native theorem ledger. Old attempts remain historical.
!(let $text (math:serialize &project)
   (case (math:restore $text 100000)
     (((Math:Restored $restored)
        (assertEqual (math:goal-status $restored "right-zero" 100000)
          (Math:GoalStatus "right-zero" checked (published right-zero)))))))
!(let $text (math:serialize &project)
   (case (math:restore $text 100000)
     (((Math:Restored $restored)
        (assertEqual (math:attempt-entry $restored "proof") (Math:Missing attempt "proof"))))))
!(let $text (math:serialize &project)
   (case (math:restore $text 100000)
     (((Math:Restored $restored)
        (let $_ (math:resume-attempt $restored "proof" "new-proof" 100000)
          (assertEqual (math:attempt-status $restored "new-proof") checked))))))
!(let $text (math:serialize &project)
   (case (math:restore $text 100000)
     (((Math:Restored $restored)
        (assertEqual (math:run $restored (quote (add (S Z) (S Z)))) (S (S Z)))))))

(= (snapshot:publications $bundle $entries)
   (case $bundle
     (((Math:Bundle 1 $name $sources $old $research $history $theory)
        (let $artifact (math:encode-artifact $entries)
          (Math:Bundle 1 $name $sources $artifact $research $history $theory))))))
(= (snapshot:theory $bundle $rows)
   (case $bundle
     (((Math:Bundle 1 $name $sources $publications $research $history $old)
        (let $artifact (math:encode-artifact $rows)
          (Math:Bundle 1 $name $sources $publications $research $history $artifact))))))
(= (snapshot:sources $bundle $entries)
   (case $bundle
     (((Math:Bundle 1 $name $old $publications $research $history $theory)
        (let $artifact (math:encode-artifact $entries)
          (Math:Bundle 1 $name $artifact $publications $research $history $theory))))))

; Recomputing every checksum cannot make an invalid proof pass replay.
!(let* (($bundle (math:bundle &project))
        ($bad ((Math:Publication right-zero "right-zero" type
          (quote (-> (n : Nat) (id Nat (add n Z) n))) (quote (lam n (refl (S n)))))))
        ($forged (snapshot:publications $bundle $bad)))
   (assertEqual
     (case (math:restore (math:pack-bundle $forged) 100000)
       (((Math:RestoreFailed publication $name $reason) RejectedProof) ($other Wrong)))
     RejectedProof))

; A claimed type row alone cannot restore a native theorem's authority.
!(let $bundle (snapshot:publications (math:bundle &project) ())
   (assertEqual (math:restore (math:pack-bundle $bundle) 100000)
     (Math:RestoreFailed theory-mismatch)))
!(let* (($original (math:bundle &project)) ($rows (math:theory-rows &project))
        ($extra (union-atom $rows ((Unexpected changed-row))))
        ($bundle (snapshot:theory $original $extra)))
   (assertEqual (math:restore (math:pack-bundle $bundle) 100000)
     (Math:RestoreFailed theory-mismatch)))
!(let* (($original (math:bundle &project)) ($rows (math:theory-rows &project))
        ($extra (cons-atom (car-atom $rows) $rows))
        ($bundle (snapshot:theory $original $extra)))
   (assertEqual (math:restore (math:pack-bundle $bundle) 100000)
     (Math:RestoreFailed theory-mismatch)))
!(let* (($original (math:bundle &project)) ($rows (math:theory-rows &project))
        ($reversed (foldl-atom $rows () $acc $row (cons-atom $row $acc)))
        ($bundle (snapshot:theory $original $reversed)))
   (assertEqual
     (case (math:restore (math:pack-bundle $bundle) 100000)
       (((Math:Restored $restored) Restored) ($other Wrong))) Restored))
!(let* (($original (math:bundle &project)) ($sources (math:sources &project))
        ($reversed (foldl-atom $sources () $acc $source (cons-atom $source $acc)))
        ($bundle (snapshot:sources $original $reversed)))
   (assertEqual
     (case (math:restore (math:pack-bundle $bundle) 100000)
       (((Math:Restored $restored) Restored) ($other Wrong))) Restored))

; The live reader refuses damaged artifacts and ambiguous wire fields.
!(assertEqual (math:decode-artifact (Math:Artifact "wrong" "()"))
   (Math:RestoreFailed artifact-digest))
!(assertEqual
   (math:restore "{\"schema\":\"prime.math-space.v1\",\"schema\":\"prime.math-space.v1\"}" 100000)
   (Math:RestoreFailed duplicate-or-invalid-json))
!(let $text (math:serialize &project)
   (assertEqual
     (case (math:restore $text 0)
       (((Math:RestoreFailed publication $name (Incomplete (checking-budget 0))) Unfinished)
        ($other Wrong))) Unfinished))

; A source artifact may describe a host effect without gaining permission to run it.
!(bind! &snapshot-counter (new-state 0))
!(let* (($original (math:bundle &project))
        ($bad ((quote (change-state! &snapshot-counter 1))))
        ($bundle (snapshot:sources $original $bad)))
   (assertEqual
     (case (math:restore (math:pack-bundle $bundle) 100000)
       (((Math:RestoreFailed snapshot-records) RefusedSource) ($other Wrong))) RefusedSource))
!(assertEqual (get-state &snapshot-counter) 0)
