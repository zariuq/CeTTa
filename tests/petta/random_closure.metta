; Regression suite for lib_random_closure and lib_evidence_ledger (--lang petta).
; Exercises positive closure-response laws, numerical enclosure + BigInt fallback,
; identified evidence ledger invariants, persistent product-tree bounds/sharing,
; local component scenario replacement, and unsupported-input rejection boundaries.

!(import! &self (library lib_random_closure))
!(import! &self (library lib_evidence_ledger))
!(import! &self (library tensor))

(= (test-rc:interval-encloses-rat? (RC:Answer (RC:Interval $lo $hi) $exact-opt $fb) (Rat $n $d))
   (let $target (/ (+ 0.0 $n) (+ 0.0 $d))
     (and (<= $lo $target)
          (and (<= $target $hi)
               (<= (- $hi $lo) 1.0e-12)))))

(= (test-rc:right-child $node) (rc:pt-right $node))

; 1. Empty-seed indistinguishability: (b :- a) vs empty rules agree on {} and split on {a}
!(let* (($p-rule (rc:prepare
                   (RC:Program (a b) () ((RC:Component c_rule () () ((RC:Rule b (a))))))
                   ((RC:Query () b) (RC:Query (a) b))))
        ($p-empty (rc:prepare
                    (RC:Program (a b) () ((RC:Component c_empty () () ())))
                    ((RC:Query () b) (RC:Query (a) b))))
        ($e-rule (rc:engine-new $p-rule ((RC:LocalState () ()))))
        ($e-empty (rc:engine-new $p-empty ((RC:LocalState () ())))))
   (test
     ((rc:query-exact $e-rule (RC:Query () b))
      (rc:query-exact $e-empty (RC:Query () b))
      (rc:query-exact $e-rule (RC:Query (a) b))
      (rc:query-exact $e-empty (RC:Query (a) b)))
     ((RC:ExactAnswer (Rat 0 1))
      (RC:ExactAnswer (Rat 0 1))
      (RC:ExactAnswer (Rat 1 1))
      (RC:ExactAnswer (Rat 0 1)))))

; 2. Unfounded self-loop (a :- a) derives nothing from {} and holds from {a}
!(let* (($prep (rc:prepare
                 (RC:Program (a) () ((RC:Component loop_c () () ((RC:Rule a (a))))))
                 ((RC:Query () a) (RC:Query (a) a))))
        ($eng (rc:engine-new $prep ((RC:LocalState () ())))))
   (test
     ((rc:query-exact $eng (RC:Query () a))
      (rc:query-exact $eng (RC:Query (a) a)))
     ((RC:ExactAnswer (Rat 0 1))
      (RC:ExactAnswer (Rat 1 1)))))

; 3. Multi-pass cyclic cross-component feedback: {a} -> b (Comp 1) -> c (Comp 2) -> d (Comp 1)
!(let* (($prog (RC:Program (a b c d) ()
                 ((RC:Component stage_1 (ch1) ()
                    ((RC:Rule b (a ch1))
                     (RC:Rule d (c ch1))))
                  (RC:Component stage_2 (ch2) ()
                    ((RC:Rule c (b ch2))
                     (RC:Rule b (c)))))))
        ($prep (rc:prepare $prog ((RC:Query (a) d))))
        ($eng (rc:engine-new $prep
                ((RC:LocalState ((Rat 1 2)) (none))
                 (RC:LocalState ((Rat 1 3)) (none))))))
   (test
     ((rc:query-exact $eng (RC:Query (a) d))
      (test-rc:interval-encloses-rat? (rc:query $eng (RC:Query (a) d)) (Rat 1 6))
      (rc:engine-fallbacks $eng))
     ((RC:ExactAnswer (Rat 1 6))
      true
      0)))

; 4. Shared cause vs. independent causes (1/2 vs 1/4)
!(let* (($p-ind (rc:prepare
                  (RC:Program (req mid ok) ()
                    ((RC:Component s1 (u1) () ((RC:Rule mid (req u1))))
                     (RC:Component s2 (u2) () ((RC:Rule ok (mid u2))))))
                  ((RC:Query (req) ok))))
        ($e-ind (rc:engine-new $p-ind
                  ((RC:LocalState ((Rat 1 2)) (none))
                   (RC:LocalState ((Rat 1 2)) (none)))))
        ($p-sh (rc:prepare
                 (RC:Program (req mid ok) (sh)
                   ((RC:Component s1 () () ((RC:Rule mid (req sh))))
                    (RC:Component s2 () () ((RC:Rule ok (mid sh))))))
                 ((RC:Query (req) ok))))
        ($e-sh (rc:engine-new-with-shared $p-sh
                 ((RC:LocalState () ()) (RC:LocalState () ()))
                 (RC:LocalState ((Rat 1 2)) (none)))))
   (test
     ((rc:query-exact $e-ind (RC:Query (req) ok))
      (rc:query-exact $e-sh (RC:Query (req) ok)))
     ((RC:ExactAnswer (Rat 1 4))
      (RC:ExactAnswer (Rat 1 2)))))

