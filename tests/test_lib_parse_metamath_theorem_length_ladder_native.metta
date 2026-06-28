!(import! &self ./support/lib_parse_metamath_grammar_v0.metta)
!(import! &self str)

(= (mm-theorem-length-ladder-prefix)
   "$c 0 term wff |- $.\n$v t P $.\n\ntt $f term t $.\nwp $f wff P $.\n\ntze $a term 0 $.\naxp $a |- P $.\n\n")

(= (mm-theorem-length-ladder-db $proof)
   (str:concat
     (mm-theorem-length-ladder-prefix)
     (str:concat "th1 $p |- P $= " (str:concat $proof " $."))))

!(assertEqual
  (mm-db-parse-class-text
    (mm-theorem-length-ladder-db "tt tze"))
  Unique)

!(assertEqual
  (mm-db-parse-class-text
    (mm-theorem-length-ladder-db "tt tze tpl tt"))
  Unique)

!(assertEqual
  (mm-db-parse-class-text
    (mm-theorem-length-ladder-db "tt tze tpl tt weq tt tt weq"))
  Unique)

!(assertEqual
  (mm-db-parse-class-text
    (mm-theorem-length-ladder-db "tt tze tpl tt weq tt tt weq tt a2 tt tze"))
  Unique)

!(assertEqual
  (mm-db-parse-class-text
    (mm-theorem-length-ladder-db "tt tze tpl tt weq tt tt weq tt a2 tt tze tpl tt weq tt"))
  Unique)

!(assertEqual
  (mm-db-parse-class-text
    (mm-theorem-length-ladder-db "tt tze tpl tt weq tt tt weq tt a2 tt tze tpl tt weq tt tze tpl tt weq"))
  Unique)

!(assertEqual
  (mm-db-parse-class-text
    (mm-theorem-length-ladder-db "tt tze tpl tt weq tt tt weq tt a2 tt tze tpl tt weq tt tze tpl tt weq tt tt weq wim"))
  Unique)

!(assertEqual
  (mm-db-parse-class-text
    (mm-theorem-length-ladder-db "tt tze tpl tt weq tt tt weq tt a2 tt tze tpl tt weq tt tze tpl tt weq tt tt weq wim tt a2 tt tze"))
  Unique)

!(assertEqual
  (mm-db-parse-class-text
    (mm-theorem-length-ladder-db "tt tze tpl tt weq tt tt weq tt a2 tt tze tpl tt weq tt tze tpl tt weq tt tt weq wim tt a2 tt tze tpl tt tt a1"))
  Unique)

!(assertEqual
  (mm-db-parse-class-text
    (mm-theorem-length-ladder-db "tt tze tpl tt weq tt tt weq tt a2 tt tze tpl tt weq tt tze tpl tt weq tt tt weq wim tt a2 tt tze tpl tt tt a1 mp mp"))
  Unique)
