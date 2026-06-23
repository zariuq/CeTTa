; ===========================================================================
; rhometta_deployment_planner  --  concurrent backward chaining for deployment
; planning / service composition.
;
; A request asks for an EU low-latency deployment of a GDPR-safe model.  A plan
; exists only if seven independently-checkable judgments hold together:
;   1. the request has the task being served,
;   2. the request has the required privacy regime,
;   3. the model supports that task,
;   4. the model satisfies that privacy regime,
;   5. the model requires some accelerator GPU,
;   6. a provider offers exactly that GPU in some region+latency class,
;   7. the request accepts that region+latency class.
;
; The seven premises are proved in PARALLEL rho cells.  The join re-matches the
; rule against the resolved premises, so the shared variables $req, $model,
; $provider, $gpu, $region, and $latency must all agree.  Wrong-region /
; wrong-latency / wrong-GPU / wrong-task / wrong-privacy branches vanish cleanly
; at the join.
;
; This is still one resolution step deep, but it is a genuinely planning-shaped
; example rather than a toy mortal/type sentence: rhometta computes the may-set
; of VALID deployment plans and prunes the invalid combinations by shared-variable
; agreement at the rendezvous.
;
; Scaling note: this demo intentionally prunes at the join, so invalid
; combinations are generated and then killed.  That is perfect for showing the
; semantics, but a production planner would index or thread bindings earlier to
; avoid the full premise cross-product on large catalogs.
; ===========================================================================

!(import! &self rhometta)

!(bind! &kb (new-space))

; --- request profile ---------------------------------------------------------
!(add-atom &kb (: req-task     (ReqTask eu-chat Dialogue)))
!(add-atom &kb (: req-privacy  (ReqPrivacy eu-chat GDPR)))
!(add-atom &kb (: req-offer    (ReqOffer eu-chat EU LowLatency)))

!(add-atom &kb (: req-task-code    (ReqTask eu-code Code)))
!(add-atom &kb (: req-privacy-code (ReqPrivacy eu-code GDPR)))
!(add-atom &kb (: req-offer-code   (ReqOffer eu-code EU LowLatency)))

!(add-atom &kb (: req-task-voice    (ReqTask eu-voice Speech)))
!(add-atom &kb (: req-privacy-voice (ReqPrivacy eu-voice GDPR)))
!(add-atom &kb (: req-offer-voice   (ReqOffer eu-voice EU LowLatency)))

; --- model catalog -----------------------------------------------------------
!(add-atom &kb (: m7b-task   (ModelTask llm7b Dialogue)))
!(add-atom &kb (: m7b-priv   (ModelPrivacy llm7b GDPR)))
!(add-atom &kb (: m7b-gpu    (ModelNeeds llm7b L40S)))

!(add-atom &kb (: m70b-task  (ModelTask llm70bq4 Dialogue)))
!(add-atom &kb (: m70b-priv  (ModelPrivacy llm70bq4 GDPR)))
!(add-atom &kb (: m70b-gpu   (ModelNeeds llm70bq4 H100)))

!(add-atom &kb (: code-task  (ModelTask codegen Code)))
!(add-atom &kb (: code-priv  (ModelPrivacy codegen GDPR)))
!(add-atom &kb (: code-gpu   (ModelNeeds codegen H100)))

!(add-atom &kb (: voice-task (ModelTask voicegen Speech)))
!(add-atom &kb (: voice-priv (ModelPrivacy voicegen HIPAA)))
!(add-atom &kb (: voice-gpu  (ModelNeeds voicegen H100)))

; --- provider catalog --------------------------------------------------------
!(add-atom &kb (: pA-offer (ProviderOffer cloudA H100 EU LowLatency)))
!(add-atom &kb (: pB-offer (ProviderOffer cloudB L40S EU LowLatency)))
!(add-atom &kb (: pC-offer (ProviderOffer cloudC H100 US LowLatency)))
!(add-atom &kb (: pD-offer (ProviderOffer cloudD H100 EU Standard)))

; --- deployment rule ---------------------------------------------------------
!(add-atom &kb
  (: deploy
     (-> (: $rqT (ReqTask $req $task))
         (: $rqP (ReqPrivacy $req $privacy))
         (: $m   (ModelTask $model $task))
         (: $mp  (ModelPrivacy $model $privacy))
         (: $mg  (ModelNeeds $model $gpu))
         (: $po  (ProviderOffer $provider $gpu $region $latency))
         (: $rqO (ReqOffer $req $region $latency))
         (Deploy $req $provider $model))))

