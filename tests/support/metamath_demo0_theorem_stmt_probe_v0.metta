!(import! &self ./lib_parse_metamath_grammar_v0.metta)

!(println! "STMT parse-class")
!(println!
  (mm-parse-stmt-class
    "th1 $p |- t = t $= tt tze tpl tt weq tt tt weq tt a2 tt tze tpl tt weq tt tze tpl tt weq tt tt weq wim tt a2 tt tze tpl tt tt a1 mp mp $."))
