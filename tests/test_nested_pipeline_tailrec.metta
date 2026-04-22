!(import! &self cverify_compat)

!(parse-metamath-statements
   (mm-tokenize-text
     (fs-read-text "tests/support/metamath/many_constants_100.mm")))
