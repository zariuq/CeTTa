; Probe the current PeTTa-flat WM-PLN loader before the bc queries.
; It checks whether the named data/rule spaces are visibly populated.

!(import! &self ./workload_genomic_pln_1m_petta_flat.metta)

!(foldall + (match &bio-wmpln-kb (eqtl-link rs10000544 $gene) 1) 0)
!(foldall + (match &bio-wmpln-kb (closest-gene rs10000544 $gene) 1) 0)
!(foldall + (match &bio-wmpln-kb (compound-treats $drug $disease) 1) 0)
!(foldall + (match &bio-wmpln-rb (rule-sig r1-eqtl-seed $sig) 1) 0)
!(foldall + (match &bio-wmpln-rb (leaf-sig leaf-eqtl $raw $proof $typed) 1) 0)
