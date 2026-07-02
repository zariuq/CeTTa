; Declarative rule/bridge space for the realistic 1.4M-tier WM-PLN workload.
; Positive example:
; leaf bridges and composition rules live as atoms in a rule space, so the
; chainer discovers them through unification.
;
; Negative example:
; encoding every domain-specific bridge directly as a special case in bc.

; ---- Leaf bridges ----------------------------------------------------------

(leaf-sig leaf-eqtl
  (eqtl-link $snp $gene)
  (eqtl-fact $snp $gene)
  (eqtl $snp $gene))

(leaf-sig leaf-proximal
  (closest-gene $snp $gene)
  (prox-fact $snp $gene)
  (proximal $snp $gene))

(leaf-sig leaf-coreg
  (co-regulate $snp1 $snp2 $surp)
  (coreg-fact $snp1 $snp2 $surp)
  (coreg $snp1 $snp2))

(leaf-sig leaf-coexpr
  (co-express $g1 $g2 $surp)
  (coexpr-fact $g1 $g2 $surp)
  (coexpr $g1 $g2))

(leaf-sig leaf-tissue
  (tissue-class $gene $tissue)
  (tissue-fact $gene $tissue)
  (has-tissue $gene $tissue))

(leaf-sig leaf-pathway
  (in-pathway $gene $pw)
  (pathway-fact $gene $pw)
  (in-pw $gene $pw))

(leaf-sig leaf-disease
  (disease-gene $disease $gene)
  (disease-fact $gene $disease)
  (has-disease $gene $disease))

(leaf-sig leaf-treats
  (compound-treats $comp $disease)
  (treats-fact $comp $disease)
  (treats $comp $disease))

(leaf-sig leaf-palliates
  (compound-palliates $comp $disease)
  (palliates-fact $comp $disease)
  (palliates $comp $disease))

(leaf-sig leaf-binds
  (compound-binds $comp $gene)
  (binds-fact $comp $gene)
  (binds $comp $gene))

; ---- Composition rules -----------------------------------------------------

(rule-sig r1-eqtl-seed
  (-> (: $e (eqtl $snp $gene))
      (seed-target $snp $gene)))

(rule-sig r2-prox-seed
  (-> (: $e (proximal $snp $gene))
      (seed-target $snp $gene)))

(rule-sig r3-contextualize
  (-> (: $e1 (seed-target $snp $gene))
      (: $e2 (has-tissue $gene $tissue))
      (contextual-target $snp $gene $tissue)))

(rule-sig r4-coreg-transfer
  (-> (: $e1 (coreg $snp1 $snp2))
      (: $e2 (contextual-target $snp2 $gene $tissue))
      (coreg-target $snp1 $gene $tissue $snp2)))

(rule-sig r5-coexpr-transfer
  (-> (: $e1 (contextual-target $snp $source $tissue))
      (: $e2 (coexpr $source $gene))
      (coexpr-target $snp $gene $tissue $source)))

(rule-sig r6-same-pathway
  (-> (: $e1 (in-pw $gene1 $pw))
      (: $e2 (in-pw $gene2 $pw))
      (same-pathway $gene1 $gene2 $pw)))

(rule-sig r7-pathway-transfer
  (-> (: $e1 (contextual-target $snp $gene1 $tissue))
      (: $e2 (same-pathway $gene1 $gene2 $pw))
      (pathway-target $snp $gene2 $tissue $gene1 $pw)))

(rule-sig r8-disease-direct
  (-> (: $e1 (contextual-target $snp $gene $tissue))
      (: $e2 (has-disease $gene $disease))
      (disease-hyp $snp $gene $disease $tissue)))

(rule-sig r9-disease-coreg
  (-> (: $e1 (coreg-target $snp $gene $tissue $other))
      (: $e2 (has-disease $gene $disease))
      (disease-hyp $snp $gene $disease $tissue)))

(rule-sig r10-disease-coexpr
  (-> (: $e1 (coexpr-target $snp $gene $tissue $source))
      (: $e2 (has-disease $gene $disease))
      (disease-hyp $snp $gene $disease $tissue)))

(rule-sig r11-disease-pathway
  (-> (: $e1 (pathway-target $snp $gene $tissue $seed $pw))
      (: $e2 (has-disease $gene $disease))
      (disease-hyp $snp $gene $disease $tissue)))

(rule-sig r12-drug-treat
  (-> (: $e1 (disease-hyp $snp $gene $disease $tissue))
      (: $e2 (treats $drug $disease))
      (drug-hyp $snp $gene $disease $drug $tissue)))

(rule-sig r13-drug-palliate
  (-> (: $e1 (disease-hyp $snp $gene $disease $tissue))
      (: $e2 (palliates $drug $disease))
      (drug-hyp $snp $gene $disease $drug $tissue)))

(rule-sig r14-supported-drug
  (-> (: $e1 (drug-hyp $snp $gene $disease $drug $tissue))
      (: $e2 (binds $drug $bound))
      (supported-drug-hyp $snp $gene $disease $drug $tissue)))
