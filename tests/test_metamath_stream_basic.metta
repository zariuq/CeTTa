!(import! &self metamath)

!(assertEqual
  (mm-read-tokens "tests/support/metamath/tiny_cverify.mm")
  ("$c" "wff" "|-" "->" "(" ")" "$." "${" "$v" "ph" "ps" "$." "$d" "ph" "ps"
   "$." "wph" "$f" "wff" "ph" "$." "wps" "$f" "wff" "ps" "$." "ax-1" "$a"
   "|-" "(" "ph" "->" "(" "ps" "->" "ph" ")" ")" "$." "th1" "$p" "|-" "("
   "ph" "->" "(" "ps" "->" "ph" ")" ")" "$=" "wph" "wps" "ax-1" "$." "$}"))

!(assertEqual
  (let $stmts (mm-parse-file-pverify "tests/support/metamath/tiny_cverify.mm")
    (index-atom $stmts 1))
  (open_frame))

!(assertEqual
  (let $stmts (mm-parse-file-pverify "tests/support/metamath/tiny_cverify.mm")
    (index-atom $stmts 7))
  (p "th1" "|-" ("(" "ph" "mm.->" "(" "ps" "mm.->" "ph" ")" ")")
     ("wph" "wps" "ax-1")))
