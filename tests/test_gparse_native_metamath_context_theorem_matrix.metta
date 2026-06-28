!(import! &self ./support/lib_parse_metamath_grammar_v0.metta)
!(import! &self gparse)

(= (mm-context-theorem-text $path $raw)
   (case (fs:read-text $path)
     (((Error $call $why) (Err fs-read $why))
      ($text (str:concat (str:concat $text "\n") (str:concat $raw "\n"))))))

(= (mm-context-theorem-class $path $raw)
   (case (mm-context-theorem-text $path $raw)
     (((Err $tag $why) (Err $tag $why))
      ($text
       (case (mm-lex-string $text)
         (((Ok $toks)
           (gparse:parse-class
             (gparse:repo-path "tests/support/lib_parse_metamath_grammar_v0.metta")
             mm-db-g db $toks))
          ($o $o)))))))

!(assertEqual
  (gparse:backend-choice
    (gparse:repo-path "tests/support/lib_parse_metamath_grammar_v0.metta")
    mm-db-g db)
  (GParseBackendChoice GLR (GParseSLRSummary 43 44 40 39 1 0)))

!(assertEqual
  (mm-context-theorem-class "tests/support/metamath_mini_v0.mm" (mm-normal-thm-raw))
  Unique)

!(assertEqual
  (mm-context-theorem-class "tests/support/metamath_demo0_defs_v0.mm" (mm-normal-thm-raw))
  Unique)

!(assertEqual
  (mm-context-theorem-class "tests/support/metamath_demo0_axioms_v0.mm" (mm-normal-thm-raw))
  Unique)

!(assertEqual
  (mm-context-theorem-class "tests/support/metamath_demo0_block_v0.mm" (mm-normal-thm-raw))
  Unique)

!(assertEqual
  (mm-context-theorem-class "tests/support/metamath_mini_v0.mm" (mm-compressed-thm-raw))
  Unique)

!(assertEqual
  (mm-context-theorem-class "tests/support/metamath_demo0_defs_v0.mm" (mm-compressed-thm-raw))
  Unique)

!(assertEqual
  (mm-context-theorem-class "tests/support/metamath_demo0_axioms_v0.mm" (mm-compressed-thm-raw))
  Unique)

!(assertEqual
  (mm-context-theorem-class "tests/support/metamath_demo0_block_v0.mm" (mm-compressed-thm-raw))
  Unique)

!(assertEqual
  (mm-context-theorem-class "tests/support/metamath_mini_v0.mm" (mm-demo0-theorem-noblock-stmt-raw))
  Unique)

!(assertEqual
  (mm-context-theorem-class "tests/support/metamath_demo0_defs_v0.mm" (mm-demo0-theorem-noblock-stmt-raw))
  Unique)

!(assertEqual
  (mm-context-theorem-class "tests/support/metamath_demo0_axioms_v0.mm" (mm-demo0-theorem-noblock-stmt-raw))
  Unique)

!(assertEqual
  (mm-context-theorem-class "tests/support/metamath_demo0_block_v0.mm" (mm-demo0-theorem-noblock-stmt-raw))
  Unique)
