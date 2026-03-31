!(import! &self support/bio_wmpln_core_1m.metta)

; Regression for a let/bc interaction where the outer rule-sig match leaked
; hidden derivation bindings into the inner let that consumed a bc result.
!(let (rule-sig $r
        (-> (: $p1 $t1)
            (: $p2 $t2)
            (pathway-target rs10000544 ensg00000090316 tissue-moderate ensg00000145384 r_hsa_1430728)))
      (match &bio-wmpln-rb
        (rule-sig $r
          (-> (: $p1 $t1)
              (: $p2 $t2)
              (pathway-target rs10000544 ensg00000090316 tissue-moderate ensg00000145384 r_hsa_1430728)))
        (rule-sig $r
          (-> (: $p1 $t1)
              (: $p2 $t2)
              (pathway-target rs10000544 ensg00000090316 tissue-moderate ensg00000145384 r_hsa_1430728))))
   (let $res
        (bc &bio-wmpln-kb &bio-wmpln-rb (fromNumber 4)
            (: $plain (same-pathway ensg00000145384 ensg00000090316 r_hsa_1430728)))
      (bio-bc-let-ok $r $t2 $res)))
