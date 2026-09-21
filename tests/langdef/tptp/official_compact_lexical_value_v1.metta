!(import! &self ../../../lib/lib_tptp.metta)

!(println! (tptp:word-value (tptp-rec:word-lower "local")))
!(println! (tptp:word-value
  (tptp-rec:word-quoted "'Axioms/SET003+0.ax'")))
!(println! (tptp:word-value
  (tptp-rec:word-quoted "'a\\'b\\\\c'")))
!(println! (tptp:word-value (tptp-rec:word-back "`Upper")))
!(println! (tptp:name-value (tptp-rec:name-integer "17")))
!(println! (tptp:word-value (tptp-rec:word-quoted "'bad\\q'")))
!(println! (tptp:word-value (tptp-rec:word-quoted "'open")))
