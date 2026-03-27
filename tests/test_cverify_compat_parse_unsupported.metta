!(import! &self cverify_compat)

!(assertEqualToResult
  (parse_mm_file "tests/support/metamath/tiny_cverify_compressed.mm")
  (((c ("wff" "|-" "mm.->" "(" ")"))
    (open_frame)
    (v ("ph" "ps"))
    (d ("ph" "ps"))
    (f "wph" "wff" "ph")
    (f "wps" "wff" "ps")
    (a "ax-1" "|-" ("(" "ph" "mm.->" "(" "ps" "mm.->" "ph" ")" ")"))
    (p "th1" "|-" ("(" "ph" "mm.->" "(" "ps" "mm.->" "ph" ")" ")")
       (compressed ("ax-1") (0 1 2)))
    (close_frame))))
