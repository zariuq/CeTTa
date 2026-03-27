; ============================================================================
; Heavy PLN Backward Chaining: 100K eQTL × depth 4 × open reverse queries
; ============================================================================
;
; Designed to genuinely stress PeTTa:
; 1. Forward chaining at depth 4 for 7 SNPs (exponential search tree)
; 2. OPEN reverse queries: "find ALL SNPs targeting gene X" (full KB scan)
; 3. Cross-pattern inference: shared targets between co-regulation cliques
;
; Target: 5-20 minutes on PeTTa.
; ============================================================================

; ---- Nat -------------------------------------------------------------------
(: Nat Type)
(: Z Nat)
(: S (-> Nat Nat))
(: fromNumber (-> Number Nat))
(= (fromNumber $n) (if (<= $n 0) Z (S (fromNumber (- $n 1)))))

; ---- nilbc -----------------------------------------------------------------
(: bc (-> $a Nat $b $b))
(= (bc $kb $_ (: $prf $thm))
   (match $kb (: $prf $thm) (: $prf $thm)))
(= (bc $kb $_ (rule-sig $name $type))
   (match $kb (rule-sig $name $type) (rule-sig $name $type)))

; Bridge: untyped data → typed facts
(= (bc $kb $_ (: (eqtl-fact $snp $gene) (eqtl $snp $gene)))
   (match $kb (eqtl-link $snp $gene)
     (: (eqtl-fact $snp $gene) (eqtl $snp $gene))))
(= (bc $kb $_ (: (coreg-fact $s1 $s2 $surp) (coreg $s1 $s2)))
   (match $kb (co-regulate $s1 $s2 $surp)
     (: (coreg-fact $s1 $s2 $surp) (coreg $s1 $s2))))
(= (bc $kb $_ (: (coexpr-fact $g1 $g2 $surp) (coexpr $g1 $g2)))
   (match $kb (co-express $g1 $g2 $surp)
     (: (coexpr-fact $g1 $g2 $surp) (coexpr $g1 $g2))))

; Unary + binary rules — match rule-sig for rule lookup
(= (bc $kb (S $d) (: ($r $p1) $thm))
   (let* (((rule-sig $r (-> (: $p1 $t1) $thm)) (bc $kb $d (rule-sig $r (-> (: $p1 $t1) $thm))))
          ((: $p1 $t1) (bc $kb $d (: $p1 $t1))))
     (: ($r $p1) $thm)))
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

; ---- PLN rules (rule-sig avoids dependent-binder BadArgType, see C4) ---------
(rule-sig dt (-> (: $e (eqtl $snp $gene)) (target $snp $gene)))
(rule-sig ct (-> (: $e1 (coreg $snp1 $snp2))
          (: $e2 (target $snp2 $gene))
          (target $snp1 $gene)))
(rule-sig xt (-> (: $e1 (target $snp $gene1))
          (: $e2 (coexpr $gene1 $gene2))
          (target $snp $gene2)))

; ---- Tissue-aware filtering rule (adds depth) -----
(rule-sig tf (-> (: $e1 (target $snp $gene))
          (: $e2 (has-tissue $gene $tissue))
          (tissue-target $snp $gene $tissue)))

; Bridge for tissue metadata
(= (bc $kb $_ (: (tissue-fact $gene $tissue) (has-tissue $gene $tissue)))
   (match $kb (tissue-class $gene $tissue)
     (: (tissue-fact $gene $tissue) (has-tissue $gene $tissue))))

; ---- Evidence scoring ------------------------------------------------------
(= (proof-score (dt $e)) 1.0)
(= (proof-score (ct $e1 $sub)) (* 0.99 (proof-score $sub)))
(= (proof-score (xt $sub $e2)) (* 0.99 (proof-score $sub)))
(= (proof-score (tf $sub $e2)) (proof-score $sub))

; ============================================================================
; Phase 1: Forward chaining depth 4, all pattern-rich SNPs
; ============================================================================
; Depth 4 = 3-hop chains: coreg→coreg→coreg→direct or coreg→direct→coexpr→coexpr
; This is exponential in the number of co-regulation partners.

(= (discover $snp $depth)
   (let (: $proof (target $snp $gene))
        (bc &self (fromNumber $depth) (: $proof (target $snp $gene)))
     (gene-hypothesis $snp $gene (proof-score $proof))))

!(println! "=== Phase 1a: Depth 4 rs10000544 ===")
!(discover rs10000544 4)

!(println! "=== Phase 1b: Depth 4 rs10000620 ===")
!(discover rs10000620 4)

!(println! "=== Phase 1c: Depth 4 rs10000960 ===")
!(discover rs10000960 4)

!(println! "=== Phase 1d: Depth 4 rs10000689 ===")
!(discover rs10000689 4)

!(println! "=== Phase 1e: Depth 4 rs10000797 ===")
!(discover rs10000797 4)

!(println! "=== Phase 1f: Depth 4 rs10000726 ===")
!(discover rs10000726 4)

!(println! "=== Phase 1g: Depth 4 rs10000891 ===")
!(discover rs10000891 4)

; ============================================================================
; Phase 2: Reverse query — OPEN search for all SNPs targeting specific genes
; This forces a full scan of 100K eQTL atoms for each gene.
; ============================================================================

!(println! "=== Phase 2a: ALL SNPs targeting ensg00000138660 (depth 2) ===")
!(let (: $proof (target $snp ensg00000138660))
      (bc &self (fromNumber 2) (: $proof (target $snp ensg00000138660)))
   (reverse-hit $snp ensg00000138660 (proof-score $proof)))

!(println! "=== Phase 2b: ALL SNPs targeting ensg00000145365 (depth 2) ===")
!(let (: $proof (target $snp ensg00000145365))
      (bc &self (fromNumber 2) (: $proof (target $snp ensg00000145365)))
   (reverse-hit $snp ensg00000145365 (proof-score $proof)))

!(println! "=== Phase 2c: ALL SNPs targeting ensg00000174749 (depth 2) ===")
!(let (: $proof (target $snp ensg00000174749))
      (bc &self (fromNumber 2) (: $proof (target $snp ensg00000174749)))
   (reverse-hit $snp ensg00000174749 (proof-score $proof)))

; ============================================================================
; Phase 3: Tissue-aware targets (depth 3 = target@d2 + tissue@d1)
; ============================================================================

!(println! "=== Phase 3: Tissue-broad targets of rs10000544 (depth 3) ===")
!(let (: $proof (tissue-target rs10000544 $gene tissue-broad))
      (bc &self (fromNumber 3) (: $proof (tissue-target rs10000544 $gene tissue-broad)))
   (tissue-hit rs10000544 $gene tissue-broad))

!(println! "=== Phase 3: Tissue-broad targets of rs10000620 (depth 3) ===")
!(let (: $proof (tissue-target rs10000620 $gene tissue-broad))
      (bc &self (fromNumber 3) (: $proof (tissue-target rs10000620 $gene tissue-broad)))
   (tissue-hit rs10000620 $gene tissue-broad))

!(println! "=== DONE ===")
