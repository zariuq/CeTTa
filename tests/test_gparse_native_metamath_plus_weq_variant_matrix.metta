!(import! &self ./support/lib_parse_metamath_grammar_v0.metta)
!(import! &self gparse)

(= (mm-plus-weq-base-text)
   "$c 0 + = -> ( ) term wff |- $.\n$v t r s P Q $.\n\ntt $f term t $.\ntr $f term r $.\nts $f term s $.\nwp $f wff P $.\nwq $f wff Q $.\n\ntze $a term 0 $.\ntpl $a term ( t + r ) $.\nweq $a wff t = r $.\n")

(= (mm-plus-weq-variant-text $variant $proof)
   (str:concat
     (str:concat
       (mm-plus-weq-base-text)
       (if (== $variant "") "" (str:concat $variant "\n")))
     (str:concat "th1 $p |- t = t $= " (str:concat $proof " $.\n"))))

(= (mm-plus-weq-variant-class $variant $proof)
   (case (mm-lex-string (mm-plus-weq-variant-text $variant $proof))
     (((Ok $toks)
       (gparse:parse-class
         (gparse:repo-path "tests/support/lib_parse_metamath_grammar_v0.metta")
         mm-db-g db $toks))
      ($o $o))))

(= (mm-plus-weq-proof-31)
   "tt tze tpl tt weq tt tt weq tt a2 tt tze tpl tt weq tt tze tpl tt weq tt tt weq wim tt a2 tt tze tpl tt tt")

(= (mm-plus-weq-proof-32)
   "tt tze tpl tt weq tt tt weq tt a2 tt tze tpl tt weq tt tze tpl tt weq tt tt weq wim tt a2 tt tze tpl tt tt a1")

!(assertEqual
  (gparse:backend-choice
    (gparse:repo-path "tests/support/lib_parse_metamath_grammar_v0.metta")
    mm-db-g db)
  (GParseBackendChoice GLR (GParseSLRSummary 43 44 40 39 1 0)))

!(assertEqual
  (mm-plus-weq-variant-class "" (mm-plus-weq-proof-31))
  Unique)

!(assertEqual
  (mm-plus-weq-variant-class "" (mm-plus-weq-proof-32))
  Unique)

!(assertEqual
  (mm-plus-weq-variant-class "wpp $f wff P $." (mm-plus-weq-proof-31))
  Unique)

!(assertEqual
  (mm-plus-weq-variant-class "wpp $f wff P $." (mm-plus-weq-proof-32))
  Unique)

!(assertEqual
  (mm-plus-weq-variant-class "pid $a |- P $." (mm-plus-weq-proof-31))
  Unique)

!(assertEqual
  (mm-plus-weq-variant-class "pid $a |- P $." (mm-plus-weq-proof-32))
  Unique)

!(assertEqual
  (mm-plus-weq-variant-class "weq2 $a wff t = r $." (mm-plus-weq-proof-31))
  Unique)

!(assertEqual
  (mm-plus-weq-variant-class "weq2 $a wff t = r $." (mm-plus-weq-proof-32))
  Unique)

!(assertEqual
  (mm-plus-weq-variant-class "wpar $a wff ( P ) $." (mm-plus-weq-proof-31))
  Unique)

!(assertEqual
  (mm-plus-weq-variant-class "wpar $a wff ( P ) $." (mm-plus-weq-proof-32))
  Unique)

!(assertEqual
  (mm-plus-weq-variant-class "tpl2 $a term ( t + r ) $." (mm-plus-weq-proof-31))
  Unique)

!(assertEqual
  (mm-plus-weq-variant-class "tpl2 $a term ( t + r ) $." (mm-plus-weq-proof-32))
  Unique)

!(assertEqual
  (mm-plus-weq-variant-class "wim2 $a wff ( P -> Q ) $." (mm-plus-weq-proof-31))
  Unique)

!(assertEqual
  (mm-plus-weq-variant-class "wim2 $a wff ( P -> Q ) $." (mm-plus-weq-proof-32))
  Unique)
