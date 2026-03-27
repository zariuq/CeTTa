!(import! &self cverify_compat)

!(assertEqual
  (parse_mm_file_textual "tests/support/metamath/tiny_cverify.mm")
  ((c ("wff" "|-" "mm.->" "(" ")"))
   (open_frame)
   (v ("ph" "ps"))
   (d ("ph" "ps"))
   (f "wph" "wff" "ph")
   (f "wps" "wff" "ps")
   (a "ax-1" "|-" ("(" "ph" "mm.->" "(" "ps" "mm.->" "ph" ")" ")"))
   (p "th1" "|-" ("(" "ph" "mm.->" "(" "ps" "mm.->" "ph" ")" ")") ("wph" "wps" "ax-1"))
   (close_frame)))
