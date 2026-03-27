!(import! &self cverify_textual_parse)

!(assertEqual
   (let $stmts (parse_mm_file_textual "../../hyperon/metamath/metamath-test/tests/core/small/anatomy.mm")
      (size-atom $stmts))
   8)

!(assertEqual
   (let $stmts (parse_mm_file_textual "../../hyperon/metamath/metamath-test/tests/core/small/anatomy.mm")
      (car-atom $stmts))
   (c ("(" ")" "mm.->" "wff")))

!(assertEqual
   (let $stmts (parse_mm_file_textual "../../hyperon/metamath/metamath-test/tests/core/small/anatomy.mm")
      (index-atom $stmts 7))
   (p "wnew" "wff" ("(" "s" "mm.->" "(" "r" "mm.->" "p" ")" ")")
      ("ws" "wr" "wp" "w2" "w2")))
