; PLN evidence aggregation over 100K real GTEx eQTL pairs.
; Real data from Rejuve KB v3: 100K (SNP, Gene) pairs, 40K SNPs, 14K genes.
; Tests: KB loading, match &self at scale, PLN evidence-toStrength.
;
; CeTTa vs PeTTa: measures real pattern-matching throughput on genomic data.

; --- PLN evidence operations (vendored, portable) ---
!(import! &self support/pln_portable/pln_evidence)

; --- Load 100K real eQTL evidence atoms ---
!(import! &self bio_kb_real_100k)

; --- PLN scoring: single-channel evidence → truth value ---
(= (score-eqtl $snp $gene)
   (match &self
     (eqtl-evidence $snp $gene $pos $neg)
     (let $str (evidence-toStrength (evidence $pos $neg))
       (eqtl-score $snp $gene $str))))

; --- Query 1: Score all genes for a specific SNP ---
!(score-eqtl rs1000000 $gene)

; --- Query 2: Find high-evidence pairs (>10 tissues) for a region ---
; (This exercises deeper pattern matching — not just single-key lookup)
!(match &self (eqtl-evidence rs10000003 $gene $pos $neg)
   (if (> $pos 5)
     (high-evidence rs10000003 $gene $pos)
     (low-evidence rs10000003 $gene $pos)))
