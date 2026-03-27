; ============================================================================
; PLN Backward Chaining over Real Genomic Evidence
; ============================================================================
;
; Pipeline:  Mine patterns → Backward chain → Score evidence → Rank genes
;
; Uses Nil Geisweiller's nilbc typed backward chainer to discover novel gene
; targets of GWAS SNPs via multi-hop PLN inference over real GTEx eQTL data
; and miner-discovered co-regulation/co-expression patterns.
;
; Data:  100 real eQTL links (15 SNPs × 47 genes, GTEx pilot)
;        13 co-regulation patterns (mined, surprisingness > 0.98)
;         7 co-expression patterns (mined, surprisingness > 0.99)
;
; Rules: direct-target   — eQTL link → gene is a target
;        coreg-transfer  — co-regulation + target → target (PLN deduction)
;        coexpr-transfer — target + co-expression → target (PLN deduction)
;
; Query: "What genes can SNP X influence, and via what evidence chain?"
;
; The chainer produces PROOF TERMS showing exactly which facts and rules
; were used.  Post-processing scores proofs by path type and chain length.
;
; Portable: runs on PeTTa and CeTTa.
; ============================================================================

; ---- Nat (depth counter) ---------------------------------------------------
(: Nat Type)
(: Z Nat)
(: S (-> Nat Nat))
(: fromNumber (-> Number Nat))
(= (fromNumber $n) (if (<= $n 0) Z (S (fromNumber (- $n 1)))))

; ---- Backward Chainer (nilbc, after Geisweiller) ---------------------------
(: bc (-> $a Nat $b $b))

; Base: match axiom in knowledge base
(= (bc $kb $_ (: $prf $thm))
   (match $kb (: $prf $thm) (: $prf $thm)))

; Unary rule
(= (bc $kb (S $d) (: ($r $p1) $thm))
   (let* (((: $r (-> (: $p1 $t1) $thm))
           (bc $kb $d (: $r (-> (: $p1 $t1) $thm))))
          ((: $p1 $t1)
           (bc $kb $d (: $p1 $t1))))
     (: ($r $p1) $thm)))

; Binary rule
(= (bc $kb (S $d) (: ($r $p1 $p2) $thm))
   (let* (((: $r (-> (: $p1 $t1) (: $p2 $t2) $thm))
           (bc $kb $d (: $r (-> (: $p1 $t1) (: $p2 $t2) $thm))))
          ((: $p1 $t1)
           (bc $kb $d (: $p1 $t1)))
          ((: $p2 $t2)
           (bc $kb $d (: $p2 $t2))))
     (: ($r $p1 $p2) $thm)))

; ============================================================================
; Knowledge Base
; ============================================================================
!(bind! &bio (new-space))

; ---- PLN Inference Rules ---------------------------------------------------

; Rule 1: Direct eQTL → target
!(add-atom &bio (: dt (-> (: $e (eqtl $snp $gene))
                          (target $snp $gene))))

; Rule 2: Co-regulation transfer (PLN deduction)
; If SNP1 co-regulates with SNP2 and SNP2 targets Gene, then SNP1 targets Gene
!(add-atom &bio (: ct (-> (: $e1 (coreg $snp1 $snp2))
                          (: $e2 (target $snp2 $gene))
                          (target $snp1 $gene))))

; Rule 3: Co-expression transfer (PLN deduction)
; If SNP targets Gene1 and Gene1 co-expresses with Gene2, then SNP targets Gene2
!(add-atom &bio (: xt (-> (: $e1 (target $snp $gene1))
                          (: $e2 (coexpr $gene1 $gene2))
                          (target $snp $gene2))))

