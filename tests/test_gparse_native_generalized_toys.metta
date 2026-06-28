!(import! &self lib_parse)
!(import! &self gparse)
!(import! &self ./support/lib_parse_generalized_toy_grammars.metta)

(= (toy-grammar-path)
   (gparse:repo-path "tests/support/lib_parse_generalized_toy_grammars.metta"))

(= (unique-cert-of $res)
   (case $res
     (((Unique $cert) $cert)
      ($o (Err no-unique-cert)))))

(= (unique-cert-valid $g $start $toks $res)
   (case $res
     (((Unique $cert) (lp-shared-parse-cert $g $start $toks $cert))
      ($o False))))

(= (shared-result-class $res)
   (case $res
     (((Unique $cert) Unique)
      (Ambiguous Ambiguous)
      (NoParse NoParse)
      ($o (Err bad-shared-result)))))

(= (toy-plus-toks)
   (mkL (x PLUS x PLUS x)))

(= (toy-sss-toks)
   (mkL (b b b)))

(= (toy-nullable-toks)
   (mkL (a c)))

!(assertEqual
  (gparse:glr-class (toy-grammar-path) toy-plus-g E (toy-plus-toks))
  Unique)

!(assertEqual
  (shared-result-class (gparse:gll-parse-shared (toy-grammar-path) toy-plus-g E (toy-plus-toks)))
  Unique)

!(assertEqual
  (gparse:glr-class (toy-grammar-path) toy-sss-g S (toy-sss-toks))
  Ambiguous)

!(assertEqual
  (gparse:gll-parse-shared (toy-grammar-path) toy-sss-g S (toy-sss-toks))
  Ambiguous)

!(assertEqual
  (gparse:dual-parse-compare (toy-sss-g) (toy-grammar-path) toy-sss-g S (toy-sss-toks))
  (GParseDualClass Ambiguous))

!(assertEqual
  (gparse:glr-class (toy-grammar-path) toy-nullable-g S (toy-nullable-toks))
  Unique)

!(assertEqual
  (case (gparse:glr-parse-shared (toy-grammar-path) toy-nullable-g S (toy-nullable-toks))
    (((Unique $cert) (gparse:cert-shape $cert))
     ($o $o)))
  (GParseCertShape 2 2 0 1 0 2))

!(assertEqual
  (shared-result-class (gparse:gll-parse-shared (toy-grammar-path) toy-nullable-g S (toy-nullable-toks)))
  Unique)

!(assertEqual
  (case (gparse:gll-parse-shared (toy-grammar-path) toy-nullable-g S (toy-nullable-toks))
    (((Unique $cert) (gparse:cert-shape $cert))
     ($o $o)))
  (GParseCertShape 2 2 0 1 0 2))

!(assertEqual
  (gparse:dual-parse-compare (toy-nullable-g) (toy-grammar-path) toy-nullable-g S (toy-nullable-toks))
  (GParseDualUnique
    (GParseCertShape 2 2 0 1 0 2)
    (GParseCertShape 2 2 0 1 0 2)
    True
    True
    True
    True))

!(assertEqual
  (gparse:dual-parse-compare (toy-nullable-g) (toy-grammar-path) toy-nullable-g S (mkL (a b)))
  (GParseDualClass NoParse))