; 5. Unnormalized cross-component shared-cause revision via isolated local symptom probe
;    Prior P(dns_flap) = 1/5 => P(checkout_down) = 1/5.
;    Observing auth_svc private canary_fail (true under dns_flap, 1/10 false alarm otherwise)
;    revises downstream checkout_down to (1/5) / (1/5 + 4/5 * 1/10) = 5/7, and retracts back to 1/5.
!(let* (($prog (RC:Program (checkout_down) (dns_flap)
                 ((RC:Component auth_svc (local_noise) (canary_fail)
                    ((RC:Rule canary_fail (dns_flap))
                     (RC:Rule canary_fail (local_noise))))
                  (RC:Component checkout_svc () ()
                    ((RC:Rule checkout_down (dns_flap)))))))
        ($prep (rc:prepare $prog ((RC:Query () checkout_down))))
        ($led0 (ledger:from-prepared $prep (((Rat 1 10)) ()) ((Rat 1 5))))
        ($b0 (ledger:bind-engine $led0))
        ((Ledger:Step $t1 $b1)
         (ledger:observe $b0 (ledger:hard-symptom-obs obs_canary auth_svc canary_fail true)))
        ((Ledger:Step $t2 $b2)
         (ledger:retract $b1 obs_canary)))
   (test
     ($t1
      (ledger:query-exact $b0 (RC:Query () checkout_down))
      (ledger:query-exact $b1 (RC:Query () checkout_down))
      (test-rc:interval-encloses-rat? (ledger:query $b1 (RC:Query () checkout_down)) (Rat 5 7))
      $t2
      (ledger:query-exact $b2 (RC:Query () checkout_down)))
     (Activated
      (RC:ExactAnswer (Rat 1 5))
      (RC:ExactAnswer (Rat 5 7))
      true
      Retracted
      (RC:ExactAnswer (Rat 1 5)))))

; 6. Evidence ledger: idempotent replay, conflict rejection, independent confirmation,
;    impossible evidence (Z = 0 vs P = 0), and retraction recovery
!(let* (($prog (RC:Program (alarm) ()
                 ((RC:Component sensor (fault) () ((RC:Rule alarm (fault)))))))
        ($prep (rc:prepare $prog ((RC:Query () alarm))))
        ($b0 (ledger:bind-engine (ledger:from-prepared $prep (((Rat 1 2))) ())))
        ($obs1 (ledger:likelihood-obs ticket_1 fault (Rat 1 1) (Rat 2 1)))
        ((Ledger:Step $tag1 $b1) (ledger:observe $b0 $obs1))
        ((Ledger:Step $tag-rep $b-rep) (ledger:observe $b1 $obs1))
        ($conflict (ledger:observe $b1 (ledger:likelihood-obs ticket_1 fault (Rat 1 1) (Rat 5 1))))
        ((Ledger:Step $tag2 $b2)
         (ledger:observe $b-rep (ledger:likelihood-obs ticket_2 fault (Rat 1 1) (Rat 2 1))))
        ((Ledger:Step $tag-h1 $b3)
         (ledger:observe $b2 (ledger:hard-obs hard_pos fault true)))
        ((Ledger:Step $tag-h2 $b4)
         (ledger:observe $b3 (ledger:hard-obs hard_neg fault false)))
        ((Ledger:Step $tag-ret1 $b5) (ledger:retract $b4 hard_neg))
        ((Ledger:Step $tag-ret2 $b6) (ledger:retract $b5 hard_pos)))
   (test
     ($tag1
      (ledger:query-exact $b1 (RC:Query () alarm))
      $tag-rep
      (ledger:query-exact $b-rep (RC:Query () alarm))
      (rc:unsupported? $conflict)
      $tag2
      (ledger:query-exact $b2 (RC:Query () alarm))
      (ledger:query-exact $b4 (RC:Query () alarm))
      (ledger:query $b4 (RC:Query () alarm))
      $tag-ret1
      (ledger:query-exact $b5 (RC:Query () alarm))
      $tag-ret2
      (ledger:query-exact $b6 (RC:Query () alarm)))
     (Activated
      (RC:ExactAnswer (Rat 2 3))
      Replayed
      (RC:ExactAnswer (Rat 2 3))
      true
      Activated
      (RC:ExactAnswer (Rat 4 5))
      RC:ImpossibleEvidence
      RC:ImpossibleEvidence
      Retracted
      (RC:ExactAnswer (Rat 1 1))
      Retracted
      (RC:ExactAnswer (Rat 4 5)))))

