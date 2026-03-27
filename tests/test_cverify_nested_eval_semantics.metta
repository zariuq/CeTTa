!(import! &self cverify_compat)

; Baseline: untyped calls evaluate ordinary nested arithmetic.
(= (normal-id $x) $x)
!(assertEqual (normal-id (+ 1 2)) 3)

; For cverify parsing surfaces, direct nesting should be equivalent to staged `let`.
(= (parse-mm-dispatch-direct $raw)
   (parse-metamath-statements
     (mm-tokenize-text $raw)))

!(assertEqual
  (parse-metamath-statements (mm-tokenize-text "$c wff $."))
  ((c ("wff"))))

!(assertEqual
  (parse-mm-dispatch-direct "$c wff $.")
  ((c ("wff"))))

; NOTE: helpers like `size-atom` and `car-atom` are `Atom`-typed and inspect
; syntax without forcing evaluation. We bind parser output first to test values.
!(assertEqual
  (let $stmts (parse_mm_file_textual "tests/support/metamath/tiny_cverify.mm")
    (size-atom $stmts))
  9)

!(assertEqual
  (let $stmts (parse_mm_file_textual "tests/support/metamath/tiny_cverify.mm")
    (car-atom $stmts))
  (c ("wff" "|-" "mm.->" "(" ")")))

!(assertEqual
  (let $stmts (parse_mm_file_textual "../../hyperon/metamath/metamath-test/tests/core/small/demo0.mm")
    (size-atom $stmts))
  19)

!(assertEqual
  (let $stmts (parse_mm_file_textual "../../hyperon/metamath/metamath-test/tests/core/small/demo0.mm")
    (car-atom $stmts))
  (c ("0" "mm.+" "mm.=" "mm.->" "(" ")" "term" "wff" "|-")))
