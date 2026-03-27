!(import! &self cverify_compat)

!(assertEqual
   (let $stmts (parse_mm_file "../../hyperon/metamath/metamath-test/tests/core/small/demo0-includer.mm")
      (size-atom $stmts))
   19)

!(assertEqual
   (let $stmts (parse_mm_file "../../hyperon/metamath/metamath-test/tests/core/small/demo0-includer.mm")
      (index-atom $stmts 18))
   (p "th1" "|-" ("t" "mm.=" "t")
      ("tt" "tze" "tpl" "tt" "weq" "tt" "tt" "weq" "tt" "a2"
       "tt" "tze" "tpl" "tt" "weq" "tt" "tze" "tpl" "tt" "weq"
       "tt" "tt" "weq" "wim" "tt" "a2" "tt" "tze" "tpl" "tt"
       "tt" "a1" "mp" "mp")))