; Optional external ranking metadata per complete plan.
!(add-atom &kb (plan-stv (Deploy eu-chat cloudA llm70bq4) (stv 0.93 0.90)))
!(add-atom &kb (plan-stv (Deploy eu-chat cloudB llm7b)     (stv 0.88 0.92)))
!(add-atom &kb (plan-stv (Deploy eu-code cloudA codegen)   (stv 0.91 0.88)))

; --- rho plumbing ------------------------------------------------------------
(= (chan $n)
   (if (== $n 0) (rho:quote rho:nil)
       (rho:quote (rho:send (chan (- $n 1)) rho:nil))))
(= (unval (rho:quote (rho:val $x))) $x)
(= (cell $i $job)
   (rho:par (rho:send (chan $i) (rhometta:eval $job))
            (rho:recv (chan $i) $y (rho:drop $y))))

; --- one backward-chaining resolution step ----------------------------------
(= (step (: $prf $concl) $depth $b)
   (match &kb (: $prf $concl) (: $prf $concl)))
(= (step (: $prf $concl) (S $d) $b)
   (match &kb (: $rule (-> (: $p1 $t1) (: $p2 $t2) (: $p3 $t3)
                           (: $p4 $t4) (: $p5 $t5) (: $p6 $t6)
                           (: $p7 $t7) $concl))
     (assemble7 $rule
       (: $p1 $t1) (: $p2 $t2) (: $p3 $t3) (: $p4 $t4)
       (: $p5 $t5) (: $p6 $t6) (: $p7 $t7)
       $d $b)))

; --- 7-way AND-node ----------------------------------------------------------
(= (assemble7 $rule $prem1 $prem2 $prem3 $prem4 $prem5 $prem6 $prem7 $d $b)
   (let* (($c1 (+ (* $b 8) 1)) ($c2 (+ (* $b 8) 2)) ($c3 (+ (* $b 8) 3))
          ($c4 (+ (* $b 8) 4)) ($c5 (+ (* $b 8) 5)) ($c6 (+ (* $b 8) 6))
          ($c7 (+ (* $b 8) 7)) ($c8 (+ (* $b 8) 8)))
     (rho:par
       (rho:send (chan $c1) (rhometta:eval (step $prem1 $d $c1)))
       (rho:send (chan $c2) (rhometta:eval (step $prem2 $d $c2)))
       (rho:send (chan $c3) (rhometta:eval (step $prem3 $d $c3)))
       (rho:send (chan $c4) (rhometta:eval (step $prem4 $d $c4)))
       (rho:send (chan $c5) (rhometta:eval (step $prem5 $d $c5)))
       (rho:send (chan $c6) (rhometta:eval (step $prem6 $d $c6)))
       (rho:send (chan $c7) (rhometta:eval (step $prem7 $d $c7)))
       (rho:recv (chan $c1) $r1
         (rho:recv (chan $c2) $r2
           (rho:recv (chan $c3) $r3
             (rho:recv (chan $c4) $r4
               (rho:recv (chan $c5) $r5
                 (rho:recv (chan $c6) $r6
                   (rho:recv (chan $c7) $r7
                     (rho:send (chan $c8)
                       (rhometta:eval
                         (combine7 $rule
                           (unval $r1) (unval $r2) (unval $r3) (unval $r4)
                           (unval $r5) (unval $r6) (unval $r7)))))))))))
       (rho:recv (chan $c8) $z (rho:drop $z)))))

(= (plan-tv $goal)
   (let $r (collapse (match &kb (plan-stv $goal $tv) $tv))
     (if (== $r ()) (stv 0.80 0.80) (car-atom $r))))

(= (combine7 $rule (: $p1 $t1) (: $p2 $t2) (: $p3 $t3) (: $p4 $t4)
                   (: $p5 $t5) (: $p6 $t6) (: $p7 $t7))
   (let $res
        (collapse
          (match &kb (: $rule (-> (: $p1 $t1) (: $p2 $t2) (: $p3 $t3)
                                  (: $p4 $t4) (: $p5 $t5) (: $p6 $t6)
                                  (: $p7 $t7) $concl))
            (proved (: ($rule $p1 $p2 $p3 $p4 $p5 $p6 $p7) $concl)
                    (plan-tv $concl))))
     (if (== $res ()) rho:nil (car-atom $res))))