; ---- eQTL Axioms (100 real GTEx links) -------------------------------------
; rs10000544 (9 targets)
!(add-atom &bio (: eq1  (eqtl rs10000544 ensg00000138735)))
!(add-atom &bio (: eq2  (eqtl rs10000544 ensg00000145384)))
!(add-atom &bio (: eq3  (eqtl rs10000544 ensg00000145390)))
!(add-atom &bio (: eq4  (eqtl rs10000544 ensg00000164096)))
!(add-atom &bio (: eq5  (eqtl rs10000544 ensg00000245958)))
!(add-atom &bio (: eq6  (eqtl rs10000544 ensg00000249244)))
!(add-atom &bio (: eq7  (eqtl rs10000544 ensg00000250412)))
!(add-atom &bio (: eq8  (eqtl rs10000544 ensg00000250772)))
!(add-atom &bio (: eq9  (eqtl rs10000544 ensg00000260404)))
; rs10000620 (5 targets — shares genes with the co-expression cluster)
!(add-atom &bio (: eq10 (eqtl rs10000620 ensg00000073331)))
!(add-atom &bio (: eq11 (eqtl rs10000620 ensg00000138660)))
!(add-atom &bio (: eq12 (eqtl rs10000620 ensg00000145365)))
!(add-atom &bio (: eq13 (eqtl rs10000620 ensg00000174749)))
!(add-atom &bio (: eq14 (eqtl rs10000620 ensg00000272567)))
; rs10000689 (8 targets)
!(add-atom &bio (: eq15 (eqtl rs10000689 ensg00000109472)))
!(add-atom &bio (: eq16 (eqtl rs10000689 ensg00000183439)))
!(add-atom &bio (: eq17 (eqtl rs10000689 ensg00000236941)))
!(add-atom &bio (: eq18 (eqtl rs10000689 ensg00000248229)))
!(add-atom &bio (: eq19 (eqtl rs10000689 ensg00000248632)))
!(add-atom &bio (: eq20 (eqtl rs10000689 ensg00000250227)))
!(add-atom &bio (: eq21 (eqtl rs10000689 ensg00000250486)))
!(add-atom &bio (: eq22 (eqtl rs10000689 ensg00000250712)))
; rs10000700 (5 targets — same as rs10000620)
!(add-atom &bio (: eq23 (eqtl rs10000700 ensg00000073331)))
!(add-atom &bio (: eq24 (eqtl rs10000700 ensg00000138660)))
!(add-atom &bio (: eq25 (eqtl rs10000700 ensg00000145365)))
!(add-atom &bio (: eq26 (eqtl rs10000700 ensg00000174749)))
!(add-atom &bio (: eq27 (eqtl rs10000700 ensg00000272567)))
; rs10000726 (6 targets)
!(add-atom &bio (: eq28 (eqtl rs10000726 ensg00000145384)))
!(add-atom &bio (: eq29 (eqtl rs10000726 ensg00000164096)))
!(add-atom &bio (: eq30 (eqtl rs10000726 ensg00000178636)))
!(add-atom &bio (: eq31 (eqtl rs10000726 ensg00000245958)))
!(add-atom &bio (: eq32 (eqtl rs10000726 ensg00000249244)))
!(add-atom &bio (: eq33 (eqtl rs10000726 ensg00000250412)))
; rs10000795 (11 targets)
!(add-atom &bio (: eq34 (eqtl rs10000795 ensg00000138735)))
!(add-atom &bio (: eq35 (eqtl rs10000795 ensg00000145384)))
!(add-atom &bio (: eq36 (eqtl rs10000795 ensg00000145390)))
!(add-atom &bio (: eq37 (eqtl rs10000795 ensg00000150961)))
!(add-atom &bio (: eq38 (eqtl rs10000795 ensg00000164096)))
!(add-atom &bio (: eq39 (eqtl rs10000795 ensg00000172399)))
!(add-atom &bio (: eq40 (eqtl rs10000795 ensg00000245958)))
!(add-atom &bio (: eq41 (eqtl rs10000795 ensg00000249244)))
!(add-atom &bio (: eq42 (eqtl rs10000795 ensg00000250412)))
!(add-atom &bio (: eq43 (eqtl rs10000795 ensg00000250772)))
!(add-atom &bio (: eq44 (eqtl rs10000795 ensg00000260404)))
; rs10000797 (8 targets)
!(add-atom &bio (: eq45 (eqtl rs10000797 ensg00000090316)))
!(add-atom &bio (: eq46 (eqtl rs10000797 ensg00000159692)))
!(add-atom &bio (: eq47 (eqtl rs10000797 ensg00000163945)))
!(add-atom &bio (: eq48 (eqtl rs10000797 ensg00000179979)))
!(add-atom &bio (: eq49 (eqtl rs10000797 ensg00000196810)))
!(add-atom &bio (: eq50 (eqtl rs10000797 ensg00000253399)))
!(add-atom &bio (: eq51 (eqtl rs10000797 ensg00000254094)))
!(add-atom &bio (: eq52 (eqtl rs10000797 ensg00000273179)))
; rs10000873 (5 targets — same as rs10000620)
!(add-atom &bio (: eq53 (eqtl rs10000873 ensg00000073331)))
!(add-atom &bio (: eq54 (eqtl rs10000873 ensg00000138660)))
!(add-atom &bio (: eq55 (eqtl rs10000873 ensg00000145365)))
!(add-atom &bio (: eq56 (eqtl rs10000873 ensg00000174749)))
!(add-atom &bio (: eq57 (eqtl rs10000873 ensg00000272567)))
; rs10000874 (8 targets)
!(add-atom &bio (: eq58 (eqtl rs10000874 ensg00000090316)))
!(add-atom &bio (: eq59 (eqtl rs10000874 ensg00000145217)))
!(add-atom &bio (: eq60 (eqtl rs10000874 ensg00000159692)))
!(add-atom &bio (: eq61 (eqtl rs10000874 ensg00000163945)))
!(add-atom &bio (: eq62 (eqtl rs10000874 ensg00000179979)))
!(add-atom &bio (: eq63 (eqtl rs10000874 ensg00000196810)))
!(add-atom &bio (: eq64 (eqtl rs10000874 ensg00000253399)))
!(add-atom &bio (: eq65 (eqtl rs10000874 ensg00000254094)))
; rs10000891 (5 targets)
!(add-atom &bio (: eq66 (eqtl rs10000891 ensg00000109339)))
!(add-atom &bio (: eq67 (eqtl rs10000891 ensg00000145283)))
!(add-atom &bio (: eq68 (eqtl rs10000891 ensg00000163629)))
!(add-atom &bio (: eq69 (eqtl rs10000891 ensg00000172493)))
!(add-atom &bio (: eq70 (eqtl rs10000891 ensg00000273389)))
; rs10000960 (6 targets)
!(add-atom &bio (: eq71 (eqtl rs10000960 ensg00000138709)))
!(add-atom &bio (: eq72 (eqtl rs10000960 ensg00000164066)))
!(add-atom &bio (: eq73 (eqtl rs10000960 ensg00000164070)))
!(add-atom &bio (: eq74 (eqtl rs10000960 ensg00000164073)))
!(add-atom &bio (: eq75 (eqtl rs10000960 ensg00000164074)))
!(add-atom &bio (: eq76 (eqtl rs10000960 ensg00000251432)))
; rs10001010 (5 targets — same as rs10000891)
!(add-atom &bio (: eq77 (eqtl rs10001010 ensg00000109339)))
!(add-atom &bio (: eq78 (eqtl rs10001010 ensg00000145283)))
!(add-atom &bio (: eq79 (eqtl rs10001010 ensg00000163629)))
!(add-atom &bio (: eq80 (eqtl rs10001010 ensg00000172493)))
!(add-atom &bio (: eq81 (eqtl rs10001010 ensg00000273389)))
; rs10001048 (8 targets)
!(add-atom &bio (: eq82 (eqtl rs10001048 ensg00000138709)))
!(add-atom &bio (: eq83 (eqtl rs10001048 ensg00000164066)))
!(add-atom &bio (: eq84 (eqtl rs10001048 ensg00000164070)))
!(add-atom &bio (: eq85 (eqtl rs10001048 ensg00000164073)))
!(add-atom &bio (: eq86 (eqtl rs10001048 ensg00000164074)))
!(add-atom &bio (: eq87 (eqtl rs10001048 ensg00000248394)))
!(add-atom &bio (: eq88 (eqtl rs10001048 ensg00000251432)))
!(add-atom &bio (: eq89 (eqtl rs10001048 ensg00000273077)))
; rs10001312 (5 targets — same as rs10000620)
!(add-atom &bio (: eq90 (eqtl rs10001312 ensg00000073331)))
!(add-atom &bio (: eq91 (eqtl rs10001312 ensg00000138660)))
!(add-atom &bio (: eq92 (eqtl rs10001312 ensg00000145365)))
!(add-atom &bio (: eq93 (eqtl rs10001312 ensg00000174749)))
!(add-atom &bio (: eq94 (eqtl rs10001312 ensg00000272567)))
; rs10001548 (6 targets)
!(add-atom &bio (: eq95 (eqtl rs10001548 ensg00000183439)))
!(add-atom &bio (: eq96 (eqtl rs10001548 ensg00000236941)))
!(add-atom &bio (: eq97 (eqtl rs10001548 ensg00000248229)))
!(add-atom &bio (: eq98 (eqtl rs10001548 ensg00000248632)))
!(add-atom &bio (: eq99 (eqtl rs10001548 ensg00000250227)))
!(add-atom &bio (: eq100 (eqtl rs10001548 ensg00000250486)))

