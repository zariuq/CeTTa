!(import! &self ./support/lib_parse_metamath_grammar_v0.metta)
!(import! &self gparse)

(= (mm-defs-theorem-ladder-text $proof)
   (case (fs:read-text (mm-demo0-defs-path))
     (((Error $call $why) (Err fs-read $why))
      ($text
       (str:concat
         (str:concat $text "\n")
         (str:concat "th1 $p |- t = t $= " (str:concat $proof " $.\n")))))))

(= (mm-defs-theorem-ladder-class $proof)
   (case (mm-defs-theorem-ladder-text $proof)
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
  (mm-defs-theorem-ladder-class "tt tze")
  Unique)

!(assertEqual
  (mm-defs-theorem-ladder-class "tt tze tpl tt")
  Unique)

!(assertEqual
  (mm-defs-theorem-ladder-class "tt tze tpl tt weq tt tt weq")
  Unique)

!(assertEqual
  (mm-defs-theorem-ladder-class "tt tze tpl tt weq tt tt weq tt a2 tt tze")
  Unique)

!(assertEqual
  (mm-defs-theorem-ladder-class "tt tze tpl tt weq tt tt weq tt a2 tt tze tpl tt weq tt")
  Unique)

!(assertEqual
  (mm-defs-theorem-ladder-class "tt tze tpl tt weq tt tt weq tt a2 tt tze tpl tt weq tt tze tpl tt weq")
  Unique)

!(assertEqual
  (mm-defs-theorem-ladder-class "tt tze tpl tt weq tt tt weq tt a2 tt tze tpl tt weq tt tze tpl tt weq tt tt weq wim")
  Unique)

!(assertEqual
  (mm-defs-theorem-ladder-class "tt tze tpl tt weq tt tt weq tt a2 tt tze tpl tt weq tt tze tpl tt weq tt tt weq wim tt a2 tt tze")
  Unique)

!(assertEqual
  (mm-defs-theorem-ladder-class "tt tze tpl tt weq tt tt weq tt a2 tt tze tpl tt weq tt tze tpl tt weq tt tt weq wim tt a2 tt tze tpl tt")
  Unique)

!(assertEqual
  (mm-defs-theorem-ladder-class "tt tze tpl tt weq tt tt weq tt a2 tt tze tpl tt weq tt tze tpl tt weq tt tt weq wim tt a2 tt tze tpl tt tt")
  Unique)

!(assertEqual
  (mm-defs-theorem-ladder-class "tt tze tpl tt weq tt tt weq tt a2 tt tze tpl tt weq tt tze tpl tt weq tt tt weq wim tt a2 tt tze tpl tt tt a1")
  Unique)

!(assertEqual
  (mm-defs-theorem-ladder-class "tt tze tpl tt weq tt tt weq tt a2 tt tze tpl tt weq tt tze tpl tt weq tt tt weq wim tt a2 tt tze tpl tt tt a1 mp mp")
  Unique)
