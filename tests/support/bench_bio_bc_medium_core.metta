; ============================================================================
; Medium bio backward-chaining benchmark core
; ============================================================================
;
; Shared by plain and variant wrappers so we can compare the same workload
; under different search policies without waiting on the full deep gate.
;
; Positive example:
; keep the same bio workload body and vary only the search policy wrapper.
;
; Negative example:
; compare two differently trimmed workloads and pretend the timing delta is real.
;
; This medium rung keeps phases 1-7 from the deep benchmark.
; ============================================================================

(: Nat Type)
(: Z Nat)
(: S (-> Nat Nat))
(: fromNumber (-> Number Nat))
(= (fromNumber $n) (if (<= $n 0) Z (S (fromNumber (- $n 1)))))

(: bc (-> $a Nat $b $b))

(= (bc $kb $_ (: $prf $thm))
   (match $kb (: $prf $thm) (: $prf $thm)))

(= (bc $kb $_ (rule-sig $name $type))
   (match $kb (rule-sig $name $type) (rule-sig $name $type)))

(= (bc $kb $_ (: (eqtl-fact $snp $gene) (eqtl $snp $gene)))
   (match $kb (eqtl-link $snp $gene)
     (: (eqtl-fact $snp $gene) (eqtl $snp $gene))))

(= (bc $kb $_ (: (coreg-fact $s1 $s2 $surp) (coreg $s1 $s2)))
   (match $kb (co-regulate $s1 $s2 $surp)
     (: (coreg-fact $s1 $s2 $surp) (coreg $s1 $s2))))

(= (bc $kb $_ (: (coexpr-fact $g1 $g2 $surp) (coexpr $g1 $g2)))
   (match $kb (co-express $g1 $g2 $surp)
     (: (coexpr-fact $g1 $g2 $surp) (coexpr $g1 $g2))))

(= (bc $kb $_ (: (pathway-fact $gene $pw) (in-pw $gene $pw)))
   (match $kb (in-pathway $gene $pw)
     (: (pathway-fact $gene $pw) (in-pw $gene $pw))))

(= (bc $kb $_ (: (disease-fact $gene $disease) (has-disease $gene $disease)))
   (match $kb (disease-gene $disease $gene)
     (: (disease-fact $gene $disease) (has-disease $gene $disease))))

(= (bc $kb $_ (: (treats-fact $compound $disease) (treats $compound $disease)))
   (match $kb (compound-treats $compound $disease)
     (: (treats-fact $compound $disease) (treats $compound $disease))))

(= (bc $kb $_ (: (binds-fact $compound $gene) (binds $compound $gene)))
   (match $kb (compound-binds $compound $gene)
     (: (binds-fact $compound $gene) (binds $compound $gene))))

(= (bc $kb (S $d) (: ($r $p1) $thm))
   (let* (((rule-sig $r (-> (: $p1 $t1) $thm))
            (bc $kb $d (rule-sig $r (-> (: $p1 $t1) $thm))))
           ((: $p1 $t1) (bc $kb $d (: $p1 $t1))))
     (: ($r $p1) $thm)))

(= (bc $kb (S $d) (: ($r $p1 $p2) $thm))
   (let* (((rule-sig $r (-> (: $p1 $t1) (: $p2 $t2) $thm))
            (bc $kb $d (rule-sig $r (-> (: $p1 $t1) (: $p2 $t2) $thm))))
           ((: $p1 $t1) (bc $kb $d (: $p1 $t1)))
           ((: $p2 $t2) (bc $kb $d (: $p2 $t2))))
     (: ($r $p1 $p2) $thm)))

!(import! &self ../eqtl_for_mining)
!(import! &self ../discovered_patterns_1k)
!(import! &self ../bio_multirelational_kb_scaled)
!(import! &self ../bio_hetionet_kb)

(rule-sig dt (-> (: $e (eqtl $snp $gene)) (target $snp $gene)))

(rule-sig ct (-> (: $e1 (coreg $snp1 $snp2))
          (: $e2 (target $snp2 $gene))
          (target $snp1 $gene)))

(rule-sig xt (-> (: $e1 (target $snp $gene1))
          (: $e2 (coexpr $gene1 $gene2))
          (target $snp $gene2)))

