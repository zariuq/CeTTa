!(import! &self cverify_compat)

!(parse-metamath-statements
   (mm-tokenize-text
     (fs-read-text "runtime/repro_many_statements_100.mm")))
