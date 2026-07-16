; Native generalized-parser helpers live behind this import.

!(import! &self lib_parse)
!(import! &self system)
!(import! &self str)

(= (gparse:rho-canon->mrho-text $ast)
   (function
     (case (__cetta_lib_gparse_rho_canon_mrho_text $ast)
       (($result (return $result))))))

(= (gparse:repo-path $rel)
   (str:concat (str:concat (system:cwd) "/") $rel))

(= (gparse:grammar-summary $path $def)
   (function
     (case (__cetta_lib_gparse_grammar_summary $path $def)
       (($result (return $result))))))

(= (gparse:grammar-data $path $def)
   (function
     (case (__cetta_lib_gparse_grammar_data $path $def)
       (($result (return $result))))))

(= (gparse:inference-presentation $grammar $source $tokens)
   (function
     (case (__cetta_lib_gparse_inference_presentation
             $grammar $source $tokens)
       (((return $result) (return $result))
        ($result (return $result))))))

(= (gparse:slr-summary $path $def $start)
   (function
     (case (__cetta_lib_gparse_slr_summary $path $def $start)
       (($result (return $result))))))

(= (gparse:slr-parse-shared $path $def $start $toks)
   (function
     (case (__cetta_lib_gparse_slr_parse_shared $path $def $start $toks)
       (($result (return $result))))))

(= (gparse:glr-class $path $def $start $toks)
   (function
     (case (__cetta_lib_gparse_glr_class $path $def $start $toks)
       (($result (return $result))))))

(= (gparse:glr-parse-shared $path $def $start $toks)
   (function
     (case (__cetta_lib_gparse_glr_parse_shared $path $def $start $toks)
       (($result (return $result))))))

(= (gparse:glr-forest-summary $path $def $start $toks)
   (function
     (case (__cetta_lib_gparse_glr_forest_summary $path $def $start $toks)
       (($result (return $result))))))

(= (gparse:glr-forest-signature $path $def $start $toks)
   (function
     (case (__cetta_lib_gparse_glr_forest_signature $path $def $start $toks)
       (($result (return $result))))))

(= (gparse:glr-forest-signature-digest $path $def $start $toks)
   (function
     (case (__cetta_lib_gparse_glr_forest_signature_digest $path $def $start $toks)
       (($result (return $result))))))

(= (gparse:glr-forest-data $path $def $start $toks)
   (function
     (case (__cetta_lib_gparse_glr_forest_data $path $def $start $toks)
       (($result (return $result))))))

(= (gparse:gll-parse-shared $path $def $start $toks)
   (function
     (case (__cetta_lib_gparse_gll_parse_shared $path $def $start $toks)
       (($result (return $result))))))

(= (gparse:gll-forest-summary $path $def $start $toks)
   (function
     (case (__cetta_lib_gparse_gll_forest_summary $path $def $start $toks)
       (($result (return $result))))))

(= (gparse:gll-forest-summary-token-file $path $def $start $token-path)
   (function
     (case (__cetta_lib_gparse_gll_forest_summary_token_file
             $path $def $start $token-path)
       (($result (return $result))))))

(= (gparse:gll-forest-signature $path $def $start $toks)
   (function
     (case (__cetta_lib_gparse_gll_forest_signature $path $def $start $toks)
       (($result (return $result))))))

(= (gparse:gll-forest-signature-digest $path $def $start $toks)
   (function
     (case (__cetta_lib_gparse_gll_forest_signature_digest $path $def $start $toks)
       (($result (return $result))))))

(= (gparse:gll-forest-signature-digest-token-file $path $def $start $token-path)
   (function
     (case (__cetta_lib_gparse_gll_forest_signature_digest_token_file
             $path $def $start $token-path)
       (($result (return $result))))))

(= (gparse:gll-forest-data $path $def $start $toks)
   (function
     (case (__cetta_lib_gparse_gll_forest_data $path $def $start $toks)
       (($result (return $result))))))

; Integrated generalized policy over the two native backends:
; GLR is the first integrated generalized backend because it is the practical
; LR-shaped baseline that can fork on conflicts. GLL remains the kernel-aligned
; reference line; both still emit the same shared parse-cert shape.
(= (gparse:backend-choice-from-summary
      (GParseSLRSummary $states $shifts $gotos $reduces $accepts $conflicts))
   GLR)

(= (gparse:backend-choice $path $def $start)
   (let $summary (gparse:slr-summary $path $def $start)
     (GParseBackendChoice (gparse:backend-choice-from-summary $summary) $summary)))

(= (gparse:class-from-shared $result)
   (case $result
     (((Unique $cert) Unique)
      (Ambiguous Ambiguous)
      (NoParse NoParse)
      ($o (Err bad-shared-result)))))