; ---- Co-regulation Axioms (13 patterns × 2 for symmetry = 26) --------------
; Mined from 10K eQTL atoms, surprisingness > 0.98
!(add-atom &bio (: cr1  (coreg rs10000544 rs10000726)))
!(add-atom &bio (: cr2  (coreg rs10000726 rs10000544)))
!(add-atom &bio (: cr3  (coreg rs10000544 rs10000795)))
!(add-atom &bio (: cr4  (coreg rs10000795 rs10000544)))
!(add-atom &bio (: cr5  (coreg rs10000620 rs10000700)))
!(add-atom &bio (: cr6  (coreg rs10000700 rs10000620)))
!(add-atom &bio (: cr7  (coreg rs10000620 rs10000873)))
!(add-atom &bio (: cr8  (coreg rs10000873 rs10000620)))
!(add-atom &bio (: cr9  (coreg rs10000620 rs10001312)))
!(add-atom &bio (: cr10 (coreg rs10001312 rs10000620)))
!(add-atom &bio (: cr11 (coreg rs10000689 rs10001548)))
!(add-atom &bio (: cr12 (coreg rs10001548 rs10000689)))
!(add-atom &bio (: cr13 (coreg rs10000700 rs10000873)))
!(add-atom &bio (: cr14 (coreg rs10000873 rs10000700)))
!(add-atom &bio (: cr15 (coreg rs10000700 rs10001312)))
!(add-atom &bio (: cr16 (coreg rs10001312 rs10000700)))
!(add-atom &bio (: cr17 (coreg rs10000726 rs10000795)))
!(add-atom &bio (: cr18 (coreg rs10000795 rs10000726)))
!(add-atom &bio (: cr19 (coreg rs10000797 rs10000874)))
!(add-atom &bio (: cr20 (coreg rs10000874 rs10000797)))
!(add-atom &bio (: cr21 (coreg rs10000873 rs10001312)))
!(add-atom &bio (: cr22 (coreg rs10001312 rs10000873)))
!(add-atom &bio (: cr23 (coreg rs10000891 rs10001010)))
!(add-atom &bio (: cr24 (coreg rs10001010 rs10000891)))
!(add-atom &bio (: cr25 (coreg rs10000960 rs10001048)))
!(add-atom &bio (: cr26 (coreg rs10001048 rs10000960)))

