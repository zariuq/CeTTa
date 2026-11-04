; ============================================================================
; SERVICE-DEPENDENCY DIAGNOSIS SHOWCASE (--lang petta)
; ============================================================================
;
; Demonstrates modular positive-Horn probabilistic diagnosis using
; lib_random_closure and lib_evidence_ledger over CeTTa's generic tensor and
; math libraries:
;
;   Part I  — Inspectable 3-component service architecture (EdgeGateway,
;             AuthCache, CheckoutCore) with shared dns_flap outage cause,
;             cyclic retry-storm feedback, cross-component shared-cause
;             revision via private canary symptoms, immutable evidence-ID
;             catalogue & channel-independence guards, contradictory-evidence
;             detection + retraction recovery, a quantized interior-support
;             affine-sigmoid neural telemetry front end, and a circuit-breaker
;             local component scenario change.
;
;   Part II — Nonsaturating scaling of the 4-atom interface + shared-cause
;             service family across N = 16, 64, and 256 components (with
;             O(1/N)-scaled per-replica fault rates keeping outage probability
;             bounded away from 0 and 1), reporting preparation time,
;             O(log N) SlotTree + ProductTree update visits and untouched
;             subtree sharing, local scenario replacement (O(N) peer-ownership
;             check + O(log N) tree update), and O(1) root query readout with
;             compositional Higham interval enclosure.
; ============================================================================

!(import! &self (library lib_random_closure))
!(import! &self (library lib_evidence_ledger))
!(import! &self (library tensor))
!(import! &self (library system))

(= (showcase:rat-approx (Rat $n $d))
   (/ (+ 0.0 $n) (+ 0.0 $d)))

(= (showcase:encloses? (RC:Answer (RC:Interval $lo $hi) $exact-opt $fb) (Rat $n $d))
   (let $p (/ (+ 0.0 $n) (+ 0.0 $d))
     (and (<= $lo $p) (and (<= $p $hi) (<= (- $hi $lo) 1.0e-12)))))

; ----------------------------------------------------------------------------
; Part I: Inspectable 3-Service Diagnosis Model
; ----------------------------------------------------------------------------
;
; Interface atoms:
;   traffic          — incoming customer request load
;   auth_degraded    — authentication tier latency / token validation degradation
;   retry_storm      — cross-tier retry amplification loop
;   checkout_outage  — user-visible checkout failure
;
; Shared cause:
;   dns_flap         — internal DNS instability affecting both AuthCache and
;                      CheckoutCore (prior 1/5)

(= (showcase:edge-gateway)
   (RC:Component edge_gateway
     (gw_overload)
     ()
     ((RC:Rule retry_storm (traffic auth_degraded gw_overload))
      (RC:Rule checkout_outage (retry_storm)))))

(= (showcase:edge-gateway-circuit-breaker)
   ; Scenario change: circuit breaker sheds retry amplification so auth_degraded
   ; no longer triggers a cross-tier retry_storm.
   (RC:Component edge_gateway
     (gw_overload)
     (cb_tripped)
     ((RC:Rule cb_tripped (traffic auth_degraded))
      (RC:Rule checkout_outage (retry_storm)))))

(= (showcase:auth-cache)
   (RC:Component auth_cache
     (cache_miss_burst canary_noise)
     (auth_canary_alert)
     ((RC:Rule auth_canary_alert (dns_flap))
      (RC:Rule auth_canary_alert (canary_noise))
      (RC:Rule auth_degraded (traffic dns_flap))
      (RC:Rule auth_degraded (traffic cache_miss_burst))
      ; Cyclic cross-component feedback: retry_storm sustains auth_degraded
      (RC:Rule auth_degraded (retry_storm)))))

(= (showcase:checkout-core)
   (RC:Component checkout_core
     (db_lock)
     ()
     ((RC:Rule checkout_outage (traffic dns_flap))
      (RC:Rule checkout_outage (traffic auth_degraded db_lock)))))

(= (showcase:service-program)
   (RC:Program
     (traffic auth_degraded retry_storm checkout_outage)
     (dns_flap)
     ((showcase:edge-gateway)
      (showcase:auth-cache)
      (showcase:checkout-core))))

