; ============================================================================
; Deep Multi-Relational Backward Chaining over Genomic Knowledge Graph
; ============================================================================
;
; Chain: SNP → Gene → Pathway → Gene' → Pathway' (depth 5+)
;
; Data sources:
;   - 100K GTEx eQTL links (SNP → Gene)
;   - 20 mined co-regulation/co-expression patterns
;   - 193 Reactome pathway memberships (Gene → Pathway, human)
;   - 750 reverse pathway edges (Pathway → Gene, top 15 shared pathways)
;
; Note: PLN rule declarations use (rule-sig name type) instead of (: name type).
; Both HE and CeTTa treat (: $e T) in -> arg slots as a literal expected type,
; not a dependent binder, causing BadArgType (see DISCREPANCIES.md C4).
; The (rule-sig ...) encoding is semantically equivalent on all runtimes.
;
; Expected output: 1,411,430 hypotheses across 17 phases.
; ============================================================================

; ---- Nat -------------------------------------------------------------------
(: Nat Type)
(: Z Nat)
(: S (-> Nat Nat))
(: fromNumber (-> Number Nat))
(= (fromNumber $n) (if (<= $n 0) Z (S (fromNumber (- $n 1)))))

; ---- nilbc backward chainer (adapted for rule-sig) -------------------------
(: bc (-> $a Nat $b $b))

; Base case: match typed axioms
(= (bc $kb $_ (: $prf $thm))
   (match $kb (: $prf $thm) (: $prf $thm)))

; Base case: match rule signatures
(= (bc $kb $_ (rule-sig $name $type))
   (match $kb (rule-sig $name $type) (rule-sig $name $type)))

; Bridge base cases for untyped data
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

; Unary rule — matches rule-sig instead of (: ...) for rules
(= (bc $kb (S $d) (: ($r $p1) $thm))
   (let* (((rule-sig $r (-> (: $p1 $t1) $thm)) (bc $kb $d (rule-sig $r (-> (: $p1 $t1) $thm))))
          ((: $p1 $t1) (bc $kb $d (: $p1 $t1))))
     (: ($r $p1) $thm)))

; Binary rule — matches rule-sig instead of (: ...) for rules
(= (bc $kb (S $d) (: ($r $p1 $p2) $thm))
   (let* (((rule-sig $r (-> (: $p1 $t1) (: $p2 $t2) $thm)) (bc $kb $d (rule-sig $r (-> (: $p1 $t1) (: $p2 $t2) $thm))))
          ((: $p1 $t1) (bc $kb $d (: $p1 $t1)))
          ((: $p2 $t2) (bc $kb $d (: $p2 $t2))))
     (: ($r $p1 $p2) $thm)))

; ============================================================================
; Load data
; ============================================================================
!(import! &self eqtl_for_mining)
!(import! &self discovered_patterns_1k)
!(import! &self bio_multirelational_kb_scaled)
!(import! &self bio_hetionet_kb)

; ============================================================================
; PLN rules — 9 relation types, using (rule-sig ...) to avoid type-checker
; ============================================================================

; Rule 1: Direct eQTL → target
(rule-sig dt (-> (: $e (eqtl $snp $gene)) (target $snp $gene)))

; Rule 2: Co-regulation transfer
(rule-sig ct (-> (: $e1 (coreg $snp1 $snp2))
          (: $e2 (target $snp2 $gene))
          (target $snp1 $gene)))

; Rule 3: Co-expression transfer
(rule-sig xt (-> (: $e1 (target $snp $gene1))
          (: $e2 (coexpr $gene1 $gene2))
          (target $snp $gene2)))

; Rule 4: Pathway reach
(rule-sig pr (-> (: $e1 (target $snp $gene))
          (: $e2 (in-pw $gene $pathway))
          (pathway-reach $snp $pathway $gene)))

; Rule 5: Same-pathway gene
(rule-sig sp (-> (: $e1 (in-pw $gene1 $pathway))
          (: $e2 (in-pw $gene2 $pathway))
          (same-pathway $gene1 $gene2 $pathway)))