; 7. Affine-sigmoid neural Bernoulli channel via tensor:matvec and math:exp
!(let* (($prog (RC:Program (alarm) ()
                 ((RC:Component sensor (fault) () ((RC:Rule alarm (fault)))))))
        ($prep (rc:prepare $prog ((RC:Query () alarm))))
        ($b0 (ledger:bind-engine (ledger:from-prepared $prep (((Rat 1 2))) ())))
        ($w-zero (tensor:from-values f64 (1 2) (2.0 -1.0)))
        ($x-zero (tensor:from-values f64 (2) (0.5 1.0)))
        ((Ledger:Step $t-neutral $b1)
         (ledger:observe $b0 (ledger:neural-bernoulli-obs nn_neutral fault $w-zero $x-zero 0.0 true)))
        ($w-pos (tensor:from-values f64 (1 2) (2.0 1.0)))
        ($x-pos (tensor:from-values f64 (2) (1.0 0.0)))
        ((Ledger:Step $t-pos $b2)
         (ledger:observe $b1 (ledger:neural-bernoulli-obs nn_pos fault $w-pos $x-pos 0.0 true)))
        ((RC:ExactAnswer (Rat $n2 $d2)) (ledger:query-exact $b2 (RC:Query () alarm))))
   (test
     ($t-neutral
      (ledger:query-exact $b1 (RC:Query () alarm))
      $t-pos
      (> (/ (+ 0.0 $n2) (+ 0.0 $d2)) 0.88))
     (Activated
      (RC:ExactAnswer (Rat 1 2))
      Activated
      true)))

; 8. Rare-query 10^-100 cancellation control triggers automatic BigInt rational fallback
!(let* (($p50 (rc:pow10 50))
        ($p100 (rc:pow10 100))
        ($prog (RC:Program (a b c) ()
                 ((RC:Component r1 (u1) () ((RC:Rule b (a u1))))
                  (RC:Component r2 (u2) () ((RC:Rule c (b u2)))))))
        ($prep (rc:prepare $prog ((RC:Query (a) c))))
        ($eng0 (rc:engine-new $prep
                 ((RC:LocalState ((Rat 1 $p50)) (none))
                  (RC:LocalState ((Rat 1 $p50)) (none)))))
        ((RC:QueryOutcome (RC:Answer $iv $exact $used-fb) $eng1)
         (rc:query-stateful $eng0 (RC:Query (a) c))))
   (test
     ($used-fb
      $exact
      (rc:engine-fallbacks $eng1))
     (true
      (Rat 1 $p100)
      1)))

; 9. Persistent ProductTree logarithmic visits, untouched subtree sharing & local scenario replacement
(= (test-rc:mk-16 $i)
   (if (>= $i 16) ()
     (let* (($ci (parse (format-args "svc_{}" ($i))))
            ($fi (parse (format-args "fl_{}" ($i))))
            ($c (RC:Component $ci ($fi) () ((RC:Rule out (in $fi)))))
            ($rest (test-rc:mk-16 (+ $i 1))))
       (cons-atom $c $rest))))

(= (test-rc:mk-16-priors $i)
   (if (>= $i 16) ()
     (let $rest (test-rc:mk-16-priors (+ $i 1))
       (cons-atom ((Rat 1 10)) $rest))))

!(let* (($prog (RC:Program (in out) () (test-rc:mk-16 0)))
        ($prep (rc:prepare $prog ((RC:Query (in) out))))
        ($b0 (ledger:bind-engine (ledger:from-prepared $prep (test-rc:mk-16-priors 0) ())))
        ($e0 (ledger:engine $b0))
        ((Ledger:Step $t1 $b1)
         (ledger:observe $b0 (ledger:hard-obs pin_3 fl_3 false)))
        ($e1 (ledger:engine $b1))
        ($shared-right-ok
         (== (test-rc:right-child (rc:engine-tree $e0))
             (test-rc:right-child (rc:engine-tree $e1))))
        ((Ledger:Step $t2 $b2) (ledger:retract $b1 pin_3))
        ; Scenario change: replace svc_3 with a hardened bypass component that always forwards in -> out
        ($b3 (ledger:replace-component $b2 3
               (RC:Component svc_3_bypass () () ((RC:Rule out (in))))
               ()))
        ($e3 (ledger:engine $b3)))
   (test
     ($t1
      (rc:engine-last-visits $e1)
      $shared-right-ok
      $t2
      (rc:engine-last-visits $e3)
      (ledger:query-exact $b3 (RC:Query (in) out)))
     (Activated
      4
      true
      Retracted
      4
      (RC:ExactAnswer (Rat 1 1)))))