; Contrast model where AuthCache and CheckoutCore suffer independent 1/5 DNS
; glitches instead of a single shared dns_flap cause
(= (showcase:independent-dns-program)
   (RC:Program
     (traffic auth_degraded retry_storm checkout_outage)
     ()
     ((showcase:edge-gateway)
      (RC:Component auth_cache
        (dns_auth cache_miss_burst canary_noise)
        (auth_canary_alert)
        ((RC:Rule auth_canary_alert (dns_auth))
         (RC:Rule auth_canary_alert (canary_noise))
         (RC:Rule auth_degraded (traffic dns_auth))
         (RC:Rule auth_degraded (traffic cache_miss_burst))
         (RC:Rule auth_degraded (retry_storm))))
      (RC:Component checkout_core
        (dns_checkout db_lock)
        ()
        ((RC:Rule checkout_outage (traffic dns_checkout))
         (RC:Rule checkout_outage (traffic auth_degraded db_lock)))))))

!(let* (($queries
         ((RC:Query (traffic) checkout_outage)
          (RC:Query (traffic) retry_storm)
          (RC:Query (traffic) auth_degraded)))
        ($prep (rc:prepare (showcase:service-program) $queries))
        ($prep-ind (rc:prepare (showcase:independent-dns-program) $queries))

        ; Priors:
        ;   edge_gateway:  gw_overload = 1/4
        ;   auth_cache:    cache_miss_burst = 1/4, canary_noise = 1/10
        ;   checkout_core: db_lock = 1/5
        ;   shared:        dns_flap = 1/5
        ($b0 (ledger:bind-engine
               (ledger:from-prepared $prep
                 (((Rat 1 4))
                  ((Rat 1 4) (Rat 1 10))
                  ((Rat 1 5)))
                 ((Rat 1 5)))))
        ($b0-ind (ledger:bind-engine
                   (ledger:from-prepared $prep-ind
                     (((Rat 1 4))
                      ((Rat 1 5) (Rat 1 4) (Rat 1 10))
                      ((Rat 1 5) (Rat 1 5)))
                     ())))

        ; Step 0: Baseline queries (shared vs independent DNS failure cause)
        ((RC:ExactAnswer $p0-out) (ledger:query-exact $b0 (RC:Query (traffic) checkout_outage)))
        ((RC:ExactAnswer $p0-ind-out) (ledger:query-exact $b0-ind (RC:Query (traffic) checkout_outage)))
        ((RC:ExactAnswer $p0-storm) (ledger:query-exact $b0 (RC:Query (traffic) retry_storm)))
        ($iv0-out (ledger:query $b0 (RC:Query (traffic) checkout_outage)))
        ($_0 (println! (Showcase:Step0_Baseline
                         (families (rc:prepared-family-count $prep) (columns (rc:prepared-columns $prep)))
                         (shared_dns_outage $p0-out (approx (showcase:rat-approx $p0-out)))
                         (indep_dns_outage  $p0-ind-out (approx (showcase:rat-approx $p0-ind-out)))
                         (retry_storm       $p0-storm (approx (showcase:rat-approx $p0-storm))))))

        ; Step 1: Private canary alert on AuthCache revises CheckoutCore risk via shared dns_flap
        ($canary-obs (ledger:hard-symptom-obs ticket_canary_1 auth_cache auth_canary_alert true))
        ((Ledger:Step $tag-c1 $b1) (ledger:observe $b0 $canary-obs))
        ((RC:ExactAnswer $p1-out) (ledger:query-exact $b1 (RC:Query (traffic) checkout_outage)))
        ((Ledger:Step $tag-ind-c1 $b1-ind)
         (ledger:observe $b0-ind (ledger:hard-symptom-obs ticket_canary_1 auth_cache auth_canary_alert true)))
        ((RC:ExactAnswer $p1-ind-out) (ledger:query-exact $b1-ind (RC:Query (traffic) checkout_outage)))
        ($_1 (println! (Showcase:Step1_CrossComponentCanary
                         (status $tag-c1)
                         (shared_model_outage $p1-out (approx (showcase:rat-approx $p1-out)))
                         (indep_model_outage  $p1-ind-out (approx (showcase:rat-approx $p1-ind-out))))))

        ; Step 2: Duplicate webhook replay (idempotent) + conflicting ID guard + fresh gateway pager
        ((Ledger:Step $tag-replay $b2-rep) (ledger:observe $b1 $canary-obs))
        ((RC:ExactAnswer $p2-rep-out) (ledger:query-exact $b2-rep (RC:Query (traffic) checkout_outage)))
        ($conflict-try
         (ledger:observe $b2-rep
           (ledger:hard-symptom-obs ticket_canary_1 auth_cache auth_canary_alert false)))
        ((Ledger:Step $tag-gw $b2)
         (ledger:observe $b2-rep
           (ledger:channel-likelihood-obs pager_gw_cpu chan_gw_pager gw_overload (Rat 1 1) (Rat 3 1))))
        ($dup-chan-try
         (ledger:observe $b2
           (ledger:channel-likelihood-obs pager_gw_cpu_dup chan_gw_pager gw_overload (Rat 1 1) (Rat 3 1))))
        ((RC:ExactAnswer $p2-out) (ledger:query-exact $b2 (RC:Query (traffic) checkout_outage)))
        ((RC:ExactAnswer $p2-storm) (ledger:query-exact $b2 (RC:Query (traffic) retry_storm)))
        ($_2 (println! (Showcase:Step2_EvidenceLedger
                         (replay_status $tag-replay (unchanged (== $p1-out $p2-rep-out)))
                         (conflict_rejected (rc:unsupported? $conflict-try))
                         (same_channel_rejected (rc:unsupported? $dup-chan-try))
                         (after_gw_pager_outage $p2-out (approx (showcase:rat-approx $p2-out)))
                         (after_gw_pager_storm  $p2-storm (approx (showcase:rat-approx $p2-storm))))))

        ; Step 3: Quantized interior-support affine-sigmoid neural channel on db_lock
        ($nn-weights (tensor:from-values f64 (1 3) (1.2 0.8 -0.6)))
        ($nn-telemetry (tensor:from-values f64 (3) (1.5 0.5 -0.5)))
        ((Ledger:Step $tag-nn $b3)
         (ledger:observe $b2
           (ledger:channel-neural-bernoulli-obs
             nn_db_telemetry chan_db_nn db_lock $nn-weights $nn-telemetry -0.3 true)))
        ((RC:ExactAnswer $p3-out) (ledger:query-exact $b3 (RC:Query (traffic) checkout_outage)))
        ($iv3-out (ledger:query $b3 (RC:Query (traffic) checkout_outage)))
        ($_3 (println! (Showcase:Step3_NeuralTelemetry
                         (status $tag-nn)
                         (tree_visits (rc:engine-last-visits (ledger:engine $b3)))
                         (outage_exact $p3-out (approx (showcase:rat-approx $p3-out)))
                         (enclosure_ok (showcase:encloses? $iv3-out $p3-out)))))

        ; Step 4: Contradictory operator pins -> RC:ImpossibleEvidence -> retraction recovery
        ;         + immutable catalogue guard rejecting post-retraction ID mutation
        ((Ledger:Step $th1 $b4a) (ledger:observe $b3 (ledger:hard-obs op_pin_pos db_lock true)))
        ((Ledger:Step $th2 $b4b) (ledger:observe $b4a (ledger:hard-obs op_pin_neg db_lock false)))
        ($imp-ans (ledger:query $b4b (RC:Query (traffic) checkout_outage)))
        ((Ledger:Step $tr1 $b4c) (ledger:retract $b4b op_pin_neg))
        ((Ledger:Step $tr2 $b4d) (ledger:retract $b4c op_pin_pos))
        ($post-retract-mutate-try (ledger:observe $b4d (ledger:hard-obs op_pin_neg db_lock true)))
        ((RC:ExactAnswer $p4-recovered) (ledger:query-exact $b4d (RC:Query (traffic) checkout_outage)))
        ($_4 (println! (Showcase:Step4_ContradictionAndRecovery
                         (contradictory_state $imp-ans)
                         (post_retract_id_mutation_rejected (rc:unsupported? $post-retract-mutate-try))
                         (recovered_matches_step3 (== $p4-recovered $p3-out)))))

        ; Step 5: Local component scenario change — deploy circuit-breaker EdgeGateway
        ;         (preserves active AuthCache canary, Gateway pager, and Checkout neural evidence)
        ($b5 (ledger:replace-component $b4d 0
               (showcase:edge-gateway-circuit-breaker)
               ((Rat 1 4))))
        ((RC:ExactAnswer $p5-out) (ledger:query-exact $b5 (RC:Query (traffic) checkout_outage)))
        ((RC:ExactAnswer $p5-storm) (ledger:query-exact $b5 (RC:Query (traffic) retry_storm)))
        ($_5 (println! (Showcase:Step5_CircuitBreakerScenarioChange
                         (tree_visits (rc:engine-last-visits (ledger:engine $b5)))
                         (retry_storm_after $p5-storm)
                         (outage_before (approx (showcase:rat-approx $p3-out)))
                         (outage_after  $p5-out (approx (showcase:rat-approx $p5-out)))))))

   ; Contract assertions verifying every semantic invariant of Part I
   (test
     ((showcase:encloses? $iv0-out $p0-out)
      (> (showcase:rat-approx $p1-out) (showcase:rat-approx $p0-out))
      (> (showcase:rat-approx $p1-out) (showcase:rat-approx $p1-ind-out))
      $tag-replay
      (== $p1-out $p2-rep-out)
      (rc:unsupported? $conflict-try)
      (rc:unsupported? $dup-chan-try)
      (> (showcase:rat-approx $p3-out) (showcase:rat-approx $p2-out))
      (showcase:encloses? $iv3-out $p3-out)
      $imp-ans
      (rc:unsupported? $post-retract-mutate-try)
      (== $p4-recovered $p3-out)
      $p5-storm
      (< (showcase:rat-approx $p5-out) (showcase:rat-approx $p3-out)))
     (true
      true
      true
      Replayed
      true
      true
      true
      true
      true
      RC:ImpossibleEvidence
      true
      true
      (Rat 0 1)
      true)))

