; ============================================================================
; 1.4M-Atom Multi-Relational Backward Chaining Benchmark
; ============================================================================
;
; Data: 723K closest-gene (RefSeq) + 415K TF-binding (TFBS) +
;       100K eQTL (GTEx) + 132K pathway (Reactome) + Hetionet
;       = 1,399,976 atoms
;
; Benchmark: nilbc backward chaining at depth 1-3 over real genomic KB.
; Tests CeTTa vs PeTTa scaling on real bio inference.
; ============================================================================

; ---- Nat -------------------------------------------------------------------
(: Nat Type)
(: Z Nat)
(: S (-> Nat Nat))
(: fromNumber (-> Number Nat))
(= (fromNumber $n) (if (<= $n 0) Z (S (fromNumber (- $n 1)))))

; ---- nilbc (rule-sig variant for CeTTa compatibility) ---------------------
(: bc (-> $a Nat $b $b))

(= (bc $kb $_ (: $prf $thm))
   (match $kb (: $prf $thm) (: $prf $thm)))

(= (bc $kb $_ (rule-sig $name $type))
   (match $kb (rule-sig $name $type) (rule-sig $name $type)))

; Bridge base cases for untyped data
(= (bc $kb $_ (: (eqtl-fact $snp $gene) (eqtl $snp $gene)))
   (match $kb (eqtl-link $snp $gene)
     (: (eqtl-fact $snp $gene) (eqtl $snp $gene))))

(= (bc $kb $_ (: (proximal-fact $snp $gene) (proximal $snp $gene)))
   (match $kb (closest-gene $snp $gene)
     (: (proximal-fact $snp $gene) (proximal $snp $gene))))

(= (bc $kb $_ (: (tfbs-fact $gene $site) (has-tfbs $gene $site)))
   (match $kb (tf-binds $gene $site)
     (: (tfbs-fact $gene $site) (has-tfbs $gene $site))))

(= (bc $kb $_ (: (pathway-fact $gene $pw) (in-pw $gene $pw)))
   (match $kb (in-pathway $gene $pw)
     (: (pathway-fact $gene $pw) (in-pw $gene $pw))))

(= (bc $kb $_ (: (coreg-fact $s1 $s2 $surp) (coreg $s1 $s2)))
   (match $kb (co-regulate $s1 $s2 $surp)
     (: (coreg-fact $s1 $s2 $surp) (coreg $s1 $s2))))

(= (bc $kb $_ (: (disease-fact $gene $disease) (has-disease $gene $disease)))
   (match $kb (disease-gene $disease $gene)
     (: (disease-fact $gene $disease) (has-disease $gene $disease))))

; Unary rule
(= (bc $kb (S $d) (: ($r $p1) $thm))
   (let* (((rule-sig $r (-> (: $p1 $t1) $thm))
           (bc $kb $d (rule-sig $r (-> (: $p1 $t1) $thm))))
          ((: $p1 $t1)
           (bc $kb $d (: $p1 $t1))))
     (: ($r $p1) $thm)))

; Binary rule
(= (bc $kb (S $d) (: ($r $p1 $p2) $thm))
   (let* (((rule-sig $r (-> (: $p1 $t1) (: $p2 $t2) $thm))
           (bc $kb $d (rule-sig $r (-> (: $p1 $t1) (: $p2 $t2) $thm))))
          ((: $p1 $t1)
           (bc $kb $d (: $p1 $t1)))
          ((: $p2 $t2)
           (bc $kb $d (: $p2 $t2))))
     (: ($r $p1 $p2) $thm)))

; ============================================================================
; Load 1.4M atoms
; ============================================================================
!(import! &self eqtl_for_mining)
!(import! &self bio_refseq_723k)
!(import! &self bio_tfbs_415k)
!(import! &self bio_multirelational_kb_scaled)
!(import! &self bio_hetionet_kb)
!(import! &self discovered_patterns_1k)

; ============================================================================
; PLN rules (11 rules — 4 evidence sources + pathway + disease + coreg)
; ============================================================================

; Direct evidence rules
(rule-sig dt  (-> (: $e (eqtl $snp $gene))     (target $snp $gene)))
(rule-sig prx (-> (: $e (proximal $snp $gene))  (target $snp $gene)))

; Transfer rules
(rule-sig ct  (-> (: $e1 (coreg $snp1 $snp2))
                  (: $e2 (target $snp2 $gene))
                  (target $snp1 $gene)))

; Pathway rule
(rule-sig pr  (-> (: $e1 (target $snp $gene))
                  (: $e2 (in-pw $gene $pathway))
                  (pathway-reach $snp $pathway $gene)))

; Same-pathway gene
(rule-sig sp  (-> (: $e1 (in-pw $gene1 $pathway))
                  (: $e2 (in-pw $gene2 $pathway))
                  (same-pathway $gene1 $gene2 $pathway)))

; Disease link
(rule-sig dg  (-> (: $e1 (target $snp $gene))
                  (: $e2 (has-disease $gene $disease))
                  (disease-reach $snp $disease $gene)))

; TF regulatory reach
(rule-sig tfr (-> (: $e1 (target $snp $gene))
                  (: $e2 (has-tfbs $gene $site))
                  (regulatory-reach $snp $gene $site)))

; ============================================================================
; Queries — escalating complexity over 1.4M atoms
; ============================================================================

; Phase 1: Direct targets (depth 1) — scans eQTL (100K) + RefSeq (723K)
!(println! "=== Phase 1: Direct targets of rs10000544 (depth 1, 1.4M atoms) ===")
!(bc &self (fromNumber 1) (: $proof (target rs10000544 $gene)))

; Phase 2: Disease reach (depth 2) — target@d1 + disease scan
!(println! "=== Phase 2: Diseases from rs10000544 (depth 2) ===")
!(bc &self (fromNumber 2) (: $proof (disease-reach rs10000544 $disease $gene)))

; Phase 3: Regulatory reach (depth 2) — target@d1 + TFBS scan (415K)
!(println! "=== Phase 3: TF regulatory targets of rs10000544 (depth 2) ===")
!(bc &self (fromNumber 2) (: $proof (regulatory-reach rs10000544 $gene $site)))

; Phase 4: Pathway reach (depth 2) — target@d1 + pathway scan (132K)
!(println! "=== Phase 4: Pathways from rs10000544 (depth 2) ===")
!(bc &self (fromNumber 2) (: $proof (pathway-reach rs10000544 $pathway $gene)))

; Phase 5: Different SNP — rs1000014 (from RefSeq, not eQTL)
!(println! "=== Phase 5: Direct targets of rs1000014 (depth 1) ===")
!(bc &self (fromNumber 1) (: $proof (target rs1000014 $gene)))

; Phase 6: Disease reach for rs1000014
!(println! "=== Phase 6: Diseases from rs1000014 (depth 2) ===")
!(bc &self (fromNumber 2) (: $proof (disease-reach rs1000014 $disease $gene)))

!(println! "=== DONE ===")