; 10. Negative / unsupported-input boundary controls
!(let* (($neg-lit
         (rc:prepare
           (RC:Program (a b) () ((RC:Component c1 () () ((RC:Rule b ((Not a)))))))
           ((RC:Query (a) b))))
        ($out-scope
         (rc:prepare
           (RC:Program (a b) () ((RC:Component c1 () () ((RC:Rule ghost (a))))))
           ((RC:Query (a) b))))
        ($derived-choice
         (rc:prepare
           (RC:Program (a b) () ((RC:Component c1 (ch) () ((RC:Rule ch (a))))))
           ((RC:Query (a) b))))
        ($dup-comp
         (rc:prepare
           (RC:Program (a b) ()
             ((RC:Component same () () ())
              (RC:Component same () () ())))
           ((RC:Query (a) b))))
        ($priv-clash
         (rc:prepare
           (RC:Program (a b) ()
             ((RC:Component c1 () (secret) ())
              (RC:Component c2 () (secret) ())))
           ((RC:Query (a) b))))
        ($valid-prep
         (rc:prepare
           (RC:Program (a b) ()
             ((RC:Component c1 (ch1) (priv1) ((RC:Rule b (a ch1))))
              (RC:Component c2 (ch2) (priv2) ((RC:Rule b (a ch2))))))
           ((RC:Query (a) b))))
        ($outside-q
         (rc:prepare
           (RC:Program (a b) () ((RC:Component c1 () (priv1) ())))
           ((RC:Query () priv1))))
        ($b-valid
         (ledger:bind-engine (ledger:from-prepared $valid-prep (((Rat 1 2)) ((Rat 1 2))) ())))
        ($undeclared-q
         (ledger:query-exact $b-valid (RC:Query () b)))
        ((Ledger:Step $t-act $b-with-obs)
         (ledger:observe $b-valid (ledger:hard-obs active_ch1 ch1 true)))
        ($orphan-replace
         (ledger:replace-component $b-with-obs 0
           (RC:Component c1_new (ch1_new) () ((RC:Rule b (a ch1_new))))
           ((Rat 1 3))))
        ($clash-replace
         (ledger:replace-component $b-valid 0
           (RC:Component c1_clash (priv2) () ())
           ((Rat 1 3)))))
   (test
     ((rc:unsupported? $neg-lit)
      (rc:unsupported? $out-scope)
      (rc:unsupported? $derived-choice)
      (rc:unsupported? $dup-comp)
      (rc:unsupported? $priv-clash)
      (rc:unsupported? $outside-q)
      (rc:unsupported? $undeclared-q)
      (rc:unsupported? $orphan-replace)
      (rc:unsupported? $clash-replace))
     (true true true true true true true true true)))

; 11. Compositional Higham enclosure & fallback on N-component OR 1 - (9999/10000)^N
(= (test-rc:mk-or-comps $i $n)
   (if (>= $i $n) ()
     (let* (($ci (parse (format-args "or_c_{}" ($i))))
            ($xi (parse (format-args "or_x_{}" ($i))))
            ($rest (test-rc:mk-or-comps (+ $i 1) $n)))
       (cons-atom (RC:Component $ci ($xi) () ((RC:Rule q ($xi)))) $rest))))

(= (test-rc:mk-or-states $i $n)
   (if (>= $i $n) ()
     (let $rest (test-rc:mk-or-states (+ $i 1) $n)
       (cons-atom (RC:LocalState ((Rat 1 10000)) (none)) $rest))))