(= (gparse:parse-shared $path $def $start $toks)
   (case (gparse:backend-choice $path $def $start)
     (((GParseBackendChoice GLR $summary)
       (gparse:glr-parse-shared $path $def $start $toks))
      ((GParseBackendChoice GLL $summary)
       (gparse:gll-parse-shared $path $def $start $toks))
      ($o (Err bad-backend-choice)))))

(= (gparse:parse-class $path $def $start $toks)
   (gparse:class-from-shared (gparse:parse-shared $path $def $start $toks)))

(= (gparse:parse-replay-report $g $path $def $start $toks)
   (case (gparse:parse-shared $path $def $start $toks)
     (((Unique $cert)
       (GParseReplay Unique
         (gparse:cert-shape $cert)
         (lp-shared-parse-cert $g $start $toks $cert)))
      (Ambiguous (GParseReplay Ambiguous))
      (NoParse (GParseReplay NoParse)))))

(= (gparse:forest-summary $path $def $start $toks)
   (case (gparse:backend-choice $path $def $start)
     (((GParseBackendChoice GLR $summary)
       (gparse:glr-forest-summary $path $def $start $toks))
      ((GParseBackendChoice GLL $summary)
       (gparse:gll-forest-summary $path $def $start $toks))
      ($o (Err bad-backend-choice)))))

(= (gparse:forest-signature $path $def $start $toks)
   (case (gparse:backend-choice $path $def $start)
     (((GParseBackendChoice GLR $summary)
       (gparse:glr-forest-signature $path $def $start $toks))
      ((GParseBackendChoice GLL $summary)
       (gparse:gll-forest-signature $path $def $start $toks))
      ($o (Err bad-backend-choice)))))

(= (gparse:forest-signature-digest $path $def $start $toks)
   (case (gparse:backend-choice $path $def $start)
     (((GParseBackendChoice GLR $summary)
       (gparse:glr-forest-signature-digest $path $def $start $toks))
      ((GParseBackendChoice GLL $summary)
       (gparse:gll-forest-signature-digest $path $def $start $toks))
      ($o (Err bad-backend-choice)))))

(= (gparse:forest-data $path $def $start $toks)
   (case (gparse:backend-choice $path $def $start)
     (((GParseBackendChoice GLR $summary)
       (gparse:glr-forest-data $path $def $start $toks))
      ((GParseBackendChoice GLL $summary)
       (gparse:gll-forest-data $path $def $start $toks))
      ($o (Err bad-backend-choice)))))

; Shared parse-certs are the stable witness boundary across native backends.
; This summary keeps tests focused on the public cert contract, not backend internals.
(= (gparse:cert-shape-stats-combine
      (GParseCertStats $n1 $l1 $t1 $e1)
      (GParseCertStats $n2 $l2 $t2 $e2))
   (GParseCertStats (+ $n1 $n2) (+ $l1 $l2) (+ $t1 $t2) (+ $e1 $e2)))

(= (gparse:cert-shape-kids Nil)
   (GParseCertStats 0 0 0 0))

(= (gparse:cert-shape-kids (Cons $kid $rest))
   (case (gparse:cert-shape-go $kid)
     (((GParseCertShapeOk $nodes $leaves $tokens $eps $left $right)
       (gparse:cert-shape-stats-combine
         (GParseCertStats $nodes $leaves $tokens $eps)
         (gparse:cert-shape-kids $rest)))
      ($o (GParseCertStats 0 0 0 0)))))

(= (gparse:cert-shape-go (TokC $tok $idx))
   (GParseCertShapeOk 0 0 1 0 $idx (+ $idx 1)))

(= (gparse:cert-shape-go (LeafC $tok $left $right))
   (GParseCertShapeOk 0 1 0 0 $left $right))

(= (gparse:cert-shape-go (EpsC))
   (GParseCertShapeOk 0 0 0 1 0 0))

(= (gparse:cert-shape-go (NodeC $label $lhs $left $right $kids))
   (case (gparse:cert-shape-kids $kids)
     (((GParseCertStats $nodes $leaves $tokens $eps)
       (GParseCertShapeOk (+ $nodes 1) $leaves $tokens $eps $left $right))
      ($o (Err bad-cert-shape)))))

(= (gparse:cert-shape $cert)
   (case (gparse:cert-shape-go $cert)
     (((GParseCertShapeOk $nodes $leaves $tokens $eps $left $right)
       (GParseCertShape $nodes $leaves $tokens $eps $left $right))
      ($o (Err bad-cert-shape)))))