; Rule 6: Pathway-mediated target
(rule-sig pt (-> (: $e1 (target $snp $gene1))
          (: $e2 (same-pathway $gene1 $gene2 $pathway))
          (pathway-target $snp $gene2 $pathway)))

; Rule 7: Disease link
(rule-sig dg (-> (: $e1 (target $snp $gene))
          (: $e2 (has-disease $gene $disease))
          (disease-reach $snp $disease $gene)))

; Rule 8: Compound treats disease
(rule-sig td (-> (: $e1 (disease-reach $snp $disease $gene))
          (: $e2 (treats $compound $disease))
          (drug-hypothesis $snp $compound $disease $gene)))

; Rule 9: Compound binds gene
(rule-sig bg (-> (: $e1 (target $snp $gene))
          (: $e2 (binds $compound $gene))
          (binding-hypothesis $snp $compound $gene)))

; ============================================================================
; Evidence scoring
; ============================================================================
(= (proof-score (dt $e)) 1.0)
(= (proof-score (ct $e1 $sub)) (* 0.99 (proof-score $sub)))
(= (proof-score (xt $sub $e2)) (* 0.99 (proof-score $sub)))
(= (proof-score (pr $sub $e2)) (proof-score $sub))
(= (proof-score (sp $e1 $e2)) 0.5)
(= (proof-score (pt $sub1 $sub2)) (* 0.5 (proof-score $sub1)))
(= (proof-score (dg $sub $e2)) (proof-score $sub))
(= (proof-score (td $sub $e2)) (proof-score $sub))
(= (proof-score (bg $sub $e2)) (proof-score $sub))

; ============================================================================
; Queries — full SNP → Gene → Disease → Drug chain
; ============================================================================

; Phase 1: Direct compound binding (depth 2)
!(println! "=== Phase 1: Compounds binding rs10000544 targets (depth 2) ===")
!(let (: $proof (binding-hypothesis rs10000544 $compound $gene))
      (bc &self (fromNumber 2) (: $proof (binding-hypothesis rs10000544 $compound $gene)))
   (binding rs10000544 $compound $gene))

; Phase 2: Disease reach (depth 2)
!(println! "=== Phase 2: Diseases reachable from rs10000544 (depth 2) ===")
!(let (: $proof (disease-reach rs10000544 $disease $gene))
      (bc &self (fromNumber 2) (: $proof (disease-reach rs10000544 $disease $gene)))
   (disease rs10000544 $disease $gene))

; Phase 3: Drug hypothesis (depth 3)
!(println! "=== Phase 3: Drug hypotheses for rs10000544 (depth 3) ===")
!(let (: $proof (drug-hypothesis rs10000544 $compound $disease $gene))
      (bc &self (fromNumber 3) (: $proof (drug-hypothesis rs10000544 $compound $disease $gene)))
   (drug rs10000544 $compound $disease $gene))

; Phase 4: Co-regulation + drug (depth 4)
!(println! "=== Phase 4: Drug hypotheses via coreg for rs10000544 (depth 4) ===")
!(let (: $proof (drug-hypothesis rs10000544 $compound $disease $gene))
      (bc &self (fromNumber 4) (: $proof (drug-hypothesis rs10000544 $compound $disease $gene)))
   (deep-drug rs10000544 $compound $disease $gene))

; Phase 5: Pathway reach (depth 2)
!(println! "=== Phase 5: Pathway reach of rs10000544 (depth 2) ===")
!(let (: $proof (pathway-reach rs10000544 $pathway $gene))
      (bc &self (fromNumber 2) (: $proof (pathway-reach rs10000544 $pathway $gene)))
   (pw-reach rs10000544 $pathway $gene))

; Phase 6: Disease reach from rs10000620 (depth 2)
!(println! "=== Phase 6: Diseases reachable from rs10000620 (depth 2) ===")
!(let (: $proof (disease-reach rs10000620 $disease $gene))
      (bc &self (fromNumber 2) (: $proof (disease-reach rs10000620 $disease $gene)))
   (disease rs10000620 $disease $gene))