; ----------------------------------------------------------------------------
; Part II: Nonsaturating Scaling (N = 16, 64, 256) & Subtree Sharing
; ----------------------------------------------------------------------------
;
; Per-replica fault priors scale as 1/(2N) and 1/(3N) so aggregate outage risk
; stays bounded in the nonsaturating interior (0.15, 0.30) across all N.
; Complexity profile:
;   - rc:engine-update            : O(log N) SlotTree + ProductTree visits (zero O(N) scans)
;   - rc:engine-replace-component : O(N) peer-ownership validation + O(log N) tree update
;   - rc:query (f64 fast path)    : O(1) root tensor readout with compositional Higham bound

(= (showcase:mk-replica $i)
   (let* (($cname (parse (format-args "replica_{}" ($i))))
          ($overload (parse (format-args "ov_{}" ($i))))
          ($db-glitch (parse (format-args "db_{}" ($i)))))
     (RC:Component $cname ($overload $db-glitch) ()
       ((RC:Rule auth_degraded (traffic $overload))
        (RC:Rule retry_storm (traffic auth_degraded $overload dns_flap))
        (RC:Rule checkout_outage (retry_storm))
        (RC:Rule checkout_outage (traffic auth_degraded $db-glitch))))))

(= (showcase:mk-replicas $i $n)
   (if (>= $i $n) ()
     (let $rest (showcase:mk-replicas (+ $i 1) $n)
       (cons-atom (showcase:mk-replica $i) $rest))))