(rule-sig pr (-> (: $e1 (target $snp $gene))
          (: $e2 (in-pw $gene $pathway))
          (pathway-reach $snp $pathway $gene)))

(rule-sig sp (-> (: $e1 (in-pw $gene1 $pathway))
          (: $e2 (in-pw $gene2 $pathway))
          (same-pathway $gene1 $gene2 $pathway)))

(rule-sig pt (-> (: $e1 (target $snp $gene1))
          (: $e2 (same-pathway $gene1 $gene2 $pathway))
          (pathway-target $snp $gene2 $pathway)))

(rule-sig dg (-> (: $e1 (target $snp $gene))
          (: $e2 (has-disease $gene $disease))
          (disease-reach $snp $disease $gene)))

(rule-sig td (-> (: $e1 (disease-reach $snp $disease $gene))
          (: $e2 (treats $compound $disease))
          (drug-hypothesis $snp $compound $disease $gene)))

(rule-sig bg (-> (: $e1 (target $snp $gene))
          (: $e2 (binds $compound $gene))
          (binding-hypothesis $snp $compound $gene)))

(= (proof-score (dt $e)) 1.0)
(= (proof-score (ct $e1 $sub)) (* 0.99 (proof-score $sub)))
(= (proof-score (xt $sub $e2)) (* 0.99 (proof-score $sub)))
(= (proof-score (pr $sub $e2)) (proof-score $sub))
(= (proof-score (sp $e1 $e2)) 0.5)
(= (proof-score (pt $sub1 $sub2)) (* 0.5 (proof-score $sub1)))
(= (proof-score (dg $sub $e2)) (proof-score $sub))
(= (proof-score (td $sub $e2)) (proof-score $sub))
(= (proof-score (bg $sub $e2)) (proof-score $sub))

!(println! "=== Phase 1: Compounds binding rs10000544 targets (depth 2) ===")
!(let (: $proof (binding-hypothesis rs10000544 $compound $gene))
      (bc &self (fromNumber 2) (: $proof (binding-hypothesis rs10000544 $compound $gene)))
   (binding rs10000544 $compound $gene))

!(println! "=== Phase 2: Diseases reachable from rs10000544 (depth 2) ===")
!(let (: $proof (disease-reach rs10000544 $disease $gene))
      (bc &self (fromNumber 2) (: $proof (disease-reach rs10000544 $disease $gene)))
   (disease rs10000544 $disease $gene))

!(println! "=== Phase 3: Drug hypotheses for rs10000544 (depth 3) ===")
!(let (: $proof (drug-hypothesis rs10000544 $compound $disease $gene))
      (bc &self (fromNumber 3) (: $proof (drug-hypothesis rs10000544 $compound $disease $gene)))
   (drug rs10000544 $compound $disease $gene))

!(println! "=== Phase 4: Drug hypotheses via coreg for rs10000544 (depth 4) ===")
!(let (: $proof (drug-hypothesis rs10000544 $compound $disease $gene))
      (bc &self (fromNumber 4) (: $proof (drug-hypothesis rs10000544 $compound $disease $gene)))
   (deep-drug rs10000544 $compound $disease $gene))

!(println! "=== Phase 5: Pathway reach of rs10000544 (depth 2) ===")
!(let (: $proof (pathway-reach rs10000544 $pathway $gene))
      (bc &self (fromNumber 2) (: $proof (pathway-reach rs10000544 $pathway $gene)))
   (pw-reach rs10000544 $pathway $gene))

!(println! "=== Phase 6: Diseases reachable from rs10000620 (depth 2) ===")
!(let (: $proof (disease-reach rs10000620 $disease $gene))
      (bc &self (fromNumber 2) (: $proof (disease-reach rs10000620 $disease $gene)))
   (disease rs10000620 $disease $gene))

!(println! "=== Phase 7: Drug hypotheses for rs10000620 (depth 3) ===")
!(let (: $proof (drug-hypothesis rs10000620 $compound $disease $gene))
      (bc &self (fromNumber 3) (: $proof (drug-hypothesis rs10000620 $compound $disease $gene)))
   (drug rs10000620 $compound $disease $gene))

!(println! "=== DONE ===")