; One harness over both native generalized backends.  A unique result is judged by
; replay and tree/shape agreement; ambiguous and failed parses must agree on class.
(= (gparse:dual-parse-compare-unique
      $g $start $toks $glr-cert $gll-cert)
   (GParseDualUnique
     (gparse:cert-shape $glr-cert)
     (gparse:cert-shape $gll-cert)
     (== (gparse:cert-shape $glr-cert) (gparse:cert-shape $gll-cert))
     (== (lp-shared-cert-tree $g $start $toks $glr-cert)
         (lp-shared-cert-tree $g $start $toks $gll-cert))
     (lp-shared-parse-cert $g $start $toks $glr-cert)
     (lp-shared-parse-cert $g $start $toks $gll-cert)))

(= (gparse:dual-parse-compare-cases
      $g $start $toks (Unique $glr-cert) (Unique $gll-cert))
   (gparse:dual-parse-compare-unique $g $start $toks $glr-cert $gll-cert))

(= (gparse:dual-parse-compare-cases
      $g $start $toks (Unique $glr-cert) Ambiguous)
   (GParseDualMismatch Unique Ambiguous))

(= (gparse:dual-parse-compare-cases
      $g $start $toks (Unique $glr-cert) NoParse)
   (GParseDualMismatch Unique NoParse))

(= (gparse:dual-parse-compare-cases
      $g $start $toks Ambiguous (Unique $gll-cert))
   (GParseDualMismatch Ambiguous Unique))

(= (gparse:dual-parse-compare-cases $g $start $toks Ambiguous Ambiguous)
   (GParseDualClass Ambiguous))

(= (gparse:dual-parse-compare-cases $g $start $toks Ambiguous NoParse)
   (GParseDualMismatch Ambiguous NoParse))

(= (gparse:dual-parse-compare-cases
      $g $start $toks NoParse (Unique $gll-cert))
   (GParseDualMismatch NoParse Unique))

(= (gparse:dual-parse-compare-cases $g $start $toks NoParse Ambiguous)
   (GParseDualMismatch NoParse Ambiguous))

(= (gparse:dual-parse-compare-cases $g $start $toks NoParse NoParse)
   (GParseDualClass NoParse))

(= (gparse:dual-parse-compare $g $path $def $start $toks)
   (gparse:dual-parse-compare-cases
     $g
     $start
     $toks
     (gparse:glr-parse-shared $path $def $start $toks)
     (gparse:gll-parse-shared $path $def $start $toks)))

(= (gparse:dual-forest-signature $path $def $start $toks)
   (let $glr (gparse:glr-forest-signature $path $def $start $toks)
     (let $gll (gparse:gll-forest-signature $path $def $start $toks)
       (GParseDualForestSignature $glr $gll (== $glr $gll)))))

(= (gparse:dual-forest-signature-digest $path $def $start $toks)
   (let $glr (gparse:glr-forest-signature-digest $path $def $start $toks)
     (let $gll (gparse:gll-forest-signature-digest $path $def $start $toks)
       (GParseDualForestSignatureDigest $glr $gll (== $glr $gll)))))

(= (gparse:list-len-acc Nil $n) $n)
(= (gparse:list-len-acc (Cons $x $xs) $n)
   (gparse:list-len-acc $xs (+ $n 1)))
(= (gparse:list-len $xs) (gparse:list-len-acc $xs 0))

(= (gparse:packed-forest-shape
      (GParsePackedForest $status $tok-len $root-count $root $nodes))
   (GParsePackedForestShape
     $status $tok-len $root-count $root (gparse:list-len $nodes)))

(= (gparse:forest-signature-summary
      (GParseForestSignature $status $tok-len $root-count $spans))
   (GParseForestSignatureSummary
     $status $tok-len $root-count (gparse:list-len $spans)))

(= (gparse:dual-forest-signature-summary $path $def $start $toks)
   (let $glr (gparse:glr-forest-signature $path $def $start $toks)
     (let $gll (gparse:gll-forest-signature $path $def $start $toks)
       (GParseDualForestSignatureSummary
         (gparse:forest-signature-summary $glr)
         (gparse:forest-signature-summary $gll)
         (== $glr $gll)))))

; One public, reproducible comparison shape for the generalized native backends.
; This keeps integration judgment in MeTTa/C, over the same grammar-as-data input.
(= (gparse:backend-report $g $path $def $start $toks)
   (GParseBackendReport
     (gparse:backend-choice $path $def $start)
     (gparse:slr-summary $path $def $start)
     (gparse:parse-class $path $def $start $toks)
     (gparse:parse-replay-report $g $path $def $start $toks)
     (gparse:forest-summary $path $def $start $toks)
     (gparse:glr-forest-summary $path $def $start $toks)
     (gparse:gll-forest-summary $path $def $start $toks)
     (gparse:forest-signature-digest $path $def $start $toks)
     (gparse:dual-forest-signature-digest $path $def $start $toks)
     (gparse:dual-forest-signature-summary $path $def $start $toks)
     (gparse:dual-parse-compare $g $path $def $start $toks)))
