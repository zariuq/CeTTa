; Top-level PeTTa checkpoint validation for the post-split WM-PLN workload.
; Keep the loader top-level so PeTTa can see the named spaces it binds.

!(import! &self ./support/bio_wmpln_workload_rules.metta)

!(import! &bio-wmpln-rb ./support/bio_wmpln_rulebase_1m.metta)

!(import! &bio-wmpln-kb ./eqtl_for_mining.metta)
!(import! &bio-wmpln-kb-refseq ./bio_refseq_723k.metta)
!(import! &bio-wmpln-kb-tfbs ./bio_multirelational_kb_scaled.metta)
!(import! &bio-wmpln-kb-hetionet ./bio_hetionet_kb.metta)
!(import! &bio-wmpln-kb-patterns ./discovered_patterns_1k.metta)

(: copy-space-atoms-fold (-> Atom Atom Number))
(= (copy-space-atoms-fold $src $dst)
   (foldall + (match $src $x (let $u (add-atom $dst $x) 0)) 0))

!(let $ignored (copy-space-atoms-fold &bio-wmpln-kb-refseq &bio-wmpln-kb) ())
!(let $ignored (copy-space-atoms-fold &bio-wmpln-kb-tfbs &bio-wmpln-kb) ())
!(let $ignored (copy-space-atoms-fold &bio-wmpln-kb-hetionet &bio-wmpln-kb) ())
!(let $ignored (copy-space-atoms-fold &bio-wmpln-kb-patterns &bio-wmpln-kb) ())

!(let $hits
      (collapse
        (let (: $proof (seed-target rs10000544 $gene))
             (bc &bio-wmpln-kb &bio-wmpln-rb (fromNumber 2)
                 (: $proof (seed-target rs10000544 $gene)))
          (seed-hit rs10000544 $gene)))
   (seed-target-count (size-atom $hits)))

!(let $hits
      (collapse
        (let (: $proof (contextual-target rs10000544 $gene $tissue))
             (bc &bio-wmpln-kb &bio-wmpln-rb (fromNumber 3)
                 (: $proof (contextual-target rs10000544 $gene $tissue)))
          (context-hit rs10000544 $gene $tissue)))
   (contextual-target-count (size-atom $hits)))

!(let $hits
      (collapse
        (let (: $proof (disease-hyp rs10000544 $gene $disease $tissue))
             (bc &bio-wmpln-kb &bio-wmpln-rb (fromNumber 6)
                 (: $proof (disease-hyp rs10000544 $gene $disease $tissue)))
          (disease-proof-hit rs10000544 $gene $disease $tissue (proof-trace-route $proof))))
   (disease-proof-count (size-atom $hits)))

!(let $hits
      (collapse
        (let (: $proof (supported-drug-hyp rs10000544 $gene $disease $drug $tissue))
             (bc &bio-wmpln-kb &bio-wmpln-rb (fromNumber 8)
                 (: $proof (supported-drug-hyp rs10000544 $gene $disease $drug $tissue)))
          (supported-drug-proof rs10000544 $gene $disease $drug $tissue
                                (proof-trace-route $proof)
                                (proof-trace-mode $proof)
                                (proof-trace-bound-gene $proof))))
   (supported-drug-proof-count (size-atom $hits)))

!(let $hits
      (collapse
        (unique
          (let (: $proof (supported-drug-hyp rs10000544 $gene $disease $drug $tissue))
               (bc &bio-wmpln-kb &bio-wmpln-rb (fromNumber 8)
                   (: $proof (supported-drug-hyp rs10000544 $gene $disease $drug $tissue)))
            (supported-drug-hypothesis rs10000544 $gene $disease $drug $tissue))))
   (supported-drug-hyp-count (size-atom $hits)))