; --- projections over the may-set -------------------------------------------
(= (proof-of (rho:val (proved $j $tv))) (proved $j $tv))
(= (proof-of $other) (empty))
(= (judgment-of (rho:val (proved $j $tv))) $j)
(= (judgment-of $other) (empty))
(= (stuck-of $r)
   (case $r
     (((rho:val (proved $j $tv)) (empty))
      (rho:nil (empty))
      ($other $other))))

; sequential oracle in nilbc style over the same KB
(= (bc $kb $_ (: $proof $theorem))
   (match $kb (: $proof $theorem) (: $proof $theorem)))
(= (bc $kb (S $depth) (: ($rule $p1 $p2 $p3 $p4 $p5 $p6 $p7) $theorem))
   (let* (((: $rule (-> (: $p1 $t1) (: $p2 $t2) (: $p3 $t3)
                       (: $p4 $t4) (: $p5 $t5) (: $p6 $t6)
                       (: $p7 $t7) $theorem))
           (bc $kb $depth (: $rule (-> (: $p1 $t1) (: $p2 $t2) (: $p3 $t3)
                                       (: $p4 $t4) (: $p5 $t5) (: $p6 $t6)
                                       (: $p7 $t7) $theorem))))
          ((: $p1 $t1) (bc $kb $depth (: $p1 $t1)))
          ((: $p2 $t2) (bc $kb $depth (: $p2 $t2)))
          ((: $p3 $t3) (bc $kb $depth (: $p3 $t3)))
          ((: $p4 $t4) (bc $kb $depth (: $p4 $t4)))
          ((: $p5 $t5) (bc $kb $depth (: $p5 $t5)))
          ((: $p6 $t6) (bc $kb $depth (: $p6 $t6)))
          ((: $p7 $t7) (bc $kb $depth (: $p7 $t7))))
     (: ($rule $p1 $p2 $p3 $p4 $p5 $p6 $p7) $theorem)))

(= (count-tuple $t) (if (== $t ()) 0 (+ 1 (count-tuple (cdr-atom $t)))))
(= (verdict $exp $got) (if (== $exp $got) PASS *FAIL*))
(= (run-proofs $goal) (collapse (proof-of (rhometta:run (cell 1 (step $goal (S Z) 1))))))
(= (act $tag $exp $goal $desc)
   (let $ps (run-proofs $goal)
     (=== $tag (verdict $exp (count-tuple $ps)) expect $exp got (count-tuple $ps) : $desc :: $ps)))
(= (gate $tag $exp $got $desc)
   (=== $tag (verdict $exp $got) expect $exp got $got : $desc))

; ==========================================================================
!(act ACT1-chat-plans 2 (: $pf (Deploy eu-chat $provider $model))
   "EU low-latency chat deployment yields 2 valid plans (cheap + premium), with bad joins pruned")

!(act ACT2-code-plans 1 (: $pf (Deploy eu-code $provider $model))
   "EU low-latency code deployment yields exactly 1 valid plan")

!(act ACT3-voice-neg 0 (: $pf (Deploy eu-voice $provider $model))
   "EU low-latency voice deployment yields 0 plans (privacy/task/offer mismatch)")

!(gate ACT4-no-stuck 0
   (count-tuple (collapse (stuck-of (rhometta:run (cell 1 (step (: $pf (Deploy eu-chat $provider $model)) (S Z) 1))))))
   "no dangling rho:recv/send/eval-payload after pruning invalid provider/model combinations")

!(gate ACT5-expand 1
   (count-tuple (collapse (rhometta:transitions (cell 1 (step (: $pf (Deploy eu-chat $provider $model)) (S Z) 1)))))
   "one top-level transition plants the parallel premise network; the branching happens inside those premise cells")

!(gate ACT6-oracle
   (count-tuple (collapse (bc &kb (S Z) (: $pf (Deploy eu-chat $provider $model)))))
   (count-tuple (run-proofs (: $pf (Deploy eu-chat $provider $model))))
   "sequential nilbc oracle over the same KB agrees on the plan count")

!("---- deployment plans (proof term + ranked result) ----")
!(run-proofs (: $pf (Deploy eu-chat $provider $model)))
