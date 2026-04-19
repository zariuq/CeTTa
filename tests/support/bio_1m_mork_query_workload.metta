; Shared direct-query MORK-space workload over the current 1.4M-atom bio tier.
; Positive example:
; keep the workload on stable direct facts so storage mode is the main variable.
;
; Negative example:
; mix in backward chaining and then treat the result as a compiled-space-only
; benchmark.

(= (run-bio-1m-mork-workload $kb)
   (let* (($q1 (collapse (match $kb (eqtl-link rs10000544 $gene) $gene)))
          ($q2 (collapse (match $kb (closest-gene rs1000014 $gene) $gene)))
          ($q3 (collapse (match $kb
                             (tf-binds $gene chr16_67108503_67108927_grch38)
                             $gene)))
          ($q4 (collapse (match $kb (in-pathway ensg00000050820 $pathway)
                             $pathway)))
          ($q5 (collapse (match $kb (disease-gene $disease ensg00000138660)
                             $disease)))
          ($q6 (collapse (match $kb (co-regulate rs10000544 $snp $score)
                             (pair $snp $score)))))
     (tuple $q1 $q2 $q3 $q4 $q5 $q6)))

(= (run-bio-1m-mork-workload $eqtl $refseq $tfbs $pathway $disease $pattern)
   (let* (($q1 (collapse (mork:match $eqtl (eqtl-link rs10000544 $gene) $gene)))
          ($q2 (collapse (mork:match $refseq (closest-gene rs1000014 $gene) $gene)))
          ($q3 (collapse (mork:match $tfbs
                             (tf-binds $gene chr16_67108503_67108927_grch38)
                             $gene)))
          ($q4 (collapse (mork:match $pathway (in-pathway ensg00000050820 $pathway-name)
                             $pathway-name)))
          ($q5 (collapse (mork:match $disease (disease-gene $disease-name ensg00000138660)
                             $disease-name)))
          ($q6 (collapse (mork:match $pattern (co-regulate rs10000544 $snp $score)
                             (pair $snp $score)))))
     (tuple $q1 $q2 $q3 $q4 $q5 $q6)))
