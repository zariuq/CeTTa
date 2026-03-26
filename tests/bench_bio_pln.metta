; PLN evidence-aggregation benchmark over Rejuve chr16 pilot KB.
; Dialect-agnostic — runs identically on CeTTa and PeTTa.
; Combines eQTL + ABC + regulatory evidence via PLN revision (evidence-hplus).
; 41 SNPs, 183 (SNP, Gene) pairs, 3 evidence channels.
;
; Output: (bio-score SNP Gene Strength Confidence) per pair.

; --- PLN evidence operations (vendored, portable) ---
!(import! &self support/pln_portable/pln_evidence)

; --- Load bio KB into &self ---
!(import! &self bio_kb_chr16)

; --- Combine 3 evidence channels via PLN revision ---
(= (combine-evidence $snp $gene)
   (match &self
     (evidence-for $snp $gene $ep $en $ap $an $rp $rn)
     (let* (($e-eqtl (evidence $ep $en))
            ($e-abc (evidence $ap $an))
            ($e-reg (evidence $rp $rn))
            ($combined (evidence-hplus (evidence-hplus $e-eqtl $e-abc) $e-reg))
            ($str (evidence-toStrength $combined))
            ($conf (evidence-toConfidence $combined)))
       (bio-score $snp $gene $str $conf))))

; --- Query: aggregate evidence for ALL SNP-gene pairs ---
!(combine-evidence $snp $gene)
