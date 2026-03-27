; PLN inference using miner-discovered co-regulation patterns.
; Loads: eQTL evidence + discovered patterns.
; Computes: For each SNP, find genes reachable via co-regulation,
;           then aggregate evidence using PLN revision.

!(import! &self support/pln_portable/pln_evidence)
!(import! &self bio_kb_chr16)
!(import! &self discovered_patterns_1k)

; Rule 1: direct eQTL evidence
(= (gene-score $snp $gene)
   (match &self (evidence-for $snp $gene $ep $en $ap $an $rp $rn)
     (let $str (evidence-toStrength
       (evidence-hplus (evidence-hplus (evidence $ep $en) (evidence $ap $an))
                       (evidence $rp $rn)))
       (direct-score $snp $gene $str))))

; Rule 2: co-regulation transfer — if SNP1 co-regulates with SNP2,
; and SNP2 has evidence for a gene, transfer scaled evidence to SNP1
(= (transfer-score $snp1 $gene)
   (match &self (co-regulate $snp1 $snp2 $surp)
     (match &self (evidence-for $snp2 $gene $ep $en $ap $an $rp $rn)
       (let* (($base-str (evidence-toStrength
                (evidence-hplus (evidence-hplus (evidence $ep $en) (evidence $ap $an))
                                (evidence $rp $rn))))
              ($transferred (* $base-str $surp)))
         (transfer-score $snp1 $gene $transferred via $snp2)))))

; Rule 3: co-expression — if gene1 co-expresses with gene2,
; evidence for gene1 partially transfers to gene2
(= (coexpress-score $snp $gene2)
   (match &self (evidence-for $snp $gene1 $ep $en $ap $an $rp $rn)
     (match &self (co-express $gene1 $gene2 $surp)
       (let* (($base-str (evidence-toStrength
                (evidence-hplus (evidence-hplus (evidence $ep $en) (evidence $ap $an))
                                (evidence $rp $rn))))
              ($transferred (* $base-str $surp)))
         (coexpress-score $snp $gene2 $transferred via $gene1)))))

; Query: all scoring methods for pilot SNPs
!(gene-score rs9923732 $gene)
!(transfer-score rs10083777 $gene)
!(coexpress-score rs9923732 $gene)
