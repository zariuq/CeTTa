; Native PeTTa lane for the true full-1.4M WM-PLN checkpoint.
; Keep the loader top-level so PeTTa can see the named spaces it binds, while
; matching the same raw-loader tier as the CeTTa full checkpoint.

!(import! &self ./lib_pln_inference_rules.metta)

!(import! &bio-wmpln-rb ./lib_pln_bridge_rules.metta)

!(import! &bio-wmpln-kb ../data/kb_eqtl_gene_links.metta)
!(import! &bio-wmpln-kb-refseq ../data/kb_refseq_gene_annotations.metta)
!(import! &bio-wmpln-kb-tfbs ../data/kb_tf_binding_sites.metta)
!(import! &bio-wmpln-kb-multirel ../data/kb_multirelational_scaled.metta)
!(import! &bio-wmpln-kb-hetionet ../data/kb_hetionet_biomedical_graph.metta)
!(import! &bio-wmpln-kb-patterns ../data/kb_mined_patterns.metta)

(: copy-space-atoms-fold (-> Atom Atom Number))
(= (copy-space-atoms-fold $src $dst)
   (foldall + (match $src $x (let $u (add-atom $dst $x) 0)) 0))

!(let $ignored (copy-space-atoms-fold &bio-wmpln-kb-refseq &bio-wmpln-kb) ())
!(let $ignored (copy-space-atoms-fold &bio-wmpln-kb-tfbs &bio-wmpln-kb) ())
!(let $ignored (copy-space-atoms-fold &bio-wmpln-kb-multirel &bio-wmpln-kb) ())
!(let $ignored (copy-space-atoms-fold &bio-wmpln-kb-hetionet &bio-wmpln-kb) ())
!(let $ignored (copy-space-atoms-fold &bio-wmpln-kb-patterns &bio-wmpln-kb) ())

!(wmpln-dataset-tier full-1p4m-raw-loader 1399997 tfbs-included yes)

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

!(collapse
    (unique
      (let (: $proof (supported-drug-hyp rs10000544 $gene $disease $drug $tissue))
           (bc &bio-wmpln-kb &bio-wmpln-rb (fromNumber 8)
               (: $proof (supported-drug-hyp rs10000544 $gene $disease $drug $tissue)))
        (supported-drug-hypothesis rs10000544 $gene $disease $drug $tissue))))

!(supported-drug-count-contract
    proofs-are-distinct-proof-traces
    hypotheses-are-unique-gene-disease-drug-tissue-keys)
