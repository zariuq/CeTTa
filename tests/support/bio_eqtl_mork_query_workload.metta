; Shared MORK-space query workload for the 100K eQTL benchmark family.
; This keeps the query shape fixed while we vary only the storage/load mode.

(= (run-bio-eqtl-mork-workload $kb)
   (let* (($q1 (collapse (mork:match $kb (eqtl-link rs10000544 $gene) $gene)))
          ($q2 (collapse (mork:match $kb (eqtl-link rs10000620 $gene) $gene)))
          ($q3 (collapse (mork:match $kb (eqtl-link $snp ensg00000138660) $snp)))
          ($q4 (collapse (mork:match $kb (tissue-class ensg00000138660 $class) $class)))
          ($q5 (collapse (mork:match $kb (effect-class ensg00000138660 $class) $class))))
     (tuple $q1 $q2 $q3 $q4 $q5)))
