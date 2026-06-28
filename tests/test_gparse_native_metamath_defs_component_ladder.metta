!(import! &self ./support/lib_parse_metamath_grammar_v0.metta)
!(import! &self gparse)

(= (mm-defs-component-sigfloats-text)
   "$c 0 + = -> ( ) term wff |- $.\n$v t r s P Q $.\n\ntt $f term t $.\ntr $f term r $.\nts $f term s $.\nwp $f wff P $.\nwq $f wff Q $.\n")

(= (mm-defs-component-plus-tze-text)
   (str:concat (mm-defs-component-sigfloats-text) "tze $a term 0 $.\n"))

(= (mm-defs-component-plus-tpl-text)
   (str:concat (mm-defs-component-plus-tze-text) "tpl $a term ( t + r ) $.\n"))

(= (mm-defs-component-plus-weq-text)
   (str:concat (mm-defs-component-plus-tpl-text) "weq $a wff t = r $.\n"))

(= (mm-defs-component-plus-wim-text)
   (str:concat (mm-defs-component-plus-weq-text) "wim $a wff ( P -> Q ) $.\n"))

(= (mm-defs-component-theorem-text $ctx $proof)
   (str:concat
     (str:concat $ctx "\n")
     (str:concat "th1 $p |- t = t $= " (str:concat $proof " $.\n"))))

(= (mm-defs-component-class-text $ctx $proof)
   (case (mm-lex-string (mm-defs-component-theorem-text $ctx $proof))
     (((Ok $toks)
       (gparse:parse-class
         (gparse:repo-path "tests/support/lib_parse_metamath_grammar_v0.metta")
         mm-db-g db $toks))
      ($o $o))))

(= (mm-defs-component-class-file $path $proof)
   (case (fs:read-text $path)
     (((Error $call $why) (Err fs-read $why))
      ($text (mm-defs-component-class-text $text $proof)))))

(= (mm-defs-component-proof-31)
   "tt tze tpl tt weq tt tt weq tt a2 tt tze tpl tt weq tt tze tpl tt weq tt tt weq wim tt a2 tt tze tpl tt tt")

(= (mm-defs-component-proof-32)
   "tt tze tpl tt weq tt tt weq tt a2 tt tze tpl tt weq tt tze tpl tt weq tt tt weq wim tt a2 tt tze tpl tt tt a1")

!(assertEqual
  (gparse:backend-choice
    (gparse:repo-path "tests/support/lib_parse_metamath_grammar_v0.metta")
    mm-db-g db)
  (GParseBackendChoice GLR (GParseSLRSummary 43 44 40 39 1 0)))

!(assertEqual
  (mm-defs-component-class-text (mm-defs-component-sigfloats-text) (mm-defs-component-proof-31))
  Unique)

!(assertEqual
  (mm-defs-component-class-text (mm-defs-component-sigfloats-text) (mm-defs-component-proof-32))
  Unique)

!(assertEqual
  (mm-defs-component-class-text (mm-defs-component-plus-tze-text) (mm-defs-component-proof-31))
  Unique)

!(assertEqual
  (mm-defs-component-class-text (mm-defs-component-plus-tze-text) (mm-defs-component-proof-32))
  Unique)

!(assertEqual
  (mm-defs-component-class-text (mm-defs-component-plus-tpl-text) (mm-defs-component-proof-31))
  Unique)

!(assertEqual
  (mm-defs-component-class-text (mm-defs-component-plus-tpl-text) (mm-defs-component-proof-32))
  Unique)

!(assertEqual
  (mm-defs-component-class-text (mm-defs-component-plus-weq-text) (mm-defs-component-proof-31))
  Unique)

!(assertEqual
  (mm-defs-component-class-text (mm-defs-component-plus-weq-text) (mm-defs-component-proof-32))
  Unique)

!(assertEqual
  (mm-defs-component-class-text (mm-defs-component-plus-wim-text) (mm-defs-component-proof-31))
  Unique)

!(assertEqual
  (mm-defs-component-class-text (mm-defs-component-plus-wim-text) (mm-defs-component-proof-32))
  Unique)

!(assertEqual
  (mm-defs-component-class-file (mm-demo0-defs-path) (mm-defs-component-proof-31))
  Unique)

!(assertEqual
  (mm-defs-component-class-file (mm-demo0-defs-path) (mm-defs-component-proof-32))
  Unique)