; ---- Co-expression Axioms (7 patterns × 2 for symmetry = 14) ---------------
; Mined from gene co-occurrence in eQTL targets, surprisingness > 0.99
!(add-atom &bio (: cx1  (coexpr ensg00000164167 ensg00000250673)))
!(add-atom &bio (: cx2  (coexpr ensg00000250673 ensg00000164167)))
!(add-atom &bio (: cx3  (coexpr ensg00000138660 ensg00000145365)))
!(add-atom &bio (: cx4  (coexpr ensg00000145365 ensg00000138660)))
!(add-atom &bio (: cx5  (coexpr ensg00000138660 ensg00000174749)))
!(add-atom &bio (: cx6  (coexpr ensg00000174749 ensg00000138660)))
!(add-atom &bio (: cx7  (coexpr ensg00000138660 ensg00000272567)))
!(add-atom &bio (: cx8  (coexpr ensg00000272567 ensg00000138660)))
!(add-atom &bio (: cx9  (coexpr ensg00000145365 ensg00000174749)))
!(add-atom &bio (: cx10 (coexpr ensg00000174749 ensg00000145365)))
!(add-atom &bio (: cx11 (coexpr ensg00000145365 ensg00000272567)))
!(add-atom &bio (: cx12 (coexpr ensg00000272567 ensg00000145365)))
!(add-atom &bio (: cx13 (coexpr ensg00000174749 ensg00000272567)))
!(add-atom &bio (: cx14 (coexpr ensg00000272567 ensg00000174749)))

