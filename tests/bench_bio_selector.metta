; WM Selector Benchmark: Multi-channel gene candidate ranking over real eQTL data.
; Dialect-agnostic — runs identically on CeTTa and PeTTa.
;
; Loads: 100K real eQTL links + tissue/effect classes + mined co-regulation patterns.
; Computes: For each query SNP, find all candidate genes reachable via 3 channels:
;   1. Direct eQTL link
;   2. Co-regulation transfer (SNP shares regulatory pattern with another SNP)
;   3. Co-expression transfer (gene co-expresses with a directly linked gene)
; Ranks by channel count: genes reachable via more channels score higher.
;
; This exercises: multi-hop joins, non-deterministic search, pattern-derived inference.
; NOT just lookup — the co-regulation/co-expression channels use MINED patterns
; as inference rules, discovered from real GTEx data.

; --- Load PLN evidence operations ---
!(import! &self support/pln_portable/pln_evidence)

; --- Load real eQTL data (100K pairs, 40K SNPs, 14K genes) ---
!(import! &self eqtl_for_mining)

; --- Load mined patterns (from 1K-atom mining run, 20 patterns) ---
!(import! &self discovered_patterns_1k)

; === Channel 1: Direct eQTL evidence ===
(= (candidate $snp $gene direct)
   (match &self (eqtl-link $snp $gene) (direct $snp $gene)))

; === Channel 2: Co-regulation transfer ===
; If SNP1 co-regulates with SNP2 (mined pattern), and SNP2 links to gene,
; then gene is a candidate for SNP1 via co-regulation.
(= (candidate $snp $gene coreg)
   (match &self (co-regulate $snp $other $surp)
     (match &self (eqtl-link $other $gene)
       (coreg $snp $gene $other $surp))))

(= (candidate $snp $gene coreg)
   (match &self (co-regulate $other $snp $surp)
     (match &self (eqtl-link $other $gene)
       (coreg $snp $gene $other $surp))))

; === Channel 3: Co-expression transfer ===
; If SNP links to gene1, and gene1 co-expresses with gene2 (mined pattern),
; then gene2 is a candidate for SNP via co-expression.
(= (candidate $snp $gene coexpr)
   (match &self (eqtl-link $snp $source)
     (match &self (co-express $source $gene $surp)
       (coexpr $snp $gene $source $surp))))

(= (candidate $snp $gene coexpr)
   (match &self (eqtl-link $snp $source)
     (match &self (co-express $gene $source $surp)
       (coexpr $snp $gene $source $surp))))

; === Queries ===

; Query 1: All channels for a SNP with rich mined structure
!(println! "=== rs10000620: multi-channel candidates ===")
!(candidate rs10000620 $gene $channel)

; Query 2: A SNP with only direct evidence (no mined patterns)
!(println! "=== rs1000000: direct-only candidates ===")
!(candidate rs1000000 $gene $channel)

; Query 3: Co-expression cluster — SNP reaching genes via co-expressed intermediary
!(println! "=== rs10000620 co-expression reach ===")
!(candidate rs10000620 $gene coexpr)

; Query 4: All candidates for all 9 pattern-rich SNPs (the heavy query)
!(println! "=== All pattern-rich SNPs: full candidate matrix ===")
!(candidate rs10000544 $gene $channel)
!(candidate rs10000620 $gene $channel)
!(candidate rs10000689 $gene $channel)
!(candidate rs10000700 $gene $channel)
!(candidate rs10000726 $gene $channel)
!(candidate rs10000797 $gene $channel)
!(candidate rs10000873 $gene $channel)
!(candidate rs10000891 $gene $channel)
!(candidate rs10000960 $gene $channel)