(= (showcase:mk-replica-states $i $n)
   (if (>= $i $n) ()
     (let* (($p-ov (Rat 1 (* 2 $n)))
            ($p-db (Rat 1 (* 3 $n)))
            ($rest (showcase:mk-replica-states (+ $i 1) $n)))
       (cons-atom (RC:LocalState ($p-ov $p-db) (none none)) $rest))))

(= (showcase:scale-probe $n $expected-log-visits $expected-fb)
   (let* (($comps (showcase:mk-replicas 0 $n))
          ($prog (RC:Program
                   (traffic auth_degraded retry_storm checkout_outage)
                   (dns_flap)
                   $comps))
          ($qs ((RC:Query (traffic) checkout_outage)
                (RC:Query (traffic) retry_storm)))
          ($t0 (system:monotonic-ns))
          ($prep (rc:prepare $prog $qs))
          ($t1 (system:monotonic-ns))
          ($states (showcase:mk-replica-states 0 $n))
          ($eng0 (rc:engine-new-with-shared $prep $states (RC:LocalState ((Rat 1 5)) (none))))
          ($t2 (system:monotonic-ns))
          ; Incremental local evidence update on leaf 2 (in left half, so right subtree is shared)
          ($eng1 (rc:engine-update $eng0
                   ((2 (RC:LocalState ((Rat 1 (* 2 $n)) (Rat 1 (* 3 $n))) (true none))))
                   keep))
          ($t3 (system:monotonic-ns))
          ($right-shared
           (== (rc:pt-right (rc:engine-tree $eng0))
               (rc:pt-right (rc:engine-tree $eng1))))
          ; Local component scenario change on leaf 2 (circuit-breaker replica)
          ($cb-rep (RC:Component replica_2_cb (ov_2_cb) ()
                     ((RC:Rule auth_degraded (traffic ov_2_cb)))))
          ($eng2 (rc:engine-replace-component $eng1 2 $cb-rep
                   (RC:LocalState ((Rat 1 (* 2 $n))) (none))))
          ($t4 (system:monotonic-ns))
          ((RC:QueryOutcome $ans $eng3)
           (rc:query-stateful $eng2 (RC:Query (traffic) checkout_outage)))
          ($t5 (system:monotonic-ns))
          ((RC:Answer (RC:Interval $lo $hi) $ex $fb) $ans)
          ; Second incremental update + query (reuses retained exact tree in O(log N) if fallback fired)
          ($eng4 (rc:engine-update $eng3
                   ((1 (RC:LocalState ((Rat 1 $n) (Rat 1 (* 3 $n))) (none none))))
                   keep))
          ((RC:QueryOutcome (RC:Answer (RC:Interval $lo2 $hi2) $ex2 $fb2) $eng5)
           (rc:query-stateful $eng4 (RC:Query (traffic) checkout_outage)))
          ($t6 (system:monotonic-ns)))
     (let $_ (println! (Showcase:Scaling $n
                         (families (rc:prepared-family-count $prep) (columns (rc:prepared-columns $prep)))
                         (prep_ms (/ (- $t1 $t0) 1000000.0))
                         (init_ms (/ (- $t2 $t1) 1000000.0) (build_visits (rc:engine-last-visits $eng0)))
                         (update_ms (/ (- $t3 $t2) 1000000.0) (visits (rc:engine-last-visits $eng1)))
                         (scenario_replace_ms (/ (- $t4 $t3) 1000000.0) (visits (rc:engine-last-visits $eng2)))
                         (first_query_ms (/ (- $t5 $t4) 1000000.0) (fallback $fb) (interval $lo $hi))
                         (inc_update_and_query_ms (/ (- $t6 $t5) 1000000.0) (fallback $fb2) (interval $lo2 $hi2))
                         (right_subtree_shared $right-shared)))
       (test
         ((rc:engine-last-visits $eng0)
          (rc:engine-last-visits $eng1)
          (rc:engine-last-visits $eng2)
          $right-shared
          $fb
          (and (> $lo 0.10) (< $hi 0.45))
          (<= (- $hi $lo) 1.0e-12)
          (<= (- $hi2 $lo2) 1.0e-12))
         ((- $n 1)
          $expected-log-visits
          $expected-log-visits
          true
          $expected-fb
          true
          true
          true)))))

!(showcase:scale-probe 16 4 false)
!(showcase:scale-probe 64 6 false)
!(showcase:scale-probe 256 8 true)