; ============================================================================
; Evidence Scoring (applied to proof terms after chaining)
; ============================================================================
; Direct evidence: full weight.
; Each co-regulation hop: 0.99× decay (high surprisingness).
; Each co-expression hop: 0.99× decay.
(= (proof-score (dt $e)) 1.0)
(= (proof-score (ct $e1 $sub)) (* 0.99 (proof-score $sub)))
(= (proof-score (xt $sub $e2)) (* 0.99 (proof-score $sub)))

; ============================================================================
; Queries
; ============================================================================

; Discover: backward-chain then score.  Output: (gene score proof).
(= (discover $snp $depth)
   (let (: $proof (target $snp $gene))
        (bc &bio (fromNumber $depth) (: $proof (target $snp $gene)))
     (gene-hypothesis $snp $gene (proof-score $proof) $proof)))

; ---- Phase A: Direct targets only (depth 1) --------------------------------
!(println! "=== Phase A: Direct targets of rs10000544 (depth 1) ===")
!(discover rs10000544 1)

; ---- Phase B: Including co-reg transfer (depth 2) --------------------------
; Novel genes expected: ensg00000178636 (via rs10000726),
;                       ensg00000150961, ensg00000172399 (via rs10000795)
!(println! "=== Phase B: All targets of rs10000544 (depth 2) ===")
!(discover rs10000544 2)

; ---- Phase C: Multi-evidence convergence (depth 2) -------------------------
; rs10000620 has 4 co-regulation partners (700, 873, 1312) pointing to
; the SAME 5 genes.  Expect: many redundant proof paths, each an
; independent evidence source suitable for PLN revision.
!(println! "=== Phase C: Multi-evidence paths for rs10000620 (depth 2) ===")
!(discover rs10000620 2)

; ---- Phase D: Co-expression transfer (depth 2) -----------------------------
; rs10000620 → eqtl → ensg138660 → coexpr → ensg145365/174749/272567
; These are redundant (already direct), but the proof terms show the
; independent co-expression evidence path.
; Also: coexpr should produce ensg00000073331 via reverse co-expression
; from the cluster genes.

; ---- Phase E: Novel genes via co-regulation (depth 2) ----------------------
; rs10000960: expects novel genes ensg00000248394, ensg00000273077 via rs10001048
!(println! "=== Phase E: Novel targets of rs10000960 (depth 2) ===")
!(discover rs10000960 2)

; ---- Phase F: Cross-cluster chaining (depth 3) -----------------------------
; At depth 3, chains can go: coreg → coreg → direct (2-hop co-regulation)
; or: coreg → direct → coexpr (co-regulation into co-expression cluster)
; This is the deepest inference — genuine trans-eQTL hypothesis generation.
!(println! "=== Phase F: Deep chaining for rs10000544 (depth 3) ===")
!(discover rs10000544 3)

!(println! "=== DONE ===")