!(let* (($p64 (rc:prepare
                (RC:Program (q) () (test-rc:mk-or-comps 0 64))
                ((RC:Query () q))))
        ($e64 (rc:engine-new $p64 (test-rc:mk-or-states 0 64)))
        ((RC:ExactAnswer $rat64) (rc:query-exact $e64 (RC:Query () q)))
        ($ans64 (rc:query $e64 (RC:Query () q)))
        ((RC:Answer (RC:Interval $lo64 $hi64) $ex64 $fb64) $ans64)
        ($p256 (rc:prepare
                 (RC:Program (q) () (test-rc:mk-or-comps 0 256))
                 ((RC:Query () q))))
        ($e256 (rc:engine-new $p256 (test-rc:mk-or-states 0 256)))
        ((RC:ExactAnswer (Rat $n256 $d256)) (rc:query-exact $e256 (RC:Query () q)))
        ((RC:Answer (RC:Interval $lo256 $hi256) $ex256 $fb256)
         (rc:query $e256 (RC:Query () q)))
        ($s20 (rc:pow10 20))
        ($approx256 (/ (+ 0.0 (rc:int-div (* $n256 $s20) $d256)) (+ 0.0 $s20))))
   (test
     ($fb64
      (test-rc:interval-encloses-rat? $ans64 $rat64)
      $fb256
      (== $ex256 (Rat $n256 $d256))
      (and (< $lo256 $approx256)
           (and (< $approx256 $hi256)
                (<= (- $hi256 $lo256) 1.0e-12))))
     (false
      true
      true
      true
      true)))

; 12. Quantized interior-support neural boundary: sigmoid(+-40) stays in (0, 1)
;     and never fabricates RC:ImpossibleEvidence on extreme finite logits
!(let* (($w0 (tensor:from-values f64 (1 1) (0.0)))
        ($x0 (tensor:from-values f64 (1) (0.0)))
        ($s-pos (ledger:affine-sigmoid-prob $w0 $x0 40.0))
        ($s-neg (ledger:affine-sigmoid-prob $w0 $x0 -40.0))
        ($s-bad (ledger:affine-sigmoid-prob $w0 (tensor:from-values f64 (2) (1.0 2.0)) 0.0))
        ($prep (rc:prepare
                 (RC:Program (alarm) ()
                   ((RC:Component sensor (fault) () ((RC:Rule alarm (fault))))))
                 ((RC:Query () alarm))))
        ($b0 (ledger:bind-engine (ledger:from-prepared $prep (((Rat 1 2))) ())))
        ((Ledger:Step $tn $b1)
         (ledger:observe $b0 (ledger:neural-bernoulli-obs nn_tail fault $w0 $x0 -40.0 true)))
        ((Ledger:Step $th $b2)
         (ledger:observe $b1 (ledger:hard-obs hard_fault fault true))))
   (test
     ($s-pos
      $s-neg
      (rc:unsupported? $s-bad)
      $tn
      $th
      (ledger:query-exact $b2 (RC:Query () alarm)))
     ((Rat 999999999999 1000000000000)
      (Rat 1 1000000000000)
      true
      Activated
      Activated
      (RC:ExactAnswer (Rat 1 1)))))

; 13. Immutable append-only observation catalogue & single-draw sensor-channel guard
!(let* (($prep (rc:prepare
                 (RC:Program (q) ()
                   ((RC:Component c (x) () ((RC:Rule q (x))))))
                 ((RC:Query () q))))
        ($b0 (ledger:bind-engine (ledger:from-prepared $prep (((Rat 1 2))) ())))
        ((Ledger:Step $t1 $b1) (ledger:observe $b0 (ledger:hard-obs report x true)))
        ((Ledger:Step $t2 $b2) (ledger:retract $b1 report))
        ; Reusing retracted ID with different payload is rejected
        ($mutate-try (ledger:observe $b2 (ledger:hard-obs report x false)))
        ; Re-activating retracted ID from immutable catalogue succeeds
        ((Ledger:Step $t3 $b3) (ledger:observe-id $b2 report))
        ; Distinct soft observation IDs sharing the same single-draw channel are rejected
        ((Ledger:Step $tc1 $bc1)
         (ledger:observe $b0 (ledger:channel-likelihood-obs r1 chan_alpha x (Rat 1 1) (Rat 3 1))))
        ($same-chan-try
         (ledger:observe $bc1 (ledger:channel-likelihood-obs r2 chan_alpha x (Rat 1 1) (Rat 3 1))))
        ; Distinct channels multiply cleanly
        ((Ledger:Step $tc2 $bc2)
         (ledger:observe $bc1 (ledger:channel-likelihood-obs r2 chan_beta x (Rat 1 1) (Rat 3 1)))))
   (test
     ($t1
      $t2
      (rc:unsupported? $mutate-try)
      $t3
      (ledger:query-exact $b3 (RC:Query () q))
      $tc1
      (rc:unsupported? $same-chan-try)
      $tc2
      (ledger:query-exact $bc2 (RC:Query () q)))
     (Activated
      Retracted
      true
      Activated
      (RC:ExactAnswer (Rat 1 1))
      Activated
      true
      Activated
      (RC:ExactAnswer (Rat 9 10)))))
