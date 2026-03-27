; ============================================================================
; PLN Backward Chaining at Scale: 100K eQTL × Mined Patterns × Depth 3
; ============================================================================
;
; Loads the FULL 100K eQTL dataset (40,484 SNPs × 14,043 genes) plus
; mined co-regulation/co-expression patterns, then runs nilbc backward
; chaining at depth 2-3 over this real-scale genomic knowledge base.
;
; Target: 5-20 minutes on PeTTa (genuine load test).
;
; The bridge base cases let nilbc consume untyped data atoms directly,
; so the eQTL file is loaded as-is without format conversion.
; ============================================================================

; ---- Nat -------------------------------------------------------------------
(: Nat Type)
(: Z Nat)
(: S (-> Nat Nat))
(: fromNumber (-> Number Nat))
(= (fromNumber $n) (if (<= $n 0) Z (S (fromNumber (- $n 1)))))

; ---- Backward Chainer (nilbc) ----------------------------------------------
(: bc (-> $a Nat $b $b))

; Base: match typed axiom (: name type) in KB
(= (bc $kb $_ (: $prf $thm))
   (match $kb (: $prf $thm) (: $prf $thm)))

; Base: match rule signatures (rule-sig name type) in KB
(= (bc $kb $_ (rule-sig $name $type))
   (match $kb (rule-sig $name $type) (rule-sig $name $type)))

; Bridge base cases: match UNTYPED data atoms and present as typed facts.
; This lets nilbc consume the raw eqtl_for_mining.metta format directly.
(= (bc $kb $_ (: (eqtl-fact $snp $gene) (eqtl $snp $gene)))
   (match $kb (eqtl-link $snp $gene)
     (: (eqtl-fact $snp $gene) (eqtl $snp $gene))))

(= (bc $kb $_ (: (coreg-fact $s1 $s2 $surp) (coreg $s1 $s2)))
   (match $kb (co-regulate $s1 $s2 $surp)
     (: (coreg-fact $s1 $s2 $surp) (coreg $s1 $s2))))

(= (bc $kb $_ (: (coexpr-fact $g1 $g2 $surp) (coexpr $g1 $g2)))
   (match $kb (co-express $g1 $g2 $surp)
     (: (coexpr-fact $g1 $g2 $surp) (coexpr $g1 $g2))))

; Unary rule — matches rule-sig for rule lookup
(= (bc $kb (S $d) (: ($r $p1) $thm))
   (let* (((rule-sig $r (-> (: $p1 $t1) $thm))
           (bc $kb $d (rule-sig $r (-> (: $p1 $t1) $thm))))
          ((: $p1 $t1)
           (bc $kb $d (: $p1 $t1))))
     (: ($r $p1) $thm)))

; Binary rule — matches rule-sig for rule lookup
(= (bc $kb (S $d) (: ($r $p1 $p2) $thm))
   (let* (((rule-sig $r (-> (: $p1 $t1) (: $p2 $t2) $thm))
           (bc $kb $d (rule-sig $r (-> (: $p1 $t1) (: $p2 $t2) $thm))))
          ((: $p1 $t1)
           (bc $kb $d (: $p1 $t1)))
          ((: $p2 $t2)
           (bc $kb $d (: $p2 $t2))))
     (: ($r $p1 $p2) $thm)))

; ============================================================================
; Load Data (100K eQTL + mined patterns + tissue metadata)
; ============================================================================
!(import! &self eqtl_for_mining)
!(import! &self discovered_patterns_1k)

; ---- PLN Inference Rules (rule-sig avoids dependent-binder BadArgType, see C4) ---
(rule-sig dt (-> (: $e (eqtl $snp $gene))
          (target $snp $gene)))

(rule-sig ct (-> (: $e1 (coreg $snp1 $snp2))
          (: $e2 (target $snp2 $gene))
          (target $snp1 $gene)))

(rule-sig xt (-> (: $e1 (target $snp $gene1))
          (: $e2 (coexpr $gene1 $gene2))
          (target $snp $gene2)))

; ---- Evidence scoring ------------------------------------------------------
(= (proof-score (dt $e)) 1.0)
(= (proof-score (ct $e1 $sub)) (* 0.99 (proof-score $sub)))
(= (proof-score (xt $sub $e2)) (* 0.99 (proof-score $sub)))

(= (discover $snp $depth)
   (let (: $proof (target $snp $gene))
        (bc &self (fromNumber $depth) (: $proof (target $snp $gene)))
     (gene-hypothesis $snp $gene (proof-score $proof) $proof)))

; ============================================================================
; Queries — scaled up
; ============================================================================

; Phase A: Depth 2, all 9 co-regulation SEED SNPs (those with novel genes)
!(println! "=== Phase A: Depth 2, rs10000544 (novel: 178636,150961,172399) ===")
!(discover rs10000544 2)

!(println! "=== Phase A: Depth 2, rs10000620 (multi-evidence convergence) ===")
!(discover rs10000620 2)

!(println! "=== Phase A: Depth 2, rs10000689 (co-reg with rs10001548) ===")
!(discover rs10000689 2)

!(println! "=== Phase A: Depth 2, rs10000726 (novel via 544,795) ===")
!(discover rs10000726 2)

!(println! "=== Phase A: Depth 2, rs10000797 (co-reg with rs10000874) ===")
!(discover rs10000797 2)

!(println! "=== Phase A: Depth 2, rs10000891 (co-reg with rs10001010) ===")
!(discover rs10000891 2)

!(println! "=== Phase A: Depth 2, rs10000960 (novel: 248394,273077) ===")
!(discover rs10000960 2)

; Phase B: Depth 3 — deep chaining (most expensive)
; 2-hop chains: coreg→coreg→direct, coreg→direct→coexpr
!(println! "=== Phase B: Depth 3, rs10000544 (2-hop co-regulation chains) ===")
!(discover rs10000544 3)

!(println! "=== Phase B: Depth 3, rs10000620 (deep multi-evidence) ===")
!(discover rs10000620 3)

; Phase C: Reverse query — "which SNPs influence this gene?"
; Scans the full 100K KB to find ALL SNPs targeting genes in the
; co-expression cluster, then follows co-regulation chains back.
(= (reverse-discover $gene $depth)
   (let (: $proof (target $snp $gene))
        (bc &self (fromNumber $depth) (: $proof (target $snp $gene)))
     (gene-hypothesis $snp $gene (proof-score $proof) $proof)))

!(println! "=== Phase C: Reverse query — who targets ensg00000138660? (depth 2) ===")
!(reverse-discover ensg00000138660 2)

!(println! "=== Phase C: Reverse query — who targets ensg00000174749? (depth 2) ===")
!(reverse-discover ensg00000174749 2)

!(println! "=== DONE ===")