; Phase 7: Drug hypotheses from rs10000620 (depth 3)
!(println! "=== Phase 7: Drug hypotheses for rs10000620 (depth 3) ===")
!(let (: $proof (drug-hypothesis rs10000620 $compound $disease $gene))
      (bc &self (fromNumber 3) (: $proof (drug-hypothesis rs10000620 $compound $disease $gene)))
   (drug rs10000620 $compound $disease $gene))

; Phase 8: Pathway targets (depth 3, scaled)
!(println! "=== Phase 8: Pathway targets of rs10000544 (depth 3, scaled) ===")
!(let (: $proof (pathway-target rs10000544 $gene2 $pathway))
      (bc &self (fromNumber 3) (: $proof (pathway-target rs10000544 $gene2 $pathway)))
   (pw-target rs10000544 $gene2 $pathway))

!(println! "=== Phase 9: Pathway targets of rs10000620 (depth 3, scaled) ===")
!(let (: $proof (pathway-target rs10000620 $gene2 $pathway))
      (bc &self (fromNumber 3) (: $proof (pathway-target rs10000620 $gene2 $pathway)))
   (pw-target rs10000620 $gene2 $pathway))

!(println! "=== Phase 10: Coreg + pathway targets of rs10000544 (depth 4, scaled) ===")
!(let (: $proof (pathway-target rs10000544 $gene2 $pathway))
      (bc &self (fromNumber 4) (: $proof (pathway-target rs10000544 $gene2 $pathway)))
   (deep-pw rs10000544 $gene2 $pathway))

!(println! "=== Phase 11: Pathway targets of rs10000797 (depth 3, scaled) ===")
!(let (: $proof (pathway-target rs10000797 $gene2 $pathway))
      (bc &self (fromNumber 3) (: $proof (pathway-target rs10000797 $gene2 $pathway)))
   (pw-target rs10000797 $gene2 $pathway))

!(println! "=== Phase 12: Pathway targets of rs10000960 (depth 3, scaled) ===")
!(let (: $proof (pathway-target rs10000960 $gene2 $pathway))
      (bc &self (fromNumber 3) (: $proof (pathway-target rs10000960 $gene2 $pathway)))
   (pw-target rs10000960 $gene2 $pathway))

!(println! "=== Phase 13: Deep pathway targets of rs10000620 (depth 4) ===")
!(let (: $proof (pathway-target rs10000620 $gene2 $pathway))
      (bc &self (fromNumber 4) (: $proof (pathway-target rs10000620 $gene2 $pathway)))
   (deep-pw rs10000620 $gene2 $pathway))

!(println! "=== Phase 14: Deep pathway targets of rs10000689 (depth 4) ===")
!(let (: $proof (pathway-target rs10000689 $gene2 $pathway))
      (bc &self (fromNumber 4) (: $proof (pathway-target rs10000689 $gene2 $pathway)))
   (deep-pw rs10000689 $gene2 $pathway))

!(println! "=== Phase 15: Deep pathway targets of rs10000797 (depth 4) ===")
!(let (: $proof (pathway-target rs10000797 $gene2 $pathway))
      (bc &self (fromNumber 4) (: $proof (pathway-target rs10000797 $gene2 $pathway)))
   (deep-pw rs10000797 $gene2 $pathway))

!(println! "=== Phase 16: Pathway targets of rs10000726 (depth 3) ===")
!(let (: $proof (pathway-target rs10000726 $gene2 $pathway))
      (bc &self (fromNumber 3) (: $proof (pathway-target rs10000726 $gene2 $pathway)))
   (pw-target rs10000726 $gene2 $pathway))

!(println! "=== Phase 17: Pathway targets of rs10000891 (depth 3) ===")
!(let (: $proof (pathway-target rs10000891 $gene2 $pathway))
      (bc &self (fromNumber 3) (: $proof (pathway-target rs10000891 $gene2 $pathway)))
   (pw-target rs10000891 $gene2 $pathway))

!(println! "=== DONE ===")
